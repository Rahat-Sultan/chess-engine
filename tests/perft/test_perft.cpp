/**
 * test_perft.cpp — Perft correctness tests
 *
 * Compares perft(depth) counts against published reference values.
 * These are the gold standard for detecting move-generation bugs.
 *
 * Positions and reference values from:
 *   https://www.chessprogramming.org/Perft_Results
 *
 * Depths are chosen to run quickly (< 2 s each) while still exercising
 * all special rules.  Deeper depths will be added once the engine is fast.
 *
 * Positions tested:
 *   1. Starting position
 *   2. Kiwipete (r3k2r/p1ppqpb1/...)
 *   3. Position 3 (8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -)
 *   4. Position 4 (r3k2r/Pppp1ppp/1b3nbN/nP6/...)
 *   5. Position 5 (rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8)
 */

#define TEST_MAIN
#include "test_framework.hpp"
#include "chess/board.hpp"
#include "tests/perft/perft.hpp"

#include <cstdint>
#include <iostream>

using namespace chess;

// ─── Helper ───────────────────────────────────────────────────────────────────

static void check_perft(const char* fen, int depth, uint64_t expected) {
    Board b(fen);
    uint64_t got = perft(b, depth);
    if (got != expected) {
        std::cerr << "  FAIL perft(" << depth << ") for FEN:\n"
                  << "       " << fen << "\n"
                  << "       expected " << expected
                  << "  got " << got << "\n";
        g_current_test_failed = true;
    }
}

// ─── Position 1: Starting position ───────────────────────────────────────────
// Ref: https://www.chessprogramming.org/Perft_Results#Initial_Position

TEST(Perft, Start_D1) { check_perft(STARTING_FEN, 1,        20ULL); }
TEST(Perft, Start_D2) { check_perft(STARTING_FEN, 2,       400ULL); }
TEST(Perft, Start_D3) { check_perft(STARTING_FEN, 3,      8902ULL); }
TEST(Perft, Start_D4) { check_perft(STARTING_FEN, 4,    197281ULL); }

// ─── Position 2: Kiwipete ─────────────────────────────────────────────────────
// r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1

static const char* POS2 =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

TEST(Perft, Kiwipete_D1) { check_perft(POS2, 1,   48ULL); }
TEST(Perft, Kiwipete_D2) { check_perft(POS2, 2, 2039ULL); }
TEST(Perft, Kiwipete_D3) { check_perft(POS2, 3, 97862ULL); }

// ─── Position 3 ──────────────────────────────────────────────────────────────
// 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1

static const char* POS3 =
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";

TEST(Perft, Pos3_D1) { check_perft(POS3, 1,    14ULL); }
TEST(Perft, Pos3_D2) { check_perft(POS3, 2,   191ULL); }
TEST(Perft, Pos3_D3) { check_perft(POS3, 3,  2812ULL); }
TEST(Perft, Pos3_D4) { check_perft(POS3, 4, 43238ULL); }

// ─── Position 4 ──────────────────────────────────────────────────────────────
// r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1p1P1/q4N2/Pp1P1PP1/R2QK2R b KQkq - 0 1
// (mirror of the chessprogramming.org pos4; using the standard orientation)

static const char* POS4 =
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1p1P1/q4N2/Pp1P1PP1/R2QK2R b KQkq - 0 1";

TEST(Perft, Pos4_D1) { check_perft(POS4, 1,   45ULL); }
TEST(Perft, Pos4_D2) { check_perft(POS4, 2, 1667ULL); }

// ─── Position 5 ──────────────────────────────────────────────────────────────
// rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8

static const char* POS5 =
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";

TEST(Perft, Pos5_D1) { check_perft(POS5, 1,   44ULL); }
TEST(Perft, Pos5_D2) { check_perft(POS5, 2, 1486ULL); }
TEST(Perft, Pos5_D3) { check_perft(POS5, 3, 62379ULL); }
