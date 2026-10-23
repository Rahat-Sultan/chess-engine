/**
 * test_transposition.cpp — Unit tests for the transposition table (Phase 5)
 *
 * Covers:
 *   - Packed move round-trip (normal, promotion, en-passant, castling)
 *   - TT store and probe (exact, lower, upper bounds)
 *   - TT miss on unknown key
 *   - TT clear
 *   - TT resize (entries change, table cleared)
 *   - TT generation-based replacement
 *   - TT hashfull reporting
 *   - Zobrist key consistency (same position → same key, different position → different key)
 */

#include "test_framework.hpp"
#include "chess/board.hpp"
#include "chess/movegen.hpp"
#include "chess/transposition.hpp"
#include "chess/types.hpp"

using namespace chess;

// ─── Packed move tests ────────────────────────────────────────────────────────

TEST(TT, PackUnpackNormalMove) {
    Move m;
    m.from = square_from_name("e2");
    m.to   = square_from_name("e4");
    m.flags = MF_DOUBLE_PUSH;
    uint16_t packed = pack_move(m);
    Move m2 = unpack_move(packed);

    EXPECT_EQ(m2.from, square_from_name("e2"));
    EXPECT_EQ(m2.to,   square_from_name("e4"));
    EXPECT_FALSE(m2.is_promotion());
}

TEST(TT, PackUnpackPromotion) {
    Move m;
    m.from = square_from_name("g7");
    m.to   = square_from_name("g8");
    m.flags = MF_PROMOTION;
    m.promotion_piece = PieceType::Queen;
    uint16_t packed = pack_move(m);
    Move m2 = unpack_move(packed);

    EXPECT_EQ(m2.from, square_from_name("g7"));
    EXPECT_EQ(m2.to,   square_from_name("g8"));
    EXPECT_TRUE(m2.is_promotion());
    EXPECT_EQ(static_cast<int>(m2.promotion_piece), static_cast<int>(PieceType::Queen));
}

TEST(TT, PackUnpackKnightPromotion) {
    Move m;
    m.from = square_from_name("a7");
    m.to   = square_from_name("a8");
    m.flags = MF_PROMOTION;
    m.promotion_piece = PieceType::Knight;
    uint16_t packed = pack_move(m);
    Move m2 = unpack_move(packed);

    EXPECT_TRUE(m2.is_promotion());
    EXPECT_EQ(static_cast<int>(m2.promotion_piece), static_cast<int>(PieceType::Knight));
}

TEST(TT, PackUnpackNullMove) {
    Move m;  // NULL_MOVE by default
    uint16_t packed = pack_move(m);
    EXPECT_EQ(packed, static_cast<uint16_t>(0));
    Move m2 = unpack_move(packed);
    EXPECT_FALSE(m2.is_valid());
}

TEST(TT, PackUnpackCastling) {
    Move m;
    m.from = E1;
    m.to   = G1;
    m.flags = MF_CASTLING;
    uint16_t packed = pack_move(m);
    Move m2 = unpack_move(packed);

    EXPECT_EQ(m2.from, E1);
    EXPECT_EQ(m2.to,   G1);
    EXPECT_FALSE(m2.is_promotion());
}

// ─── TT store/probe tests ─────────────────────────────────────────────────────

TEST(TT, StoreAndProbeExact) {
    TranspositionTable tt(1);
    tt.new_search();

    uint64_t key = 0xABCDEF1234567890ULL;
    Move m;
    m.from = square_from_name("e2");
    m.to   = square_from_name("e4");
    m.flags = MF_DOUBLE_PUSH;

    tt.store(key, 42, 5, BOUND_EXACT, m);

    const TTEntry* entry = tt.probe(key);
    EXPECT_TRUE(entry != nullptr);
    EXPECT_EQ(entry->score, 42);
    EXPECT_EQ(static_cast<int>(entry->depth), 5);
    EXPECT_EQ(static_cast<int>(entry->bound), static_cast<int>(BOUND_EXACT));

    Move stored = unpack_move(entry->move);
    EXPECT_EQ(stored.from, square_from_name("e2"));
    EXPECT_EQ(stored.to,   square_from_name("e4"));
}

TEST(TT, StoreAndProbeLowerBound) {
    TranspositionTable tt(1);
    tt.new_search();

    uint64_t key = 0x1111111111111111ULL;
    tt.store(key, 100, 3, BOUND_LOWER, NULL_MOVE);

    const TTEntry* entry = tt.probe(key);
    EXPECT_TRUE(entry != nullptr);
    EXPECT_EQ(entry->score, 100);
    EXPECT_EQ(static_cast<int>(entry->bound), static_cast<int>(BOUND_LOWER));
}

