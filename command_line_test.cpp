#include <iostream>

#include "command_line_parser.hpp"

int main(int argc, char** argv) {
    bool printHelp = false;
    std::optional<int> compression;
    std::vector<std::string_view> files;
    univang::CommandLineParser poParser;
    poParser.addFlag(printHelp, "help,h", "print help")
        .add(compression, "+compression,c,level", "compression level")
        .add(files, "+,,path"sv, "file path(s)", -1);
    bool ok = poParser.parse(argc, argv);
    if(!ok) {
        std::cerr << poParser.error() << '\n' << poParser.getHelp() << '\n';
        return -1;
    }
    if(printHelp) {
        std::cout << poParser.getHelp() << '\n';
        return 0;
    }
    if(!poParser.checkRequired()) {
        std::cerr << poParser.error() << '\n' << poParser.getHelp() << '\n';
        return -1;
    }
    if(compression)
        std::cout << "compression level is " << *compression << '\n';
    return 0;
}
