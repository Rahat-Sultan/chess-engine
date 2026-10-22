/**
 * uci.cpp — UCI protocol controller implementation (Phase 4)
 */

#include "chess/uci.hpp"
#include "chess/movegen.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace chess {

// ─── Construction / Destruction ───────────────────────────────────────────────

UciController::UciController(std::istream& in, std::ostream& out,
                             std::unique_ptr<Evaluator> evaluator)
    : in_(in)
    , out_(out)
    , evaluator_(evaluator ? std::move(evaluator)
                           : std::make_unique<HandcraftedEvaluator>())
    , searcher_(*evaluator_)
    , board_()
{
    searcher_.set_info_callback([this](const SearchResult& res) {
        on_search_info(res);
    });
}

UciController::~UciController() {
    stop_search();
}

// ─── Output Helper ────────────────────────────────────────────────────────────

void UciController::send_response(const std::string& text) {
    std::lock_guard<std::mutex> lock(out_mutex_);
    out_ << text;
    out_.flush();
}

// ─── Search Management ────────────────────────────────────────────────────────

void UciController::stop_search() {
    if (is_searching_.load()) {
        searcher_.stop();
    }
    if (search_thread_.joinable()) {
        search_thread_.join();
    }
}

void UciController::wait_for_search() {
    if (search_thread_.joinable()) {
        search_thread_.join();
    }
}

void UciController::start_search(const SearchLimits& limits) {
    stop_search();

    searcher_.reset();
    searcher_.set_limits(limits);
    is_searching_.store(true);

    // Pass a copy of board_ to the worker thread to completely eliminate race conditions
    search_thread_ = std::thread([this, copy_board = board_]() mutable {
        SearchResult res = searcher_.search(copy_board);

        if (res.best_move.is_valid()) {
            std::string bm = "bestmove " + move_to_uci(res.best_move);
            if (res.ponder_move.has_value() && res.ponder_move->is_valid()) {
                bm += " ponder " + move_to_uci(*res.ponder_move);
            }
            send_response(bm + "\n");
        } else {
            send_response("bestmove (none)\n");
        }
        is_searching_.store(false);
    });
}

void UciController::on_search_info(const SearchResult& res) {
    std::string score_str;
    if (res.score > SCORE_MATE - 1000) {
        int plies = SCORE_MATE - res.score;
        int mate_moves = (plies + 1) / 2;
        score_str = "mate " + std::to_string(mate_moves);
    } else if (res.score < -SCORE_MATE + 1000) {
        int plies = SCORE_MATE + res.score;
        int mate_moves = -((-plies + 1) / 2);
        score_str = "mate " + std::to_string(mate_moves);
    } else {
        score_str = "cp " + std::to_string(res.score);
    }

    uint64_t nps = 0;
    if (res.elapsed_ms > 0) {
        nps = (res.nodes * 1000) / static_cast<uint64_t>(res.elapsed_ms);
    }

    std::string pv_str;
    for (const Move& m : res.pv) {
        if (!pv_str.empty()) pv_str += ' ';
        pv_str += move_to_uci(m);
    }

    std::string line = "info depth " + std::to_string(res.depth) +
                       " score " + score_str +
                       " time " + std::to_string(res.elapsed_ms) +
                       " nodes " + std::to_string(res.nodes) +
                       " nps " + std::to_string(nps);
    if (!pv_str.empty()) {
        line += " pv " + pv_str;
    }
    line += "\n";
    send_response(line);
}

// ─── Main Run Loop ────────────────────────────────────────────────────────────

void UciController::run() {
    std::string line;
    while (!quit_.load() && std::getline(in_, line)) {
        process_command(line);
    }
    stop_search();
}

// ─── Command Dispatch ─────────────────────────────────────────────────────────

void UciController::process_command(std::string line) {
    // Strip trailing carriage return if line came from Windows CRLF
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }

    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd)) return;

    // Remaining line after command
    std::string args;
    std::getline(iss, args);
    // Trim leading whitespace
    auto start = args.find_first_not_of(" \t");
    if (start != std::string::npos) {
        args = args.substr(start);
    } else {
        args.clear();
    }

    if (cmd == "uci") {
        handle_uci();
    } else if (cmd == "isready") {
        handle_isready();
    } else if (cmd == "setoption") {
        handle_setoption(args);
    } else if (cmd == "ucinewgame") {
        handle_ucinewgame();
    } else if (cmd == "position") {
        handle_position(args);
    } else if (cmd == "go") {
        handle_go(args);
    } else if (cmd == "stop") {
        handle_stop();
    } else if (cmd == "quit") {
        handle_quit();
    }
    // Unknown commands are ignored per UCI convention
}

// ─── Handlers ─────────────────────────────────────────────────────────────────

void UciController::handle_uci() {
    std::string resp;
    resp += std::string("id name ") + UCI_ENGINE_NAME + " " + UCI_ENGINE_VERSION + "\n";
    resp += std::string("id author ") + UCI_ENGINE_AUTHOR + "\n";
    resp += "option name Hash type spin default 16 min 1 max 1024\n";
    resp += "option name Threads type spin default 1 min 1 max 1\n";
    resp += "uciok\n";
    send_response(resp);
}

