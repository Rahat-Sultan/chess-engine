#pragma once

/**
 * search.hpp — Search algorithm (Phase 3)
 *
 * Implements:
 *   - Iterative deepening (starts at depth 1, goes up to limits.depth)
 *   - Negamax with alpha-beta pruning
 *   - Quiescence search to resolve tactical sequences at leaf nodes
 *   - Move ordering: captures first (MVV-LVA), then quiet moves by PST delta
 *   - Stop flag for interrupting search from another thread (UCI "stop")
 *   - Depth, time, and node limits
 *
 * Usage:
 *   Searcher searcher(evaluator);
 *   searcher.set_limits(limits);
 *   SearchResult result = searcher.search(board);
 *
 * Thread safety:
 *   stop() may be called from any thread.
 *   search() must be called from one thread at a time.
 */

#include "chess/board.hpp"
#include "chess/evaluation.hpp"
#include "chess/movegen.hpp"
#include "chess/transposition.hpp"
#include "chess/types.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace chess {

// ─── Searcher ────────────────────────────────────────────────────────────────

class Searcher {
public:
    explicit Searcher(Evaluator& evaluator);

    /**
     * Set search limits before calling search().
     * Calling search() without setting limits defaults to depth 6.
     */
    void set_limits(const SearchLimits& limits) noexcept;

    /**
     * Run a search on the given position.
     * Returns the best move and ancillary info.
     * Board is restored to its original state when this returns.
     */
    SearchResult search(Board& board);

    /**
     * Signal the search to stop at the next opportunity.
     * Safe to call from any thread. The search will return the best move
     * found so far at the last completed iteration.
     */
    void stop() noexcept;

    /** Reset stop flag (call before each new search). */
    void reset() noexcept;

    /**
     * Set an optional transposition table for caching.
     * Pass nullptr to disable TT usage.
     */
    void set_transposition_table(TranspositionTable* tt) noexcept { tt_ = tt; }

    /**
     * Replace the evaluator (called when UCI EvalFile option changes).
     * Must not be called while a search is in progress.
     */
    void set_evaluator(Evaluator& evaluator) noexcept { evaluator_ = &evaluator; }

    /** Callback invoked after each completed depth during iterative deepening. */
    using InfoCallback = std::function<void(const SearchResult&)>;
    void set_info_callback(InfoCallback cb) { info_callback_ = std::move(cb); }

    /** Nodes searched in the last search() call. */
    uint64_t nodes() const noexcept { return nodes_; }

private:
    Evaluator*   evaluator_;   // non-owning; set by constructor or set_evaluator()
    SearchLimits limits_;
    std::atomic<bool> stop_flag_{false};
    InfoCallback info_callback_;

    // Optional transposition table (Phase 5)
    TranspositionTable* tt_ = nullptr;

    // Per-search state
    uint64_t     nodes_       = 0;
    std::chrono::steady_clock::time_point start_time_;

    // ── Core algorithms ───────────────────────────────────────────────────────

    /**
     * Negamax alpha-beta.
     * Returns score from the perspective of the side to move at this node.
     * depth is remaining plies to search; ply is distance from root.
     */
    int negamax(Board& board, int depth, int alpha, int beta, int ply,
                std::vector<Move>& pv);

    /**
     * Quiescence search: only searches captures (and promotions) to avoid
     * the horizon effect.
     */
    int quiescence(Board& board, int alpha, int beta, int ply);

    // ── Helpers ───────────────────────────────────────────────────────────────

    bool should_stop() const noexcept;

    // Order moves in-place: captures (MVV-LVA) first, then quiet moves
    void order_moves(MoveList& moves, const Board& board) const noexcept;

    int elapsed_ms() const noexcept;
};

} // namespace chess
