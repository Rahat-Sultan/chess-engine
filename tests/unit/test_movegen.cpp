/**
 * test_movegen.cpp — Unit tests for move generation and check detection
 *
 * Covers:
 *  - is_in_check: known check and non-check positions
 *  - is_square_attacked: multiple piece types
 *  - Legal move count for well-known positions
 *  - Checkmate: no legal moves + in check
 *  - Stalemate: no legal moves + not in check
 *  - En-passant move is generated
 *  - Castling moves: king-side, queen-side, both blocked and allowed
 *  - Promotion moves generated (all 4 piece types)
 *  - resolve_move: UCI string → fully-annotated Move
 */

#include "test_framework.hpp"
#include "chess/movegen.hpp"
#include "chess/board.hpp"
#include "chess/types.hpp"

#include <string>
#include <algorithm>
#include <random>

using namespace chess;

// ─── Helper: count moves with a given from-square ────────────────────────────

static bool has_move(const MoveList& list, const char* uci) {
    Square from = square_from_name(std::string(uci).substr(0, 2));
    Square to   = square_from_name(std::string(uci).substr(2, 2));
    for (const Move& m : list)
        if (m.from == from && m.to == to) return true;
    return false;
}

static bool has_move_exact(const MoveList& list, const char* uci_str) {
    // Matches from, to, and promotion piece if present
    Move partial = move_from_uci(uci_str);
    for (const Move& m : list) {
        if (m.from != partial.from || m.to != partial.to) continue;
        if (partial.is_promotion() && m.promotion_piece != partial.promotion_piece) continue;
        return true;
    }
    return false;
}

// ─── is_in_check ─────────────────────────────────────────────────────────────

TEST(Check, StartingPositionNotInCheck) {
    Board b;
    EXPECT_FALSE(is_in_check(b, Color::White));
    EXPECT_FALSE(is_in_check(b, Color::Black));
}

TEST(Check, WhiteKingInCheckByRook) {
    // White king on e1, Black rook on e8, nothing between
    Board b("4k3/8/8/8/8/8/8/4K2r w - - 0 1");
    EXPECT_TRUE(is_in_check(b, Color::White));
    EXPECT_FALSE(is_in_check(b, Color::Black));
}

TEST(Check, BlackKingInCheckByQueen) {
    Board b("4k3/8/8/8/8/8/4Q3/4K3 b - - 0 1");
    EXPECT_TRUE(is_in_check(b, Color::Black));
}

TEST(Check, KingInCheckByPawn) {
    // White king on e4, Black pawn on d5 attacks e4 diagonally
    Board b("4k3/8/8/3p4/4K3/8/8/8 w - - 0 1");
    EXPECT_TRUE(is_in_check(b, Color::White));
}

TEST(Check, KingInCheckByKnight) {
    // White king on e4, Black knight on f6
    Board b("4k3/8/5n2/8/4K3/8/8/8 w - - 0 1");
    EXPECT_TRUE(is_in_check(b, Color::White));
}

TEST(Check, KingBlockedNotInCheck) {
    // White king on e1, own pawn on e2 blocks Black queen on e8
    Board b("4k2r/8/8/8/8/8/4P3/4K3 w - - 0 1");
    EXPECT_FALSE(is_in_check(b, Color::White));
}

// ─── Legal move counts ────────────────────────────────────────────────────────

TEST(MoveGen, StartingPosition20Moves) {
    Board b;
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 20);
}

