/**
 * search.cpp — Iterative deepening alpha-beta search
 *
 * Algorithm:
 *   1. Iterative deepening: search depth 1, 2, … up to limits.depth (or stop).
 *      Always returns the best move from the last completed iteration.
 *   2. Negamax alpha-beta: standard fail-hard alpha-beta with side-to-move
 *      score convention (D-7). Score = -negamax(child).
 *   3. Quiescence search: at depth 0, only considers captures (+promotions)
 *      to resolve tactical sequences and avoid the horizon effect.
 *   4. Move ordering: captures ordered by MVV-LVA (Most Valuable Victim –
 *      Least Valuable Attacker), quiet moves by simple PST delta heuristic.
 *   5. Limits: depth, movetime_ms, node count, stop flag (atomic bool).
 *   6. 50-move rule: halfmove_clock >= 100 → draw.
 *
 * Mate distance encoding: a mate in N plies from root scores
 *   SCORE_MATE - ply (positive for the mating side).
 */

#include "chess/search.hpp"
#include "chess/evaluation.hpp"
#include "chess/movegen.hpp"
#include "chess/transposition.hpp"

#include <algorithm>
#include <cassert>

namespace chess {

// ─── Mate score adjustment for TT storage ────────────────────────────────────
//
// Mate scores encode distance relative to the root. When we store in the TT
// we make them position-independent (relative to the node), and adjust back
// when retrieving.

static int adjust_to_tt(int score, int ply) noexcept {
    if (score > SCORE_MATE - 1000)  return score + ply;
    if (score < -SCORE_MATE + 1000) return score - ply;
    return score;
}

static int adjust_from_tt(int score, int ply) noexcept {
    if (score > SCORE_MATE - 1000)  return score - ply;
    if (score < -SCORE_MATE + 1000) return score + ply;
    return score;
}

// ─── Construction ─────────────────────────────────────────────────────────────

Searcher::Searcher(Evaluator& evaluator)
    : evaluator_(&evaluator)
{
    // Default: depth 6 if no limits set
    limits_.depth = 6;
}

// ─── Public interface ─────────────────────────────────────────────────────────

void Searcher::set_limits(const SearchLimits& limits) noexcept {
    limits_ = limits;
}

void Searcher::stop() noexcept {
    stop_flag_.store(true, std::memory_order_relaxed);
}

void Searcher::reset() noexcept {
    stop_flag_.store(false, std::memory_order_relaxed);
}

int Searcher::elapsed_ms() const noexcept {
    using namespace std::chrono;
    auto now = steady_clock::now();
    return static_cast<int>(duration_cast<milliseconds>(now - start_time_).count());
}

bool Searcher::should_stop() const noexcept {
    if (stop_flag_.load(std::memory_order_relaxed)) return true;
    if (limits_.nodes.has_value() && nodes_ >= *limits_.nodes) return true;
    if (limits_.movetime_ms.has_value() && (nodes_ & 255) == 0 && elapsed_ms() >= *limits_.movetime_ms) return true;
    return false;
}

// ─── Move ordering ────────────────────────────────────────────────────────────
//
// Strategy: captures sorted descending by (victim_value - attacker_value/10),
// then quiet moves. Simple and effective without killer/history tables.

static int mvv_lva(const Move& m, const Board& board) noexcept {
    if (!m.is_capture()) return 0;
    // Victim value
    Piece victim = board.piece_at(m.to);
    int vic_val  = material_value(victim.type);
    // For en-passant the captured pawn isn't on m.to
    if (m.is_en_passant()) vic_val = MAT_PAWN;
    // Attacker value
    Piece attacker = board.piece_at(m.from);
    int att_val    = material_value(attacker.type);
    // Higher victim = better; lower attacker = better
    return vic_val * 10 - att_val;
}

void Searcher::order_moves(MoveList& moves, const Board& board) const noexcept {
    // Separate captures and quiets; sort captures by MVV-LVA descending
    // We do a simple insertion-style partial sort: captures come first.
    std::size_t n = moves.size();

    // Score each move
    // Use a small fixed array rather than a heap allocation
    int scores[MoveList::MAX_MOVES] = {};
    for (std::size_t i = 0; i < n; ++i) {
        const Move& m = moves[i];
        if (m.is_capture()) {
            scores[i] = 10000 + mvv_lva(m, board);  // captures first
        } else if (m.is_promotion()) {
            scores[i] = 9000 + material_value(m.promotion_piece);
        } else {
            scores[i] = 0;  // quiet moves — no ordering for now
        }
    }

    // Insertion sort (small n, typically ≤ 80 moves at root)
    for (std::size_t i = 1; i < n; ++i) {
        Move  km = moves[i];
        int   ks = scores[i];
        std::size_t j = i;
        while (j > 0 && scores[j - 1] < ks) {
            moves[j]  = moves[j - 1];
            scores[j] = scores[j - 1];
            --j;
        }
        moves[j]  = km;
        scores[j] = ks;
    }
}

// ─── Quiescence search ────────────────────────────────────────────────────────

int Searcher::quiescence(Board& board, int alpha, int beta, int ply) {
    ++nodes_;

    if (should_stop()) return alpha;

    // Stand-pat: evaluate the position statically. If it's already good enough
    // for beta cutoff we can prune (we're not obliged to capture).
    int stand_pat = evaluator_->evaluate(board);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha)  alpha = stand_pat;

