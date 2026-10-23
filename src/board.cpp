/**
 * board.cpp — Board position implementation
 *
 * See include/chess/board.hpp for the full interface documentation.
 */

#include "chess/board.hpp"

#include <cassert>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace chess {

// ─── Zobrist hashing keys (D-15) ──────────────────────────────────────────────
//
// Generated at compile time with a SplitMix64 PRNG seeded with a fixed value
// so the keys are deterministic across builds and platforms.

namespace zobrist_detail {

// SplitMix64 — deterministic, portable
constexpr uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// 12 piece types (6 piece types × 2 colors), 64 squares each
constexpr std::size_t NUM_PIECE_KEYS = 12 * 64;
constexpr std::size_t NUM_CASTLING_KEYS = 16;
constexpr std::size_t NUM_EP_KEYS = 8;

struct ZobristKeys {
    std::array<uint64_t, NUM_PIECE_KEYS>   pieces{};
    std::array<uint64_t, NUM_CASTLING_KEYS> castling{};
    std::array<uint64_t, NUM_EP_KEYS>      ep{};
    uint64_t                               side = 0;

    constexpr ZobristKeys() {
        uint64_t state = 0x1234567890abcdefULL;
        for (auto& k : pieces)   k = splitmix64(state);
        for (auto& k : castling) k = splitmix64(state);
        for (auto& k : ep)      k = splitmix64(state);
        side = splitmix64(state);
    }
};

constexpr ZobristKeys ZK{};

// Map (color, pieceType) to a flat index [0..11]
constexpr std::size_t piece_index(Color c, PieceType pt) noexcept {
    return static_cast<std::size_t>(c) * 6 +
           (static_cast<std::size_t>(pt) - 1);
}

inline uint64_t zobrist_piece_key(Color c, PieceType pt, Square s) noexcept {
    assert(pt != PieceType::None);
    assert(s >= 0 && s < 64);
    return ZK.pieces[piece_index(c, pt) * 64 + static_cast<std::size_t>(s)];
}

} // namespace zobrist_detail

// ─── Zobrist key computation ─────────────────────────────────────────────────

void Board::compute_zobrist() noexcept {
    zobrist_key_ = 0;
    for (Square s = 0; s < 64; ++s) {
        const Piece& p = squares_[static_cast<std::size_t>(s)];
        if (!p.is_empty()) {
            zobrist_key_ ^= zobrist_detail::ZK.pieces[
                zobrist_detail::piece_index(p.color, p.type) * 64 + s];
        }
    }
    if (side_to_move_ == Color::Black)
        zobrist_key_ ^= zobrist_detail::ZK.side;
    zobrist_key_ ^= zobrist_detail::ZK.castling[castling_rights_];
    if (ep_square_ != NO_SQ)
        zobrist_key_ ^= zobrist_detail::ZK.ep[file_of(ep_square_)];
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Map a FEN piece character to a Piece.
// Uppercase = White, lowercase = Black.
static Piece piece_from_fen_char(char c) {
    Color color = std::isupper(static_cast<unsigned char>(c))
                  ? Color::White : Color::Black;
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'p': return make_piece(color, PieceType::Pawn);
        case 'n': return make_piece(color, PieceType::Knight);
        case 'b': return make_piece(color, PieceType::Bishop);
        case 'r': return make_piece(color, PieceType::Rook);
        case 'q': return make_piece(color, PieceType::Queen);
        case 'k': return make_piece(color, PieceType::King);
        default:  throw std::invalid_argument(
                      std::string("Unknown FEN piece character: ") + c);
    }
}

// Map a Piece back to a FEN character.
static char fen_char_from_piece(const Piece& p) {
    char c = '?';
    switch (p.type) {
        case PieceType::Pawn:   c = 'p'; break;
        case PieceType::Knight: c = 'n'; break;
        case PieceType::Bishop: c = 'b'; break;
        case PieceType::Rook:   c = 'r'; break;
        case PieceType::Queen:  c = 'q'; break;
        case PieceType::King:   c = 'k'; break;
        default: return '?';
    }
    if (p.color == Color::White)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return c;
}

