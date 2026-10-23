/**
 * test_uci.cpp — Unit tests for UCI Protocol Controller (Phase 4)
 *
 * Covers:
 *   - Handshake (uci -> id, option, uciok)
 *   - isready -> readyok
 *   - setoption (Hash, Threads, clamping)
 *   - ucinewgame (resets board)
 *   - position startpos
 *   - position startpos moves ...
 *   - position fen ...
 *   - position fen ... moves ...
 *   - go depth N -> info lines + legal bestmove
 *   - go nodes N
 *   - go movetime N
 *   - stop interrupts search
 *   - Terminal checkmate -> bestmove (none)
 *   - Mate-in-one announces score mate 1
 *   - Malformed FEN / illegal moves handled gracefully without crashing
 *   - Windows CRLF line endings
 */

#include "test_framework.hpp"
#include "chess/board.hpp"
#include "chess/movegen.hpp"
#include "chess/types.hpp"
#include "chess/uci.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

using namespace chess;

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static bool is_valid_uci_stream(const std::string& output) {
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("id ", 0) == 0 ||
            line == "uciok" ||
            line == "readyok" ||
            line.rfind("bestmove ", 0) == 0 ||
            line.rfind("info ", 0) == 0 ||
            line.rfind("option ", 0) == 0) {
            continue;
        }
        return false;
    }
    return true;
}

TEST(UCI, Handshake) {
    std::istringstream in("uci\n");
    std::ostringstream out;
    UciController uci(in, out);
    uci.run();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "id name LightweightChessEngine"));
    EXPECT_TRUE(contains(s, "id author Rahat Sultan"));
    EXPECT_TRUE(contains(s, "option name Hash type spin"));
    EXPECT_TRUE(contains(s, "option name Threads type spin"));
    EXPECT_TRUE(contains(s, "uciok\n"));
}

TEST(UCI, IsReady) {
    std::istringstream in("isready\n");
    std::ostringstream out;
    UciController uci(in, out);
    uci.run();

    EXPECT_TRUE(contains(out.str(), "readyok\n"));
}

TEST(UCI, SetOptionHashAndThreads) {
    std::istringstream in(
        "setoption name Hash value 64\n"
        "setoption name Threads value 1\n"
    );
    std::ostringstream out;
    UciController uci(in, out);
    uci.run();

    EXPECT_EQ(uci.hash_size_mb(), 64);
    EXPECT_EQ(uci.threads(), 1);

    // Clamping test
    uci.process_command("setoption name Hash value 2048");
    EXPECT_EQ(uci.hash_size_mb(), 1024);

    uci.process_command("setoption name Hash value 0");
    EXPECT_EQ(uci.hash_size_mb(), 1);
}

TEST(UCI, UciNewGameResetsBoard) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos moves e2e4");
    EXPECT_NE(uci.board().fen(), std::string(STARTING_FEN));

    uci.process_command("ucinewgame");
    EXPECT_EQ(uci.board().fen(), std::string(STARTING_FEN));
}

TEST(UCI, PositionStartposMoves) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos moves e2e4 e7e5 g1f3 b8c6");
    const Board& b = uci.board();

    // After 1. e4 e5 2. Nf3 Nc6, it is White's turn to move (ply 4)
    EXPECT_TRUE(b.side_to_move() == Color::White);
    EXPECT_TRUE(b.piece_at(square_from_name("e4")).type == PieceType::Pawn);
    EXPECT_TRUE(b.piece_at(square_from_name("e5")).type == PieceType::Pawn);
    EXPECT_TRUE(b.piece_at(square_from_name("f3")).type == PieceType::Knight);
    EXPECT_TRUE(b.piece_at(square_from_name("c6")).type == PieceType::Knight);
}

TEST(UCI, PositionFen) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    const char* custom_fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    uci.process_command(std::string("position fen ") + custom_fen);
    EXPECT_EQ(uci.board().fen(), std::string(custom_fen));
}

TEST(UCI, PositionFenMoves) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    // Starting from a custom position, apply moves
    const char* fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    uci.process_command(std::string("position fen ") + fen + " moves b4f4 h4g5");
    const Board& b = uci.board();

    EXPECT_TRUE(b.side_to_move() == Color::White);
    EXPECT_TRUE(b.piece_at(square_from_name("f4")).type == PieceType::Rook);
    EXPECT_TRUE(b.piece_at(square_from_name("g5")).type == PieceType::King);
}

TEST(UCI, GoDepthEmitsInfoAndLegalBestmove) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    uci.process_command("go depth 2");
    uci.wait_for_search();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "info depth 1"));
    EXPECT_TRUE(contains(s, "info depth 2"));
    EXPECT_TRUE(contains(s, "score cp"));
    EXPECT_TRUE(contains(s, "bestmove "));

    // Verify bestmove format
    auto bm_pos = s.find("bestmove ");
    EXPECT_TRUE(bm_pos != std::string::npos);
    std::string move_token = s.substr(bm_pos + 9, 4);
    Move m = move_from_uci(move_token);
    EXPECT_TRUE(m.is_valid());

    Board starting;
    Move resolved = resolve_move(starting, m);
    EXPECT_TRUE(resolved.is_valid());
}

TEST(UCI, GoNodesLimit) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    uci.process_command("go nodes 300");
    uci.wait_for_search();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "bestmove "));
}

TEST(UCI, GoMovetimeLimit) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    auto t0 = std::chrono::steady_clock::now();
    uci.process_command("go movetime 50");
    uci.wait_for_search();
    auto t1 = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_TRUE(elapsed < 1000);  // well under 1 second
    EXPECT_TRUE(contains(out.str(), "bestmove "));
}

