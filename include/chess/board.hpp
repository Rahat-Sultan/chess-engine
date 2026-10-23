#pragma once

/**
 * board.hpp — Board position representation
 *
 * D-5: 64-square array representation.
 * The Board class stores the complete chess position and supports:
 *   - FEN parsing and serialization
 *   - make_move / unmake_move (fully reversible)
 *   - Castling rights, en-passant target, halfmove clock, fullmove number
 *   - Side to move
 *
 * Move generation is handled in movegen.hpp (Phase 2).
 * The Board interface is kept stable so the representation can be
 * optimized to bitboards later without changing callers.
 */

#include "chess/types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <optional>

namespace chess {

// ─── FEN constants ────────────────────────────────────────────────────────────

inline constexpr const char* STARTING_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// ─── Undo record ─────────────────────────────────────────────────────────────
//
// Everything needed to fully restore the position after unmake_move.

struct UndoInfo {
    Piece    captured;          // piece on the to-square (NO_PIECE if none)
    Square   ep_square;         // en-passant target before the move
    uint8_t  castling_rights;   // castling rights before the move
    int      halfmove_clock;    // halfmove clock before the move
    // fullmove_number is derivable; we store it for simplicity
    int      fullmove_number;
    uint64_t zobrist_key;       // position hash before the move
};

// ─── Board ───────────────────────────────────────────────────────────────────

class Board {
public:
    // ── Construction ─────────────────────────────────────────────────────────

    Board();  // initializes to the standard starting position

    /**
     * Construct from a FEN string.
     * Throws std::invalid_argument if the FEN is malformed or invalid.
     */
    explicit Board(const std::string& fen);

    // ── FEN ──────────────────────────────────────────────────────────────────

    /**
     * Set the board from a FEN string.
     * Throws std::invalid_argument if the FEN is malformed or invalid.
     */
    void set_fen(const std::string& fen);

    /**
     * Serialize the current position to a FEN string.
     * The returned string is always the canonical six-field FEN.
     */
    std::string fen() const;

    // ── Position queries ─────────────────────────────────────────────────────

    Piece piece_at(Square s) const noexcept { return squares_[static_cast<std::size_t>(s)]; }
    Color side_to_move()     const noexcept { return side_to_move_; }
    Square ep_square()       const noexcept { return ep_square_; }
    uint8_t castling_rights()const noexcept { return castling_rights_; }
    int halfmove_clock()     const noexcept { return halfmove_clock_; }
    int fullmove_number()    const noexcept { return fullmove_number_; }

    /** Zobrist hash key of the current position. */
    uint64_t zobrist_key()   const noexcept { return zobrist_key_; }

    bool can_castle_wk() const noexcept { return (castling_rights_ & CR_WK) != 0; }
    bool can_castle_wq() const noexcept { return (castling_rights_ & CR_WQ) != 0; }
    bool can_castle_bk() const noexcept { return (castling_rights_ & CR_BK) != 0; }
    bool can_castle_bq() const noexcept { return (castling_rights_ & CR_BQ) != 0; }

    /** Return the square the side-to-move king is on, or NO_SQ if not found. */
    Square king_square(Color c) const noexcept;

    // ── Move application ─────────────────────────────────────────────────────

    /**
     * Apply a move to the board and return an UndoInfo that can be passed
     * to unmake_move to restore the exact previous position.
     *
     * The move must be legal (caller's responsibility in Phase 1;
     * movegen enforces legality in Phase 2).
     */
    UndoInfo make_move(const Move& m);

    /**
     * Undo the last move, restoring the position to what it was before
     * make_move was called with the same move and undo info.
     */
    void unmake_move(const Move& m, const UndoInfo& undo);

    // ── Equality ─────────────────────────────────────────────────────────────

    bool operator==(const Board& other) const noexcept;
    bool operator!=(const Board& other) const noexcept { return !(*this == other); }

    // ── Debug ────────────────────────────────────────────────────────────────

    /** ASCII board dump to a string, useful in test failure messages. */
    std::string to_ascii() const;

private:
    // 64-square array, index 0 = a1, index 63 = h8
    std::array<Piece, 64> squares_{};

    std::array<Square, 2> king_square_{E1, E8}; // Cached king square for [White=0, Black=1]

    Color   side_to_move_    = Color::White;
    Square  ep_square_       = NO_SQ;        // D-9
    uint8_t castling_rights_ = CR_ALL;       // D-8
    int     halfmove_clock_  = 0;
    int     fullmove_number_ = 1;
    uint64_t zobrist_key_    = 0;   // D-15: Zobrist hash for transposition table

    // ── Private helpers ───────────────────────────────────────────────────────
    void clear();
    void place_piece(Square s, Piece p) noexcept  { squares_[static_cast<std::size_t>(s)] = p; }
    void remove_piece(Square s)         noexcept  { squares_[static_cast<std::size_t>(s)] = NO_PIECE; }

    // Update castling rights when a piece moves from/to a critical square
    void revoke_castling(Square from, Square to) noexcept;

    // Recompute the full Zobrist key from scratch (called by set_fen)
    void compute_zobrist() noexcept;
};

} // namespace chess
