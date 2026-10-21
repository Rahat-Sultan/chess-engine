#pragma once

/**
 * types.hpp — Core chess types
 *
 * Defines every fundamental type used across the engine.
 * No magic integers in other modules: always use these named constants.
 *
 * Score convention (D-7): scores are always from the side-to-move's
 * perspective.  Positive = good for the side to move.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chess {

// ─── Colors ──────────────────────────────────────────────────────────────────

enum class Color : uint8_t {
    White = 0,
    Black = 1,
};

constexpr Color opponent(Color c) noexcept {
    return c == Color::White ? Color::Black : Color::White;
}

// ─── Piece types ─────────────────────────────────────────────────────────────

enum class PieceType : uint8_t {
    None   = 0,
    Pawn   = 1,
    Knight = 2,
    Bishop = 3,
    Rook   = 4,
    Queen  = 5,
    King   = 6,
};

// ─── Pieces (color + type packed into one byte) ───────────────────────────────
//
// Encoding: bits 3-1 = PieceType (1–6), bit 0 = Color (0=White, 1=Black).
// PieceType::None with either color → empty square (NO_PIECE).

struct Piece {
    PieceType type  = PieceType::None;
    Color     color = Color::White;

    constexpr bool is_empty() const noexcept { return type == PieceType::None; }

    constexpr bool operator==(const Piece& o) const noexcept {
        return type == o.type && (type == PieceType::None || color == o.color);
    }
    constexpr bool operator!=(const Piece& o) const noexcept {
        return !(*this == o);
    }
};

inline constexpr Piece NO_PIECE{PieceType::None, Color::White};

// Convenience constructors
constexpr Piece make_piece(Color c, PieceType t) noexcept { return {t, c}; }

// ─── Squares ─────────────────────────────────────────────────────────────────
//
// Squares are numbered 0–63: a1=0, b1=1, …, h1=7, a2=8, …, h8=63.
// This matches the little-endian rank-file mapping common in chess engines.

using Square = int;

constexpr Square NO_SQ = -1;  // Sentinel: no en-passant target, etc.

// Valid range check
constexpr bool is_valid_sq(Square s) noexcept { return s >= 0 && s < 64; }

// Build a square from file [0..7] and rank [0..7]
constexpr Square make_square(int file, int rank) noexcept {
    return rank * 8 + file;
}

constexpr int file_of(Square s) noexcept { return s % 8; }
constexpr int rank_of(Square s) noexcept { return s / 8; }

// Named squares for readability in tests and special-case code
constexpr Square A1 = 0,  B1 = 1,  C1 = 2,  D1 = 3;
constexpr Square E1 = 4,  F1 = 5,  G1 = 6,  H1 = 7;
constexpr Square A8 = 56, B8 = 57, C8 = 58, D8 = 59;
constexpr Square E8 = 60, F8 = 61, G8 = 62, H8 = 63;

/**
 * Parse a UCI/algebraic square name ("a1"–"h8") → Square.
 * Returns NO_SQ if the string is not a valid square name.
 */