TEST(UCI, StopInterruptsSearch) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    uci.process_command("go infinite");

    EXPECT_TRUE(uci.is_searching());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    uci.process_command("stop");
    EXPECT_FALSE(uci.is_searching());

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "bestmove "));
}

TEST(UCI, TerminalCheckmateReturnsNone) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    // Scholar's mate: Black is in checkmate already
    const char* mate_fen = "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4";
    uci.process_command(std::string("position fen ") + mate_fen);
    uci.process_command("go depth 1");
    uci.wait_for_search();

    EXPECT_TRUE(contains(out.str(), "bestmove (none)\n"));
}

TEST(UCI, MateInOneEmitsScoreMate) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    // White to move can deliver mate in one via Qxf7#
    const char* fen = "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4";
    uci.process_command(std::string("position fen ") + fen);
    uci.process_command("go depth 3");
    uci.wait_for_search();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "score mate 1"));
    EXPECT_TRUE(contains(s, "bestmove h5f7"));
}

TEST(UCI, MalformedFenHandledSafely) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    // Invalid FEN should not throw or crash
    uci.process_command("position fen totally_broken_fen_string");
    // Board should remain standard starting position
    EXPECT_EQ(uci.board().fen(), std::string(STARTING_FEN));
}

TEST(UCI, IllegalMoveHandledSafely) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    // e2e5 is not a legal move from starting position
    uci.process_command("position startpos moves e2e5");
    // Board should remain standard starting position
    EXPECT_EQ(uci.board().fen(), std::string(STARTING_FEN));
}

TEST(UCI, WindowsCrlfEndings) {
    std::istringstream in("uci\r\nisready\r\nquit\r\n");
    std::ostringstream out;
    UciController uci(in, out);
    uci.run();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "uciok\n"));
    EXPECT_TRUE(contains(s, "readyok\n"));
}

// ─── Search Limit Tests & Overshoot Measurements ─────────────────────────────

TEST(UCI, GoNodes100BoundedOvershoot) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    uci.process_command("go nodes 100");
    uci.wait_for_search();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "bestmove "));
    EXPECT_TRUE(is_valid_uci_stream(s));

    auto bm_pos = s.find("bestmove ");
    EXPECT_TRUE(bm_pos != std::string::npos);
    if (bm_pos != std::string::npos) {
        std::string move_token = s.substr(bm_pos + 9, 4);
        Move m = move_from_uci(move_token);
        EXPECT_TRUE(m.is_valid());
        Board b;
        EXPECT_TRUE(resolve_move(b, m).is_valid());
    }
}

TEST(UCI, GoMovetime1ReturnsLegalBestmove) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    uci.process_command("go movetime 1");
    uci.wait_for_search();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "bestmove "));
    EXPECT_TRUE(is_valid_uci_stream(s));

    auto bm_pos = s.find("bestmove ");
    EXPECT_TRUE(bm_pos != std::string::npos);
    if (bm_pos != std::string::npos) {
        std::string move_token = s.substr(bm_pos + 9, 4);
        Move m = move_from_uci(move_token);
        EXPECT_TRUE(m.is_valid());
        Board b;
        EXPECT_TRUE(resolve_move(b, m).is_valid());
    }
}

TEST(UCI, GoInfiniteStopWithin100ms) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    uci.process_command("go infinite");

    EXPECT_TRUE(uci.is_searching());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto t0 = std::chrono::steady_clock::now();
    uci.process_command("stop");
    uci.wait_for_search();
    auto t1 = std::chrono::steady_clock::now();

    auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_TRUE(stop_ms < 100);
    EXPECT_FALSE(uci.is_searching());

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "bestmove "));
    EXPECT_TRUE(is_valid_uci_stream(s));
}

TEST(UCI, GoDepth1ReturnsLegalBestmove) {
    std::istringstream in;
    std::ostringstream out;
    UciController uci(in, out);

    uci.process_command("position startpos");
    uci.process_command("go depth 1");
    uci.wait_for_search();

    std::string s = out.str();
    EXPECT_TRUE(contains(s, "info depth 1"));
    EXPECT_TRUE(contains(s, "bestmove "));
    EXPECT_TRUE(is_valid_uci_stream(s));

    auto bm_pos = s.find("bestmove ");
    EXPECT_TRUE(bm_pos != std::string::npos);
    if (bm_pos != std::string::npos) {
        std::string move_token = s.substr(bm_pos + 9, 4);
        Move m = move_from_uci(move_token);
        EXPECT_TRUE(m.is_valid());
        Board b;
        EXPECT_TRUE(resolve_move(b, m).is_valid());
    }
}

TEST(UCI, GoMovetimeOvershootMeasurements) {
    for (int target_ms : {100, 1000}) {
        for (int i = 0; i < 5; ++i) {
            std::istringstream in;
            std::ostringstream out;
            UciController uci(in, out);

            uci.process_command("position startpos");
            auto t0 = std::chrono::steady_clock::now();
            uci.process_command("go movetime " + std::to_string(target_ms));
            uci.wait_for_search();
            auto t1 = std::chrono::steady_clock::now();

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            EXPECT_TRUE(elapsed >= target_ms);
            EXPECT_TRUE(elapsed < target_ms + 100);

            std::string s = out.str();
            EXPECT_TRUE(contains(s, "bestmove "));
            EXPECT_TRUE(is_valid_uci_stream(s));
        }
    }
}