    // Depth guard — avoid runaway quiescence in rare positions
    if (ply >= 64) return alpha;

    // Generate only captures (and promotions)
    MoveList all_moves = generate_legal_moves(board);
    MoveList captures;
    for (const Move& m : all_moves) {
        if (m.is_capture() || m.is_promotion()) captures.push(m);
    }
    order_moves(captures, board);

    for (const Move& m : captures) {
        UndoInfo undo = board.make_move(m);
        int score = -quiescence(board, -beta, -alpha, ply + 1);
        board.unmake_move(m, undo);

        if (should_stop()) return alpha;
        if (score >= beta) return beta;   // beta cutoff
        if (score > alpha) alpha = score;
    }

    return alpha;
}

// ─── Negamax alpha-beta ───────────────────────────────────────────────────────

int Searcher::negamax(Board& board, int depth, int alpha, int beta,
                      int ply, std::vector<Move>& pv) {
    pv.clear();

    // ── Terminal checks ───────────────────────────────────────────────────────

    // 50-move draw
    if (board.halfmove_clock() >= 100) return SCORE_DRAW;

    if (should_stop()) return alpha;

    // ── Transposition table probe ─────────────────────────────────────────────

    const uint64_t key = board.zobrist_key();
    Move tt_move = NULL_MOVE;

    if (tt_) {
        const TTEntry* entry = tt_->probe(key);
        if (entry) {
            tt_move = unpack_move(entry->move);
            if (static_cast<int>(entry->depth) >= depth) {
                int score = adjust_from_tt(entry->score, ply);
                if (entry->bound == BOUND_EXACT) return score;
                if (entry->bound == BOUND_LOWER && score >= beta) return score;
                if (entry->bound == BOUND_UPPER && score <= alpha) return score;
            }
        }
    }

    // ── Leaf node ─────────────────────────────────────────────────────────────

    if (depth <= 0) {
        return quiescence(board, alpha, beta, ply);
    }

    // ── Interior node ─────────────────────────────────────────────────────────

    ++nodes_;

    MoveList moves = generate_legal_moves(board);

    // Checkmate or stalemate
    if (moves.empty()) {
        if (is_in_check(board, board.side_to_move())) {
            // Checkmate: worse the deeper (prefer faster mates)
            return -(SCORE_MATE - ply);
        }
        return SCORE_DRAW;  // stalemate
    }

    order_moves(moves, board);

    // Move the TT best move to front for better ordering
    if (tt_move.is_valid()) {
        for (std::size_t i = 0; i < moves.size(); ++i) {
            if (moves[i].from == tt_move.from && moves[i].to == tt_move.to) {
                if (i > 0) {
                    Move tmp   = moves[0];
                    moves[0]   = moves[i];
                    moves[i]   = tmp;
                }
                break;
            }
        }
    }

    std::vector<Move> child_pv;
    Move best_move   = moves[0];
    int  best_score  = -SCORE_INFINITY;
    int  orig_alpha  = alpha;

    for (const Move& m : moves) {
        UndoInfo undo = board.make_move(m);
        int score = -negamax(board, depth - 1, -beta, -alpha, ply + 1, child_pv);
        board.unmake_move(m, undo);

        if (should_stop()) break;

        if (score > best_score) {
            best_score = score;
            best_move  = m;
        }

        if (score > alpha) {
            alpha = score;
            // Update PV
            pv.clear();
            pv.push_back(m);
            pv.insert(pv.end(), child_pv.begin(), child_pv.end());
        }

        if (alpha >= beta) {
            break;  // fail-soft beta cutoff
        }
    }

    // ── Store in transposition table ──────────────────────────────────────────

    if (tt_) {
        BoundType bound;
        if (best_score >= beta)       bound = BOUND_LOWER;
        else if (best_score <= orig_alpha) bound = BOUND_UPPER;
        else                          bound = BOUND_EXACT;

        int stored = adjust_to_tt(best_score, ply);
        tt_->store(key, stored, depth, bound, best_move);
    }

    return best_score;
}

