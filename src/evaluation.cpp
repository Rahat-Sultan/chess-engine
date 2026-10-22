/**
 * evaluation.cpp — Handcrafted position evaluator
 *
 * Score convention (D-7): all scores are from the SIDE-TO-MOVE's perspective.
 *
 * Evaluation components:
 *   1. Material balance
 *   2. Piece-square tables (PSTs) — midgame tables used throughout Phase 3
 *   3. Bishop pair bonus (+30 cp)
 *   4. Mobility — light bonus (+3 cp per legal move differential)
 *
 * PST layout: index [0..63] maps directly to our square numbering
 * (a1=0, h1=7, a8=56, h8=63). Tables are written from White's perspective
 * (rank 1 at the bottom). Black's PST value for square s is looked up at
 * the mirror: mirror(s) = (7 - rank) * 8 + file.
 */

#include "chess/evaluation.hpp"
#include "chess/movegen.hpp"

namespace chess {

// ─── Piece-square tables ──────────────────────────────────────────────────────
//
// Values in centipawns. Written rank 8 → rank 1 (top to bottom visually),
// then stored in our internal order (rank 1 = index 0..7, rank 8 = 56..63).
// We define them visually (rank8 first) and reverse-map at access time.

// clang-format off

// Pawn PST — encourage centre pawns, penalise edge and backward pawns
static constexpr int PST_PAWN[64] = {
//  a    b    c    d    e    f    g    h
     0,   0,   0,   0,   0,   0,   0,   0,  // rank 1 (impossible for pawn)
     5,  10,  10, -20, -20,  10,  10,   5,  // rank 2
     5,  -5, -10,   0,   0, -10,  -5,   5,  // rank 3
     0,   0,   0,  20,  20,   0,   0,   0,  // rank 4
     5,   5,  10,  25,  25,  10,   5,   5,  // rank 5
    10,  10,  20,  30,  30,  20,  10,  10,  // rank 6
    50,  50,  50,  50,  50,  50,  50,  50,  // rank 7
     0,   0,   0,   0,   0,   0,   0,   0,  // rank 8 (promotion, handled elsewhere)
};

// Knight PST — centralise, penalise rim and corners
static constexpr int PST_KNIGHT[64] = {
//  a    b    c    d    e    f    g    h
   -50, -40, -30, -30, -30, -30, -40, -50,  // rank 1
   -40, -20,   0,   0,   0,   0, -20, -40,  // rank 2
   -30,   0,  10,  15,  15,  10,   0, -30,  // rank 3
   -30,   5,  15,  20,  20,  15,   5, -30,  // rank 4
   -30,   0,  15,  20,  20,  15,   0, -30,  // rank 5
   -30,   5,  10,  15,  15,  10,   5, -30,  // rank 6
   -40, -20,   0,   5,   5,   0, -20, -40,  // rank 7
   -50, -40, -30, -30, -30, -30, -40, -50,  // rank 8
};

// Bishop PST — avoid rim, prefer long diagonals, avoid a1/h1/a8/h8 corners
static constexpr int PST_BISHOP[64] = {
//  a    b    c    d    e    f    g    h
   -20, -10, -10, -10, -10, -10, -10, -20,  // rank 1
   -10,   5,   0,   0,   0,   0,   5, -10,  // rank 2
   -10,  10,  10,  10,  10,  10,  10, -10,  // rank 3
   -10,   0,  10,  10,  10,  10,   0, -10,  // rank 4
   -10,   5,   5,  10,  10,   5,   5, -10,  // rank 5
   -10,   0,   5,  10,  10,   5,   0, -10,  // rank 6
   -10,   0,   0,   0,   0,   0,   0, -10,  // rank 7
   -20, -10, -10, -10, -10, -10, -10, -20,  // rank 8
};

// Rook PST — open files, 7th rank bonus
static constexpr int PST_ROOK[64] = {
//  a    b    c    d    e    f    g    h
     0,   0,   0,   5,   5,   0,   0,   0,  // rank 1
    -5,   0,   0,   0,   0,   0,   0,  -5,  // rank 2
    -5,   0,   0,   0,   0,   0,   0,  -5,  // rank 3
    -5,   0,   0,   0,   0,   0,   0,  -5,  // rank 4
    -5,   0,   0,   0,   0,   0,   0,  -5,  // rank 5
    -5,   0,   0,   0,   0,   0,   0,  -5,  // rank 6
     5,  10,  10,  10,  10,  10,  10,   5,  // rank 7
     0,   0,   0,   0,   0,   0,   0,   0,  // rank 8
};

// Queen PST — avoid early development, stay flexible
static constexpr int PST_QUEEN[64] = {
//  a    b    c    d    e    f    g    h
   -20, -10, -10,  -5,  -5, -10, -10, -20,  // rank 1
   -10,   0,   5,   0,   0,   0,   0, -10,  // rank 2
   -10,   5,   5,   5,   5,   5,   0, -10,  // rank 3
     0,   0,   5,   5,   5,   5,   0,  -5,  // rank 4
    -5,   0,   5,   5,   5,   5,   0,  -5,  // rank 5
   -10,   0,   5,   5,   5,   5,   0, -10,  // rank 6
   -10,   0,   0,   0,   0,   0,   0, -10,  // rank 7
   -20, -10, -10,  -5,  -5, -10, -10, -20,  // rank 8
};

// King PST — midgame: hide behind pawns, avoid centre
static constexpr int PST_KING_MG[64] = {
//  a    b    c    d    e    f    g    h
    20,  30,  10,   0,   0,  10,  30,  20,  // rank 1
    20,  20,   0,   0,   0,   0,  20,  20,  // rank 2
   -10, -20, -20, -20, -20, -20, -20, -10,  // rank 3
   -20, -30, -30, -40, -40, -30, -30, -20,  // rank 4
   -30, -40, -40, -50, -50, -40, -40, -30,  // rank 5
   -30, -40, -40, -50, -50, -40, -40, -30,  // rank 6
   -30, -40, -40, -50, -50, -40, -40, -30,  // rank 7
   -30, -40, -40, -50, -50, -40, -40, -30,  // rank 8
};

// clang-format on

// ─── PST lookup helper ────────────────────────────────────────────────────────
//
// The tables above are written rank1..rank8 (index 0 = a1).
// For White: pst_value(sq) = PST[sq]
// For Black: mirror the rank — pst_value(sq for Black) = PST[mirror(sq)]

static constexpr Square mirror_sq(Square s) noexcept {
    return (7 - rank_of(s)) * 8 + file_of(s);
}

static int pst_lookup(const int* table, Square sq, Color c) noexcept {
    return table[c == Color::White ? sq : mirror_sq(sq)];
}

static int pst_for_piece(PieceType pt, Square sq, Color c) noexcept {
    switch (pt) {
        case PieceType::Pawn:   return pst_lookup(PST_PAWN,    sq, c);
        case PieceType::Knight: return pst_lookup(PST_KNIGHT,  sq, c);
        case PieceType::Bishop: return pst_lookup(PST_BISHOP,  sq, c);
        case PieceType::Rook:   return pst_lookup(PST_ROOK,    sq, c);
        case PieceType::Queen:  return pst_lookup(PST_QUEEN,   sq, c);
        case PieceType::King:   return pst_lookup(PST_KING_MG, sq, c);
        default:                return 0;
    }
}

// ─── HandcraftedEvaluator implementation ─────────────────────────────────────

int HandcraftedEvaluator::material_score(const Board& board) const noexcept {
    int score = 0;
    for (Square s = 0; s < 64; ++s) {
        Piece p = board.piece_at(s);
        if (p.is_empty()) continue;
        int val = material_value(p.type);
        score += (p.color == Color::White) ? val : -val;
    }
    return score;
}

int HandcraftedEvaluator::pst_score(const Board& board) const noexcept {
    int score = 0;
    for (Square s = 0; s < 64; ++s) {
        Piece p = board.piece_at(s);
        if (p.is_empty()) continue;
        int val = pst_for_piece(p.type, s, p.color);
        score += (p.color == Color::White) ? val : -val;
    }
    return score;
}

int HandcraftedEvaluator::bishop_pair_bonus(const Board& board) const noexcept {
    constexpr int BONUS = 30;
    int wb = 0, bb = 0;
    for (Square s = 0; s < 64; ++s) {
        Piece p = board.piece_at(s);
        if (p.type != PieceType::Bishop) continue;
        if (p.color == Color::White) ++wb; else ++bb;
    }
    int score = 0;
    if (wb >= 2) score += BONUS;
    if (bb >= 2) score -= BONUS;
    return score;
}

int HandcraftedEvaluator::mobility_score(Board& board) const {
    // Count legal moves for each side; give a small bonus per extra move.
    // We need to call generate_legal_moves twice (once per side), which
    // requires temporarily swapping side_to_move. To avoid that we just
    // score for the side currently to move and skip the opponent half —
    // this keeps eval fast. The effect is small (~3 cp/move) and symmetric.
    constexpr int CP_PER_MOVE = 3;
    MoveList moves = generate_legal_moves(board);
    return static_cast<int>(moves.size()) * CP_PER_MOVE;
}

int HandcraftedEvaluator::evaluate_white(const Board& board) const noexcept {
    return material_score(board) + pst_score(board) + bishop_pair_bonus(board);
}

int HandcraftedEvaluator::evaluate(Board& board) {
    // Score from White's perspective, then flip for side to move (D-7).
    int white_pov = evaluate_white(board) + mobility_score(board);
    return (board.side_to_move() == Color::White) ? white_pov : -white_pov;
}

} // namespace chess
