/**
 * test_types.cpp — Unit tests for include/chess/types.hpp
 *
 * Covers:
 *  - Square construction, file/rank accessors, named corners
 *  - square_name / square_from_name round-trip for all 64 squares
 *  - is_valid_sq
 *  - Piece construction, equality, is_empty
 *  - opponent()
 *  - move_to_uci / move_from_uci round-trip (normal + promotion)
 *  - MoveFlag independence
 *  - Score constants and is_mate_score
 */

#include "test_framework.hpp"
#include "chess/types.hpp"

using namespace chess;

// ─── Convenience squares ─────────────────────────────────────────────────────
constexpr Square E2 = make_square(4, 1);
constexpr Square E4 = make_square(4, 3);

// ─── Square tests ─────────────────────────────────────────────────────────────

TEST(Square, CornerValues) {
    EXPECT_EQ(A1,  0);
    EXPECT_EQ(H1,  7);
    EXPECT_EQ(A8, 56);
    EXPECT_EQ(H8, 63);
}

TEST(Square, FileAndRank) {
    EXPECT_EQ(file_of(A1), 0);  EXPECT_EQ(rank_of(A1), 0);
    EXPECT_EQ(file_of(H1), 7);  EXPECT_EQ(rank_of(H1), 0);
    EXPECT_EQ(file_of(A8), 0);  EXPECT_EQ(rank_of(A8), 7);
    EXPECT_EQ(file_of(H8), 7);  EXPECT_EQ(rank_of(H8), 7);
    EXPECT_EQ(file_of(E4), 4);  EXPECT_EQ(rank_of(E4), 3);
}

TEST(Square, MakeSquare) {
    EXPECT_EQ(make_square(0, 0), A1);
    EXPECT_EQ(make_square(7, 0), H1);
    EXPECT_EQ(make_square(0, 7), A8);
    EXPECT_EQ(make_square(7, 7), H8);
    EXPECT_EQ(make_square(4, 3), E4);
}

TEST(Square, CornerNames) {
    EXPECT_EQ(square_name(A1), std::string("a1"));
    EXPECT_EQ(square_name(H1), std::string("h1"));
    EXPECT_EQ(square_name(A8), std::string("a8"));
    EXPECT_EQ(square_name(H8), std::string("h8"));
}

TEST(Square, NoSqName) {
    EXPECT_EQ(square_name(NO_SQ), std::string("-"));
}

TEST(Square, NameRoundTrip) {
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            Square s = make_square(f, r);
            std::string name = square_name(s);
            EXPECT_EQ(static_cast<int>(name.size()), 2);
            EXPECT_EQ(square_from_name(name), s);
        }
    }
}

TEST(Square, InvalidNames) {
    EXPECT_EQ(square_from_name(""),    NO_SQ);
    EXPECT_EQ(square_from_name("a"),   NO_SQ);
    EXPECT_EQ(square_from_name("a0"),  NO_SQ);
    EXPECT_EQ(square_from_name("a9"),  NO_SQ);
    EXPECT_EQ(square_from_name("i1"),  NO_SQ);
    EXPECT_EQ(square_from_name("a10"), NO_SQ);
    EXPECT_EQ(square_from_name("z5"),  NO_SQ);
}

TEST(Square, IsValid) {
    EXPECT_TRUE(is_valid_sq(0));
    EXPECT_TRUE(is_valid_sq(63));
    EXPECT_FALSE(is_valid_sq(NO_SQ));
    EXPECT_FALSE(is_valid_sq(64));
    EXPECT_FALSE(is_valid_sq(-2));
}

// ─── Piece tests ──────────────────────────────────────────────────────────────

TEST(Piece, DefaultIsEmpty) {
    Piece p{};
    EXPECT_TRUE(p.is_empty());
    EXPECT_TRUE(NO_PIECE.is_empty());
}

TEST(Piece, MakePiece) {
    Piece wp = make_piece(Color::White, PieceType::King);
    EXPECT_FALSE(wp.is_empty());
    EXPECT_TRUE(wp.type  == PieceType::King);
    EXPECT_TRUE(wp.color == Color::White);

    Piece bp = make_piece(Color::Black, PieceType::Queen);
    EXPECT_TRUE(bp.type  == PieceType::Queen);
    EXPECT_TRUE(bp.color == Color::Black);
}

TEST(Piece, Equality) {
    Piece wp = make_piece(Color::White, PieceType::Pawn);
    Piece bp = make_piece(Color::Black, PieceType::Pawn);
    EXPECT_TRUE(wp == wp);
    EXPECT_FALSE(wp == bp);
    EXPECT_TRUE(NO_PIECE == NO_PIECE);
    EXPECT_FALSE(NO_PIECE == wp);
}

