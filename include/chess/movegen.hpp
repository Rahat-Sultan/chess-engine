#pragma once

/**
 * movegen.hpp — Move generation and legality
 *
 * Public API for Phase 2:
 *
 *   generate_legal_moves(board)  → MoveList of all legal moves
 *   is_in_check(board, color)    → true if <color>'s king is in check
 *   is_square_attacked(board, sq, by) → true if <sq> is attacked by <by>
 *   resolve_move(board, partial) → annotate flags on a UCI-parsed Move
 *
 * Design notes:
 *   - Pseudo-legal moves are generated first; each is tested for king
 *     safety via make_move/is_in_check/unmake_move.
 *   - MoveList is a fixed-capacity stack array (max 256 moves per position)
 *     to avoid heap allocation in the search loop.
 *   - Castling legality (path clear, no check through transit) is fully
 *     verified here before emitting the castling Move.
 */

#include "chess/board.hpp"
#include "chess/types.hpp"

#include <array>
#include <cstddef>

namespace chess {

// ─── MoveList ─────────────────────────────────────────────────────────────────
//
// A fixed-capacity, stack-allocated list of moves.
// 256 is a safe upper bound; the practical maximum in a real position is ~218.

class MoveList {
public:
    static constexpr std::size_t MAX_MOVES = 256;

    void push(const Move& m) noexcept { moves_[size_++] = m; }
    void clear()             noexcept { size_ = 0; }

    std::size_t size() const noexcept { return size_; }
    bool        empty()const noexcept { return size_ == 0; }

    const Move& operator[](std::size_t i) const noexcept { return moves_[i]; }
    Move&       operator[](std::size_t i)       noexcept { return moves_[i]; }

    // Range-based for
    const Move* begin() const noexcept { return moves_.data(); }
    const Move* end()   const noexcept { return moves_.data() + size_; }
    Move*       begin()       noexcept { return moves_.data(); }
    Move*       end()         noexcept { return moves_.data() + size_; }

private:
    std::array<Move, MAX_MOVES> moves_{};
    std::size_t                 size_ = 0;
};

// ─── Attack detection ─────────────────────────────────────────────────────────

/**
 * Return true if <sq> is attacked by any piece of color <by>.
 * Used for check detection and castling path validation.
 */
bool is_square_attacked(const Board& board, Square sq, Color by) noexcept;

/**
 * Return true if the king of <c> is currently in check.
 */
bool is_in_check(const Board& board, Color c) noexcept;

// ─── Move generation ─────────────────────────────────────────────────────────

/**
 * Generate all legal moves for the side to move.
 * A position with no legal moves is either checkmate or stalemate;
 * use is_in_check() to distinguish.
 */
MoveList generate_legal_moves(Board& board);

/**
 * Reference implementation of legal move generation (used by tests for differential testing).
 * Uses naive make_move / is_in_check / unmake_move for every candidate move.
 */
MoveList generate_legal_moves_reference(Board& board);

// ─── UCI move resolution ──────────────────────────────────────────────────────

/**
 * Given a partially-parsed Move (from move_from_uci — only from/to/promotion
 * fields set), annotate all missing flags by consulting the board.
 *
 * Returns NULL_MOVE if the move is not legal in the current position.
 * This is the safe path from UCI text input to make_move().
 */
Move resolve_move(Board& board, Move partial);

} // namespace chess