TEST(TT, StoreAndProbeUpperBound) {
    TranspositionTable tt(1);
    tt.new_search();

    uint64_t key = 0x2222222222222222ULL;
    tt.store(key, -50, 4, BOUND_UPPER, NULL_MOVE);

    const TTEntry* entry = tt.probe(key);
    EXPECT_TRUE(entry != nullptr);
    EXPECT_EQ(entry->score, -50);
    EXPECT_EQ(static_cast<int>(entry->bound), static_cast<int>(BOUND_UPPER));
}

TEST(TT, ProbeMissOnUnknownKey) {
    TranspositionTable tt(1);
    tt.new_search();

    tt.store(0x1111111111111111ULL, 42, 5, BOUND_EXACT, NULL_MOVE);

    EXPECT_TRUE(tt.probe(0x9999999999999999ULL) == nullptr);
}

TEST(TT, OverwriteSameKey) {
    TranspositionTable tt(1);
    tt.new_search();

    uint64_t key = 0xABCDEF1234567890ULL;
    tt.store(key, 42, 5, BOUND_EXACT, NULL_MOVE);
    tt.store(key, 99, 7, BOUND_LOWER, NULL_MOVE);

    const TTEntry* entry = tt.probe(key);
    EXPECT_TRUE(entry != nullptr);
    EXPECT_EQ(entry->score, 99);
    EXPECT_EQ(static_cast<int>(entry->depth), 7);
    EXPECT_EQ(static_cast<int>(entry->bound), static_cast<int>(BOUND_LOWER));
}

// ─── TT clear ─────────────────────────────────────────────────────────────────

TEST(TT, ClearRemovesAllEntries) {
    TranspositionTable tt(1);
    tt.new_search();

    uint64_t key = 0xABCDEF1234567890ULL;
    tt.store(key, 42, 5, BOUND_EXACT, NULL_MOVE);
    EXPECT_TRUE(tt.probe(key) != nullptr);

    tt.clear();
    EXPECT_TRUE(tt.probe(key) == nullptr);
    EXPECT_EQ(tt.hash_full_permille(), 0);
}

// ─── TT resize ────────────────────────────────────────────────────────────────

TEST(TT, ResizeChangesEntryCount) {
    TranspositionTable tt(1);
    std::size_t small_count = tt.num_entries();

    tt.resize(16);
    std::size_t large_count = tt.num_entries();

    EXPECT_TRUE(large_count > small_count);
    // 16 MB / 16 bytes per entry = ~1M entries
    EXPECT_TRUE(large_count >= 1000000);
}

TEST(TT, ResizeClearsEntries) {
    TranspositionTable tt(1);
    tt.new_search();

    uint64_t key = 0xABCDEF1234567890ULL;
    tt.store(key, 42, 5, BOUND_EXACT, NULL_MOVE);
    EXPECT_TRUE(tt.probe(key) != nullptr);

    tt.resize(2);
    EXPECT_TRUE(tt.probe(key) == nullptr);
}

// ─── TT generation ─────────────────────────────────────────────────────────────

TEST(TT, NewSearchIncrementsGeneration) {
    TranspositionTable tt(1);
    uint8_t gen0 = tt.generation();
    tt.new_search();
    uint8_t gen1 = tt.generation();
    EXPECT_EQ(static_cast<int>(gen1), static_cast<int>(gen0) + 1);
}

TEST(TT, GenerationBasedReplacement) {
    TranspositionTable tt(1);

    // Store in generation 0
    tt.new_search();  // gen 1
    uint64_t key1 = 0x1111111111111111ULL;
    tt.store(key1, 42, 1, BOUND_EXACT, NULL_MOVE);

    // Advance to next search (gen 2)
    tt.new_search();

    // Store a different key that maps to the same slot
    // Find a key that collides with key1
    std::size_t slot = key1 & (tt.num_entries() - 1);
    uint64_t key2 = 0;
    // Search for a key that maps to the same slot
    for (uint64_t k = 0xFFFFFFFFULL; k > 0; k <<= 1) {
        if ((k & (tt.num_entries() - 1)) == slot && k != key1) {
            key2 = k;
            break;
        }
    }
    if (key2 == 0) {
        // If we can't easily find a colliding key, just verify the
        // old entry from gen 1 can be overwritten
        key2 = key1 | 0x8000000000000000ULL;
        if ((key2 & (tt.num_entries() - 1)) != slot) {
            // Skip this specific collision test if we can't find a match
            return;
        }
    }

    tt.store(key2, 99, 1, BOUND_EXACT, NULL_MOVE);

    // Old entry should be gone (replaced due to older generation)
    EXPECT_TRUE(tt.probe(key1) == nullptr);
    // New entry should be found
    const TTEntry* e = tt.probe(key2);
    EXPECT_TRUE(e != nullptr);
    EXPECT_EQ(e->score, 99);
}

