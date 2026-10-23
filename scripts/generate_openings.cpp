#include "chess/board.hpp"
#include "chess/evaluation.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/transposition.hpp"
#include "chess/types.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

int main(int argc, char* argv[]) {
    int target_count = 500;
    uint64_t seed = 42;
    std::string output_path = "bench/openings_v2.epd";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--count" && i + 1 < argc) {
            target_count = std::stoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: generate_openings [--count N] [--seed S] [--output PATH]\n";
            return 0;
        }
    }

    std::cout << "Generating " << target_count << " distinct openings (seed=" << seed << ")..." << std::endl;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> ply_dist(6, 8); // 6 to 8 random legal plies

    chess::HandcraftedEvaluator eval;
    chess::TranspositionTable tt(16); // 16 MB TT
    chess::Searcher searcher(eval);
    searcher.set_transposition_table(&tt);

    chess::SearchLimits limits;
    limits.depth = 6; // depth-6 search of the current engine
    searcher.set_limits(limits);

    std::unordered_set<uint64_t> seen_keys;
    std::vector<std::string> accepted_epds;

    uint64_t trials = 0;
    uint64_t rejected_terminal = 0;
    uint64_t rejected_check = 0;
    uint64_t rejected_duplicate = 0;
    uint64_t rejected_eval = 0;

    while (static_cast<int>(accepted_epds.size()) < target_count) {
        ++trials;
        chess::Board board; // starting position
        int target_plies = ply_dist(rng);

        bool abort_playout = false;
        for (int p = 0; p < target_plies; ++p) {
            chess::MoveList legal_moves = chess::generate_legal_moves(board);
            if (legal_moves.empty()) {
                abort_playout = true;
                break;
            }
            std::uniform_int_distribution<size_t> move_dist(0, legal_moves.size() - 1);
            chess::Move chosen = legal_moves[move_dist(rng)];
            board.make_move(chosen);
        }

        if (abort_playout) {
            ++rejected_terminal;
            continue;
        }

        // 1. Reject if game is over (no legal moves)
        chess::MoveList remaining_moves = chess::generate_legal_moves(board);
        if (remaining_moves.empty()) {
            ++rejected_terminal;
            continue;
        }

        // 2. Reject if either side is in check
        if (chess::is_in_check(board, chess::Color::White) ||
            chess::is_in_check(board, chess::Color::Black)) {
            ++rejected_check;
            continue;
        }

        // 3. Reject duplicate by position key
        uint64_t key = board.zobrist_key();
        if (seen_keys.find(key) != seen_keys.end()) {
            ++rejected_duplicate;
            continue;
        }

        // 4. Depth-6 search of current engine: reject if score beyond +/-150 cp from White's POV
        tt.clear();
        searcher.reset();
        chess::SearchResult res = searcher.search(board);

        // Reject mate scores
        if (std::abs(res.score) >= chess::SCORE_MATE - 1000) {
            ++rejected_eval;
            continue;
        }

        // Score from White's POV
        int white_cp = (board.side_to_move() == chess::Color::White) ? res.score : -res.score;
        if (white_cp < -150 || white_cp > 150) {
            ++rejected_eval;
            continue;
        }

        // Accepted!
        seen_keys.insert(key);
        int opening_id = static_cast<int>(accepted_epds.size()) + 1;
        std::string epd_entry = board.fen() + " ; id \"opening_" + std::to_string(opening_id) + "\"";
        accepted_epds.push_back(epd_entry);

        if (accepted_epds.size() % 50 == 0 || static_cast<int>(accepted_epds.size()) == target_count) {
            std::cout << "Generated " << accepted_epds.size() << "/" << target_count
                      << " openings (trials=" << trials << ")..." << std::endl;
        }
    }

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Error: failed to open output file " << output_path << std::endl;
        return 1;
    }

    for (const auto& epd : accepted_epds) {
        out << epd << "\n";
    }
    out.close();

    std::cout << "Done! Wrote " << accepted_epds.size() << " openings to " << output_path << "\n"
              << "Summary:\n"
              << "  Total trials:        " << trials << "\n"
              << "  Rejected (game over): " << rejected_terminal << "\n"
              << "  Rejected (in check): " << rejected_check << "\n"
              << "  Rejected (duplicate): " << rejected_duplicate << "\n"
              << "  Rejected (|cp| > 150):" << rejected_eval << "\n"
              << "  Accepted:            " << accepted_epds.size() << std::endl;

    return 0;
}