// ─── Board construction ───────────────────────────────────────────────────────

Board::Board() {
    set_fen(STARTING_FEN);
}

Board::Board(const std::string& fen) {
    set_fen(fen);
}

// ─── clear ───────────────────────────────────────────────────────────────────

void Board::clear() {
    squares_.fill(NO_PIECE);
    side_to_move_    = Color::White;
    ep_square_       = NO_SQ;
    castling_rights_ = CR_NONE;
    halfmove_clock_  = 0;
    fullmove_number_ = 1;
    king_square_[static_cast<std::size_t>(Color::White)] = NO_SQ;
    king_square_[static_cast<std::size_t>(Color::Black)] = NO_SQ;
}

// ─── set_fen ─────────────────────────────────────────────────────────────────

void Board::set_fen(const std::string& fen) {
    // Split into up to 6 space-separated tokens
    std::istringstream ss(fen);
    std::string pieces_str, side_str, castling_str, ep_str;
    std::string halfmove_str = "0", fullmove_str = "1";

    if (!(ss >> pieces_str))
        throw std::invalid_argument("FEN: missing piece placement field");
    if (!(ss >> side_str))
        throw std::invalid_argument("FEN: missing side-to-move field");
    if (!(ss >> castling_str))
        throw std::invalid_argument("FEN: missing castling rights field");
    if (!(ss >> ep_str))
        throw std::invalid_argument("FEN: missing en-passant field");
    ss >> halfmove_str;   // optional in some contexts, default 0
    ss >> fullmove_str;   // optional, default 1

    clear();

    // ── 1. Piece placement ────────────────────────────────────────────────────
    // FEN piece placement starts at rank 8 (top of the board) and goes down.
    int rank = 7;
    int file = 0;
    for (char c : pieces_str) {
        if (c == '/') {
            if (file != 8)
                throw std::invalid_argument("FEN: rank did not have exactly 8 squares");
            --rank;
            file = 0;
            if (rank < 0)
                throw std::invalid_argument("FEN: too many rank separators");
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            int empty = c - '0';
            if (empty < 1 || empty > 8)
                throw std::invalid_argument("FEN: invalid empty-square digit");
            file += empty;
            if (file > 8)
                throw std::invalid_argument("FEN: rank overflow");
        } else {
            if (file >= 8)
                throw std::invalid_argument("FEN: rank overflow");
            Square sq = make_square(file, rank);
            squares_[static_cast<std::size_t>(sq)] = piece_from_fen_char(c);  // throws on unknown char
            ++file;
        }
    }
    if (rank != 0 || file != 8)
        throw std::invalid_argument("FEN: incomplete piece placement");

    // ── 2. Side to move ───────────────────────────────────────────────────────
    if (side_str == "w")       side_to_move_ = Color::White;
    else if (side_str == "b")  side_to_move_ = Color::Black;
    else throw std::invalid_argument("FEN: invalid side-to-move '" + side_str + "'");

    // ── 3. Castling rights ────────────────────────────────────────────────────
    castling_rights_ = CR_NONE;
    if (castling_str != "-") {
        for (char c : castling_str) {
            switch (c) {
                case 'K': castling_rights_ |= CR_WK; break;
                case 'Q': castling_rights_ |= CR_WQ; break;
                case 'k': castling_rights_ |= CR_BK; break;
                case 'q': castling_rights_ |= CR_BQ; break;
                default:
                    throw std::invalid_argument(
                        std::string("FEN: unknown castling char '") + c + "'");
            }
        }
    }

    // ── 4. En-passant target ──────────────────────────────────────────────────
    if (ep_str == "-") {
        ep_square_ = NO_SQ;
    } else {
        ep_square_ = square_from_name(ep_str);
        if (ep_square_ == NO_SQ)
            throw std::invalid_argument("FEN: invalid en-passant square '" + ep_str + "'");
        // Validate: must be on rank 3 (White just pushed) or rank 6 (Black just pushed)
        int ep_rank = rank_of(ep_square_);
        if (ep_rank != 2 && ep_rank != 5)
            throw std::invalid_argument("FEN: en-passant square not on rank 3 or 6");
    }

    // ── 5. Halfmove clock ─────────────────────────────────────────────────────
    try {
        halfmove_clock_ = std::stoi(halfmove_str);
    } catch (...) {
        throw std::invalid_argument("FEN: invalid halfmove clock '" + halfmove_str + "'");
    }
    if (halfmove_clock_ < 0)
        throw std::invalid_argument("FEN: negative halfmove clock");

    // ── 6. Fullmove number ────────────────────────────────────────────────────
    try {
        fullmove_number_ = std::stoi(fullmove_str);
    } catch (...) {
        throw std::invalid_argument("FEN: invalid fullmove number '" + fullmove_str + "'");
    }
    if (fullmove_number_ < 1)
        throw std::invalid_argument("FEN: fullmove number must be >= 1");

    // Cache king squares
    king_square_[static_cast<std::size_t>(Color::White)] = NO_SQ;
    king_square_[static_cast<std::size_t>(Color::Black)] = NO_SQ;
    for (Square s = 0; s < 64; ++s) {
        const Piece& p = squares_[static_cast<std::size_t>(s)];
        if (p.type == PieceType::King) {
            king_square_[static_cast<std::size_t>(p.color)] = s;
        }
    }

    // Compute Zobrist key for the new position
    compute_zobrist();
}