inline Square square_from_name(const std::string& s) noexcept {
    if (s.size() != 2) return NO_SQ;
    int file = s[0] - 'a';
    int rank = s[1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return NO_SQ;
    return make_square(file, rank);
}

/**
 * Convert a Square to its UCI name ("a1"–"h8").
 * Returns "-" for NO_SQ.
 */
inline std::string square_name(Square s) noexcept {
    if (s == NO_SQ) return "-";
    std::string name(2, ' ');
    name[0] = static_cast<char>('a' + file_of(s));
    name[1] = static_cast<char>('1' + rank_of(s));
    return name;
}

// ─── Move flags ───────────────────────────────────────────────────────────────

enum MoveFlag : uint32_t {
    MF_NONE      = 0,
    MF_PROMOTION = 1 << 0,   // promotion move
    MF_EN_PASSANT= 1 << 1,   // en-passant capture
    MF_CASTLING  = 1 << 2,   // castling move
    MF_CAPTURE   = 1 << 3,   // any capture (set alongside other flags as needed)
    MF_DOUBLE_PUSH = 1 << 4, // pawn double push (sets en-passant target)
};

// ─── Move ─────────────────────────────────────────────────────────────────────
//
// D-10: Move encoded as from + to + flags in a plain struct (no heap).
// promotion_piece is only meaningful when MF_PROMOTION is set.

struct Move {
    Square    from            = NO_SQ;
    Square    to              = NO_SQ;
    PieceType promotion_piece = PieceType::None;
    uint32_t  flags           = MF_NONE;

    constexpr bool is_valid()     const noexcept { return from != NO_SQ && to != NO_SQ; }
    constexpr bool is_promotion() const noexcept { return (flags & MF_PROMOTION)   != 0; }
    constexpr bool is_en_passant()const noexcept { return (flags & MF_EN_PASSANT)  != 0; }
    constexpr bool is_castling()  const noexcept { return (flags & MF_CASTLING)    != 0; }
    constexpr bool is_capture()   const noexcept { return (flags & MF_CAPTURE)     != 0; }
    constexpr bool is_double_push()const noexcept{ return (flags & MF_DOUBLE_PUSH) != 0; }

    constexpr bool operator==(const Move& o) const noexcept {
        return from == o.from && to == o.to &&
               promotion_piece == o.promotion_piece && flags == o.flags;
    }
    constexpr bool operator!=(const Move& o) const noexcept { return !(*this == o); }
};

inline constexpr Move NULL_MOVE{};  // Invalid sentinel move

/**
 * Format a Move as a UCI coordinate string, e.g. "e2e4", "g7g8q".
 * Returns "(null)" for an invalid move.
 */
inline std::string move_to_uci(const Move& m) noexcept {
    if (!m.is_valid()) return "(null)";
    std::string s = square_name(m.from) + square_name(m.to);
    if (m.is_promotion()) {
        switch (m.promotion_piece) {
            case PieceType::Queen:  s += 'q'; break;
            case PieceType::Rook:   s += 'r'; break;
            case PieceType::Bishop: s += 'b'; break;
            case PieceType::Knight: s += 'n'; break;
            default: break;
        }
    }
    return s;
}

/**
 * Parse a UCI move string into a Move.
 * Only sets from/to/promotion_piece/flags(MF_PROMOTION).
 * The caller (board/movegen) must resolve captures, castling, and
 * en-passant by consulting the board position.
 * Returns NULL_MOVE on parse failure.
 */
inline Move move_from_uci(const std::string& s) noexcept {
    if (s.size() < 4 || s.size() > 5) return NULL_MOVE;
    Square from = square_from_name(s.substr(0, 2));
    Square to   = square_from_name(s.substr(2, 2));
    if (from == NO_SQ || to == NO_SQ) return NULL_MOVE;

    Move m;
    m.from = from;
    m.to   = to;

    if (s.size() == 5) {
        m.flags |= MF_PROMOTION;
        switch (s[4]) {
            case 'q': m.promotion_piece = PieceType::Queen;  break;
            case 'r': m.promotion_piece = PieceType::Rook;   break;
            case 'b': m.promotion_piece = PieceType::Bishop; break;
            case 'n': m.promotion_piece = PieceType::Knight; break;
            default:  return NULL_MOVE;  // unknown promotion char
        }
    }
    return m;
}

// ─── Castling rights ──────────────────────────────────────────────────────────
//
// D-8: 4-bit flag matching FEN "KQkq" order.

enum CastlingRight : uint8_t {
    CR_NONE  = 0,
    CR_WK    = 1 << 0,  // White king-side
    CR_WQ    = 1 << 1,  // White queen-side
    CR_BK    = 1 << 2,  // Black king-side
    CR_BQ    = 1 << 3,  // Black queen-side
    CR_ALL   = CR_WK | CR_WQ | CR_BK | CR_BQ,
};

// ─── Search limits ────────────────────────────────────────────────────────────

struct SearchLimits {
    std::optional<int>      depth;       // maximum search depth (plies)
    std::optional<int>      movetime_ms; // wall-clock limit in milliseconds
    std::optional<uint64_t> nodes;       // node count limit
    bool                    infinite = false; // search until "stop"

    bool is_bounded() const noexcept {
        return depth.has_value() || movetime_ms.has_value() ||
               nodes.has_value() || !infinite;
    }
};

// ─── Search result ────────────────────────────────────────────────────────────

struct SearchResult {
    Move               best_move;
    std::optional<Move>ponder_move;
    int                score      = 0;   // centipawns, side-to-move perspective
    int                depth      = 0;   // depth reached
    uint64_t           nodes      = 0;   // nodes searched
    int                elapsed_ms = 0;   // wall-clock time
    std::vector<Move>  pv;               // principal variation
};

// ─── Score constants ──────────────────────────────────────────────────────────

constexpr int SCORE_INFINITY  =  1'000'000;
constexpr int SCORE_MATE      =    900'000;  // base mate score; subtract ply for precision
constexpr int SCORE_DRAW      =          0;
constexpr int SCORE_MIN       = -SCORE_INFINITY;
constexpr int SCORE_MAX       =  SCORE_INFINITY;

// Determine whether a score is a forced-mate score
constexpr bool is_mate_score(int score) noexcept {
    return score > SCORE_MATE - 1000 || score < -SCORE_MATE + 1000;
}

} // namespace chess
