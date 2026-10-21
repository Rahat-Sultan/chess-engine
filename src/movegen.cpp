/**
 * movegen.cpp — Move generation and legality implementation
 *
 * Strategy:
 *   1. Generate pseudo-legal moves for every piece of the side to move.
 *   2. Filter to legal moves by making each candidate, checking whether
 *      the moving side's king is left in check, then unmaking it.
 *
 * Pseudo-legal generators:
 *   - Pawns: single push, double push, captures, en-passant, promotion
 *   - Knights, Bishops, Rooks, Queens, Kings: standard offsets / sliding
 *   - Castling: checked for empty path and no check through transit
 *
 * Square layout:  a1=0 … h1=7, a2=8 … h8=63  (rank*8 + file)
 * White advances: +8   Black advances: -8
 */

#include "chess/movegen.hpp"
#include "chess/board.hpp"
#include "chess/types.hpp"

namespace chess {

// ─── Internal helpers ─────────────────────────────────────────────────────────

// Sliding piece directions
static constexpr int BISHOP_DIRS[4] = {  7,  9, -7, -9 };
static constexpr int ROOK_DIRS[4]   = {  8,  1, -8, -1 };
static constexpr int QUEEN_DIRS[8]  = {  8,  1, -8, -1,  7,  9, -7, -9 };
static constexpr int KNIGHT_JUMPS[8]= { 17, 15, 10,  6,-17,-15,-10, -6 };
static constexpr int KING_STEPS[8]  = {  8,  1, -8, -1,  7,  9, -7, -9 };

// Check that sliding in direction `dir` from square `from` to `to` stays
// on the board (no wrap-around across file boundaries).
// The "distance" heuristic: if files differ by more than 1 in a single step
// of a non-rank direction, we've wrapped.
static bool valid_slide_step(Square from, Square to, int dir) noexcept {
    if (!is_valid_sq(to)) return false;
    // Detect file wrap: for diagonal/horizontal moves the file must differ by ≤1
    int df = file_of(to) - file_of(from);
    (void)dir;
    // For rank-only dirs (±8) file must be same; for file-only (±1) rank same;
    // for diagonals (±7, ±9) both can change by exactly 1.
    // Simplest universal guard: |df| must be 0 or 1.
    return df >= -1 && df <= 1;
}

// More precise: for a single knight jump, file difference must be 1 or 2.
static bool valid_knight_jump(Square from, Square to) noexcept {
    if (!is_valid_sq(to)) return false;
    int df = file_of(to) - file_of(from);
    if (df < 0) df = -df;
    return df == 1 || df == 2;
}

// ─── is_square_attacked ───────────────────────────────────────────────────────

bool is_square_attacked(const Board& board, Square sq, Color by) noexcept {
    // ── Pawns ─────────────────────────────────────────────────────────────────
    // A pawn of color <by> attacks <sq> from one rank behind (relative to <by>).
    {
        int attack_rank_offset = (by == Color::White) ? -1 : 1;
        int r = rank_of(sq) + attack_rank_offset;
        if (r >= 0 && r <= 7) {
            int f = file_of(sq);
            if (f > 0) {
                Square pawn_sq = make_square(f - 1, r);
                Piece p = board.piece_at(pawn_sq);
                if (p.type == PieceType::Pawn && p.color == by) return true;
            }
            if (f < 7) {
                Square pawn_sq = make_square(f + 1, r);
                Piece p = board.piece_at(pawn_sq);
                if (p.type == PieceType::Pawn && p.color == by) return true;
            }
        }
    }

    // ── Knights ───────────────────────────────────────────────────────────────
    for (int jump : KNIGHT_JUMPS) {
        Square from = sq + jump;
        if (!valid_knight_jump(sq, from)) continue;
        Piece p = board.piece_at(from);
        if (p.type == PieceType::Knight && p.color == by) return true;
    }

    // ── Bishops / Queens (diagonal) ───────────────────────────────────────────
    for (int dir : BISHOP_DIRS) {
        Square cur = sq + dir;
        while (is_valid_sq(cur) && valid_slide_step(cur - dir, cur, dir)) {
            Piece p = board.piece_at(cur);
            if (!p.is_empty()) {
                if (p.color == by &&
                    (p.type == PieceType::Bishop || p.type == PieceType::Queen))
                    return true;
                break;  // blocked
            }
            cur += dir;
        }
    }

    // ── Rooks / Queens (straight) ─────────────────────────────────────────────
    for (int dir : ROOK_DIRS) {
        Square cur = sq + dir;
        while (is_valid_sq(cur) && valid_slide_step(cur - dir, cur, dir)) {
            Piece p = board.piece_at(cur);
            if (!p.is_empty()) {
                if (p.color == by &&
                    (p.type == PieceType::Rook || p.type == PieceType::Queen))
                    return true;
                break;
            }
            cur += dir;
        }
    }

    // ── King ─────────────────────────────────────────────────────────────────
    for (int step : KING_STEPS) {
        Square from = sq + step;
        if (!is_valid_sq(from)) continue;
        if (!valid_slide_step(sq, from, step)) continue;
        Piece p = board.piece_at(from);
        if (p.type == PieceType::King && p.color == by) return true;
    }

    return false;
}

bool is_in_check(const Board& board, Color c) noexcept {
    Square ksq = board.king_square(c);
    if (ksq == NO_SQ) return false;  // shouldn't happen in a legal position
    return is_square_attacked(board, ksq, opponent(c));
}

// ─── Pseudo-legal move generators ────────────────────────────────────────────
// Each push moves into `list`.  Legality (king safety) is enforced later.

static void gen_pawns(const Board& board, Color us, MoveList& list) {
    const int push_dir   = (us == Color::White) ?  8 : -8;
    const int start_rank = (us == Color::White) ?  1 :  6;
    const int promo_rank = (us == Color::White) ?  6 :  1;  // rank before promotion

    static constexpr PieceType PROMO_PIECES[4] = {
        PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight
    };

    for (Square from = 0; from < 64; ++from) {
        Piece p = board.piece_at(from);
        if (p.type != PieceType::Pawn || p.color != us) continue;

        int r = rank_of(from);
        int f = file_of(from);

        // ── Single push ───────────────────────────────────────────────────────
        Square to = from + push_dir;
        if (is_valid_sq(to) && board.piece_at(to).is_empty()) {
            if (r == promo_rank) {
                for (PieceType pt : PROMO_PIECES) {
                    Move m;
                    m.from = from; m.to = to;
                    m.flags = MF_PROMOTION;
                    m.promotion_piece = pt;
                    list.push(m);
                }
            } else {
                Move m; m.from = from; m.to = to;
                list.push(m);
            }

            // ── Double push ───────────────────────────────────────────────────
            if (r == start_rank) {
                Square to2 = to + push_dir;
                if (is_valid_sq(to2) && board.piece_at(to2).is_empty()) {
                    Move m; m.from = from; m.to = to2;
                    m.flags = MF_DOUBLE_PUSH;
                    list.push(m);
                }
            }
        }

        // ── Captures (including promotion-captures) ────────────────────────────
        int cap_files[2] = { f - 1, f + 1 };
        for (int cf : cap_files) {
            if (cf < 0 || cf > 7) continue;
            Square cap_sq = make_square(cf, r + (push_dir / 8));
            if (!is_valid_sq(cap_sq)) continue;

            // Normal capture
            Piece target = board.piece_at(cap_sq);
            if (!target.is_empty() && target.color != us) {
                if (r == promo_rank) {
                    for (PieceType pt : PROMO_PIECES) {
                        Move m;
                        m.from = from; m.to = cap_sq;
                        m.flags = MF_CAPTURE | MF_PROMOTION;
                        m.promotion_piece = pt;
                        list.push(m);
                    }
                } else {
                    Move m; m.from = from; m.to = cap_sq;
                    m.flags = MF_CAPTURE;
                    list.push(m);
                }
            }

            // En-passant capture
            if (cap_sq == board.ep_square()) {
                Move m; m.from = from; m.to = cap_sq;
                m.flags = MF_EN_PASSANT | MF_CAPTURE;
                list.push(m);
            }
        }
    }
}

static void gen_knights(const Board& board, Color us, MoveList& list) {
    for (Square from = 0; from < 64; ++from) {
        Piece p = board.piece_at(from);
        if (p.type != PieceType::Knight || p.color != us) continue;

        for (int jump : KNIGHT_JUMPS) {
            Square to = from + jump;
            if (!valid_knight_jump(from, to)) continue;
            Piece target = board.piece_at(to);
            if (!target.is_empty() && target.color == us) continue;
            Move m; m.from = from; m.to = to;
            if (!target.is_empty()) m.flags = MF_CAPTURE;
            list.push(m);
        }
    }
}

static void gen_sliding(const Board& board, Color us,
                         PieceType pt, const int* dirs, int ndirs,
                         MoveList& list) {
    for (Square from = 0; from < 64; ++from) {
        Piece p = board.piece_at(from);
        if (p.type != pt || p.color != us) continue;

        for (int d = 0; d < ndirs; ++d) {
            int dir = dirs[d];
            Square cur = from + dir;
            while (is_valid_sq(cur) && valid_slide_step(cur - dir, cur, dir)) {
                Piece target = board.piece_at(cur);
                if (target.is_empty()) {
                    Move m; m.from = from; m.to = cur;
                    list.push(m);
                } else {
                    if (target.color != us) {
                        Move m; m.from = from; m.to = cur;
                        m.flags = MF_CAPTURE;
                        list.push(m);
                    }
                    break;
                }
                cur += dir;
            }
        }
    }
}

static void gen_king(const Board& board, Color us, MoveList& list) {
    Square from = board.king_square(us);
    if (from == NO_SQ) return;

    for (int step : KING_STEPS) {
        Square to = from + step;
        if (!is_valid_sq(to)) continue;
        if (!valid_slide_step(from, to, step)) continue;
        Piece target = board.piece_at(to);
        if (!target.is_empty() && target.color == us) continue;
        Move m; m.from = from; m.to = to;
        if (!target.is_empty()) m.flags = MF_CAPTURE;
        list.push(m);
    }
}

static void gen_castling(const Board& board, Color us, MoveList& list) {
    // King must not currently be in check
    if (is_in_check(board, us)) return;

    if (us == Color::White) {
        // King-side: e1→g1, need f1 and g1 empty, f1 and g1 not attacked
        if (board.can_castle_wk()) {
            if (board.piece_at(F1).is_empty() &&
                board.piece_at(G1).is_empty() &&
                !is_square_attacked(board, F1, Color::Black) &&
                !is_square_attacked(board, G1, Color::Black)) {
                Move m; m.from = E1; m.to = G1;
                m.flags = MF_CASTLING;
                list.push(m);
            }
        }
        // Queen-side: e1→c1, need d1, c1, b1 empty, d1 and c1 not attacked
        if (board.can_castle_wq()) {
            if (board.piece_at(D1).is_empty() &&
                board.piece_at(C1).is_empty() &&
                board.piece_at(B1).is_empty() &&
                !is_square_attacked(board, D1, Color::Black) &&
                !is_square_attacked(board, C1, Color::Black)) {
                Move m; m.from = E1; m.to = C1;
                m.flags = MF_CASTLING;
                list.push(m);
            }
        }
    } else {
        // King-side: e8→g8
        if (board.can_castle_bk()) {
            if (board.piece_at(F8).is_empty() &&
                board.piece_at(G8).is_empty() &&
                !is_square_attacked(board, F8, Color::White) &&
                !is_square_attacked(board, G8, Color::White)) {
                Move m; m.from = E8; m.to = G8;
                m.flags = MF_CASTLING;
                list.push(m);
            }
        }
        // Queen-side: e8→c8
        if (board.can_castle_bq()) {
            if (board.piece_at(D8).is_empty() &&
                board.piece_at(C8).is_empty() &&
                board.piece_at(B8).is_empty() &&
                !is_square_attacked(board, D8, Color::White) &&
                !is_square_attacked(board, C8, Color::White)) {
                Move m; m.from = E8; m.to = C8;
                m.flags = MF_CASTLING;
                list.push(m);
            }
        }
    }
}

// ─── generate_legal_moves ─────────────────────────────────────────────────────

MoveList generate_legal_moves(Board& board) {
    Color us = board.side_to_move();
    MoveList pseudo;

    // Generate all pseudo-legal candidates
    gen_pawns   (board, us, pseudo);
    gen_knights (board, us, pseudo);
    gen_sliding (board, us, PieceType::Bishop, BISHOP_DIRS, 4, pseudo);
    gen_sliding (board, us, PieceType::Rook,   ROOK_DIRS,   4, pseudo);
    gen_sliding (board, us, PieceType::Queen,  QUEEN_DIRS,  8, pseudo);
    gen_king    (board, us, pseudo);
    gen_castling(board, us, pseudo);

    // Filter: keep only moves that don't leave our king in check
    MoveList legal;
    for (const Move& m : pseudo) {
        UndoInfo undo = board.make_move(m);
        bool in_check = is_in_check(board, us);
        board.unmake_move(m, undo);
        if (!in_check) legal.push(m);
    }
    return legal;
}

// ─── resolve_move ─────────────────────────────────────────────────────────────

Move resolve_move(Board& board, Move partial) {
    // Generate all legal moves and find the one matching from/to/promotion
    MoveList legal = generate_legal_moves(board);
    for (const Move& m : legal) {
        if (m.from != partial.from || m.to != partial.to) continue;
        // For promotions the caller must supply the promotion piece
        if (m.is_promotion()) {
            if (m.promotion_piece != partial.promotion_piece) continue;
        }
        return m;  // found — return with all flags filled in
    }
    return NULL_MOVE;  // not legal
}

} // namespace chess