// ─── search() — iterative deepening ──────────────────────────────────────────

SearchResult Searcher::search(Board& board) {
    reset();
    nodes_ = 0;
    start_time_ = std::chrono::steady_clock::now();

    // Advance TT generation for this search
    if (tt_) tt_->new_search();

    int max_depth = limits_.depth.value_or(64);

    SearchResult result;
    result.best_move = NULL_MOVE;

    // Generate root moves once for an early-exit check
    MoveList root_moves = generate_legal_moves(board);
    if (root_moves.empty()) {
        // No legal moves — checkmate or stalemate already
        result.score = is_in_check(board, board.side_to_move())
                       ? -SCORE_MATE
                       : SCORE_DRAW;
        return result;
    }

    // Single legal move: return it immediately without searching
    if (root_moves.size() == 1) {
        result.best_move = root_moves[0];
        result.depth     = 1;
        result.nodes     = 1;
        result.elapsed_ms= elapsed_ms();
        result.pv        = {root_moves[0]};
        if (info_callback_) {
            info_callback_(result);
        }
        return result;
    }

    // Iterative deepening loop
    for (int depth = 1; depth <= max_depth; ++depth) {
        std::vector<Move> pv;
        int score = negamax(board, depth, -SCORE_INFINITY, SCORE_INFINITY, 0, pv);

        // If we were stopped mid-search, keep the last completed iteration's result
        if (should_stop() && !pv.empty() == false) break;
        if (should_stop() && result.best_move.is_valid()) break;

        if (!pv.empty()) {
            result.best_move = pv[0];
            result.ponder_move = (pv.size() > 1)
                                 ? std::make_optional(pv[1])
                                 : std::nullopt;
            result.score      = score;
            result.depth      = depth;
            result.nodes      = nodes_;
            result.elapsed_ms = elapsed_ms();
            result.pv         = pv;

            if (info_callback_) {
                info_callback_(result);
            }
        }

        // Stop if we found a forced mate
        if (is_mate_score(score)) break;

        if (should_stop()) break;
    }

    // Fallback: if somehow PV is empty (should not happen), return first legal move
    if (!result.best_move.is_valid() && !root_moves.empty()) {
        result.best_move = root_moves[0];
    }

    result.nodes      = nodes_;
    result.elapsed_ms = elapsed_ms();
    return result;
}

} // namespace chess
