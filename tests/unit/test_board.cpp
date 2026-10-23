/**
 * test_board.cpp — Unit tests for include/chess/board.hpp / src/board.cpp
 *
 * Covers:
 *  - Default construction (standard starting position)
 *  - FEN round-trip for starting, post-e4, Kiwipete, endgame positions
 *  - Castling rights: parse/serialize/accessors, all combinations
 *  - En-passant target: parse/serialize, correct rank enforcement
 *  - Halfmove clock and fullmove number round-trip
 *  - Invalid FEN inputs throw std::invalid_argument
 *  - piece_at and king_square
 *  - make_move / unmake_move restores exact position (normal, capture,
 *    double push, castling-rights side effects)
 *  - Halfmove clock reset on pawn move
 *  - Fullmove number increments after Black's move
 *  - Board equality / inequality
 */

#include "test_framework.hpp"
#include "chess/board.hpp"
#include "chess/types.hpp"

#include <stdexcept>

using namespace chess;

// ─── FEN fixtures ─────────────────────────────────────────────────────────────

static const char* START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// After 1.e4 — en-passant target e3, Black to move
static const char* AFTER_E4_FEN =
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";

// Kiwipete — rich position widely used for perft
static const char* KIWIPETE_FEN =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

// Endgame with en-passant possible on d6
static const char* EP_D6_FEN =
    "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2";

// Bare-kings endgame with high clocks
static const char* BARE_KINGS_FEN =
    "8/8/8/8/8/8/8/K6k w - - 42 100";

// Endgame perft position
static const char* ENDGAME_FEN =
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";

// ─── Default construction ─────────────────────────────────────────────────────

TEST(Board, DefaultIsStartingFEN) {
    Board b;
    EXPECT_EQ(b.fen(), std::string(START_FEN));
}

TEST(Board, DefaultSideToMoveIsWhite) {
    Board b;
    EXPECT_TRUE(b.side_to_move() == Color::White);
}

TEST(Board, DefaultCastlingRightsAll) {
    Board b;
    EXPECT_TRUE(b.can_castle_wk());
    EXPECT_TRUE(b.can_castle_wq());
    EXPECT_TRUE(b.can_castle_bk());
    EXPECT_TRUE(b.can_castle_bq());
    EXPECT_EQ(static_cast<int>(b.castling_rights()), static_cast<int>(CR_ALL));
}

TEST(Board, DefaultNoEnPassant) {
    Board b;
    EXPECT_EQ(b.ep_square(), NO_SQ);
}

TEST(Board, DefaultClocks) {
    Board b;
    EXPECT_EQ(b.halfmove_clock(),  0);
    EXPECT_EQ(b.fullmove_number(), 1);
}

// ─── Piece placement at start ─────────────────────────────────────────────────

TEST(Board, StartingPiecesBackRank) {
    Board b;
    EXPECT_TRUE(b.piece_at(A1) == make_piece(Color::White, PieceType::Rook));
    EXPECT_TRUE(b.piece_at(B1) == make_piece(Color::White, PieceType::Knight));
    EXPECT_TRUE(b.piece_at(C1) == make_piece(Color::White, PieceType::Bishop));
    EXPECT_TRUE(b.piece_at(D1) == make_piece(Color::White, PieceType::Queen));
    EXPECT_TRUE(b.piece_at(E1) == make_piece(Color::White, PieceType::King));
    EXPECT_TRUE(b.piece_at(F1) == make_piece(Color::White, PieceType::Bishop));
    EXPECT_TRUE(b.piece_at(G1) == make_piece(Color::White, PieceType::Knight));
    EXPECT_TRUE(b.piece_at(H1) == make_piece(Color::White, PieceType::Rook));

    EXPECT_TRUE(b.piece_at(A8) == make_piece(Color::Black, PieceType::Rook));
    EXPECT_TRUE(b.piece_at(E8) == make_piece(Color::Black, PieceType::King));
    EXPECT_TRUE(b.piece_at(H8) == make_piece(Color::Black, PieceType::Rook));
}

TEST(Board, StartingWhitePawns) {
    Board b;
    for (int f = 0; f < 8; ++f)
        EXPECT_TRUE(b.piece_at(make_square(f, 1)) ==
                    make_piece(Color::White, PieceType::Pawn));
}

TEST(Board, StartingBlackPawns) {
    Board b;
    for (int f = 0; f < 8; ++f)
        EXPECT_TRUE(b.piece_at(make_square(f, 6)) ==
                    make_piece(Color::Black, PieceType::Pawn));
}

