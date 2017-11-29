# online-judge-core

judge-*.cpp 参考自[hzxie/voj](https://github.com/hzxie/voj/tree/master/judger/src/main/cpp)

## Compile Source Code

    g++ -O2 -w -std=c++11 {source_code_path} -lm -o {output_path} 
    // eg: g++ -O2 -w -std=c++11 judge-linux.cpp -lm -o judge-linux

## Execute Binary Code
use

    ./judge-linux -h

will display the information as following:

    -h  --help              Display usage infomation of this program.
    -C  --command_line      Command Line to run your own program.
    -T  --time_limit        Time Limit.
    -M  --memory_limit      Memory Limit.
    -I  --input_file_path   Standard input file path.
    -O  --output_file_path  Output file path.
    -v  --verbose           Print verbose message.

    eg: ./judge-linux -C "case/1001" -T 1000 -M 32767 -I case/input1.txt -O case/output1.src -v