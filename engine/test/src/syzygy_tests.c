#include <stdlib.h>
#include <stdio.h>

#include "unity.h"
#include "syzygy_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/move_encoding.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/search.h"
#include "../../core/src/search_state.h"
#include "../../core/src/syzygy.h"
#include "../../core/src/transposition.h"
#include "../../core/src/uci.h"

/**
 * The tablebase tests need the tables: TIKI_SYZYGY_PATH set to directories holding at least the
 * 3-5 piece WDL and DTZ tables (';' between directories on Windows, ':' elsewhere). Without it
 * they're ignored. The expected results are python-chess's (chess.syzygy, its own prober).
 */
static bool load_tables(void) {
    const char* path = getenv("TIKI_SYZYGY_PATH");
    if (path == NULL || *path == '\0') return false;
    return syzygy_init(path) >= 5;
}

static void setup(const char* fen, board_t* board, search_state_t* search_state) {
    char command[256];
    snprintf(command, sizeof(command), "position fen %s", fen);
    tt_clear();
    init_search_state(search_state);
    parse_position(command, board, search_state);
}

/* The legal move from 'from' to 'to' (the first promotion, if it's one), 0 if there's none. */
static move_t find_move(board_t* board, square from, square to) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        const move_t mv = buffer.moves[i];
        if (get_source_square(mv) != from || get_target_square(mv) != to) continue;
        const bool legal = make_move(board, mv);
        pop_move(board);
        if (legal) return mv;
    }
    return 0;
}

static int probe(const char* fen) {
    board_t board;
    search_state_t search_state;
    setup(fen, &board, &search_state);
    int wdl = 99;
    TEST_ASSERT_TRUE_MESSAGE(syzygy_probe_wdl(&board, &wdl), fen);
    return wdl;
}

void test_syzygy_bitboard_mirror() {
    /* tiki counts from a8, Fathom from a1. */
    TEST_ASSERT_EQUAL_UINT64(1ULL << 56, syzygy_bitboard(1ULL << a8));
    TEST_ASSERT_EQUAL_UINT64(1ULL << 0, syzygy_bitboard(1ULL << a1));
    TEST_ASSERT_EQUAL_UINT64(1ULL << 7, syzygy_bitboard(1ULL << h1));
    TEST_ASSERT_EQUAL_UINT64(1ULL << (4 * 8 + 3), syzygy_bitboard(1ULL << d5));
}

void test_syzygy_without_tables() {
    TEST_ASSERT_EQUAL_INT(0, syzygy_init(""));
    TEST_ASSERT_EQUAL_INT(0, syzygy_largest());
    board_t board;
    search_state_t search_state;
    setup("8/8/8/4k3/8/8/8/R3K3 w - - 0 1", &board, &search_state);
    int wdl;
    TEST_ASSERT_FALSE(syzygy_probe_wdl(&board, &wdl));
    TEST_ASSERT_FALSE(syzygy_rank_root(&board));
    TEST_ASSERT_TRUE(syzygy_root_allows(find_move(&board, a1, a2)));
    TEST_ASSERT_EQUAL_INT(0, syzygy_search_pieces);
}

void test_syzygy_probe_wdl() {
    if (!load_tables()) TEST_IGNORE_MESSAGE("TIKI_SYZYGY_PATH not set");
    TEST_ASSERT_EQUAL_INT(1, probe("8/8/8/4k3/8/8/8/R3K3 w - - 0 1"));     /* KRvK */
    TEST_ASSERT_EQUAL_INT(-1, probe("8/8/8/4k3/8/8/8/R3K3 b - - 0 1"));    /* the same, black to move */
    TEST_ASSERT_EQUAL_INT(0, probe("8/8/4k3/1r6/8/2K5/8/7R w - - 0 1"));   /* KRvKR */
    TEST_ASSERT_EQUAL_INT(0, probe("8/2k5/8/8/8/8/1q6/K7 w - - 0 1"));     /* KvKQ, white takes the queen */
    TEST_ASSERT_EQUAL_INT(1, probe("8/8/8/8/3k4/8/8/K2Q1r2 w - - 0 1"));   /* KQvKR, Qxf1 */

    /* Not probed: after a quiet move (the fifty-move counter above 0), or with castling rights. */
    board_t board;
    search_state_t search_state;
    int wdl;
    setup("8/8/8/4k3/8/8/8/R3K3 w - - 5 10", &board, &search_state);
    TEST_ASSERT_FALSE(syzygy_probe_wdl(&board, &wdl));
    setup("8/8/8/4k3/8/8/8/R3K3 w Q - 0 1", &board, &search_state);
    TEST_ASSERT_FALSE(syzygy_probe_wdl(&board, &wdl));
    syzygy_init("");
}

void test_syzygy_en_passant() {
    if (!load_tables()) TEST_IGNORE_MESSAGE("TIKI_SYZYGY_PATH not set");
    /* White wins only by taking en passant (bxa6), so the square must reach the tables. */
    TEST_ASSERT_EQUAL_INT(1, probe("8/8/8/pP6/7K/8/2k5/8 w - a6 0 1"));
    TEST_ASSERT_EQUAL_INT(0, probe("8/8/8/pP6/7K/8/2k5/8 w - - 0 1"));
    syzygy_init("");
}

