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

    tt_save(TEST_HASH, tt_exact, 4, 0, 100, 0, TT_EVAL_NONE);
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
    tt_save(TEST_HASH, tt_exact, 4, 3, MATE_VALUE - 7, 0, TT_EVAL_NONE);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 7, tt_probe(TEST_HASH, 4, 3, -INF, INF, &tt_move));

    /* Two plies deeper, the same mate is two plies further from the root. */
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 9, tt_probe(TEST_HASH, 4, 5, -INF, INF, &tt_move));

    tt_clear();
    tt_save(TEST_HASH, tt_exact, 4, 3, -MATE_VALUE + 7, 0, TT_EVAL_NONE);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE + 7, tt_probe(TEST_HASH, 4, 3, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE + 9, tt_probe(TEST_HASH, 4, 5, -INF, INF, &tt_move));
}

void test_tt_stores_best_move() {
    move_t tt_move;
    const move_t mv = encode_move(e2, e4, P, 0, 0, 1, 0, 0, 0);
    tt_clear();

    tt_save(TEST_HASH, tt_beta, 3, 0, 50, mv, TT_EVAL_NONE);

    /* Too shallow for a cutoff, but the move is still returned for ordering. */
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(TEST_HASH, 6, 0, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_UINT32(mv, tt_move);

    /* A fail-low (no move) for the same position keeps the stored move. */
    tt_new_search();
    tt_save(TEST_HASH, tt_alpha, 4, 0, 10, 0, TT_EVAL_NONE);
    TEST_ASSERT_EQUAL_INT(10, tt_probe(TEST_HASH, 4, 0, 20, 30, &tt_move)); /* Fail-soft, the stored bound. */
    TEST_ASSERT_EQUAL_UINT32(mv, tt_move);

    /* Different position, nothing returned. */
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(TEST_HASH ^ (1ULL << 63), 0, 0, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_UINT32(0, tt_move);
}

void test_tt_replacement() {
    move_t tt_move;
    tt_clear();

    /* Same search, a shallower result doesn't replace a deeper one. */
    tt_save(TEST_HASH, tt_exact, 8, 0, 100, 0, TT_EVAL_NONE);
    tt_save(TEST_HASH, tt_exact, 2, 0, 200, 0, TT_EVAL_NONE);
    TEST_ASSERT_EQUAL_INT(100, tt_probe(TEST_HASH, 2, 0, -INF, INF, &tt_move));

    /* Next search, the old entry is replaced. */
    tt_new_search();
    tt_save(TEST_HASH, tt_exact, 2, 0, 200, 0, TT_EVAL_NONE);
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

void test_tt_stores_static_eval() {
    uint64_t data;
    int eval;
    tt_clear();

    /* Any eval round trips, with the entry's own fields intact, and the key check still holds. */
    const int evals[] = { 0, 1, -1, 1234, -1234, TT_EVAL_MAX, -TT_EVAL_MAX, TT_EVAL_NONE };
    for (size_t i = 0; i < sizeof(evals) / sizeof(evals[0]); i++) {
        tt_clear();
        tt_save(TEST_HASH, tt_beta, 7, 0, -55, 0, evals[i]);
        TEST_ASSERT_TRUE(tt_load_eval(TEST_HASH, &data, &eval));
        TEST_ASSERT_EQUAL_INT(evals[i], eval);
        TEST_ASSERT_EQUAL_INT(-55, tt_data_score(data));
        TEST_ASSERT_EQUAL_INT(7, tt_data_depth(data));
        TEST_ASSERT_EQUAL_INT(tt_beta, tt_data_type(data));
    }

    /* Out of range evals are clamped, never confused with TT_EVAL_NONE. */
    tt_clear();
    tt_save(TEST_HASH, tt_exact, 1, 0, 0, 0, 100000);
    TEST_ASSERT_TRUE(tt_load_eval(TEST_HASH, &data, &eval));
    TEST_ASSERT_EQUAL_INT(TT_EVAL_MAX, eval);
    tt_save(TEST_HASH, tt_exact, 1, 0, 0, 0, -100000);
    TEST_ASSERT_TRUE(tt_load_eval(TEST_HASH, &data, &eval));
    TEST_ASSERT_EQUAL_INT(-TT_EVAL_MAX, eval);

    /* The key is the hash's upper 48 bits: another position differing there is a miss. */
    TEST_ASSERT_FALSE(tt_load(TEST_HASH ^ (1ULL << 40), &data));
}

void test_tt_read_and_cutoff() {
    tt_info_t info;
    tt_clear();
    TEST_ASSERT_FALSE(tt_read(TEST_HASH, 0, &info));

    /* An eval only entry (tt_none) gives the eval, no move and never a cutoff. */
    tt_save(TEST_HASH, tt_none, 0, 0, 0, 0, 87);
    TEST_ASSERT_TRUE(tt_read(TEST_HASH, 0, &info));
    TEST_ASSERT_EQUAL_INT(tt_none, info.type);
    TEST_ASSERT_EQUAL_INT(87, info.eval);
    TEST_ASSERT_EQUAL_UINT32(0, info.move);
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_cutoff(&info, 0, -INF, INF));
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_cutoff(&info, 0, 0, 1));

    /* tt_cutoff agrees with tt_probe, fail-soft (the stored score, not the window's bound). */
    const move_t mv = encode_move(e2, e4, P, 0, 0, 1, 0, 0, 0);
    tt_save(TEST_HASH, tt_beta, 5, 0, 60, mv, 40);
    TEST_ASSERT_TRUE(tt_read(TEST_HASH, 0, &info));
    TEST_ASSERT_EQUAL_UINT32(mv, info.move);
    TEST_ASSERT_EQUAL_INT(40, info.eval);
    TEST_ASSERT_EQUAL_INT(60, tt_cutoff(&info, 5, 10, 50));
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_cutoff(&info, 5, 10, 70));
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_cutoff(&info, 6, 10, 50)); /* Too shallow. */

    /* Mate scores come back relative to the root, as in tt_probe (a new table: the deeper entry
     * above would be kept). */
    tt_clear();
    tt_save(TEST_HASH, tt_exact, 4, 3, MATE_VALUE - 7, 0, TT_EVAL_NONE);
    TEST_ASSERT_TRUE(tt_read(TEST_HASH, 5, &info));
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 9, info.score);
    TEST_ASSERT_EQUAL_INT(TT_EVAL_NONE, info.eval);
}

