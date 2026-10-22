#pragma once

/**
 * evaluation.hpp — Position evaluation interface (D-6)
 *
 * Design goals:
 *   - Abstract Evaluator base keeps the search independent of the evaluation
 *     strategy, allowing HandcraftedEvaluator to be replaced by NNUE later.
 *   - Score convention (D-7): ALWAYS from the side-to-move's perspective.
 *     Positive = good for the side to move, negative = bad.
 *   - The evaluate() function is called at leaf nodes by the search.
 *
 * Phase 3 implementation: HandcraftedEvaluator
 *   - Material values (centipawns)
 *   - Piece-square tables (PSTs) for positional bonuses
 *   - Mobility: small bonus proportional to number of legal moves
 *   - Bishop pair bonus
 *   - Basic king safety: penalty for open files near king in midgame
 */

#include "chess/board.hpp"
#include "chess/types.hpp"

namespace chess {

// ─── Abstract evaluator interface ─────────────────────────────────────────────

class Evaluator {
public:
    virtual ~Evaluator() = default;

    /**
     * Evaluate the position from the side-to-move's perspective.
     * Returns a score in centipawns:
     *   positive  → position is good for the side to move
     *   negative  → position is bad  for the side to move
     *   ~0        → roughly equal
     */
    virtual int evaluate(Board& board) = 0;
};

// ─── Material values (centipawns) ─────────────────────────────────────────────

inline constexpr int MAT_PAWN   = 100;
inline constexpr int MAT_KNIGHT = 320;
inline constexpr int MAT_BISHOP = 330;
inline constexpr int MAT_ROOK   = 500;
inline constexpr int MAT_QUEEN  = 900;
inline constexpr int MAT_KING   =   0;  // king has no material value in search

inline constexpr int material_value(PieceType pt) noexcept {
    switch (pt) {
        case PieceType::Pawn:   return MAT_PAWN;
        case PieceType::Knight: return MAT_KNIGHT;
        case PieceType::Bishop: return MAT_BISHOP;
        case PieceType::Rook:   return MAT_ROOK;
        case PieceType::Queen:  return MAT_QUEEN;
        default:                return 0;
    }
}

// ─── HandcraftedEvaluator ─────────────────────────────────────────────────────

class HandcraftedEvaluator : public Evaluator {
public:
    int evaluate(Board& board) override;

private:
    // Compute the raw score from White's perspective, then flip for side to move
    int evaluate_white(const Board& board) const noexcept;

    int material_score(const Board& board) const noexcept;
    int pst_score(const Board& board) const noexcept;
    int bishop_pair_bonus(const Board& board) const noexcept;
    int mobility_score(Board& board) const;
};

} // namespace chess
