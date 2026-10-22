#pragma once

/**
 * uci.hpp — UCI (Universal Chess Interface) Protocol Controller (Phase 4)
 *
 * Responsibilities:
 *   - Parse UCI text commands from an input stream (std::cin by default).
 *   - Respond on an output stream (std::cout by default).
 *   - Keep protocol output on stdout and diagnostics on stderr (D-3).
 *   - Maintain board state and dispatch search tasks to a worker thread.
 *   - Support stop, isready, ucinewgame, position, go, and quit without hangs.
 *   - Thread safety: search runs asynchronously while controller listens for stop/quit/isready.
 */

#include "chess/board.hpp"
#include "chess/evaluation.hpp"
#include "chess/search.hpp"
#include "chess/types.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace chess {

// Engine metadata
inline constexpr const char* UCI_ENGINE_NAME    = "LightweightChessEngine";
inline constexpr const char* UCI_ENGINE_AUTHOR  = "Rahat Sultan";
inline constexpr const char* UCI_ENGINE_VERSION = "0.1.0-phase4";

class UciController {
public:
    /**
     * Create a UCI controller attached to the given streams.
     * Takes an optional Evaluator; if nullptr, creates a HandcraftedEvaluator.
     */
    explicit UciController(std::istream& in, std::ostream& out,
                           std::unique_ptr<Evaluator> evaluator = nullptr);

    ~UciController();

    // Non-copyable, non-movable
    UciController(const UciController&) = delete;
    UciController& operator=(const UciController&) = delete;
    UciController(UciController&&) = delete;
    UciController& operator=(UciController&&) = delete;

    /**
     * Run the main UCI loop until 'quit' is received or input stream reaches EOF.
     */
    void run();

    /**
     * Process a single UCI command line.
     * Public to facilitate direct unit/integration testing without running a loop.
     */
    void process_command(std::string line);

    /**
     * Interrupt active search if one is running, and wait for worker thread to exit.
     */
    void stop_search();

    /**
     * Wait for active search to complete naturally (without forcing early stop).
     */
    void wait_for_search();

    /** Query whether the engine is currently calculating. */
    bool is_searching() const noexcept { return is_searching_.load(); }

    /** Get the current internal board position. */
    const Board& board() const noexcept { return board_; }

    /** Configured hash size in megabytes. */
    int hash_size_mb() const noexcept { return hash_size_mb_; }

    /** Configured search threads. */
    int threads() const noexcept { return threads_; }

private:
    std::istream& in_;
    std::ostream& out_;
    std::mutex    out_mutex_;

    std::unique_ptr<Evaluator> evaluator_;
    Searcher                   searcher_;
    Board                      board_;

    std::thread        search_thread_;
    std::atomic<bool>  is_searching_{false};
    std::atomic<bool>  quit_{false};

    int hash_size_mb_ = 16;
    int threads_      = 1;

    // ── Command handlers ──────────────────────────────────────────────────────
    void handle_uci();
    void handle_isready();
    void handle_setoption(const std::string& args);
    void handle_ucinewgame();
    void handle_position(const std::string& args);
    void handle_go(const std::string& args);
    void handle_stop();
    void handle_quit();

    // ── Output helper (thread-safe, flushed) ───────────────────────────────────
    void send_response(const std::string& text);

    // ── Search worker callback ────────────────────────────────────────────────
    void on_search_info(const SearchResult& res);
    void start_search(const SearchLimits& limits);
};

} // namespace chess