void test_adjusted_eval() {
    tt_info_t tt = { .move = 0, .score = 120, .depth = 5, .eval = 50, .type = tt_exact };
    TEST_ASSERT_EQUAL_INT(50, adjusted_eval(50, NULL));
    TEST_ASSERT_EQUAL_INT(120, adjusted_eval(50, &tt));     /* Exact: the score. */

    tt.type = tt_beta;                                     /* Lower bound: only above the eval. */
    TEST_ASSERT_EQUAL_INT(120, adjusted_eval(50, &tt));
    TEST_ASSERT_EQUAL_INT(200, adjusted_eval(200, &tt));

    tt.type = tt_alpha;                                    /* Upper bound: only below the eval. */
    TEST_ASSERT_EQUAL_INT(50, adjusted_eval(50, &tt));
    TEST_ASSERT_EQUAL_INT(120, adjusted_eval(200, &tt));

    tt.type = tt_none;                                     /* No bound. */
    TEST_ASSERT_EQUAL_INT(50, adjusted_eval(50, &tt));

    tt.type = tt_exact;                                    /* Mate scores aren't an estimate. */
    tt.score = MATE_VALUE - 3;
    TEST_ASSERT_EQUAL_INT(50, adjusted_eval(50, &tt));
    tt.score = -MATE_VALUE + 3;
    TEST_ASSERT_EQUAL_INT(50, adjusted_eval(50, &tt));
}

