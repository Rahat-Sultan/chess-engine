#pragma once
/**
 * perft.hpp — Recursive perft (performance test) function
 *
 * perft(board, depth) counts the number of leaf nodes reachable from
 * the current position at exactly <depth> plies.  This is the gold
 * standard for verifying move-generation correctness.
 *
 * Reference: https://www.chessprogramming.org/Perft
 *
 * At depth 0, the position itself is counted (1 leaf).
 * At depth 1, each legal move is counted (= number of legal moves).
 */

#include "chess/movegen.hpp"
#include "chess/board.hpp"
#include <cstdint>

namespace chess {

inline uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1;

    MoveList moves = generate_legal_moves(board);
    if (depth == 1) return static_cast<uint64_t>(moves.size());

    uint64_t nodes = 0;
    for (const Move& m : moves) {
        UndoInfo undo = board.make_move(m);
        nodes += perft(board, depth - 1);
        board.unmake_move(m, undo);
    }
    return nodes;
}

} // namespace chess
