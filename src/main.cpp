/**
 * main.cpp — Chess engine entry point (Phase 4)
 *
 * Runs the UCI controller on standard input / standard output.
 * Diagnostic messages go to standard error to keep the protocol pure.
 *
 * Build:
 *   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
 *   cmake --build build
 *
 * Run:
 *   ./build/chess_engine
 */

#include "chess/uci.hpp"
#include <iostream>

int main()
{
    // Disable stdio sync for faster I/O
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Announce startup on stderr (never on stdout — that would corrupt UCI protocol)
    std::cerr << chess::UCI_ENGINE_NAME << " " << chess::UCI_ENGINE_VERSION << " starting\n";

    chess::UciController controller(std::cin, std::cout);
    controller.run();

    std::cerr << chess::UCI_ENGINE_NAME << " exiting cleanly\n";
    return 0;
}
