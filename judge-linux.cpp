#include <iostream>
using namespace std;

#include <cstdint>
#include <sstream>
#include <iterator>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <getopt.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <fcntl.h>
#include <unistd.h>

#define STDIN 0
#define STDOUT 1
#define STDERR 2
#define FPS_WAIT 500 // unit: us
#define MEMORY_EXCEEDED 1001
#define TIME_EXCEEDED 1010

#if __WORDSIZE == 64
#define REG(reg) reg.orig_rax
#else
#define REG(reg) reg.orig_eax
#endif

void printUsage(ostream &, int);
void printProgramInfo(int, char **);
bool createProcess(pid_t &, sigset_t &);
void setupIoRedirection(const string &, const string &, const string &);
void setupRunUser();
int runProcess(pid_t, sigset_t, const string &, int, int, int &, int &);
char **getCommandArgs(const string &);
bool isCurrentUsedMemoryIgnored(int, int);
int getCurrentUsedMemory(pid_t);
long long getMillisecondsNow();
long killProcess(pid_t &);

/**
 * The name of this program
 */
const char *program_name = nullptr;

// Whether to display verbose messages.
int verbose = 0;

struct JudgeResult
{
    int usedTime;
    int usedMemory;
    int exitCode;
    void printResult()
    {
        cout << "{\"usedTime\":" << usedTime << ",";
        cout << "\"usedMemory\": " << usedMemory << ",";
        cout << "\"exitCode\":" << exitCode << "}";
    }
};

const struct option longOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"command_line", required_argument, NULL, 'C'},
    {"time_limit", required_argument, NULL, 'T'},
    {"memory_limit", required_argument, NULL, 'M'},
    {"input_file_path", required_argument, NULL, 'I'},
    {"output_file_path", required_argument, NULL, 'O'},
    {"error_file_path", required_argument, NULL, 'E'},
    {"verbose", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0} // Required at end of the array.
};

int main(int argc, char *argv[], char **env)
{
    int nextOption = 0;

    const char *const shortOptions = "hC:T:M:I:O:E:v";

    // Remember the name of the program, to incorporate in messages.
    program_name = argv[0];

    string commandLine = "";    // 运行源程序的命令
    string inputFilePath = "";  // 标准输入文件路径
    string outputFilePath = ""; // 标准输出文件路径
    string errorFilePath = "";  // 标准错误输出文件路径
    int timeLimit = 0;          // ms
    int memoryLimit = 0;        // KB

    do
    {
        nextOption = getopt_long(argc, argv, shortOptions, longOptions, NULL);
        switch (nextOption)
        {
        case 'h':
            printUsage(cout, 0);
        case 'C':
            commandLine = optarg;
            break;
        case 'T':
            timeLimit = atoi(optarg);
            break;
        case 'M':
            memoryLimit = atoi(optarg);
            break;
        case 'I':
            inputFilePath = optarg;
            break;
        case 'O':
            outputFilePath = optarg;
            break;
        case 'E':
            errorFilePath = optarg;
            break;
        case 'v':
            verbose = 1;
            break;
        case '?': // The user specified an invalid option.
            printUsage(cerr, 1);
        case -1: // Done with options.
            break;
        default: // Something else: unexpected.
            abort();
        }
    } while (nextOption != -1);

    if (verbose)
        printProgramInfo(argc, argv);

    pid_t pid = -1;
    sigset_t sigset;

    bool rst = createProcess(pid, sigset);
    if (verbose)
        cout << "[DEBUG] create process return: " << rst << ", pid: " << pid << endl;

    // Setup I/O Redirection for Child Process
    if (pid == 0)
    {
        // setupRunUser();
        setupIoRedirection(inputFilePath, outputFilePath, errorFilePath);
    }
    JudgeResult result = {0, 0, 127};
    result.exitCode = runProcess(pid, sigset, commandLine, timeLimit, memoryLimit, result.usedTime, result.usedMemory);
    result.printResult();
    return 0;
}