TEST(Board, StartingEmptyRanks) {
    Board b;
    for (int r = 2; r <= 5; ++r)
        for (int f = 0; f < 8; ++f)
            EXPECT_TRUE(b.piece_at(make_square(f, r)).is_empty());
}

// ─── King square ─────────────────────────────────────────────────────────────

TEST(Board, KingSquareStart) {
    Board b;
    EXPECT_EQ(b.king_square(Color::White), E1);
    EXPECT_EQ(b.king_square(Color::Black), E8);
}

TEST(Board, KingSquareCacheMakeUnmakeRoundTrip) {
    // Kiwipete position has castling rights and many king / piece options
    Board b("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    EXPECT_EQ(b.king_square(Color::White), E1);
    EXPECT_EQ(b.king_square(Color::Black), E8);

    // Castling white kingside: e1g1
    Move m_castle;
    m_castle.from = E1;
    m_castle.to = G1;
    m_castle.flags = MF_CASTLING;
    UndoInfo undo1 = b.make_move(m_castle);
    EXPECT_EQ(b.king_square(Color::White), G1);
    EXPECT_EQ(b.king_square(Color::Black), E8);

    // Black castles queenside: e8c8
    Move m_bcastle;
    m_bcastle.from = E8;
    m_bcastle.to = C8;
    m_bcastle.flags = MF_CASTLING;
    UndoInfo undo2 = b.make_move(m_bcastle);
    EXPECT_EQ(b.king_square(Color::White), G1);
    EXPECT_EQ(b.king_square(Color::Black), C8);

    // Unmake black castle
    b.unmake_move(m_bcastle, undo2);
    EXPECT_EQ(b.king_square(Color::White), G1);
    EXPECT_EQ(b.king_square(Color::Black), E8);

    // Unmake white castle
    b.unmake_move(m_castle, undo1);
    EXPECT_EQ(b.king_square(Color::White), E1);
    EXPECT_EQ(b.king_square(Color::Black), E8);

    // Normal king move: e1f1
    Move m_king;
    m_king.from = E1;
    m_king.to = F1;
    UndoInfo undo3 = b.make_move(m_king);
    EXPECT_EQ(b.king_square(Color::White), F1);
    EXPECT_EQ(b.king_square(Color::Black), E8);

    b.unmake_move(m_king, undo3);
    EXPECT_EQ(b.king_square(Color::White), E1);
    EXPECT_EQ(b.king_square(Color::Black), E8);
}

// ─── FEN round-trips ─────────────────────────────────────────────────────────

TEST(Board, StartFENRoundTrip) {
    EXPECT_EQ(Board(START_FEN).fen(),    std::string(START_FEN));
}

TEST(Board, AfterE4FENRoundTrip) {
    EXPECT_EQ(Board(AFTER_E4_FEN).fen(), std::string(AFTER_E4_FEN));
}

TEST(Board, KiwipeteFENRoundTrip) {
    EXPECT_EQ(Board(KIWIPETE_FEN).fen(), std::string(KIWIPETE_FEN));
}

TEST(Board, EP_D6_FENRoundTrip) {
    EXPECT_EQ(Board(EP_D6_FEN).fen(),    std::string(EP_D6_FEN));
}

TEST(Board, BareKingsFENRoundTrip) {
    EXPECT_EQ(Board(BARE_KINGS_FEN).fen(), std::string(BARE_KINGS_FEN));
}

TEST(Board, EndgameFENRoundTrip) {
    EXPECT_EQ(Board(ENDGAME_FEN).fen(),  std::string(ENDGAME_FEN));
}

// ─── Castling rights from FEN ─────────────────────────────────────────────────

TEST(Board, CastlingAllFour) {
    Board b("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    EXPECT_TRUE(b.can_castle_wk());
    EXPECT_TRUE(b.can_castle_wq());
    EXPECT_TRUE(b.can_castle_bk());
    EXPECT_TRUE(b.can_castle_bq());
}

TEST(Board, CastlingNone) {
    Board b("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1");
    EXPECT_FALSE(b.can_castle_wk());
    EXPECT_FALSE(b.can_castle_wq());
    EXPECT_FALSE(b.can_castle_bk());
    EXPECT_FALSE(b.can_castle_bq());
}

TEST(Board, CastlingKqOnly) {
    Board b("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w Kq - 0 1");
    EXPECT_TRUE (b.can_castle_wk());
    EXPECT_FALSE(b.can_castle_wq());
    EXPECT_FALSE(b.can_castle_bk());
    EXPECT_TRUE (b.can_castle_bq());
}

TEST(Board, CastlingSerializedCorrectly) {
    // None
    EXPECT_EQ(Board("8/8/8/8/8/8/8/K6k w - - 0 1").fen(),
              std::string("8/8/8/8/8/8/8/K6k w - - 0 1"));
    // All four
    EXPECT_EQ(Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1").fen(),
              std::string("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
}

// ─── En-passant from FEN ──────────────────────────────────────────────────────

TEST(Board, EnPassantAfterE4) {
    Board b(AFTER_E4_FEN);
    EXPECT_EQ(b.ep_square(), square_from_name("e3"));
}

TEST(Board, EnPassantNoneAtStart) {
    EXPECT_EQ(Board(START_FEN).ep_square(), NO_SQ);
}

TEST(Board, EnPassantD6RoundTrip) {
    Board b(EP_D6_FEN);
    EXPECT_EQ(b.ep_square(), square_from_name("d6"));
    EXPECT_EQ(b.fen(), std::string(EP_D6_FEN));
}

// ─── Clocks from FEN ──────────────────────────────────────────────────────────

TEST(Board, ClocksFromFEN) {
    Board b("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 5 12");
    EXPECT_EQ(b.halfmove_clock(),  5);
    EXPECT_EQ(b.fullmove_number(), 12);
}

TEST(Board, ClocksRoundTrip) {
    EXPECT_EQ(Board(BARE_KINGS_FEN).fen(), std::string(BARE_KINGS_FEN));
}

// ─── Invalid FEN inputs ───────────────────────────────────────────────────────

TEST(Board, EmptyFENThrows) {
    EXPECT_THROW(Board(""), std::invalid_argument);
}

TEST(Board, MissingFieldsThrow) {
    EXPECT_THROW(Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"),
                 std::invalid_argument);
    EXPECT_THROW(Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w"),
                 std::invalid_argument);
    EXPECT_THROW(Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq"),
                 std::invalid_argument);
}

TEST(Board, InvalidSideToMoveThrows) {
    EXPECT_THROW(
        Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"),
        std::invalid_argument);
}

TEST(Board, InvalidCastlingCharThrows) {
    EXPECT_THROW(
        Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w XQkq - 0 1"),
        std::invalid_argument);
}

TEST(Board, InvalidEnPassantSquareThrows) {
    EXPECT_THROW(
        Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq z9 0 1"),
        std::invalid_argument);
}

TEST(Board, EnPassantWrongRankThrows) {
    // e4 is rank index 3, not 2 or 5 — invalid en-passant rank
    EXPECT_THROW(
        Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e4 0 1"),
        std::invalid_argument);
}

TEST(Board, TooManyRanksThrows) {
    EXPECT_THROW(
        Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1"),
        std::invalid_argument);
}

TEST(Board, RankOverflowThrows) {
    EXPECT_THROW(
        Board("rnbqkbnrP/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
        std::invalid_argument);
}

TEST(Board, UnknownPieceCharThrows) {
    EXPECT_THROW(
        Board("xnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
        std::invalid_argument);
}

// ─── make_move / unmake_move ──────────────────────────────────────────────────

TEST(Board, MakeUnmakeNormalMove) {
    Board b(START_FEN);
    Board original(START_FEN);

    Move e2e4 = move_from_uci("e2e4");
    e2e4.flags |= MF_DOUBLE_PUSH;

    UndoInfo undo = b.make_move(e2e4);
    EXPECT_FALSE(b == original);      // board changed

    b.unmake_move(e2e4, undo);
    EXPECT_TRUE(b == original);       // fully restored
}

TEST(Board, MakeMoveSideFlips) {
    Board b(START_FEN);
    Move e2e4 = move_from_uci("e2e4");
    e2e4.flags |= MF_DOUBLE_PUSH;
    UndoInfo undo = b.make_move(e2e4);

    EXPECT_TRUE(b.side_to_move() == Color::Black);

    b.unmake_move(e2e4, undo);
    EXPECT_TRUE(b.side_to_move() == Color::White);
}

TEST(Board, MakeMoveUpdatesPieceLocation) {
    Board b(START_FEN);
    Move e2e4 = move_from_uci("e2e4");
    e2e4.flags |= MF_DOUBLE_PUSH;
    b.make_move(e2e4);

    EXPECT_TRUE(b.piece_at(make_square(4, 1)).is_empty());   // e2 empty
    EXPECT_TRUE(b.piece_at(make_square(4, 3)) ==
                make_piece(Color::White, PieceType::Pawn));   // e4 has pawn
}

TEST(Board, DoublePushSetsEnPassant) {
    Board b(START_FEN);
    Move e2e4 = move_from_uci("e2e4");
    e2e4.flags |= MF_DOUBLE_PUSH;
    b.make_move(e2e4);

    EXPECT_EQ(b.ep_square(), square_from_name("e3"));
}

TEST(Board, MakeUnmakeCapture) {
    // 1.e4 e5 2.Nf3 — White knight can capture on e5
    const char* fen =
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    Board b(fen);
    Board original(fen);

    Move nxe5 = move_from_uci("f3e5");
    nxe5.flags |= MF_CAPTURE;

    UndoInfo undo = b.make_move(nxe5);
    EXPECT_FALSE(b == original);

    b.unmake_move(nxe5, undo);
    EXPECT_TRUE(b == original);
}

TEST(Board, MakeUnmakeSequence) {
    Board b(START_FEN);

    struct { const char* uci; uint32_t flags; } moves[] = {
        {"e2e4", MF_DOUBLE_PUSH},
        {"e7e5", MF_DOUBLE_PUSH},
        {"g1f3", MF_NONE},
        {"b8c6", MF_NONE},
    };
    Board snapshots[4];
    UndoInfo undos[4];

    for (int i = 0; i < 4; ++i) {
        snapshots[i] = b;
        Move m = move_from_uci(moves[i].uci);
        m.flags |= moves[i].flags;
        undos[i] = b.make_move(m);
    }
    for (int i = 3; i >= 0; --i) {
        Move m = move_from_uci(moves[i].uci);
        m.flags |= moves[i].flags;
        b.unmake_move(m, undos[i]);
        EXPECT_TRUE(b == snapshots[i]);
    }
}

TEST(Board, HalfmoveClockResetsOnPawnMove) {
    Board b("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 10 6");
    EXPECT_EQ(b.halfmove_clock(), 10);

    Move e2e4 = move_from_uci("e2e4");
    e2e4.flags |= MF_DOUBLE_PUSH;
    b.make_move(e2e4);

    EXPECT_EQ(b.halfmove_clock(), 0);
}

TEST(Board, FullmoveIncrementAfterBlack) {
    Board b(START_FEN);
    EXPECT_EQ(b.fullmove_number(), 1);

    Move e2e4 = move_from_uci("e2e4");
    e2e4.flags |= MF_DOUBLE_PUSH;
    b.make_move(e2e4);
    EXPECT_EQ(b.fullmove_number(), 1);   // White just moved — still 1

    Move e7e5 = move_from_uci("e7e5");
    e7e5.flags |= MF_DOUBLE_PUSH;
    b.make_move(e7e5);
    EXPECT_EQ(b.fullmove_number(), 2);   // After Black's move → 2
}

TEST(Board, CastlingRightsRevokedOnKingMove) {
    const char* fen =
        "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1";
    Board b(fen);

    Move ke1e2 = move_from_uci("e1e2");
    b.make_move(ke1e2);

    EXPECT_FALSE(b.can_castle_wk());
    EXPECT_FALSE(b.can_castle_wq());
    EXPECT_TRUE (b.can_castle_bk());   // Black unaffected
    EXPECT_TRUE (b.can_castle_bq());
}

TEST(Board, CastlingRightsRevokedOnRookMove) {
    const char* fen =
        "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1";
    Board b(fen);

    Move rh1h2 = move_from_uci("h1h2");
    b.make_move(rh1h2);

    EXPECT_FALSE(b.can_castle_wk());   // h1 rook moved
    EXPECT_TRUE (b.can_castle_wq());   // a1 rook untouched
}

// ─── Equality ─────────────────────────────────────────────────────────────────

TEST(Board, EqualityIdenticalFEN) {
    Board b1(START_FEN);
    Board b2(START_FEN);
    EXPECT_TRUE(b1 == b2);
}

TEST(Board, InequalityAfterMove) {
    Board b1(START_FEN);
    Board b2(START_FEN);

    Move e2e4 = move_from_uci("e2e4");
    e2e4.flags |= MF_DOUBLE_PUSH;
    b1.make_move(e2e4);

    EXPECT_FALSE(b1 == b2);
}
