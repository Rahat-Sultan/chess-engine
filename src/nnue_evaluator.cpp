/**
 * nnue_evaluator.cpp — NNUE position evaluator implementation (Phase 7)
 *
 * Wraps nnue-probe (src/nnue_probe/) for HalfKP 256x2-32-32 evaluation.
 * See include/chess/nnue_evaluator.hpp for the interface.
 */

#include "chess/nnue_evaluator.hpp"

// nnue-probe uses C linkage; include inside extern "C" block
extern "C" {
#include "nnue_probe/nnue.h"
}

#include <cstdio>
#include <cstring>

namespace chess {

// ─── Piece type mapping ───────────────────────────────────────────────────────
//
// nnue-probe encoding:
//   blank=0, wking=1, wqueen=2, wrook=3, wbishop=4, wknight=5, wpawn=6,
//             bking=7, bqueen=8, brook=9, bbishop=10, bknight=11, bpawn=12
//
// Our PieceType enum:
//   None=0, Pawn=1, Knight=2, Bishop=3, Rook=4, Queen=5, King=6

static int to_probe_piece(Color c, PieceType pt) noexcept {
    // White offsets: King=1, Queen=2, Rook=3, Bishop=4, Knight=5, Pawn=6
    // Black offsets: King=7, Queen=8, Rook=9, Bishop=10, Knight=11, Pawn=12
    int base = (c == Color::White) ? 0 : 6;
    switch (pt) {
        case PieceType::King:   return base + 1;
        case PieceType::Queen:  return base + 2;
        case PieceType::Rook:   return base + 3;
        case PieceType::Bishop: return base + 4;
        case PieceType::Knight: return base + 5;
        case PieceType::Pawn:   return base + 6;
        default:                return 0;
    }
}

// ─── Construction ─────────────────────────────────────────────────────────────

NNUEEvaluator::NNUEEvaluator(const std::string& network_path) noexcept
    : path_(network_path)
{
    // Verify the file exists and is readable before calling nnue_init,
    // since nnue_init calls exit() on failure in some builds.
    FILE* f = std::fopen(network_path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr,
            "info string NNUE: file not found or unreadable: %s\n",
            network_path.c_str());
        return;
    }
    std::fclose(f);

    // nnue_init prints "Loading NNUE: ..." to stdout in the original source.
    // We have patched that out below. The function loads and memory-maps
    // the network file; failure is silent in the probe library (it prints
    // to stdout and continues, so we add our own stderr log).
    nnue_init(network_path.c_str());
    loaded_ = true;

    std::fprintf(stderr,
        "info string NNUE loaded: %s\n", network_name());
}

// ─── board_to_probe_format ────────────────────────────────────────────────────
//
// nnue-probe expects:
//   pieces[0] = wking (always 1), squares[0] = white king square
//   pieces[1] = bking (always 7), squares[1] = black king square
//   pieces[2..n] = all other pieces in any order
//   pieces[n+1]  = 0 (terminator)

void NNUEEvaluator::board_to_probe_format(
    const Board& board,
    std::array<int, 33>& pieces,
    std::array<int, 33>& squares) noexcept
{
    pieces.fill(0);
    squares.fill(0);

    int idx = 2;  // slots 0 and 1 reserved for kings

    for (Square s = 0; s < 64; ++s) {
        Piece p = board.piece_at(s);
        if (p.is_empty()) continue;

        int probe_piece = to_probe_piece(p.color, p.type);

        if (p.type == PieceType::King) {
            if (p.color == Color::White) {
                pieces[0]  = probe_piece;   // wking = 1
                squares[0] = s;
            } else {
                pieces[1]  = probe_piece;   // bking = 7
                squares[1] = s;
            }
        } else {
            if (idx < 32) {                 // max 30 non-king pieces + 2 kings
                pieces[idx]  = probe_piece;
                squares[idx] = s;
                ++idx;
            }
        }
    }
    // terminator
    pieces[idx]  = 0;
    squares[idx] = 0;
}

// ─── evaluate ─────────────────────────────────────────────────────────────────

int NNUEEvaluator::evaluate(Board& board) {
    if (!loaded_) return 0;

    std::array<int, 33> pieces{};
    std::array<int, 33> squares{};
    board_to_probe_format(board, pieces, squares);

    // nnue_evaluate returns score from the perspective of *player* (0=White,1=Black).
    // We want side-to-move perspective to match HandcraftedEvaluator convention.
    int player = (board.side_to_move() == Color::White) ? 0 : 1;

    int score = nnue_evaluate(player, pieces.data(), squares.data());

    // nnue-probe returns scores in a slightly different scale (internal units).
    // Scale from internal (typically ~100 cp/pawn) to centipawns.
    // HalfKP probe scores are already approximately in centipawns.
    return score;
}

} // namespace chess
