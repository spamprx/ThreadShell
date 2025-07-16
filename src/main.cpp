#include <iostream>
#include "../include/shell.h"

int main(int argc, char* argv[]) {
    try {
        threadshell::Shell shell;
        shell.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
} 