// ─── fen ─────────────────────────────────────────────────────────────────────

std::string Board::fen() const {
    std::string result;
    result.reserve(80);

    // ── 1. Piece placement (rank 8 down to rank 1) ────────────────────────────
    for (int rank = 7; rank >= 0; --rank) {
        int empty_run = 0;
        for (int file = 0; file < 8; ++file) {
            Square sq = make_square(file, rank);
            const Piece& p = squares_[static_cast<std::size_t>(sq)];
            if (p.is_empty()) {
                ++empty_run;
            } else {
                if (empty_run > 0) {
                    result += static_cast<char>('0' + empty_run);
                    empty_run = 0;
                }
                result += fen_char_from_piece(p);
            }
        }
        if (empty_run > 0)
            result += static_cast<char>('0' + empty_run);
        if (rank > 0) result += '/';
    }

    // ── 2. Side to move ───────────────────────────────────────────────────────
    result += ' ';
    result += (side_to_move_ == Color::White) ? 'w' : 'b';

    // ── 3. Castling rights ────────────────────────────────────────────────────
    result += ' ';
    if (castling_rights_ == CR_NONE) {
        result += '-';
    } else {
        if (castling_rights_ & CR_WK) result += 'K';
        if (castling_rights_ & CR_WQ) result += 'Q';
        if (castling_rights_ & CR_BK) result += 'k';
        if (castling_rights_ & CR_BQ) result += 'q';
    }

    // ── 4. En-passant ─────────────────────────────────────────────────────────
    result += ' ';
    result += square_name(ep_square_);

    // ── 5. Halfmove clock ─────────────────────────────────────────────────────
    result += ' ';
    result += std::to_string(halfmove_clock_);

    // ── 6. Fullmove number ────────────────────────────────────────────────────
    result += ' ';
    result += std::to_string(fullmove_number_);

    return result;
}

// ─── king_square ─────────────────────────────────────────────────────────────