void printUsage(ostream &stream, int exitCode)
{
    stream << "Usage: " << program_name << " options [ C T M I O ]" << endl;
    stream << "   -h  --help              Display this usage infomation." << endl;
    stream << "   -C  --command_line      Command Line to run the program." << endl;
    stream << "   -T  --time_limit        Time Limit." << endl;
    stream << "   -M  --memory_limit      Memory Limit." << endl;
    stream << "   -I  --input_file_path   Standard input file path." << endl;
    stream << "   -O  --output_file_path  Output file path." << endl;
    stream << "   -v  --verbose           Print verbose messages." << endl;
    exit(exitCode);
}

void printProgramInfo(int argc, char **argv)
{
    cout << "[INFO] The name of this program is " << argv[0] << endl;
    cout << "[INFO] This program was invoked with " << argc - 1 << " arguments." << endl;

    char **argvPtr = argv;
    if (argc > 1)
    {
        cout << "The arguments are:" << endl;
        while (*(++argvPtr))
        {
            cout << "\t" << *argvPtr;
        }
        cout << endl;
    }
}

/**
 * 创建进程
 * @param pid - 进程ID
 * @param sigset - 进程标记
 * @return 运行创建状态(-1表示未成功创建,0表示子进程)
 */
bool createProcess(pid_t &pid, sigset_t &sigset)
{
    sigset_t originSigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &sigset, &originSigset) < 0)
        return false;

    pid = fork();
    if (pid == -1)
        return false;
    return true;
}

/**
 * 设置程序I/O重定向.
 * @param  inputFilePath  - 执行程序时的输入文件路径(可为NULL)
 * @param  outputFilePath - 执行程序后的输出文件路径(可为NULL)
 */