void UciController::handle_isready() {
    send_response("readyok\n");
}

void UciController::handle_setoption(const std::string& args) {
    std::istringstream iss(args);
    std::string tok;
    if (!(iss >> tok) || tok != "name") return;

    std::string name;
    std::string value;
    bool in_value = false;

    while (iss >> tok) {
        if (!in_value && tok == "value") {
            in_value = true;
            continue;
        }
        if (in_value) {
            if (!value.empty()) value += ' ';
            value += tok;
        } else {
            if (!name.empty()) name += ' ';
            name += tok;
        }
    }

    if (name == "Hash") {
        try {
            int mb = std::stoi(value);
            mb = std::max(1, std::min(1024, mb));
            hash_size_mb_ = mb;
        } catch (...) {}
    } else if (name == "Threads") {
        try {
            int th = std::stoi(value);
            th = std::max(1, std::min(1, th));
            threads_ = th;
        } catch (...) {}
    } else {
        std::cerr << "info string Warning: unrecognized option '" << name << "'\n";
    }
}

void UciController::handle_ucinewgame() {
    stop_search();
    board_ = Board();
}

void UciController::handle_position(const std::string& args) {
    stop_search();

    std::istringstream iss(args);
    std::string token;
    if (!(iss >> token)) return;

    if (token == "startpos") {
        board_ = Board();
        iss >> token; // peek next token (e.g. "moves" or EOF)
    } else if (token == "fen") {
        std::string fen;
        while (iss >> token && token != "moves") {
            if (!fen.empty()) fen += ' ';
            fen += token;
        }
        try {
            board_.set_fen(fen);
        } catch (const std::exception& e) {
            std::cerr << "info string Error: invalid FEN (" << e.what() << ")\n";
            return;
        }
    } else {
        std::cerr << "info string Error: unknown position argument: " << token << "\n";
        return;
    }

    if (token == "moves") {
        while (iss >> token) {
            Move partial = move_from_uci(token);
            if (!partial.is_valid()) {
                std::cerr << "info string Error: invalid move format: " << token << "\n";
                break;
            }
            Move resolved = resolve_move(board_, partial);
            if (!resolved.is_valid()) {
                std::cerr << "info string Error: illegal move: " << token << "\n";
                break;
            }
            board_.make_move(resolved);
        }
    }
}

void UciController::handle_go(const std::string& args) {
    SearchLimits limits;
    std::istringstream iss(args);
    std::string tok;

    std::optional<int> wtime;
    std::optional<int> btime;
    std::optional<int> winc;
    std::optional<int> binc;
    std::optional<int> movestogo;

    while (iss >> tok) {
        if (tok == "depth") {
            int d = 0;
            if (iss >> d) limits.depth = d;
        } else if (tok == "movetime") {
            int ms = 0;
            if (iss >> ms) limits.movetime_ms = ms;
        } else if (tok == "nodes") {
            uint64_t n = 0;
            if (iss >> n) limits.nodes = n;
        } else if (tok == "infinite") {
            limits.infinite = true;
            limits.depth = 64;
        } else if (tok == "wtime") {
            int val = 0;
            if (iss >> val) wtime = val;
        } else if (tok == "btime") {
            int val = 0;
            if (iss >> val) btime = val;
        } else if (tok == "winc") {
            int val = 0;
            if (iss >> val) winc = val;
        } else if (tok == "binc") {
            int val = 0;
            if (iss >> val) binc = val;
        } else if (tok == "movestogo") {
            int val = 0;
            if (iss >> val) movestogo = val;
        }
    }

    // Dynamic time management if wtime/btime provided and no explicit movetime/depth/nodes
    if (!limits.movetime_ms.has_value() && !limits.depth.has_value() &&
        !limits.nodes.has_value() && !limits.infinite) {
        Color us = board_.side_to_move();
        std::optional<int> our_time = (us == Color::White) ? wtime : btime;
        std::optional<int> our_inc  = (us == Color::White) ? winc  : binc;

        if (our_time.has_value()) {
            int t   = *our_time;
            int inc = our_inc.value_or(0);
            int allocated = 0;

            if (movestogo.has_value() && *movestogo > 0) {
                allocated = t / (*movestogo + 1) + inc;
            } else {
                allocated = t / 30 + inc;
            }

            // Cap to avoid flagging
            if (allocated > t - 50) {
                allocated = std::max(10, t - 50);
            }
            if (allocated < 10) {
                allocated = 10;
            }
            limits.movetime_ms = allocated;
        }
    }

    // Default if completely unbounded
    if (!limits.depth.has_value() && !limits.movetime_ms.has_value() &&
        !limits.nodes.has_value() && !limits.infinite) {
        limits.depth = 6;
    }

    start_search(limits);
}

void UciController::handle_stop() {
    stop_search();
}

void UciController::handle_quit() {
    quit_.store(true);
    stop_search();
}

} // namespace chess