Square Board::king_square(Color c) const noexcept {
#ifndef NDEBUG
    Square scanned = NO_SQ;
    for (Square s = 0; s < 64; ++s) {
        const Piece& p = squares_[static_cast<std::size_t>(s)];
        if (p.type == PieceType::King && p.color == c) {
            scanned = s;
            break;
        }
    }
    assert(king_square_[static_cast<std::size_t>(c)] == scanned && "King square cache mismatch!");
#endif
    return king_square_[static_cast<std::size_t>(c)];
}

// ─── revoke_castling ─────────────────────────────────────────────────────────
//
// Called during make_move: clear the appropriate castling rights whenever a
// king or rook moves from its starting square, or a rook's home square is
// captured.

void Board::revoke_castling(Square from, Square to) noexcept {
    // Any move from or to these squares revokes the matching right.
    // Using a lookup is cleaner than a chain of if/else.
    auto revoke = [&](Square sq) {
        switch (sq) {
            case E1: castling_rights_ &= static_cast<uint8_t>(~(CR_WK | CR_WQ)); break;
            case H1: castling_rights_ &= static_cast<uint8_t>(~CR_WK);           break;
            case A1: castling_rights_ &= static_cast<uint8_t>(~CR_WQ);           break;
            case E8: castling_rights_ &= static_cast<uint8_t>(~(CR_BK | CR_BQ)); break;
            case H8: castling_rights_ &= static_cast<uint8_t>(~CR_BK);           break;
            case A8: castling_rights_ &= static_cast<uint8_t>(~CR_BQ);           break;
            default: break;
        }
    };
    revoke(from);
    revoke(to);
}

// ─── make_move ───────────────────────────────────────────────────────────────

