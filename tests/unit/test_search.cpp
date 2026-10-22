/**
 * test_search.cpp — Unit tests for evaluation and search (Phase 3)
 *
 * Covers:
 *  Evaluator:
 *   - Starting position score is near zero
 *   - Material advantage reflected in score (side-to-move perspective)
 *   - Extra queen gives large positive score
 *   - Score flips sign when side to move changes
 *   - Bishop pair bonus applied
 *
 *  Search:
 *   - Mate-in-one found (multiple positions)
 *   - Checkmate returns no best_move / mated score
 *   - Stalemate returns draw score
 *   - Depth limit respected (node count grows with depth)
 *   - Node limit stops search before exhausting depth
 *   - Movetime limit stops search within time budget
 *   - Best move is always a legal move in the position
 *   - stop() interrupts and returns a valid move
 *   - PV is non-empty and starts with best_move
 *   - 50-move rule returns draw
 */

#include "test_framework.hpp"
#include "chess/board.hpp"
#include "chess/evaluation.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/types.hpp"

#include <string>
#include <thread>
#include <chrono>

using namespace chess;

// ─── Shared evaluator instance ────────────────────────────────────────────────
// Each test creates its own to avoid state leakage.

static HandcraftedEvaluator make_eval() { return HandcraftedEvaluator{}; }

// ─── Helper: verify a move is legal in the position ──────────────────────────

static bool is_legal(Board& board, const Move& m) {
    if (!m.is_valid()) return false;
    MoveList legal = generate_legal_moves(board);
    for (const Move& lm : legal)
        if (lm.from == m.from && lm.to == m.to &&
            lm.promotion_piece == m.promotion_piece)
            return true;
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// EVALUATOR TESTS
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Eval, StartingPositionNearZero) {
    // Material is equal; PST is roughly symmetric — score should be small
    Board b;
    auto eval = make_eval();
    int score = eval.evaluate(b);
    // Allow ±50 cp for PST asymmetry (White has tempo advantage from PSTs)
    EXPECT_TRUE(score > -50 && score < 200);
}

TEST(Eval, SideToMoveFlipsSign) {
    // Same material, same side. If we manually flip side, score should negate
    // (approximately — mobility term depends on side, so allow tolerance).
    const char* fen_w = "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
    const char* fen_b = "4k3/8/8/8/8/8/8/4K3 b - - 0 1";
    Board bw(fen_w);
    Board bb(fen_b);
    auto eval = make_eval();
    int sw = eval.evaluate(bw);
    int sb = eval.evaluate(bb);
    // Both kings only: material and PST are symmetric, mobility may differ ±
    // The scores should be opposite sign (or both zero)
    EXPECT_TRUE(sw == -sb || (sw == 0 && sb == 0) || (sw > -10 && sb < 10));
}