// ─── TT hashfull ──────────────────────────────────────────────────────────────

TEST(TT, HashFullPermille) {
    TranspositionTable tt(1);
    tt.new_search();

    EXPECT_EQ(tt.hash_full_permille(), 0);

    // Store enough entries to fill some of the table
    for (int i = 0; i < 100; ++i) {
        uint64_t key = static_cast<uint64_t>(static_cast<unsigned>(i)) * 0x100000001ULL;
        tt.store(key, i, 1, BOUND_EXACT, NULL_MOVE);
    }

    int fill = tt.hash_full_permille();
    EXPECT_TRUE(fill > 0);
    // With 100 entries in a ~65K table, fill should be small but non-zero
    // (hashfull samples first 1000 entries)
    EXPECT_TRUE(fill <= 1000);
}

// ─── Zobrist key tests ────────────────────────────────────────────────────────

TEST(TT, ZobristSamePositionSameKey) {
    Board b1;
    Board b2;
    EXPECT_EQ(b1.zobrist_key(), b2.zobrist_key());
}

TEST(TT, ZobristDifferentPositionDifferentKey) {
    Board b1;  // starting position
    Board b2("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");  // after 1.e4
    EXPECT_NE(b1.zobrist_key(), b2.zobrist_key());
}

TEST(TT, ZobristKeyAfterMakeUnmake) {
    Board b;
    uint64_t key_before = b.zobrist_key();

    MoveList moves = generate_legal_moves(b);
    EXPECT_FALSE(moves.empty());

    UndoInfo undo = b.make_move(moves[0]);
    EXPECT_NE(b.zobrist_key(), key_before);  // key changes after a move

    b.unmake_move(moves[0], undo);
    EXPECT_EQ(b.zobrist_key(), key_before);  // key restored after unmake
}

TEST(TT, ZobristKeyDeterministic) {
    // Same FEN always produces the same key
    const char* fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    Board b1(fen);
    Board b2(fen);
    EXPECT_EQ(b1.zobrist_key(), b2.zobrist_key());
}

TEST(TT, ZobristKeyEnPassantMatters) {
    // Two positions that differ only in en-passant square
    const char* fen1 = "rnbqkbnr/pp1ppppp/8/2pP4/8/8/PPP1PPPP/RNBQKBNR w KQkq c6 0 3";
    const char* fen2 = "rnbqkbnr/pp1ppppp/8/2pP4/8/8/PPP1PPPP/RNBQKBNR w KQkq - 0 3";
    Board b1(fen1);
    Board b2(fen2);
    EXPECT_NE(b1.zobrist_key(), b2.zobrist_key());
}

TEST(TT, ZobristKeySideToMoveMatters) {
    const char* fen_w = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char* fen_b = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
    Board bw(fen_w);
    Board bb(fen_b);
    EXPECT_NE(bw.zobrist_key(), bb.zobrist_key());
}

TEST(TT, IncrementalZobristRandomWalkRoundTrip) {
    const char* test_positions[] = {
        STARTING_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", // Kiwipete
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",                             // Position 3
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",       // Position 4
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",             // Position 5
        "rnbqkbnr/pp1ppppp/8/2pP4/8/8/PPP1PPPP/RNBQKBNR w KQkq c6 0 3",           // En passant active
    };

    uint64_t rng = 0xdeadbeefcafe1234ULL;
    auto next_rand = [&rng]() -> uint64_t {
        rng ^= rng >> 12;
        rng ^= rng << 25;
        rng ^= rng >> 27;
        return rng * 0x2545F4914F6CDD1DULL;
    };

    for (const char* fen : test_positions) {
        Board b(fen);
        uint64_t start_key = b.zobrist_key();

        struct PlayedMove {
            Move m;
            UndoInfo undo;
        };
        std::vector<PlayedMove> history;

        for (int step = 0; step < 100; ++step) {
            MoveList moves = generate_legal_moves(b);
            if (moves.empty()) break;

            std::size_t idx = static_cast<std::size_t>(next_rand() % moves.size());
            Move m = moves[idx];

            UndoInfo undo = b.make_move(m);
            history.push_back({m, undo});

            Board copy_b(b.fen());
            EXPECT_EQ(b.zobrist_key(), copy_b.zobrist_key());
        }

        while (!history.empty()) {
            PlayedMove pm = history.back();
            history.pop_back();

            b.unmake_move(pm.m, pm.undo);

            Board copy_b(b.fen());
            EXPECT_EQ(b.zobrist_key(), copy_b.zobrist_key());
        }

        EXPECT_EQ(b.zobrist_key(), start_key);
    }
}
