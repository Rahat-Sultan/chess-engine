#pragma once

/**
 * nnue_evaluator.hpp — NNUE position evaluator (Phase 7)
 *
 * Wraps the nnue-probe library (src/nnue_probe/) to provide NNUE evaluation
 * behind the existing Evaluator interface from evaluation.hpp.
 *
 * Network: HalfKP 256x2-32-32 (nn-04cf2b4ed1da.nnue, ~21 MB)
 *   Features=HalfKP(Friend)[41024->256x2]
 *   Loaded via nnue_init() / nnue_evaluate() from nnue-probe.
 *
 * Score convention: identical to HandcraftedEvaluator — side-to-move positive.
 *
 * Piece encoding used by nnue-probe:
 *   wking=1, wqueen=2, wrook=3, wbishop=4, wknight=5, wpawn=6
 *   bking=7, bqueen=8, brook=9, bbishop=10, bknight=11, bpawn=12
 *
 * Usage:
 *   auto eval = std::make_unique<NNUEEvaluator>("networks/nn-04cf2b4ed1da.nnue");
 *   if (eval->is_loaded()) { ... }
 *
 * Fallback: if the file is missing or corrupt, is_loaded() returns false.
 * The UciController falls back to HandcraftedEvaluator in that case.
 */

#include "chess/board.hpp"
#include "chess/evaluation.hpp"
#include "chess/types.hpp"

#include <array>
#include <string>

namespace chess {

class NNUEEvaluator : public Evaluator {
public:
    /**
     * Attempt to load the NNUE network from the given path.
     * Does not throw — check is_loaded() after construction.
     */
    explicit NNUEEvaluator(const std::string& network_path) noexcept;

    /**
     * Evaluate the position from the side-to-move's perspective.
     * Returns centipawns, positive = good for the side to move.
     * Falls back to returning 0 if the network was not loaded.
     */
    int evaluate(Board& board) override;

    /** True if the network file loaded successfully. */
    bool is_loaded() const noexcept { return loaded_; }

    /** Path supplied at construction. */
    const std::string& path() const noexcept { return path_; }

    /** Human-readable network identifier for UCI info strings. */
    static const char* network_name() noexcept {
        return "nn-04cf2b4ed1da (HalfKP 256x2-32-32)";
    }

private:
    std::string path_;
    bool        loaded_ = false;

    // Convert Board position to nnue-probe piece/square arrays.
    // pieces[] and squares[] follow nnue-probe encoding (index 0=wking,1=bking,
    // then remaining pieces in any order, terminated by 0).
    static void board_to_probe_format(const Board& board,
                                      std::array<int, 33>& pieces,
                                      std::array<int, 33>& squares) noexcept;
};

} // namespace chess