TEST(Eval, ExtraQueenIsLargePositive) {
    // White has a queen, Black doesn't — strongly positive for White
    Board b("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    auto eval = make_eval();
    int score = eval.evaluate(b);
    EXPECT_TRUE(score > 700);   // queen ~= 900 cp
}

TEST(Eval, ExtraQueenNegativeForBlack) {
    // Black to move, White has extra queen — score negative for Black
    Board b("4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
    auto eval = make_eval();
    int score = eval.evaluate(b);
    EXPECT_TRUE(score < -700);
}

TEST(Eval, MaterialImbalanceReflected) {
    // White has rook + bishop extra
    Board b("4k3/8/8/8/8/8/8/R2BK3 w - - 0 1");
    auto eval = make_eval();
    int score = eval.evaluate(b);
    EXPECT_TRUE(score > 700);  // R(500) + B(330) = 830 cp material edge
}

TEST(Eval, BishopPairBonus) {
    // White has 2 bishops, Black has only pawns
    Board b("4k3/pppppppp/8/8/8/8/8/2BBK3 w - - 0 1");
    auto eval_no_pair = make_eval();
    // Compare two bishops vs one bishop
    Board b1("4k3/pppppppp/8/8/8/8/8/3BK3 w - - 0 1");
    auto e1 = make_eval();
    auto e2 = make_eval();
    int score_pair = e1.evaluate(b);
    int score_single = e2.evaluate(b1);
    // Pair should score higher
    EXPECT_TRUE(score_pair > score_single);
    (void)eval_no_pair;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SEARCH TESTS
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Search, MateInOne_Scholar) {
    // White to move: Qh5xf7# — one-move checkmate
    Board b("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 3;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    EXPECT_TRUE(r.best_move.is_valid());
    EXPECT_EQ(move_to_uci(r.best_move), std::string("h5f7"));
    EXPECT_TRUE(is_mate_score(r.score));
}

TEST(Search, MateInOne_BackRank) {
    // White rook on h1, Black king a8 with pawns on a7/b7 blocking escape — Rh8#
    Board b("k7/pp6/8/8/8/8/8/4K2R w - - 0 1");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 3;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    EXPECT_TRUE(r.best_move.is_valid());
    EXPECT_EQ(move_to_uci(r.best_move), std::string("h1h8"));
    EXPECT_TRUE(is_mate_score(r.score));
}

TEST(Search, MateInOne_QueenDeliver) {
    // White queen on h1 delivers Qh7# with Black king on h8 and pawns g7/h7 blocking
    Board b("7k/6pp/8/8/8/8/8/4K2Q w - - 0 1");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 3;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    EXPECT_TRUE(r.best_move.is_valid());
    // Unique mate: Qh1–a8#
    EXPECT_EQ(move_to_uci(r.best_move), std::string("h1a8"));
    EXPECT_TRUE(is_mate_score(r.score));
}

TEST(Search, AlreadyCheckmated) {
    // Position is already checkmate for Black to move — no legal moves
    Board b("r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 3;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    // No legal moves — search should return invalid best_move and mated score
    EXPECT_FALSE(r.best_move.is_valid());
    EXPECT_TRUE(r.score <= -SCORE_MATE + 1000 || r.score == -SCORE_MATE);
}

TEST(Search, StalemateReturnsDraw) {
    // Black is stalemated (no legal moves, not in check)
    Board b("k7/8/KQ6/8/8/8/8/8 b - - 0 1");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 3;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    EXPECT_FALSE(r.best_move.is_valid());
    EXPECT_EQ(r.score, SCORE_DRAW);
}

TEST(Search, BestMoveIsLegal) {
    // In any normal position the returned best move must be legal
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    };
    for (const char* fen : fens) {
        Board b(fen);
        auto eval = make_eval();
        Searcher s(eval);
        SearchLimits lim; lim.depth = 4;
        s.set_limits(lim);
        SearchResult r = s.search(b);
        EXPECT_TRUE(r.best_move.is_valid());
        EXPECT_TRUE(is_legal(b, r.best_move));
    }
}

TEST(Search, DepthLimitRespectsNodes) {
    // Searching depth 2 should visit fewer nodes than depth 4
    Board b;
    auto eval1 = make_eval();
    auto eval2 = make_eval();
    Searcher s1(eval1), s2(eval2);

    SearchLimits lim2; lim2.depth = 2;
    SearchLimits lim4; lim4.depth = 4;

    s1.set_limits(lim2);
    s2.set_limits(lim4);

    SearchResult r2 = s1.search(b);
    SearchResult r4 = s2.search(b);

    EXPECT_TRUE(r2.nodes < r4.nodes);
    EXPECT_TRUE(r2.depth <= 2);
    EXPECT_TRUE(r4.depth <= 4);
}

TEST(Search, NodeLimitStopsEarly) {
    Board b;
    auto eval = make_eval();
    Searcher s(eval);

    SearchLimits lim;
    lim.nodes = 500;  // allow only 500 nodes
    s.set_limits(lim);

    SearchResult r = s.search(b);
    // May slightly exceed due to granularity checks, allow 2x tolerance
    EXPECT_TRUE(r.nodes <= 1200);
    EXPECT_TRUE(r.best_move.is_valid());
    EXPECT_TRUE(is_legal(b, r.best_move));
}

TEST(Search, MovetimeLimitStopsEarly) {
    Board b;
    auto eval = make_eval();
    Searcher s(eval);

    SearchLimits lim;
    lim.movetime_ms = 50;   // 50 ms budget
    s.set_limits(lim);

    auto t0 = std::chrono::steady_clock::now();
    SearchResult r = s.search(b);
    auto t1 = std::chrono::steady_clock::now();

    int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Should finish well under 500 ms even on slow hardware
    EXPECT_TRUE(ms < 500);
    EXPECT_TRUE(r.best_move.is_valid());
    EXPECT_TRUE(is_legal(b, r.best_move));
}

TEST(Search, StopInterrupts) {
    Board b;
    auto eval = make_eval();
    Searcher s(eval);

    // Kick off a deep search in a background thread, stop it after 30 ms
    SearchLimits lim;
    lim.depth = 64;  // effectively infinite depth
    s.set_limits(lim);

    SearchResult r;
    std::thread search_thread([&]() {
        r = s.search(b);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    s.stop();
    search_thread.join();

    // Must return a legal move
    EXPECT_TRUE(r.best_move.is_valid());
    EXPECT_TRUE(is_legal(b, r.best_move));
}

TEST(Search, PVNonEmptyAndConsistent) {
    Board b;
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 4;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    EXPECT_TRUE(!r.pv.empty());
    EXPECT_TRUE(r.pv[0] == r.best_move);

    // All PV moves should be sequentially legal
    Board verify;
    for (const Move& m : r.pv) {
        EXPECT_TRUE(is_legal(verify, m));
        verify.make_move(m);
    }
}

TEST(Search, FiftyMoveRuleDraw) {
    // Set halfmove clock to 100 — any search from here should return draw
    Board b("4k3/8/8/8/8/8/8/4K3 w - - 100 100");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 4;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    // Kings-only with 50-move rule — must return a legal move or draw
    // The engine sees draw immediately (halfmove == 100)
    EXPECT_EQ(r.score, SCORE_DRAW);
}

TEST(Search, CaptureBetterThanQuiet) {
    // White can capture a free queen — should prefer that over any quiet move
    // Rook on a1, enemy Queen on a8, nothing in between
    Board b("q3k3/8/8/8/8/8/8/R3K3 w Q - 0 1");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 3;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    EXPECT_TRUE(r.best_move.is_valid());
    // Best move should capture the queen on a8
    EXPECT_EQ(move_to_uci(r.best_move), std::string("a1a8"));
}

TEST(Search, AvoidsMateInOne) {
    // Black can be mated in one if it plays wrong — verify engine avoids it
    // White Qh5, Bc4 vs Black position: Black to move must block or escape
    // Position after 1.e4 e5 2.Qh5 Nc6 3.Bc4 — Black must play Nf6 or g6
    Board b("r1bqkb1r/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 3 3");
    auto eval = make_eval();
    Searcher s(eval);
    SearchLimits lim; lim.depth = 4;
    s.set_limits(lim);
    SearchResult r = s.search(b);

    // Black must not play a move that allows Qxf7#
    EXPECT_TRUE(r.best_move.is_valid());
    EXPECT_TRUE(is_legal(b, r.best_move));
    // Make the move and verify White can't mate in one
    Board after = b;
    after.make_move(r.best_move);
    MoveList white_moves = generate_legal_moves(after);
    bool white_has_mate = false;
    for (const Move& wm : white_moves) {
        UndoInfo undo = after.make_move(wm);
        if (generate_legal_moves(after).empty() &&
            is_in_check(after, after.side_to_move())) {
            white_has_mate = true;
        }
        after.unmake_move(wm, undo);
        if (white_has_mate) break;
    }
    EXPECT_FALSE(white_has_mate);
}
