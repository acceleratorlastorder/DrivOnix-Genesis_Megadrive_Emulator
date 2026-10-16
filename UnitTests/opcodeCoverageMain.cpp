#include "opcodeCoverage.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--optimized"))
    {
        std::cerr << "Usage: opcode_coverage [--optimized]" << std::endl;
        return 2;
    }
    return RunOpcodeCoverage(argc == 2);
}
