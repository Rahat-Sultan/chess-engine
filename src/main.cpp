/**
 * main.cpp — Chess engine entry point (Phase 0 stub)
 *
 * Phase 0 purpose: verify the build system compiles a C++17 executable.
 * The UCI command loop, board, search, and evaluator are added in later phases.
 *
 * Build:
 *   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
 *   cmake --build build
 *
 * Run:
 *   ./build/chess_engine
 */

#include <iostream>
#include <string>

// Engine identity — update these when the final name is confirmed.
static constexpr const char* ENGINE_NAME    = "LightweightChessEngine";
static constexpr const char* ENGINE_AUTHOR  = "Rahat Sultan";
static constexpr const char* ENGINE_VERSION = "0.1.0-phase0";

/**
 * Minimal UCI handshake so the executable can already be tested with
 * a GUI or script. The full UCI controller is implemented in Phase 4.
 *
 * Supported commands in this stub:
 *   uci      → id name / id author / uciok
 *   isready  → readyok
 *   quit     → clean exit
 *   (anything else is silently ignored, per UCI convention)
 *
 * UCI rule: diagnostic output goes to stderr, not stdout.
 */
static void run_uci_stub()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "uci") {
            std::cout << "id name "   << ENGINE_NAME   << "\n"
                      << "id author " << ENGINE_AUTHOR << "\n"
                      << "uciok"                       << "\n"
                      << std::flush;
        } else if (line == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (line == "quit") {
            break;
        }
        // All other commands are intentionally ignored in the Phase 0 stub.
    }
}

int main()
{
    // Disable stdio sync for faster I/O — important once the search loop runs.
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Announce startup on stderr (never on stdout — that would corrupt UCI).
    std::cerr << ENGINE_NAME << " " << ENGINE_VERSION << " starting\n";

    run_uci_stub();

    std::cerr << ENGINE_NAME << " exiting cleanly\n";
    return 0;
}