void setupIoRedirection(const string &inputFilePath, const string &outputFilePath, const string &errorFilePath)
{
    if (inputFilePath != "")
    {
        int inputFileDescriptor = open(inputFilePath.c_str(), O_RDONLY);
        dup2(inputFileDescriptor, STDIN);
        close(inputFileDescriptor);
    }

    if (outputFilePath != "")
    {
        int outputFileDescriptor = open(outputFilePath.c_str(), O_CREAT | O_WRONLY, 0644);
        chmod(outputFilePath.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        dup2(outputFileDescriptor, STDOUT);
        close(outputFileDescriptor);
    }
    if (errorFilePath != "")
    {
        int errorFileDescriptor = open(errorFilePath.c_str(), O_CREAT | O_WRONLY, 0644);
        chmod(errorFilePath.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        dup2(errorFileDescriptor, STDERR);
        close(errorFileDescriptor);
    }
}

void setupRunUser()
{
    gid_t gid = 1005;
    uid_t uid = 1005;
    while (setgid(gid) != 0)
    {
        if (verbose)
            cout << "[WARN] setgid(" << gid << ") failed." << endl;
        sleep(1);
    }
    while (setuid(uid) != 0)
    {
        if (verbose)
            cout << "[WARN] setuid(" << uid << ") failed." << endl;
        sleep(1);
    }
    while (setresuid(uid, uid, uid) != 0)
    {
        if (verbose)
            cout << "[WARN] setresuid(" << uid << ", " << uid << ", " << uid << ") failed." << endl;
        sleep(1);
    }
}

/**
 * 运行进程
 * @param pid - 子进程ID
 * @param sigset - 进程的标记
 * @param argv - 命令行
 * @param timeLimit - 运行时时间限制(ms)
 * @param memoryLimit - 运行时空间限制(KB)
 * @param usedTime - 运行时时间占用(ms)
 * @param usedMemory - 运行时空间占用(KB)
 * @return 进程退出状态
 */
int runProcess(pid_t pid, sigset_t sigset, const string &commandLine, int timeLimit, int memoryLimit, int &usedTime, int &usedMemory)
{
    char **argv = getCommandArgs(commandLine);

    long long startTime = 0;
    long long endTime = 0;
    int exitCode = 0;

    // Run child process
    if (pid == 0)
    {
        execvp(argv[0], argv);
    }
    // Setup Monitor in Parent Process
    else if (pid > 0)
    {
        startTime = getMillisecondsNow();
        long times = 0;
        // recalculate the timt limit :timelimit = timlimit + 20ms
        timeLimit = timeLimit + 100;
        while (waitpid(pid, &exitCode, WNOHANG) != -1)
        {
            // usleep(5000);
            times++;
            // sleep(FPS_WAIT);
            usleep(FPS_WAIT + log(times));
            // Check time limit
            endTime = getMillisecondsNow();
            usedTime = endTime - startTime;
            if (usedTime > timeLimit)
            {
                // Exceeded Time
                killProcess(pid);
                exitCode = TIME_EXCEEDED;
                break;
            }

            // Check memory limit
            int currentUsedMemory = getCurrentUsedMemory(pid);
            if (currentUsedMemory > usedMemory &&
                !isCurrentUsedMemoryIgnored(currentUsedMemory, memoryLimit))
            {
                usedMemory = currentUsedMemory;
            }
            if (memoryLimit != 0 && currentUsedMemory > memoryLimit &&
                !isCurrentUsedMemoryIgnored(currentUsedMemory, memoryLimit))
            {
                // Exceeded Memory
                killProcess(pid);
                exitCode = MEMORY_EXCEEDED;
                break;
            }
        }
        if (verbose)
            cout << "[DEBUG] loop times: " << times << endl;
    }

    return exitCode;
}

/**
 * 获取命令行参数列表
 * @param commandLine - 命令行
 * @return 命令行参数列表
 */
char **getCommandArgs(const string &commandLine)
{
    istringstream iss(commandLine);
    vector<string> args = {
        istream_iterator<string>{iss},
        istream_iterator<string>{}};

    size_t numberOfArguments = args.size();
    char **argv = new char *[numberOfArguments + 1]();

    for (size_t i = 0; i < numberOfArguments; i++)
    {
        char *arg = new char[args[i].size() + 1];
        strcpy(arg, args[i].c_str());
        argv[i] = arg;
    }
    argv[numberOfArguments] = nullptr;

    return argv;
}

/**
 * 获取当前系统时间
 * 用于统计程序运行时间
 * @return 当前系统时间(以毫秒为单位) 
 */
long long getMillisecondsNow()
{
    long milliseconds;
    time_t seconds;
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);
    seconds = spec.tv_sec;
    milliseconds = round(spec.tv_nsec / 1.0e6);
    long long currentTime = seconds * 1000 + milliseconds;

    return currentTime;
}

/**
 * 是否忽略当前获得的内存占用值
 * 实际运行过程中，程序可能会获取到JVM环境中的内存占用.
 * 对于这种情况，应该忽略这样的值
 * @param currentUsedMemory - 当前获取到的内存占用
 * @param memoryLimit - 运行时空间限制(KB)
 * @return 是否忽略当前获取到的内存占用
 */
bool isCurrentUsedMemoryIgnored(int currentUsedMemory, int memoryLimit)
{
    int jvmUsedMemory = getCurrentUsedMemory(getpid());

    if (currentUsedMemory >= jvmUsedMemory / 2 &&
        currentUsedMemory <= jvmUsedMemory * 2)
    {
        return true;
    }
    return false;
}

/**
 * 获取内存占用情况
 * @param pid - 进程ID
 * @return 当前无力内存使用量(KB)
 */
int getCurrentUsedMemory(pid_t pid)
{
    int currentUsedMemory = 0;
    long residentSetSize = 0L;
    FILE *fp = NULL;
    stringstream stringStream;

    stringStream << "/proc/" << pid << "/statm";
    const string tmp = stringStream.str();
    const char *filePath = tmp.c_str();

    if ((fp = fopen(filePath, "r")) != NULL)
    {
        if (fscanf(fp, "%*s%ld", &residentSetSize) == 1)
        {
            currentUsedMemory = (int)residentSetSize * (int)sysconf(_SC_PAGESIZE) >> 10;

            if (currentUsedMemory < 0)
            {
                currentUsedMemory = numeric_limits<int32_t>::max() >> 10;
            }
        }
        fclose(fp);
    }
    return currentUsedMemory;
}

/**
 * 强制销毁进程(当触发阈值时)
 * @param pid - 进程ID
 * @return 0, 表示进程被成功终止
 */
long killProcess(pid_t &pid)
{

    if (verbose)
        cout << "[DEBUG] Process [PID = " << pid << "] is going to be killled." << endl;

    ptrace(PTRACE_KILL, pid, NULL, NULL);
    return kill(pid, SIGKILL);
}