void test_syzygy_root_keeps_the_result() {
    if (!load_tables()) TEST_IGNORE_MESSAGE("TIKI_SYZYGY_PATH not set");
    board_t board;
    search_state_t search_state;
    /* KQvKR: only Qxf1 wins; Qe1/Qc1 lose the queen, Qb1 draws, the king moves let the rook take. */
    setup("8/8/8/8/3k4/8/8/K2Q1r2 w - - 0 1", &board, &search_state);
    TEST_ASSERT_TRUE(syzygy_rank_root(&board));
    int wdl = 99;
    TEST_ASSERT_TRUE(syzygy_root_wdl(&wdl));
    TEST_ASSERT_EQUAL_INT(1, wdl);
    TEST_ASSERT_TRUE(syzygy_root_allows(find_move(&board, d1, f1)));
    TEST_ASSERT_FALSE(syzygy_root_allows(find_move(&board, d1, b1)));
    TEST_ASSERT_FALSE(syzygy_root_allows(find_move(&board, d1, e1)));
    TEST_ASSERT_FALSE(syzygy_root_allows(find_move(&board, a1, b2)));
    /* With the DTZ tables every move left keeps the win, so the search doesn't probe. */
    TEST_ASSERT_EQUAL_INT(0, syzygy_search_pieces);

    syzygy_end_search();
    TEST_ASSERT_TRUE(syzygy_root_allows(find_move(&board, d1, b1)));
    TEST_ASSERT_EQUAL_INT(syzygy_largest(), syzygy_search_pieces);
    syzygy_init("");
}

void test_syzygy_root_restriction_only_at_the_root() {
    if (!load_tables()) TEST_IGNORE_MESSAGE("TIKI_SYZYGY_PATH not set");
    board_t board;
    search_state_t search_state;
    setup("8/8/8/8/3k4/8/8/K2Q1r2 w - - 0 1", &board, &search_state);
    TEST_ASSERT_TRUE(syzygy_rank_root(&board));

    /* Elsewhere in the tree, in check after a hundred quiet plies with moves to play: a fifty-move
     * draw. The checkmate test behind it must look at this position's moves, not the root's allowed
     * ones (it once did, and searched on past the fifty-move rule). */
    board_t deep;
    search_state_t deep_state;
    setup("k7/8/8/8/8/8/8/R3K3 b - - 100 90", &deep, &deep_state);
    TEST_ASSERT_TRUE(is_draw(&deep, &deep_state));
    TEST_ASSERT_NOT_EQUAL(0, first_legal_move(&deep));

    syzygy_end_search();
    syzygy_init("");
}

void test_syzygy_probe_limit() {
    if (!load_tables()) TEST_IGNORE_MESSAGE("TIKI_SYZYGY_PATH not set");
    board_t board;
    search_state_t search_state;
    int wdl;
    /* KRPvKR, 5 pieces (a draw). */
    setup("8/8/4k3/8/4P3/8/1r6/4K2R w - - 0 1", &board, &search_state);

    syzygy_set_probe_limit(4);
    TEST_ASSERT_FALSE(syzygy_probe_wdl(&board, &wdl));
    TEST_ASSERT_FALSE(syzygy_rank_root(&board));
    TEST_ASSERT_EQUAL_INT(4, syzygy_search_pieces);

    syzygy_set_probe_limit(5);
    TEST_ASSERT_TRUE(syzygy_probe_wdl(&board, &wdl));
    TEST_ASSERT_EQUAL_INT(0, wdl);
    TEST_ASSERT_TRUE(syzygy_rank_root(&board));
    syzygy_end_search();

    syzygy_set_probe_limit(0);
    TEST_ASSERT_EQUAL_INT(0, syzygy_search_pieces);
    TEST_ASSERT_FALSE(syzygy_probe_wdl(&board, &wdl));

    syzygy_set_probe_limit(SYZYGY_PROBE_LIMIT_DEFAULT);
    TEST_ASSERT_EQUAL_INT(syzygy_largest(), syzygy_search_pieces);
    syzygy_init("");
}

void test_syzygy_search_plays_the_win() {
    if (!load_tables()) TEST_IGNORE_MESSAGE("TIKI_SYZYGY_PATH not set");
    board_t board;
    search_state_t search_state;
    limits_t limits;
    setup("8/8/8/8/3k4/8/8/K2Q1r2 w - - 0 1", &board, &search_state);
    reset_time_control(&limits);
    limits.depth = 4;
    limits.quiet = true;
    const move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(d1, get_source_square(mv));
    TEST_ASSERT_EQUAL_INT(f1, get_target_square(mv));
    /* The restriction ends with the search. */
    TEST_ASSERT_TRUE(syzygy_root_allows(find_move(&board, d1, b1)));
    syzygy_init("");
}

void test_syzygy_search_scores_tablebase_wins() {
    if (!load_tables()) TEST_IGNORE_MESSAGE("TIKI_SYZYGY_PATH not set");
    board_t board;
    search_state_t search_state;
    limits_t limits;
    /* Searched directly (no root ranking): Qxf1 reaches KQvK, a win the tables score at ply 1. */
    setup("8/8/8/8/3k4/8/8/K2Q1r2 w - - 0 1", &board, &search_state);
    reset_time_control(&limits);
    limits.depth = 2;
    TEST_ASSERT_EQUAL_INT(TB_WIN_SCORE - 1, negamax(-INF, INF, 2, &board, &search_state, &limits));
    TEST_ASSERT_EQUAL_INT(d1, get_source_square(search_state.pv_table[0][0]));
    TEST_ASSERT_EQUAL_INT(f1, get_target_square(search_state.pv_table[0][0]));

    /* Black to move takes the queen (Rxd1, KvKR): a loss for white, scored at ply 1. */
    setup("8/8/8/8/3k4/8/8/K2Q1r2 b - - 0 1", &board, &search_state);
    reset_time_control(&limits);
    TEST_ASSERT_EQUAL_INT(TB_WIN_SCORE - 1, negamax(-INF, INF, 2, &board, &search_state, &limits));
    syzygy_init("");
}