UndoInfo Board::make_move(const Move& m) {
    // Snapshot everything needed to undo
    UndoInfo undo;
    undo.captured        = squares_[static_cast<std::size_t>(m.to)];
    undo.ep_square       = ep_square_;
    undo.castling_rights = castling_rights_;
    undo.halfmove_clock  = halfmove_clock_;
    undo.fullmove_number = fullmove_number_;
    undo.zobrist_key     = zobrist_key_;

    const Piece moving = squares_[static_cast<std::size_t>(m.from)];

    // ── Halfmove clock ────────────────────────────────────────────────────────
    if (moving.type == PieceType::Pawn || !undo.captured.is_empty())
        halfmove_clock_ = 0;
    else
        ++halfmove_clock_;

    // ── Fullmove number ───────────────────────────────────────────────────────
    if (side_to_move_ == Color::Black)
        ++fullmove_number_;

    // ── Clear en-passant target (set again below if double push) ─────────────
    ep_square_ = NO_SQ;

    // ── Update castling rights ────────────────────────────────────────────────
    revoke_castling(m.from, m.to);

    // ── Update king square cache ──────────────────────────────────────────────
    if (moving.type == PieceType::King) {
        king_square_[static_cast<std::size_t>(moving.color)] = m.to;
    }
    if (!undo.captured.is_empty() && undo.captured.type == PieceType::King) {
        king_square_[static_cast<std::size_t>(undo.captured.color)] = NO_SQ;
    }

    // ── Apply the move & update Zobrist key incrementally ─────────────────────

    // 1. Remove old en-passant file if one was active
    if (undo.ep_square != NO_SQ) {
        zobrist_key_ ^= zobrist_detail::ZK.ep[static_cast<std::size_t>(file_of(undo.ep_square))];
    }

    // 2. Update castling rights key if changed
    if (castling_rights_ != undo.castling_rights) {
        zobrist_key_ ^= zobrist_detail::ZK.castling[undo.castling_rights];
        zobrist_key_ ^= zobrist_detail::ZK.castling[castling_rights_];
    }

    // 3. Piece movements and captures
    if (m.is_castling()) {
        // Move king
        if (!moving.is_empty()) {
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(moving.color, moving.type, m.from);
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(moving.color, moving.type, m.to);
        }
        remove_piece(m.from);
        place_piece(m.to, moving);

        // Move rook: undo.captured stores the rook piece before move
        Square rook_from = NO_SQ;
        Square rook_to   = NO_SQ;
        if (m.to == G1)      { rook_from = H1; rook_to = F1; }
        else if (m.to == C1) { rook_from = A1; rook_to = D1; }
        else if (m.to == G8) { rook_from = H8; rook_to = F8; }
        else if (m.to == C8) { rook_from = A8; rook_to = D8; }

        if (rook_from != NO_SQ) {
            undo.captured = squares_[static_cast<std::size_t>(rook_from)];
            if (!undo.captured.is_empty()) {
                remove_piece(rook_from);
                place_piece(rook_to, undo.captured);
                zobrist_key_ ^= zobrist_detail::zobrist_piece_key(undo.captured.color, undo.captured.type, rook_from);
                zobrist_key_ ^= zobrist_detail::zobrist_piece_key(undo.captured.color, undo.captured.type, rook_to);
            }
        }

    } else if (m.is_en_passant()) {
        // Remove the captured pawn (not on m.to but one rank back)
        int captured_rank = (side_to_move_ == Color::White) ? rank_of(m.to) - 1
                                                             : rank_of(m.to) + 1;
        Square captured_sq = make_square(file_of(m.to), captured_rank);
        undo.captured = squares_[static_cast<std::size_t>(captured_sq)];  // overwrite with actual captured piece
        if (!undo.captured.is_empty()) {
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(undo.captured.color, undo.captured.type, captured_sq);
        }
        remove_piece(captured_sq);

        if (!moving.is_empty()) {
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(moving.color, moving.type, m.from);
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(moving.color, moving.type, m.to);
        }
        remove_piece(m.from);
        place_piece(m.to, moving);

    } else if (m.is_promotion()) {
        if (!moving.is_empty()) {
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(moving.color, moving.type, m.from);
        }
        remove_piece(m.from);

        // Remove any piece on the destination (capture-promotion)
        if (!undo.captured.is_empty()) {
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(undo.captured.color, undo.captured.type, m.to);
        }
        remove_piece(m.to);

        Piece promo = make_piece(side_to_move_, m.promotion_piece);
        zobrist_key_ ^= zobrist_detail::zobrist_piece_key(promo.color, promo.type, m.to);
        place_piece(m.to, promo);

    } else {
        // Normal move (including captures)
        if (!moving.is_empty()) {
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(moving.color, moving.type, m.from);
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(moving.color, moving.type, m.to);
        }
        remove_piece(m.from);

        if (!undo.captured.is_empty()) {
            zobrist_key_ ^= zobrist_detail::zobrist_piece_key(undo.captured.color, undo.captured.type, m.to);
        }
        place_piece(m.to, moving);
    }

    // Set en-passant target for double pawn push
    if (m.is_double_push()) {
        int ep_rank = (side_to_move_ == Color::White) ? 2 : 5;
        ep_square_ = make_square(file_of(m.from), ep_rank);
        zobrist_key_ ^= zobrist_detail::ZK.ep[static_cast<std::size_t>(file_of(ep_square_))];
    }

    // ── Toggle side to move ───────────────────────────────────────────────────
    zobrist_key_ ^= zobrist_detail::ZK.side;
    side_to_move_ = opponent(side_to_move_);

#ifndef NDEBUG
    uint64_t incremental_key = zobrist_key_;
    compute_zobrist();
    assert(incremental_key == zobrist_key_ && "Incremental Zobrist mismatch in make_move!");
#endif

    return undo;
}

// ─── unmake_move ─────────────────────────────────────────────────────────────