void test_qsearch_saves_to_tt() {
    /* A capture is available, so qsearch searches it and saves the position at depth 0. */
    board_t board;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);
    tt_clear();
    parse_position("position fen 4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", &board, &search_state);
    clear_pv_table(&search_state);
    const int score = quiescence(-INF, INF, &board, &search_state, &limits);

    tt_info_t info;
    TEST_ASSERT_TRUE(tt_read(board.hash, 0, &info));
    TEST_ASSERT_EQUAL_INT(0, info.depth);
    TEST_ASSERT_EQUAL_INT(tt_exact, info.type);
    TEST_ASSERT_EQUAL_INT(score, info.score);
    TEST_ASSERT_TRUE(info.eval != TT_EVAL_NONE);
    TEST_ASSERT_EQUAL_UINT32(encode_move(e4, d5, P, 0, 1, 0, 0, 0, 0), info.move);

    /* The same search again, now from the TT's eval and move: the same score. */
    clear_pv_table(&search_state);
    TEST_ASSERT_EQUAL_INT(score, quiescence(-INF, INF, &board, &search_state, &limits));
}

/* Positions in TEST_HASH's bucket (the same low bits), told apart by their key's top byte. */
static uint64_t bucket_hash(int k) {
    return TEST_HASH ^ ((uint64_t) (k + 1) << 56);
}

void test_tt_bucket_keeps_positions_apart() {
    move_t tt_move;
    tt_clear();

    /* A bucket holds TT_BUCKET positions: each keeps its own entry. */
    for (int k = 0; k < TT_BUCKET; k++) tt_save(bucket_hash(k), tt_exact, 4 + k, 0, 10 * k, 0, TT_EVAL_NONE);
    for (int k = 0; k < TT_BUCKET; k++) TEST_ASSERT_EQUAL_INT(10 * k, tt_probe(bucket_hash(k), 0, 0, -INF, INF, &tt_move));

    /* Saving a position again updates its entry, it doesn't take a second one. */
    tt_save(bucket_hash(2), tt_exact, 9, 0, 77, 0, TT_EVAL_NONE);
    TEST_ASSERT_EQUAL_INT(77, tt_probe(bucket_hash(2), 0, 0, -INF, INF, &tt_move));
    for (int k = 0; k < TT_BUCKET; k++) {
        if (k != 2) TEST_ASSERT_EQUAL_INT(10 * k, tt_probe(bucket_hash(k), 0, 0, -INF, INF, &tt_move));
    }
}

void test_tt_bucket_replaces_least_valuable() {
    move_t tt_move;
    tt_clear();

    /* Depths 4, 5, 9 and 7: a new position takes the shallowest's entry (depth 4), the deep ones stay. */
    const int depths[TT_BUCKET] = { 4, 5, 9, 7 };
    for (int k = 0; k < TT_BUCKET; k++) tt_save(bucket_hash(k), tt_exact, depths[k], 0, 100 + k, 0, TT_EVAL_NONE);
    tt_save(bucket_hash(TT_BUCKET), tt_exact, 0, 0, 1, 0, TT_EVAL_NONE); /* A qsearch entry. */
    TEST_ASSERT_EQUAL_INT(1, tt_probe(bucket_hash(TT_BUCKET), 0, 0, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(bucket_hash(0), 0, 0, -INF, INF, &tt_move));
    for (int k = 1; k < TT_BUCKET; k++) TEST_ASSERT_EQUAL_INT(100 + k, tt_probe(bucket_hash(k), 0, 0, -INF, INF, &tt_move));

    /* After a search, an earlier search's deep entry counts TT_AGE_WEIGHT plies less per search
     * since: two searches later, depth 9 is worth 9 - 16 < 0, below this search's depth 0 entries. */
    tt_new_search();
    tt_new_search();
    for (int k = 0; k < TT_BUCKET; k++) tt_save(bucket_hash(10 + k), tt_exact, 0, 0, 200 + k, 0, TT_EVAL_NONE);
    for (int k = 0; k < TT_BUCKET; k++) TEST_ASSERT_EQUAL_INT(200 + k, tt_probe(bucket_hash(10 + k), 0, 0, -INF, INF, &tt_move));
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(bucket_hash(2), 0, 0, -INF, INF, &tt_move));
}

void test_go_keeps_tt() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    move_t tt_move;
    init_search_state(&search_state);
    tt_clear();

    tt_save(TEST_HASH, tt_exact, 5, 0, 42, 0, TT_EVAL_NONE);
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
