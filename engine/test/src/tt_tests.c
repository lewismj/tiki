#include "unity.h"
#include "tt_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/search.h"
#include "../../core/src/transposition.h"
#include "../../core/src/uci.h"
#include "../../core/src/zobrist_key.h"

/* Doubled rooks on the e-file, Re8+ Rxe8 Rxe8#, mate in 2. */
#define MATE_IN_2_FEN "r5k1/5ppp/8/8/8/8/4RPPP/4R1K1 w - - 0 1"

#define TEST_HASH 0x123456789abcdef0ULL


static int search_score(board_t* board, search_state_t* search_state, int depth) {
    limits_t limits;
    reset_time_control(&limits);
    clear_pv_table(search_state);
    tt_new_search();
    return negamax(-INF, INF, depth, board, search_state, &limits);
}

void test_tt_entry_layout() {
    TEST_ASSERT_EQUAL_size_t(16, sizeof(transposition_node_t));
    TEST_ASSERT_TRUE(tt_size > 0);
    TEST_ASSERT_EQUAL_size_t(0, tt_size & (tt_size - 1)); /* Power of two, indexed with a mask. */
}

void test_tt_pack_round_trip() {
    const move_t mv = encode_move(e7, e8, p, q, 1, 0, 0, 0, 1); /* All flag bits set. */
    const int scores[] = {0, 1, -1, INF, -INF, MATE_VALUE + MAX_PLY, -MATE_VALUE - MAX_PLY};

    for (size_t i = 0; i < sizeof(scores) / sizeof(scores[0]); i++) {
        const uint64_t data = tt_pack(mv, scores[i], 255, tt_beta, 254);
        TEST_ASSERT_EQUAL_UINT32(mv, tt_data_move(data));
        TEST_ASSERT_EQUAL_INT(scores[i], tt_data_score(data));
        TEST_ASSERT_EQUAL_INT(255, tt_data_depth(data));
        TEST_ASSERT_EQUAL_INT(tt_beta, tt_data_type(data));
        TEST_ASSERT_EQUAL_UINT8(254, tt_data_generation(data));
    }
}

void test_tt_torn_entry_is_miss() {
    move_t tt_move;
    uint64_t data;
    tt_clear();

    tt_save(TEST_HASH, tt_exact, 4, 0, 100, 0);
    TEST_ASSERT_TRUE(tt_load(TEST_HASH, &data));

    /* Another thread's save overwrote 'data' only, the check no longer matches. */
    transposition_node_t* entry = tt_entry(TEST_HASH);
    atomic_store(&entry->data, tt_pack(0, -100, 9, tt_exact, tt_generation));
    TEST_ASSERT_FALSE(tt_load(TEST_HASH, &data));
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(TEST_HASH, 0, 0, -INF, INF, &tt_move));
}

void test_tt_mate_score_round_trip() {
    move_t tt_move;
    tt_clear();

    /* Probe at the same ply gives back the same score. */
    tt_save(TEST_HASH, tt_exact, 4, 3, MATE_VALUE - 7, 0);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 7, tt_probe(TEST_HASH, 4, 3, -INF, INF, &tt_move));

    /* Two plies deeper, the same mate is two plies further from the root. */
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 9, tt_probe(TEST_HASH, 4, 5, -INF, INF, &tt_move));

    tt_clear();
    tt_save(TEST_HASH, tt_exact, 4, 3, -MATE_VALUE + 7, 0);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE + 7, tt_probe(TEST_HASH, 4, 3, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE + 9, tt_probe(TEST_HASH, 4, 5, -INF, INF, &tt_move));
}

void test_tt_stores_best_move() {
    move_t tt_move;
    const move_t mv = encode_move(e2, e4, P, 0, 0, 1, 0, 0, 0);
    tt_clear();

    tt_save(TEST_HASH, tt_beta, 3, 0, 50, mv);

    /* Too shallow for a cutoff, but the move is still returned for ordering. */
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(TEST_HASH, 6, 0, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_UINT32(mv, tt_move);

    /* A fail-low (no move) for the same position keeps the stored move. */
    tt_new_search();
    tt_save(TEST_HASH, tt_alpha, 4, 0, 10, 0);
    TEST_ASSERT_EQUAL_INT(20, tt_probe(TEST_HASH, 4, 0, 20, 30, &tt_move)); /* Fail-hard, returns alpha. */
    TEST_ASSERT_EQUAL_UINT32(mv, tt_move);

    /* Different position, nothing returned. */
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(TEST_HASH ^ (1ULL << 63), 0, 0, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_UINT32(0, tt_move);
}

void test_tt_replacement() {
    move_t tt_move;
    tt_clear();

    /* Same search, a shallower result doesn't replace a deeper one. */
    tt_save(TEST_HASH, tt_exact, 8, 0, 100, 0);
    tt_save(TEST_HASH, tt_exact, 2, 0, 200, 0);
    TEST_ASSERT_EQUAL_INT(100, tt_probe(TEST_HASH, 2, 0, -INF, INF, &tt_move));

    /* Next search, the old entry is replaced. */
    tt_new_search();
    tt_save(TEST_HASH, tt_exact, 2, 0, 200, 0);
    TEST_ASSERT_EQUAL_INT(200, tt_probe(TEST_HASH, 2, 0, -INF, INF, &tt_move));
}

void test_castle_key_all_rights() {
    /* KQkq (15) used to share key 0 with no castling rights. */
    TEST_ASSERT_NOT_EQUAL(get_castle_key(0), get_castle_key(15));

    board_t with_rights, without_rights;
    parse_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", &with_rights);
    parse_fen("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1", &without_rights);
    TEST_ASSERT_NOT_EQUAL(with_rights.hash, without_rights.hash);
}

void test_go_keeps_tt() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    move_t tt_move;
    init_search_state(&search_state);
    tt_clear();

    tt_save(TEST_HASH, tt_exact, 5, 0, 42, 0);
    parse_position("position startpos", &board, &search_state);
    parse_go("go depth 2", &board, &search_state, &limits);

    TEST_ASSERT_EQUAL_INT(42, tt_probe(TEST_HASH, 5, 0, -INF, INF, &tt_move));
}

void test_search_stores_exact_root() {
    board_t board;
    search_state_t search_state;
    move_t tt_move;
    init_search_state(&search_state);
    tt_clear();

    parse_position("position startpos", &board, &search_state);
    int score = search_score(&board, &search_state, 3);

    uint64_t data;
    TEST_ASSERT_TRUE(tt_load(board.hash, &data));
    TEST_ASSERT_EQUAL_INT(tt_exact, tt_data_type(data));
    TEST_ASSERT_EQUAL_UINT32(search_state.pv_table[0][0], tt_data_move(data));
    TEST_ASSERT_EQUAL_INT(score, tt_probe(board.hash, 3, 0, -INF, INF, &tt_move));
}

void test_search_mate_score_with_warm_tt() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);
    tt_clear();

    parse_position("position fen " MATE_IN_2_FEN, &board, &search_state);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, search_score(&board, &search_state, 4));

    /* The TT is kept, mate distances must not drift on later searches. */
    for (int depth = 4; depth <= 6; depth++) {
        TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, search_score(&board, &search_state, depth));
    }
}