TEST(MoveGen, AfterE4Black20Moves) {
    Board b("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 20);
}

TEST(MoveGen, KiwipetePosition) {
    // Kiwipete — well-known perft position, 48 legal moves at depth 1
    Board b("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 48);
}

TEST(MoveGen, OnlyKings) {
    // Two kings facing each other across the board — 5 moves for White
    Board b("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 5);
}

// ─── Checkmate ────────────────────────────────────────────────────────────────

TEST(MoveGen, CheckmateNoLegalMoves) {
    // Scholar's mate: White queen on f7 delivers checkmate
    Board b("r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4");
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 0);
    EXPECT_TRUE(is_in_check(b, Color::Black));
}

TEST(MoveGen, QueenKingCheckmate) {
    // Queen on e7, White king e6 — Black king on e8 has no escape
    Board b("4k3/4Q3/4K3/8/8/8/8/8 b - - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 0);
    EXPECT_TRUE(is_in_check(b, Color::Black));
}

TEST(MoveGen, BackRankMate) {
    // Arabian mate: White rook h7, White knight f6, Black king h8 — checkmate
    Board b("7k/7R/5N2/8/8/8/8/7K b - - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 0);
    EXPECT_TRUE(is_in_check(b, Color::Black));
}

// ─── Stalemate ────────────────────────────────────────────────────────────────

TEST(MoveGen, StalemateNoLegalMoves) {
    // Classic stalemate: Black king on a8, White queen on b6, White king on a6
    Board b("k7/8/KQ6/8/8/8/8/8 b - - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_EQ(static_cast<int>(moves.size()), 0);
    EXPECT_FALSE(is_in_check(b, Color::Black));
}

// ─── En-passant ───────────────────────────────────────────────────────────────

TEST(MoveGen, EnPassantMoveGenerated) {
    // White pawn on e5, Black just played d7d5 (ep target d6)
    Board b("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
    MoveList moves = generate_legal_moves(b);
    EXPECT_TRUE(has_move(moves, "e5d6"));
}

TEST(MoveGen, EnPassantNotGeneratedWhenNoTarget) {
    Board b("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 2");
    MoveList moves = generate_legal_moves(b);
    EXPECT_FALSE(has_move(moves, "e5d6"));
}

TEST(MoveGen, EnPassantRemovesEnemyPawn) {
    Board b("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
    Move ep = resolve_move(b, move_from_uci("e5d6"));
    EXPECT_TRUE(ep.is_valid());
    EXPECT_TRUE(ep.is_en_passant());

    UndoInfo undo = b.make_move(ep);
    // d5 pawn should be gone
    EXPECT_TRUE(b.piece_at(make_square(3, 4)).is_empty());   // d5
    EXPECT_FALSE(b.piece_at(make_square(3, 5)).is_empty());  // d6 has our pawn
    b.unmake_move(ep, undo);
    // restored
    EXPECT_FALSE(b.piece_at(make_square(3, 4)).is_empty());  // d5 pawn back
}

// ─── Castling ─────────────────────────────────────────────────────────────────

TEST(MoveGen, WhiteKingSideCastling) {
    Board b("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_TRUE(has_move(moves, "e1g1"));
}

TEST(MoveGen, WhiteQueenSideCastling) {
    Board b("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_TRUE(has_move(moves, "e1c1"));
}

TEST(MoveGen, BlackKingSideCastling) {
    Board b("r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_TRUE(has_move(moves, "e8g8"));
}

TEST(MoveGen, CastlingBlockedByPiece) {
    // f1 occupied — king-side castling not possible
    Board b("4k3/8/8/8/8/8/8/4KB1R w K - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_FALSE(has_move(moves, "e1g1"));
}

TEST(MoveGen, CastlingBlockedByCheck) {
    // King is in check — no castling allowed
    Board b("4k2r/8/8/8/8/8/8/4K2R w K - 0 1");  // not in check here
    // Put the king in check via a rook on e8
    Board b2("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    // Artificially: king under attack on e1 by placing a Black rook on e8
    Board b3("4k3/8/8/8/8/8/8/r3K2R w K - 0 1");
    // Black rook on a1 attacks e1? No, a1-e1 is along rank 1.
    // Use rook on e8 pointing to e1 — but need clear path.
    Board b4("4k3/4r3/8/8/8/8/8/4K2R w K - 0 1");
    // e2..e7 empty, Black rook on e7 attacks e1 via e-file
    MoveList moves = generate_legal_moves(b4);
    EXPECT_FALSE(has_move(moves, "e1g1"));  // in check, can't castle
}

TEST(MoveGen, CastlingThroughAttackedSquare) {
    // Black rook on f8 attacks f1 — White king-side castling not allowed
    Board b("4k1r1/8/8/8/8/8/8/4K2R w K - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_FALSE(has_move(moves, "e1g1"));
}

TEST(MoveGen, CastlingRightsRevoked) {
    Board b("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    Move km = resolve_move(b, move_from_uci("e1d1"));  // king moves away
    EXPECT_TRUE(km.is_valid());
    UndoInfo undo = b.make_move(km);
    // Black moves
    Board b2 = b;
    b2.set_fen("4k3/8/8/8/8/8/8/3K3R b - - 1 1");
    // After king moves, castling rights should be gone for White
    EXPECT_FALSE(b.can_castle_wk());
    b.unmake_move(km, undo);
    EXPECT_TRUE(b.can_castle_wk());  // restored after unmake
}

// ─── Promotions ───────────────────────────────────────────────────────────────

TEST(MoveGen, PromotionAllFourPieces) {
    // White pawn on a7, open path to a8
    Board b("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    MoveList moves = generate_legal_moves(b);

    EXPECT_TRUE(has_move_exact(moves, "a7a8q"));
    EXPECT_TRUE(has_move_exact(moves, "a7a8r"));
    EXPECT_TRUE(has_move_exact(moves, "a7a8b"));
    EXPECT_TRUE(has_move_exact(moves, "a7a8n"));
}

TEST(MoveGen, CapturePromotion) {
    // White pawn on a7, Black rook on b8 — can capture+promote
    Board b("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    MoveList moves = generate_legal_moves(b);
    EXPECT_TRUE(has_move_exact(moves, "a7b8q"));
    EXPECT_TRUE(has_move_exact(moves, "a7b8r"));
}

// ─── Pin: piece pinned to king cannot move ────────────────────────────────────

TEST(MoveGen, PinnedPieceCannotMove) {
    // White rook on e2, White king on e1, Black rook on e7 — rook is pinned along e-file.
    // The rook CAN move along the pin line but NOT off it.
    Board b("4k3/4r3/8/8/8/8/4R3/4K3 w - - 0 1");
    MoveList moves = generate_legal_moves(b);

    // Rook can move along e-file (e.g. capture e7)
    EXPECT_TRUE(has_move(moves, "e2e7"));
    EXPECT_TRUE(has_move(moves, "e2e3"));
    // Rook cannot move off the e-file (would expose king)
    EXPECT_FALSE(has_move(moves, "e2a2"));
    EXPECT_FALSE(has_move(moves, "e2h2"));
}

// ─── resolve_move ─────────────────────────────────────────────────────────────

TEST(ResolveMove, NormalMove) {
    Board b;
    Move m = resolve_move(b, move_from_uci("e2e4"));
    EXPECT_TRUE(m.is_valid());
    EXPECT_TRUE(m.is_double_push());
    EXPECT_FALSE(m.is_capture());
}

TEST(ResolveMove, CaptureAnnotated) {
    // 1.e4 e5 2.Nf3 Nc6 3.Bb5 — White bishop can capture on c6 eventually.
    // Simpler: White rook captures Black pawn
    Board b("4k3/8/8/8/8/8/p7/R3K3 w Q - 0 1");
    // White Ra1 captures pawn on a2
    Move m = resolve_move(b, move_from_uci("a1a2"));
    EXPECT_TRUE(m.is_valid());
    EXPECT_TRUE(m.is_capture());
}

TEST(ResolveMove, IllegalMoveReturnsNull) {
    Board b;
    // e2e5 is not a legal move
    Move m = resolve_move(b, move_from_uci("e2e5"));
    EXPECT_FALSE(m.is_valid());
}

TEST(ResolveMove, PromotionResolved) {
    Board b("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    Move m = resolve_move(b, move_from_uci("a7a8q"));
    EXPECT_TRUE(m.is_valid());
    EXPECT_TRUE(m.is_promotion());
    EXPECT_TRUE(m.promotion_piece == PieceType::Queen);
}

TEST(ResolveMove, CastlingResolved) {
    Board b("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    Move m = resolve_move(b, move_from_uci("e1g1"));
    EXPECT_TRUE(m.is_valid());
    EXPECT_TRUE(m.is_castling());
}

TEST(MoveGen, NoCastlingMovesWhenRightsSanitized) {
    // Malformed FEN with Black king on g8 and no rook on h8, but claiming KQkq
    const char* malformed_fen =
        "r2q1rk1/ppp2ppp/2n1bn2/1B1p4/3P4/1Q3N2/PP3PPP/RNB1K2R b KQkq - 0 1";
    Board b(malformed_fen);

    MoveList moves = generate_legal_moves(b);
    EXPECT_FALSE(has_move(moves, "e8g8"));
    EXPECT_FALSE(has_move(moves, "e8c8"));
}

// ─── Differential Test: Fast Legality vs Reference ───────────────────────────

TEST(MoveGen, DifferentialFastVsReferencePerftAndRandomPlayouts) {
    const std::vector<std::string> seed_fens = {
        // Standard startpos
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        // Kiwipete (rich pins, castling, en-passant)
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        // Perft Pos 3
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        // Perft Pos 4 (pins and double checks)
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1p1P1/q4N2/Pp1P1PP1/R2QK2R b KQkq - 0 1",
        // Perft Pos 5 (promotions and discovered checks)
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        // Tactical benchmark
        "r1b1k1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
        // Middlegame benchmark
        "r2q1rk1/ppp2ppp/2n1bn2/1B1p4/3P4/1Q3N2/PP3PPP/RNB1K2R w KQ - 0 1",
        // Forks benchmark
        "r3k2r/p1pp1pp1/bn2pnp1/3P4/1p2P3/2N5/PP3PPP/R1BQK1NR w KQkq - 0 1",
        // Pin along diagonal
        "k7/8/8/8/3b4/8/4R3/4K3 w - - 0 1",
        // Pin along rank
        "k7/8/8/8/8/8/4r3/4K2R w K - 0 1",
        // Double check position
        "k7/8/8/8/2n5/8/8/K2B1N1r w - - 0 1"
    };

    auto assert_moves_equal = [](Board& b) {
        MoveList fast = generate_legal_moves(b);
        MoveList ref  = generate_legal_moves_reference(b);
        EXPECT_EQ(fast.size(), ref.size());
        for (std::size_t i = 0; i < fast.size(); ++i) {
            EXPECT_EQ(fast[i].from, ref[i].from);
            EXPECT_EQ(fast[i].to, ref[i].to);
            EXPECT_EQ(fast[i].flags, ref[i].flags);
            EXPECT_EQ(static_cast<int>(fast[i].promotion_piece),
                      static_cast<int>(ref[i].promotion_piece));
        }
    };

    // Test all initial seed positions directly
    for (const auto& fen : seed_fens) {
        Board b(fen);
        assert_moves_equal(b);
    }

    // Random legal playouts: generate at least 25,000 distinct positions
    std::size_t tested_positions = 0;
    const std::vector<uint64_t> seeds = { 42ULL, 1337ULL, 987654ULL, 123456789ULL };
    std::size_t fen_idx = 0;

    for (uint64_t seed : seeds) {
        std::mt19937_64 rng(seed);

        while (tested_positions < 25000) {
            Board b(seed_fens[fen_idx % seed_fens.size()]);
            ++fen_idx;

            for (int ply = 0; ply < 120; ++ply) {
                MoveList legal = generate_legal_moves(b);
                MoveList ref   = generate_legal_moves_reference(b);

                EXPECT_EQ(legal.size(), ref.size());
                for (std::size_t i = 0; i < legal.size(); ++i) {
                    EXPECT_EQ(legal[i].from, ref[i].from);
                    EXPECT_EQ(legal[i].to, ref[i].to);
                    EXPECT_EQ(legal[i].flags, ref[i].flags);
                    EXPECT_EQ(static_cast<int>(legal[i].promotion_piece),
                              static_cast<int>(ref[i].promotion_piece));
                }

                ++tested_positions;
                if (tested_positions >= 25000) break;

                if (legal.empty() || b.halfmove_clock() >= 100) break;

                std::uniform_int_distribution<std::size_t> dist(0, legal.size() - 1);
                b.make_move(legal[dist(rng)]);
            }
        }
    }

    EXPECT_TRUE(tested_positions >= 20000);
}
