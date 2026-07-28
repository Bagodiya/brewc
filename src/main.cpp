#include <iostream>
#include <string>

#include "brewc/repl.h"
#include "brewc/runner.h"

// tiny front door. with no arguments you get the repl like before. `brewc run
// <file>` runs a program straight from disk. anything else is a typo, so say so
// and print how it's meant to be used rather than silently doing nothing.
int main(int argc, char* argv[]) {
    if (argc == 1) {
        return brewc::run_repl();
    }

    std::string command = argv[1];
    if (command == "run") {
        if (argc < 3) {
            std::cerr << "usage: brewc run <file>\n";
            return 1;
        }
        return brewc::run_file(argv[2]);
    }

    // accept the dashed spellings too, that's what everyone types first.
    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "brewc " << BREWC_VERSION << "\n";
        return 0;
    }

    std::cerr << "brewc: unknown command '" << command << "'\n";
    std::cerr << "usage: brewc            start the repl\n";
    std::cerr << "       brewc run <file> run a program\n";
    std::cerr << "       brewc version    print the version\n";
    return 1;
}