void Board::unmake_move(const Move& m, const UndoInfo& undo) {
    // Flip side back first — the moving side is the one that made the move
    side_to_move_ = opponent(side_to_move_);

    // Restore state fields
    ep_square_       = undo.ep_square;
    castling_rights_ = undo.castling_rights;
    halfmove_clock_  = undo.halfmove_clock;
    fullmove_number_ = undo.fullmove_number;
    zobrist_key_     = undo.zobrist_key;

    const Piece moving = squares_[static_cast<std::size_t>(m.to)];  // the piece that moved (before promotion swap)

    // ── Restore king square cache ─────────────────────────────────────────────
    if (m.is_castling() || moving.type == PieceType::King) {
        king_square_[static_cast<std::size_t>(side_to_move_)] = m.from;
    }
    if (!undo.captured.is_empty() && undo.captured.type == PieceType::King) {
        king_square_[static_cast<std::size_t>(undo.captured.color)] = m.to;
    }

    if (m.is_castling()) {
        // Restore king
        place_piece(m.from, moving);
        remove_piece(m.to);

        // Restore rook
        Square rook_from = NO_SQ;
        Square rook_to   = NO_SQ;
        if (m.to == G1)      { rook_from = H1; rook_to = F1; }
        else if (m.to == C1) { rook_from = A1; rook_to = D1; }
        else if (m.to == G8) { rook_from = H8; rook_to = F8; }
        else if (m.to == C8) { rook_from = A8; rook_to = D8; }

        if (rook_from != NO_SQ && !undo.captured.is_empty()) {
            place_piece(rook_from, undo.captured);
            remove_piece(rook_to);
        }

    } else if (m.is_en_passant()) {
        place_piece(m.from, moving);
        remove_piece(m.to);
        // Restore the captured pawn to its actual square
        int captured_rank = (side_to_move_ == Color::White) ? rank_of(m.to) - 1
                                                             : rank_of(m.to) + 1;
        Square captured_sq = make_square(file_of(m.to), captured_rank);
        place_piece(captured_sq, undo.captured);

    } else if (m.is_promotion()) {
        // Restore original pawn on from-square
        place_piece(m.from, make_piece(side_to_move_, PieceType::Pawn));
        // Restore captured piece (or empty) on to-square
        place_piece(m.to, undo.captured);

    } else {
        // Normal move or capture
        place_piece(m.from, moving);
        place_piece(m.to, undo.captured);
    }

#ifndef NDEBUG
    uint64_t restored_key = zobrist_key_;
    compute_zobrist();
    assert(restored_key == zobrist_key_ && "Incremental Zobrist mismatch in unmake_move!");
#endif
}

// ─── operator== ──────────────────────────────────────────────────────────────

bool Board::operator==(const Board& other) const noexcept {
    return squares_          == other.squares_
        && side_to_move_     == other.side_to_move_
        && ep_square_        == other.ep_square_
        && castling_rights_  == other.castling_rights_
        && halfmove_clock_   == other.halfmove_clock_
        && fullmove_number_  == other.fullmove_number_;
}

// ─── to_ascii ────────────────────────────────────────────────────────────────

std::string Board::to_ascii() const {
    std::string out;
    out += "  a b c d e f g h\n";
    for (int rank = 7; rank >= 0; --rank) {
        out += static_cast<char>('1' + rank);
        out += ' ';
        for (int file = 0; file < 8; ++file) {
            Piece p = squares_[static_cast<std::size_t>(make_square(file, rank))];
            out += p.is_empty() ? '.' : fen_char_from_piece(p);
            out += ' ';
        }
        out += static_cast<char>('1' + rank);
        out += '\n';
    }
    out += "  a b c d e f g h\n";
    out += "Side: ";
    out += (side_to_move_ == Color::White ? "White" : "Black");
    out += "  Castling: ";
    if (castling_rights_ == CR_NONE) out += '-';
    else {
        if (castling_rights_ & CR_WK) out += 'K';
        if (castling_rights_ & CR_WQ) out += 'Q';
        if (castling_rights_ & CR_BK) out += 'k';
        if (castling_rights_ & CR_BQ) out += 'q';
    }
    out += "  EP: " + square_name(ep_square_) + "\n";
    return out;
}

} // namespace chess