TEST(Piece, AllTypesDistinct) {
    PieceType types[] = {
        PieceType::Pawn,   PieceType::Knight, PieceType::Bishop,
        PieceType::Rook,   PieceType::Queen,  PieceType::King
    };
    for (auto t1 : types) {
        for (auto t2 : types) {
            Piece p1 = make_piece(Color::White, t1);
            Piece p2 = make_piece(Color::White, t2);
            if (t1 == t2) { EXPECT_TRUE(p1 == p2); }
            else           { EXPECT_FALSE(p1 == p2); }
        }
    }
}

// ─── Color tests ──────────────────────────────────────────────────────────────

TEST(Color, Opponent) {
    EXPECT_TRUE(opponent(Color::White) == Color::Black);
    EXPECT_TRUE(opponent(Color::Black) == Color::White);
    EXPECT_TRUE(opponent(opponent(Color::White)) == Color::White);
    EXPECT_TRUE(opponent(opponent(Color::Black)) == Color::Black);
}

// ─── Move tests ───────────────────────────────────────────────────────────────

TEST(Move, NullMoveIsInvalid) {
    EXPECT_FALSE(NULL_MOVE.is_valid());
    EXPECT_EQ(move_to_uci(NULL_MOVE), std::string("(null)"));
}

TEST(Move, NormalMoveRoundTrip) {
    Move m = move_from_uci("e2e4");
    EXPECT_TRUE(m.is_valid());
    EXPECT_EQ(m.from, E2);
    EXPECT_EQ(m.to,   E4);
    EXPECT_FALSE(m.is_promotion());
    EXPECT_EQ(move_to_uci(m), std::string("e2e4"));
}

TEST(Move, PromotionRoundTrip) {
    const char* cases[] = {"a7a8q", "a7a8r", "a7a8b", "a7a8n"};
    for (const char* uci : cases) {
        Move m = move_from_uci(uci);
        EXPECT_TRUE(m.is_valid());
        EXPECT_TRUE(m.is_promotion());
        EXPECT_EQ(move_to_uci(m), std::string(uci));
    }
}

TEST(Move, PromotionPieceTypes) {
    EXPECT_TRUE(move_from_uci("a7a8q").promotion_piece == PieceType::Queen);
    EXPECT_TRUE(move_from_uci("a7a8r").promotion_piece == PieceType::Rook);
    EXPECT_TRUE(move_from_uci("a7a8b").promotion_piece == PieceType::Bishop);
    EXPECT_TRUE(move_from_uci("a7a8n").promotion_piece == PieceType::Knight);
}

TEST(Move, CornerSquareMoves) {
    struct { const char* uci; Square from; Square to; } cases[] = {
        {"a1h8", A1, H8},
        {"h1a8", H1, A8},
        {"a8h1", A8, H1},
        {"h8a1", H8, A1},
    };
    for (auto& c : cases) {
        Move m = move_from_uci(c.uci);
        EXPECT_TRUE(m.is_valid());
        EXPECT_EQ(m.from, c.from);
        EXPECT_EQ(m.to,   c.to);
        EXPECT_EQ(move_to_uci(m), std::string(c.uci));
    }
}

TEST(Move, InvalidUCIStrings) {
    EXPECT_FALSE(move_from_uci("").is_valid());
    EXPECT_FALSE(move_from_uci("e2").is_valid());
    EXPECT_FALSE(move_from_uci("e2e").is_valid());
    EXPECT_FALSE(move_from_uci("e2e9").is_valid());
    EXPECT_FALSE(move_from_uci("e2e4x").is_valid());
    EXPECT_FALSE(move_from_uci("i2e4").is_valid());
    EXPECT_FALSE(move_from_uci("e2e4q5").is_valid());
}

TEST(Move, FlagsAreIndependent) {
    Move m{};
    m.from  = A1;
    m.to    = make_square(0, 1);  // a2
    m.flags = MF_PROMOTION | MF_CAPTURE;
    EXPECT_TRUE(m.is_promotion());
    EXPECT_TRUE(m.is_capture());
    EXPECT_FALSE(m.is_en_passant());
    EXPECT_FALSE(m.is_castling());
    EXPECT_FALSE(m.is_double_push());
}

// ─── Score tests ──────────────────────────────────────────────────────────────

TEST(Score, Constants) {
    EXPECT_TRUE(SCORE_INFINITY > 0);
    EXPECT_TRUE(SCORE_MATE > 0);
    EXPECT_TRUE(SCORE_MATE < SCORE_INFINITY);
    EXPECT_EQ(SCORE_DRAW, 0);
    EXPECT_EQ(SCORE_MIN, -SCORE_INFINITY);
    EXPECT_EQ(SCORE_MAX,  SCORE_INFINITY);
}

TEST(Score, IsMateScore) {
    EXPECT_TRUE(is_mate_score( SCORE_MATE));
    EXPECT_TRUE(is_mate_score(-SCORE_MATE));
    EXPECT_TRUE(is_mate_score( SCORE_MATE - 5));
    EXPECT_TRUE(is_mate_score(-SCORE_MATE + 5));
    EXPECT_FALSE(is_mate_score(100));
    EXPECT_FALSE(is_mate_score(0));
    EXPECT_FALSE(is_mate_score(-500));
}
