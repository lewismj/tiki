#include <math.h>
#include "unity.h"
#include "search_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/search.h"
#include "../../core/src/evaluation.h"
#include "../../core/src/transposition.h"
#include "../../core/src/uci.h"
#include "../../core/src/bench.h"
#include "../../core/src/see.h"
#include "../../core/src/ref_see.h"
#include "../../core/src/tune.h"


static void setup_position(const char* fen, board_t* board, search_state_t* search_state, limits_t* limits, int depth) {
    char command[256];
    snprintf(command, sizeof(command), "position fen %s", fen);
    tt_clear();
    init_search_state(search_state);
    parse_position(command, board, search_state);
    reset_time_control(limits);
    limits->depth = depth;
}

void test_search_mate_in_one() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Back rank, Rd8#. */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 3);
    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(d1, get_source_square(mv));
    TEST_ASSERT_EQUAL_INT(d8, get_target_square(mv));

    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 3);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 1, negamax(-INF, INF, 3, &board, &search_state, &limits));
}

void test_search_with_helpers() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Lazy SMP helpers set up their own tables (start_helpers copies only the game history), the
     * search still finds the mate... */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 6);
    limits.threads = 4;
    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(d1, get_source_square(mv));
    TEST_ASSERT_EQUAL_INT(d8, get_target_square(mv));

    /* ...and runs from a position with game history (knights out and back, twice). */
    tt_clear();
    init_search_state(&search_state);
    parse_position("position startpos moves g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8", &board, &search_state);
    reset_time_control(&limits);
    limits.depth = 6;
    limits.threads = 4;
    mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_mvv_lva_range_matches_constants() {
    /* The ordering bands are checked at compile time with MVV_LVA_MIN / MAX, they must be the
     * table's actual range. */
    int lowest = INT32_MAX, highest = INT32_MIN;
    for (int attacker = 0; attacker < 12; attacker++) {
        for (int victim = 0; victim < 12; victim++) {
            const int v = mvv_lva[attacker][victim];
            if (v < lowest) lowest = v;
            if (v > highest) highest = v;
        }
    }
    TEST_ASSERT_EQUAL_INT(MVV_LVA_MIN, lowest);
    TEST_ASSERT_EQUAL_INT(MVV_LVA_MAX, highest);
}

/* Test helpers: a node's quiet context at the search state's ply, and a quiet move's main, pawn and
 * continuation entries set to 'value'. */
static quiet_ctx_t test_ctx(search_state_t* search_state, board_t* board) {
    return quiet_ctx(search_state->quiet, board, search_state->ply, search_state->path_moves, false);
}

static void set_quiet_history(search_state_t* search_state, board_t* board, move_t mv, int16_t value) {
    const quiet_ctx_t qc = test_ctx(search_state, board);
    *main_history_entry(&qc, mv) = value;
    (*qc.pawn)[piece_to(mv)] = value;
    for (int i = 0; i < CONT_PLIES; i++)
        if (qc.cont[i]) (*qc.cont[i])[piece_to(mv)] = value;
}

void test_quiet_history_update() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 1);
    const move_t best = parse_move("g1f3", &board), other = parse_move("a2a3", &board);
    const move_t tried[2] = { other, best };
    const quiet_ctx_t qc = test_ctx(&search_state, &board);
    /* Depth 5, no score difference: main and pawn bonus min(-113 + 164 * 5, 2178) = 707, malus
     * -min(-51 + 160 * 5, 1844) = -749. From the cleared 802: 802 + 707 - 802 * 707 / 16384 = 1475 and
     * 802 - 749 - 802 * 749 / 16384 = 17; the pawn history from 0: 707 and -749. */
    quiet_history_update(&qc, tried, 2, best, 5, 0);
    TEST_ASSERT_EQUAL_INT(1475, *main_history_entry(&qc, best));
    TEST_ASSERT_EQUAL_INT(17, *main_history_entry(&qc, other));
    TEST_ASSERT_EQUAL_INT(707, (*qc.pawn)[piece_to(best)]);
    TEST_ASSERT_EQUAL_INT(-749, (*qc.pawn)[piece_to(other)]);
    /* The score difference: 64 cp adds HIST_BONUS_SCORE_DIFF (247) to the bonus: 954; capped at
     * HIST_SCORE_DIFF_MAX by the caller. */
    quiet_history_clear(search_state.quiet);
    quiet_history_update(&qc, tried, 2, best, 5, 64);
    TEST_ASSERT_EQUAL_INT(954, (*qc.pawn)[piece_to(best)]);
    /* The bonus is capped: depth 20, min(-113 + 3280, 2178) = 2178. */
    quiet_history_clear(search_state.quiet);
    quiet_history_update(&qc, tried, 2, best, 20, 0);
    TEST_ASSERT_EQUAL_INT(2178, (*qc.pawn)[piece_to(best)]);
}

void test_quiet_history_skips_uncertain_updates() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 1);
    const move_t best = parse_move("g1f3", &board);
    const quiet_ctx_t qc = test_ctx(&search_state, &board);
    /* One quiet move tried at depth 1: no update; at depth 2 it is updated. */
    quiet_history_update(&qc, &best, 1, best, 1, 0);
    TEST_ASSERT_EQUAL_INT(HIST_CLEAR_MAIN, *main_history_entry(&qc, best));
    quiet_history_update(&qc, &best, 1, best, 2, 0);
    TEST_ASSERT_TRUE(*main_history_entry(&qc, best) > HIST_CLEAR_MAIN);
}

void test_history_counter_gravity() {
    /* v += d - v * |d| / 16384: 16000 + 2000 - 1953 = 16047, under the bound; and symmetric. */
    int16_t v = 16000;
    history_counter_update(&v, 2000);
    TEST_ASSERT_EQUAL_INT(16047, v);
    v = -16000;
    history_counter_update(&v, -2000);
    TEST_ASSERT_EQUAL_INT(-16047, v);
    for (int i = 0; i < 1000; i++) history_counter_update(&v, -4000);
    TEST_ASSERT_TRUE(v > -16384 - 4000 && v < -16000);
}

void test_cont_rows_follow_the_path() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    /* Black to move at ply 3 after white e2e4 (ply 0), a null move (ply 1), white d2xd5 capture (ply 2). */
    setup_position("rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", &board, &search_state, &limits, 1);
    const move_t pawn_e4 = encode_move(e2, e4, P, 0, 0, 1, 0, 0, 0);
    const move_t xd5 = encode_move(e4, d5, P, 0, 1, 0, 0, 0, 0);
    search_state.path_moves[0] = pawn_e4;
    search_state.path_moves[1] = 0;
    search_state.path_moves[2] = xd5;
    piece_to_history_t* rows[CONT_PLIES];
    cont_rows_at(search_state.quiet, black, 3, search_state.path_moves, rows);
    /* 1 back: white's capture xd5 -> [captured][black][white][pawn][d5]; 2 back: the null move, none;
     * 3 back: white's e4 -> [quiet][black][white][pawn][e4]; 4 back and further: before the root, none. */
    TEST_ASSERT_TRUE(rows[0] == &search_state.quiet->cont[1][black][white][0][d5]);
    TEST_ASSERT_NULL(rows[1]);
    TEST_ASSERT_TRUE(rows[2] == &search_state.quiet->cont[0][black][white][0][e4]);
    TEST_ASSERT_NULL(rows[3]);
    TEST_ASSERT_NULL(rows[5]);
}

void test_quiet_stat_and_order_score() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 1);
    /* Ply 6 so every continuation row exists (the path: quiet moves of both sides). */
    const move_t path[6] = { parse_move("g1f3", &board), encode_move(g8, f6, n, 0, 0, 0, 0, 0, 0),
                             parse_move("b1c3", &board), encode_move(b8, c6, n, 0, 0, 0, 0, 0, 0),
                             parse_move("a2a3", &board), encode_move(a7, a6, p, 0, 0, 0, 0, 0, 0) };
    for (int i = 0; i < 6; i++) search_state.path_moves[i] = path[i];
    search_state.ply = 6;
    const move_t mv = parse_move("e2e4", &board);
    const quiet_ctx_t qc = test_ctx(&search_state, &board);
    *main_history_entry(&qc, mv) = 1000;
    (*qc.pawn)[piece_to(mv)] = 400;
    const int16_t c[6] = { 100, 200, 300, 400, 500, 600 };
    for (int i = 0; i < 6; i++) (*qc.cont[i])[piece_to(mv)] = c[i];
    /* stat: main + 1, 2 and 4 plies back = 1000 + 100 + 200 + 400. */
    TEST_ASSERT_EQUAL_INT(1700, quiet_stat(&qc, mv));
    /* order: main + pawn + c1 + c2 * 1019/1024 + c4 * 555/1024 + c6 * 582/1024 = 1000 + 400 + 100 + 199 + 216 + 341. */
    TEST_ASSERT_EQUAL_INT(2256, quiet_order_score(&qc, mv));
    search_state.ply = 0;
}

void test_threat_terms_in_quiet_order() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    /* The white knight on d4 is attacked by the pawn on e5: leaving earns +4000; c3 -> b5 moves into
     * the a6 pawn's attack (-4000). */
    setup_position("4k3/8/p7/4p3/3N4/2N5/8/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const quiet_ctx_t qc = test_ctx(&search_state, &board);
    const move_t escape = parse_move("d4f3", &board), enter = parse_move("c3b5", &board), plain = parse_move("c3e2", &board);
    TEST_ASSERT_EQUAL_INT(quiet_order_score(&qc, plain) + THREAT_ESCAPE_MINOR -
                          (*main_history_entry(&qc, plain) - *main_history_entry(&qc, escape)),
                          quiet_order_score(&qc, escape));
    TEST_ASSERT_TRUE(quiet_order_score(&qc, enter) < quiet_order_score(&qc, plain) - THREAT_ENTER_MINOR / 2);
    TEST_ASSERT_TRUE(qc.threats.pawns & (1ULL << d4));
}

void test_node_cache() {
    quiet_history_t* tables = &main_quiet_history;
    quiet_history_clear(tables);
    tables->node_cache_generation = 1;
    node_cache_entry_t* e = node_cache_entry(tables, 0x1234);
    TEST_ASSERT_NOT_NULL(e);
    node_cache_add(e, 11, 300);
    node_cache_add(e, 22, 100);
    node_cache_add(e, 11, 100);
    TEST_ASSERT_EQUAL_UINT64(500, e->nodes_sum);
    /* The same position finds it again; another one in the slot is refused in this generation, taken next. */
    TEST_ASSERT_TRUE(node_cache_entry(tables, 0x1234) == e);
    TEST_ASSERT_NULL(node_cache_entry(tables, 0x1234 + NODE_CACHE_SIZE));
    tables->node_cache_generation = 2;
    TEST_ASSERT_TRUE(node_cache_entry(tables, 0x1234 + NODE_CACHE_SIZE) == e);
    TEST_ASSERT_EQUAL_UINT64(0, e->nodes_sum);
    quiet_history_clear(tables);
}

void test_quiet_history_new_search() {
    search_state_t search_state;
    init_search_state(&search_state);
    TEST_ASSERT_EQUAL_INT(HIST_CLEAR_MAIN, search_state.quiet->main[0][0][0][0]);
    TEST_ASSERT_EQUAL_INT(HIST_CLEAR_CONT, search_state.quiet->cont[0][0][0][0][0][0]);
    search_state.quiet->main[white][1][0][100] = 800;
    search_state.quiet->pawn[black][7][50] = -400;
    search_state.quiet->cont[0][white][black][1][10][20] = 1000;
    search_state.killer_moves[3] = 77;
    /* Before each 'go': main and pawn x 7/8, continuation kept, killers emptied. */
    clear_pv_table(&search_state);
    TEST_ASSERT_EQUAL_INT(700, search_state.quiet->main[white][1][0][100]);
    TEST_ASSERT_EQUAL_INT(-350, search_state.quiet->pawn[black][7][50]);
    TEST_ASSERT_EQUAL_INT(1000, search_state.quiet->cont[0][white][black][1][10][20]);
    TEST_ASSERT_EQUAL_UINT32(0, search_state.killer_moves[3]);
}

void test_lmr_tables_match_formula() {
    /* PLY_GRAIN * (bias + scale * ln d * ln m), truncated; the table's log differs from libm's by ~1e-7. */
    for (int d = 1; d < LMR_TABLE_SIZE; d += 7) {
        for (int m = 1; m < LMR_TABLE_SIZE; m += 5) {
            const double l = log((double) d) * log((double) m);
            const int quiet = (int) (PLY_GRAIN * (LMR_QUIET_BIAS / 1000.0 + LMR_QUIET_SCALE / 1000.0 * l));
            const int capture = (int) (PLY_GRAIN * (LMR_CAPTURE_BIAS / 1000.0 + LMR_CAPTURE_SCALE / 1000.0 * l));
            TEST_ASSERT_INT_WITHIN(1, quiet, lmr_quiet_table[d][m]);
            TEST_ASSERT_INT_WITHIN(1, capture, lmr_capture_table[d][m]);
        }
    }
    TEST_ASSERT_EQUAL_INT(0, lmr_quiet_table[0][5]);
    TEST_ASSERT_EQUAL_INT(0, lmr_capture_table[5][0]);
}

void test_lmr_reduction_terms() {
    /* Quiet, depth 6, move 10, non-PV, cut node, not improving, stat 0: table + 240 + 2928 + 608 - 6877 / 15,
     * rounded to the nearest ply. */
    const int q = lmr_quiet_table[6][10];
    int expected = (q + LMR_QUIET_NON_PV + LMR_QUIET_CUT_NODE + LMR_QUIET_NOT_IMPROVING - LMR_STAT_OFFSET / LMR_STAT_DIV +
                    PLY_GRAIN / 2) / PLY_GRAIN;
    TEST_ASSERT_EQUAL_INT(expected, lmr_reduction(6, 10, 4, false, true, false, false, true, 0, false, false, 0, false));
    /* Each term moves it its own way: a refutation, a check, a deep TT entry less; a TT capture more. */
    const int base = lmr_reduction(12, 20, 4, false, false, true, false, true, 0, false, false, 0, false);
    TEST_ASSERT_TRUE(lmr_reduction(12, 20, 4, false, false, true, false, true, 0, false, true, 0, false) < base);
    TEST_ASSERT_TRUE(lmr_reduction(12, 20, 4, false, false, true, true, true, 0, false, false, 0, false) < base);
    TEST_ASSERT_TRUE(lmr_reduction(12, 20, 4, false, false, true, false, true, 0, true, false, 0, false) > base);
    /* A good history reduces less: stat 15360 is a ply. */
    TEST_ASSERT_EQUAL_INT(base - 1, lmr_reduction(12, 20, 4, false, false, true, false, true, LMR_STAT_DIV * PLY_GRAIN, false,
                                                  false, 0, false));
    /* Captures: the capture table, a winning capture less, at a cut node more. */
    const int c = lmr_capture_table[8][12];
    expected = (c - LMR_CAPTURE_WINNING + LMR_CAPTURE_CUT_NODE + PLY_GRAIN / 2) / PLY_GRAIN;
    TEST_ASSERT_EQUAL_INT(expected, lmr_reduction(8, 12, 3, false, true, true, false, false, 0, false, false, 2, false));
    /* PV nodes: PLY_GRAIN * depth / (1 + ply + depth) less. */
    expected = (lmr_quiet_table[6][10] - LMR_STAT_OFFSET / LMR_STAT_DIV - PLY_GRAIN * 6 / (1 + 2 + 6) + PLY_GRAIN / 2) / PLY_GRAIN;
    TEST_ASSERT_EQUAL_INT(expected, lmr_reduction(6, 10, 2, true, false, true, false, true, 0, false, false, 0, false));
}

void test_move_loop_pruning_thresholds() {
    /* LMP: 4 + d^2 improving, 4 + d^2 / 2 not. */
    TEST_ASSERT_EQUAL_INT(13, lmp_threshold(3, true));
    TEST_ASSERT_EQUAL_INT(8, lmp_threshold(3, false));
    /* History pruning: -234 * d - 148 * d^2. */
    TEST_ASSERT_EQUAL_INT(-1060, history_prune_threshold(2));
    /* Futility: 19.2 cp * d^2 + stat / 638. */
    TEST_ASSERT_EQUAL_INT(76 + 1, futility_margin(2, 638));
    /* The reduced depth estimate: quiet moves after the first, the quiet table's rounded reduction, at least 1. */
    TEST_ASSERT_EQUAL_INT(7, lmr_depth_estimate(7, 0, true));
    TEST_ASSERT_EQUAL_INT(7, lmr_depth_estimate(7, 5, false));
    TEST_ASSERT_EQUAL_INT(7 - (lmr_quiet_table[7][6] + PLY_GRAIN / 2) / PLY_GRAIN, lmr_depth_estimate(7, 5, true));
    TEST_ASSERT_EQUAL_INT(1, lmr_depth_estimate(2, 40, true));
}

void test_ref_see_pins_and_values() {
    board_t board;
    /* Rxe6: the e6 knight's defender d7 is pinned to e8 by b5, so it doesn't recapture: a knight won. */
    parse_fen("4k3/3p4/4n3/1B6/8/8/8/4R1K1 w - - 0 1", &board);
    TEST_ASSERT_TRUE(see_ge_ref(&board, parse_move("e1e6", &board), 0));
    /* Without the pin the pawn recaptures: rook for knight. */
    parse_fen("4k3/3p4/4n3/8/8/8/8/4R1K1 w - - 0 1", &board);
    TEST_ASSERT_FALSE(see_ge_ref(&board, parse_move("e1e6", &board), 0));
    /* Bishop for knight is even at see_ge_ref's values (300 each), losing at see.h's (330 vs 320). */
    parse_fen("4k3/3p4/4n3/8/2B5/8/8/6K1 w - - 0 1", &board);
    TEST_ASSERT_TRUE(see_ge_ref(&board, parse_move("c4e6", &board), 0));
    TEST_ASSERT_FALSE(see_ge(&board, parse_move("c4e6", &board), 0));
}

void test_ref_capture_class() {
    board_t board;
    /* Nxe6 wins (knight below bishop), Bxe6 is good (even), Rxe6 with the pawn recapturing loses. */
    parse_fen("4k3/3p4/4b3/2N5/8/8/8/4R1K1 w - - 0 1", &board);
    TEST_ASSERT_EQUAL_INT(2, ref_capture_class(&board, parse_move("c5e6", &board)));
    TEST_ASSERT_EQUAL_INT(0, ref_capture_class(&board, parse_move("e1e6", &board)));
    parse_fen("4k3/3p4/4n3/8/2B5/8/8/6K1 w - - 0 1", &board);
    TEST_ASSERT_EQUAL_INT(1, ref_capture_class(&board, parse_move("c4e6", &board)));
    /* A promotion without a capture is below the good captures. */
    parse_fen("4k3/P7/8/8/8/8/8/6K1 w - - 0 1", &board);
    TEST_ASSERT_EQUAL_INT(0, ref_capture_class(&board, parse_move("a7a8q", &board)));
}

void test_killer_and_counter_scores() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 1);
    const move_t killer = parse_move("g1f3", &board), counter = parse_move("b1c3", &board), quiet = parse_move("a2a3", &board);
    search_state.killer_moves[0] = killer;
    /* The counter move answers the move that led here (none at the root): with a previous move it is
     * stored at a fail high and scored just under the killer. */
    search_state.path_moves[0] = encode_move(e7, e5, p, 0, 0, 1, 0, 0, 0);
    search_state.ply = 1;
    search_state.killer_moves[1] = killer;
    const quiet_ctx_t qc = test_ctx(&search_state, &board);
    quiet_history_update(&qc, &counter, 1, counter, 5, 0);
    TEST_ASSERT_EQUAL_UINT32(counter, counter_move(&qc));
    TEST_ASSERT_EQUAL_INT(KILLER_SCORE, score_move(killer, 0, &board, &search_state));
    TEST_ASSERT_EQUAL_INT(COUNTER_SCORE, score_move(counter, 0, &board, &search_state));
    TEST_ASSERT_TRUE(score_move(quiet, 0, &board, &search_state) < COUNTER_SCORE);
    search_state.ply = 0;
}

void test_prior_counter_move_bonus() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    /* A fail low at a node raises the quiet move that led to it in the parent's continuation history:
     * min(1200, 120 * depth - 100), weighted per ply back. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 1);
    const move_t first = parse_move("g1f3", &board);
    const move_t reply = encode_move(g8, f6, n, 0, 0, 0, 0, 0, 0);
    search_state.path_moves[0] = first;
    piece_to_history_t* rows[CONT_PLIES];
    cont_rows_at(search_state.quiet, black, 1, search_state.path_moves, rows);
    TEST_ASSERT_NOT_NULL(rows[0]);
    const int before = (*rows[0])[piece_to(reply)];
    cont_history_update(rows, reply, 5 * PRIOR_CMH_SCALE - PRIOR_CMH_BIAS);
    TEST_ASSERT_EQUAL_INT(before + 500 - before * 500 / HISTORY_GRAVITY, (*rows[0])[piece_to(reply)]);
}

void test_pv_flag_applies_to_one_node() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];

    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 1);
    generate_moves(&board, &buffer);
    const move_t pv_move = parse_move("e2e4", &board);
    int index = -1;
    for (int i = 0; i < buffer.index; i++) {
        if (buffer.moves[i] == pv_move) index = i;
    }
    TEST_ASSERT_TRUE(index >= 0);

    /* The PV move is also the TT move, so it's scored as the TT move and the flag isn't used... */
    const quiet_ctx_t qc = test_ctx(&search_state, &board);
    search_state.pv_table[0][0] = pv_move;
    search_state.score_pv = true;
    score_moves(&buffer, scores, pv_move, &board, &search_state, &qc);
    TEST_ASSERT_EQUAL_INT(TT_MOVE_SCORE, scores[index]);
    TEST_ASSERT_FALSE(search_state.score_pv);

    /* ...and the next node to score the same move at this ply doesn't get it as the PV move. */
    score_moves(&buffer, scores, 0, &board, &search_state, &qc);
    TEST_ASSERT_TRUE(scores[index] < COUNTER_SCORE);

    /* Flagged and not the TT move, it's scored as the PV move. */
    search_state.score_pv = true;
    score_moves(&buffer, scores, 0, &board, &search_state, &qc);
    TEST_ASSERT_EQUAL_INT(PV_MOVE_SCORE, scores[index]);
    TEST_ASSERT_FALSE(search_state.score_pv);
}

void test_search_at_max_ply() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* A search reaching MAX_PLY returns the static eval there, within the tables: the PV lengths
     * stay inside the path. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 3);
    nnue_reset(&search_state.nnue);
    search_state.ply = MAX_PLY - 2;
    const uint64_t nodes = search_state.nodes;
    const int score = negamax(-INF, INF, 3, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(score > -MATE_SCORE && score < MATE_SCORE);
    TEST_ASSERT_EQUAL_INT(MAX_PLY - 2, search_state.ply);
    TEST_ASSERT_TRUE(search_state.nodes > nodes && search_state.nodes < nodes + 10000);
    TEST_ASSERT_TRUE(search_state.pv_length[MAX_PLY - 2] <= MAX_PLY);
}

void test_search_tables_cleared_between_searches() {
    search_state_t search_state;
    init_search_state(&search_state);
    search_state.quiet->main[white][0][0][g1 * 64 + f3] = 1600;
    search_state.killer_moves[3] = 77;
    search_state.pv_table[0][0] = 88;

    /* Before each 'go': the path tables are emptied, the main history x 7/8. */
    clear_pv_table(&search_state);
    TEST_ASSERT_EQUAL_INT(1400, search_state.quiet->main[white][0][0][g1 * 64 + f3]);
    TEST_ASSERT_EQUAL_UINT32(0, search_state.killer_moves[3]);
    TEST_ASSERT_EQUAL_UINT32(0, search_state.pv_table[0][0]);

    /* A new game ('ucinewgame') starts from the cleared values. */
    init_search_state(&search_state);
    TEST_ASSERT_EQUAL_INT(HIST_CLEAR_MAIN, search_state.quiet->main[white][0][0][g1 * 64 + f3]);
}

void test_search_stalemate_root() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Black to move has no legal moves and isn't in check. */
    setup_position("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", &board, &search_state, &limits, 3);
    TEST_ASSERT_EQUAL_INT(0, negamax(-INF, INF, 3, &board, &search_state, &limits));
    TEST_ASSERT_EQUAL_UINT32(0, find_best_move(&board, &search_state, &limits)); /* 'bestmove 0000'. */
}

void test_search_checkmated_root() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 3);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE, negamax(-INF, INF, 3, &board, &search_state, &limits));
    TEST_ASSERT_EQUAL_UINT32(0, find_best_move(&board, &search_state, &limits));
}

void test_bench_positions_are_legal() {
    for (int i = 0; i < bench_position_count; i++) {
        board_t board;
        parse_fen(bench_positions[i], &board);

        /* The side that just moved can't be in check, and the side to move has a move. */
        square king_sq = trailing_zero_count(board.pieces[board.side == white ? k : K]);
        bool attacked = board.side == white ? is_square_attacked_by_white(&board, king_sq)
                                            : is_square_attacked_by_black(&board, king_sq);
        TEST_ASSERT_FALSE_MESSAGE(attacked, bench_positions[i]);

        move_buffer_t buffer;
        generate_moves(&board, &buffer);
        bool has_legal_move = false;
        for (int j = 0; j < buffer.index && !has_legal_move; j++) {
            has_legal_move = make_move(&board, buffer.moves[j]);
            pop_move(&board);
        }
        TEST_ASSERT_TRUE_MESSAGE(has_legal_move, bench_positions[i]);
    }
}

void test_bench_is_deterministic() {
    const size_t tt_entries = tt_size;

    uint64_t first = bench(4);
    uint64_t second = bench(4);
    TEST_ASSERT_TRUE(first > 0);
    TEST_ASSERT_EQUAL_UINT64(first, second);

    /* The TT size is restored after the run. */
    TEST_ASSERT_EQUAL_size_t(tt_entries, tt_size);
}

void test_time_checked_once_per_2048_nodes() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                   &board, &search_state, &limits, 10);
    find_best_move(&board, &search_state, &limits);

    /* Used to also check before counting, several times on the same count. */
    TEST_ASSERT_TRUE(search_state.nodes > 10 * 2048);
    TEST_ASSERT_EQUAL_UINT64(search_state.nodes / 2048, limits.time_checks);
}

void test_time_limit_stops_at_first_check() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Already out of time, the first check (at 2048 nodes) stops the search (deep enough not to finish first). */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 40);
    limits.time_set = true;
    limits.stop_time = 0;
    limits.soft_stop_time = UINT64_MAX; /* Only the hard limit, the soft one would stop after depth 1. */

    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_EQUAL_UINT64(1, limits.time_checks);
    TEST_ASSERT_TRUE(search_state.nodes >= 2048 && search_state.nodes < 2 * 2048);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_only_legal_move() {
    board_t board;
    /* In check from an unprotected queen, the king can only take it. */
    parse_fen("k7/8/8/8/8/8/6q1/7K w - - 0 1", &board);
    const move_t only = only_legal_move(&board);
    TEST_ASSERT_EQUAL_INT(h1, get_source_square(only));
    TEST_ASSERT_EQUAL_INT(g2, get_target_square(only));
    /* More than one move, or none (stalemate): 0. */
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    TEST_ASSERT_EQUAL_INT(0, only_legal_move(&board));
    parse_fen("k7/8/8/8/8/8/5q2/7K w - - 0 1", &board);
    TEST_ASSERT_EQUAL_INT(0, only_legal_move(&board));
}

void test_tm_scale() {
    /* Just changed and the moves close (30% of the nodes): more than the soft time. */
    TEST_ASSERT_TRUE(tm_scale(0, 30, 100) > 2.0);
    /* Stable for long and clearly best (90%): well under it. */
    TEST_ASSERT_TRUE(tm_scale(TM_STABILITY_MAX, 90, 100) < 0.7);

    /* More stability, or a larger share of the nodes, never gives more time. */
    for (int s = 0; s < TM_STABILITY_MAX; s++) {
        TEST_ASSERT_TRUE(tm_scale(s + 1, 50, 100) <= tm_scale(s, 50, 100));
    }
    for (uint64_t n = 0; n < 100; n += 10) {
        TEST_ASSERT_TRUE(tm_scale(2, n + 10, 100) <= tm_scale(2, n, 100));
    }

    /* Out of range: stability is capped, and no root nodes counts as all on the best move. */
    TEST_ASSERT_TRUE(tm_scale(TM_STABILITY_MAX, 50, 100) == tm_scale(TM_STABILITY_MAX + 10, 50, 100));
    TEST_ASSERT_TRUE(tm_scale(1, 100, 100) == tm_scale(1, 0, 0));
    TEST_ASSERT_TRUE(tm_scale(1, 100, 100) == tm_scale(1, 200, 100));
}

void test_clock_search_within_hard_limit() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* A real clock search: however the soft limit is scaled, it ends by the hard limit (plus the
     * time to unwind the search and a little slack for a loaded machine). */
    setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                   &board, &search_state, &limits, MAX_SEARCH_DEPTH);
    parse_go_limits("go wtime 2000 btime 2000 winc 20 binc 20", &board, &limits);
    limits.quiet = true;
    TEST_ASSERT_TRUE(limits.adaptive);

    move_t mv = find_best_move(&board, &search_state, &limits);
    const uint64_t elapsed = clock_time_ms() - limits.start_time;
    TEST_ASSERT_TRUE(elapsed <= limits.stop_time - limits.start_time + 50);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_pick_next_move_order() {
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    static const int input[] = { 5, 100, 0, 9000, 5, 20000, -3, 100 };
    const int n = (int) (sizeof(input) / sizeof(input[0]));

    /* Move i is just i + 1, so the picked moves can be checked against their scores. */
    buffer.index = n;
    for (int i = 0; i < n; i++) {
        buffer.moves[i] = (move_t) (i + 1);
        scores[i] = input[i];
    }

    bool seen[16] = { false };
    int previous = 1 << 30;
    for (int i = 0; i < n; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);
        TEST_ASSERT_EQUAL_INT(input[mv - 1], scores[i]); /* Score still belongs to its move. */
        TEST_ASSERT_TRUE(scores[i] <= previous);         /* Best first. */
        TEST_ASSERT_FALSE(seen[mv]);                     /* Each move once. */
        seen[mv] = true;
        previous = scores[i];
    }
    TEST_ASSERT_EQUAL_UINT32(6, buffer.moves[0]); /* 20000 */
    TEST_ASSERT_EQUAL_UINT32(4, buffer.moves[1]); /* 9000  */
}

void test_quiescence_sees_quiet_queen_promotion() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* No captures, only a7a8=Q. The old qsearch skipped non-captures, so it stood pat. */
    setup_position("8/P7/8/8/8/8/k7/7K w - - 0 1", &board, &search_state, &limits, 1);
    const int stand_pat = evaluation(&board, &search_state.nnue);
    const int score = quiescence(-INF, INF, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(score > stand_pat + 500);
}

void test_mate_seen_at_horizon() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mated, searched at depth 0: in check the qsearch searches every evasion, there are none. */
    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 0);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE, negamax(-INF, INF, 0, &board, &search_state, &limits));
    /* Below depth 0 (a negative extension) it's the qsearch too. */
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE, negamax(-INF, INF, -2, &board, &search_state, &limits));

    /* Mate in 1 at depth 1: Rd8+ leaves the reply at depth 0, the qsearch still sees the mate. */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 1);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 1, negamax(-INF, INF, 1, &board, &search_state, &limits));
}

void test_null_move_guards() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    const int low_beta = -10000; /* Static eval is always above this. */

    /* Middlegame, pieces on both sides, not in check, one ply in. */
    setup_position("r1bqkb1r/pppp1ppp/2n2n2/4p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 4 4",
                   &board, &search_state, &limits, 5);
    search_state.ply = 1;
    search_state.repetition_index = 1;
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));

    /* Depth, check and PV node. */
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, NMP_MIN_DEPTH - 1, low_beta, evaluation(&board, &search_state.nnue), false, false));
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), true, false));
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, true));

    /* Static eval below beta. */
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, evaluation(&board, &search_state.nnue) + 1, evaluation(&board, &search_state.nnue), false, false));
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, evaluation(&board, &search_state.nnue), evaluation(&board, &search_state.nnue), false, false));

    /* Root. */
    search_state.ply = 0;
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.ply = 1;

    /* Straight after a null move, but not two plies after one. */
    search_state.null_move_index = 1;
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.repetition_index = 2;
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.null_move_index = 0;
    search_state.repetition_index = 1;

    /* Inside a null move verification search: not below nmp_min_ply. */
    search_state.nmp_min_ply = 3;
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.ply = 3;
    search_state.repetition_index = 3;
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.nmp_min_ply = 0;
    search_state.ply = 1;
    search_state.repetition_index = 1;

    /* Beta a mate score: not even with the eval above it. */
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, -MATE_VALUE + 10, evaluation(&board, &search_state.nnue), false, false));
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, MATE_VALUE - 10, MATE_VALUE, false, false));

    /* Side to move has only king and pawns (the opponent's pieces don't matter). */
    setup_position("4k3/8/8/2r5/8/8/PPP5/4K3 w - - 0 1", &board, &search_state, &limits, 5);
    search_state.ply = 1;
    search_state.repetition_index = 1;
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    setup_position("4k3/8/8/2r5/8/8/PPP5/4K3 b - - 0 1", &board, &search_state, &limits, 5);
    search_state.ply = 1;
    search_state.repetition_index = 1;
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
}

void test_null_move_depth() {
    /* depth - (base + depth * rate + min((eval - beta) / eval_div * PLY_GRAIN, eval_max)) / PLY_GRAIN, at least 0. */
    for (int depth = NMP_MIN_DEPTH; depth <= 30; depth++) {
        const int base = depth - (NMP_R_BASE + depth * NMP_R_DEPTH_RATE) / PLY_GRAIN;
        TEST_ASSERT_EQUAL_INT(base > 0 ? base : 0, null_move_depth(depth, 0, 0));
        const int far = depth - (NMP_R_BASE + depth * NMP_R_DEPTH_RATE + NMP_R_EVAL_MAX) / PLY_GRAIN;
        TEST_ASSERT_EQUAL_INT(far > 0 ? far : 0, null_move_depth(depth, 100000, 0));
        /* The eval term counts whole plies: just under NMP_R_EVAL_DIV above beta adds nothing, NMP_R_EVAL_DIV a ply. */
        TEST_ASSERT_EQUAL_INT(null_move_depth(depth, 0, 0), null_move_depth(depth, NMP_R_EVAL_DIV - 1, 0));
        const int one = depth - (NMP_R_BASE + depth * NMP_R_DEPTH_RATE + PLY_GRAIN) / PLY_GRAIN;
        TEST_ASSERT_EQUAL_INT(one > 0 ? one : 0, null_move_depth(depth, NMP_R_EVAL_DIV, 0));
        /* More eval above beta, never a deeper null search. */
        TEST_ASSERT_TRUE(null_move_depth(depth, NMP_R_EVAL_DIV, 0) <= null_move_depth(depth, NMP_R_EVAL_DIV - 1, 0));
        /* Always shallower than the node, never below qsearch. */
        TEST_ASSERT_TRUE(null_move_depth(depth, 0, 0) < depth && null_move_depth(depth, 100000, 0) >= 0);
        TEST_ASSERT_TRUE(null_move_depth(depth + 1, 0, 0) >= null_move_depth(depth, 0, 0));
    }
    /* At the shallowest depths (up to NMP_R_BASE) the null move goes straight to qsearch. */
    for (int depth = 1; depth <= NMP_R_BASE / PLY_GRAIN; depth++) TEST_ASSERT_EQUAL_INT(0, null_move_depth(depth, 0, 0));
}

void test_null_move_verification_restores_state() {
    /* White a rook and bishop up: null move cutoffs everywhere, and deep enough for the verification
     * search (NMP_VERIFY_DEPTH). Afterwards null moves are allowed again (nmp_min_ply cleared) and
     * the search still sees the win. */
    board_t board;
    search_state_t search_state;
    limits_t limits;
    setup_position("8/8/4k3/8/8/2B5/3RK3/8 w - - 0 1", &board, &search_state, &limits, NMP_VERIFY_DEPTH + 1);
    const int score = negamax(-INF, INF, NMP_VERIFY_DEPTH + 1, &board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(0, search_state.nmp_min_ply);
    TEST_ASSERT_TRUE(score > 300);
}



void test_penalised_quiet_stays_above_losing_captures() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 1.e4 e5 2.Qh5 Nc6: Qxf7+ loses the queen to ...Kxf7 (SEE < 0). */
    setup_position("r1bqkbnr/pppp1ppp/2n5/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR w KQkq - 2 3", &board, &search_state, &limits, 1);
    const move_t quiet = parse_move("g1f3", &board);
    const move_t losing_capture = parse_move("h5f7", &board);

    set_quiet_history(&search_state, &board, quiet, -16384);
    TEST_ASSERT_TRUE(score_move(losing_capture, 0, &board, &search_state) < 0);
    TEST_ASSERT_TRUE(score_move(quiet, 0, &board, &search_state) >
                     score_move(losing_capture, 0, &board, &search_state));
}

void test_history_stays_below_killers_and_captures() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 1.e4 d5, exd5 is a capture. */
    setup_position("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", &board, &search_state, &limits, 1);
    const move_t quiet = parse_move("g1f3", &board);
    const move_t killer = parse_move("b1c3", &board);
    const move_t capture = parse_move("e4d5", &board);

    set_quiet_history(&search_state, &board, quiet, 16383);
    search_state.killer_moves[0] = killer;

    const int quiet_score = score_move(quiet, 0, &board, &search_state);
    const int killer_score = score_move(killer, 0, &board, &search_state);
    const int capture_score = score_move(capture, 0, &board, &search_state);
    TEST_ASSERT_TRUE(quiet_score < killer_score);
    TEST_ASSERT_TRUE(killer_score < capture_score);
}

void test_queen_promotion_ordered_with_captures() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* b7-b8: the queen promotion goes over the killers and every quiet, the underpromotion doesn't. */
    setup_position("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const move_t queen = parse_move("b7b8q", &board);
    const move_t knight = parse_move("b7b8n", &board);
    const move_t quiet = parse_move("e1e2", &board);
    const move_t killer = parse_move("e1d1", &board);
    set_quiet_history(&search_state, &board, quiet, 16383);
    search_state.killer_moves[0] = killer;

    TEST_ASSERT_EQUAL_INT(QUEEN_PROMOTION_SCORE, score_move(queen, 0, &board, &search_state));
    TEST_ASSERT_TRUE(score_move(queen, 0, &board, &search_state) > score_move(killer, 0, &board, &search_state));
    TEST_ASSERT_TRUE(score_move(queen, 0, &board, &search_state) > score_move(quiet, 0, &board, &search_state));
    TEST_ASSERT_TRUE(score_move(knight, 0, &board, &search_state) < COUNTER_SCORE);
}

void test_capture_history_shifts_captures_within_band() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 1.e4 d5, exd5: its history moves it by at most CAPTURE_HISTORY_SPAN, always over the killers. */
    setup_position("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", &board, &search_state, &limits, 1);
    const move_t capture = parse_move("e4d5", &board);
    const int base = score_move(capture, 0, &board, &search_state);

    for (int i = 0; i < 100000; i++) update_capture_history(&search_state, &board, capture, 40);
    TEST_ASSERT_EQUAL_INT(CAPTURE_HISTORY_MAX, search_state.capture_history[P][d5][0]);
    TEST_ASSERT_EQUAL_INT(base + CAPTURE_HISTORY_SPAN, score_move(capture, 0, &board, &search_state));

    for (int i = 0; i < 100000; i++) penalise_capture_history(&search_state, &board, capture, 40);
    TEST_ASSERT_EQUAL_INT(-CAPTURE_HISTORY_MAX, search_state.capture_history[P][d5][0]);
    TEST_ASSERT_EQUAL_INT(base - CAPTURE_HISTORY_SPAN, score_move(capture, 0, &board, &search_state));
    TEST_ASSERT_TRUE(score_move(capture, 0, &board, &search_state) > KILLER_SCORE);

    /* Halved before each search, like quiet history. */
    clear_pv_table(&search_state);
    TEST_ASSERT_EQUAL_INT(-CAPTURE_HISTORY_MAX / 2, search_state.capture_history[P][d5][0]);
}

void test_capture_history_reorders_captures() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* The d4 pawn takes a knight (c5) or a bishop (e5). MVV-LVA prefers the bishop, until its
     * capture keeps failing and the knight's keeps cutting off. Keyed by the victim's type. */
    setup_position("4k3/8/8/2n1b3/3P4/8/8/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const move_t takes_knight = parse_move("d4c5", &board);
    const move_t takes_bishop = parse_move("d4e5", &board);
    TEST_ASSERT_TRUE(score_move(takes_bishop, 0, &board, &search_state) > score_move(takes_knight, 0, &board, &search_state));

    for (int i = 0; i < 100000; i++) {
        update_capture_history(&search_state, &board, takes_knight, 40);
        penalise_capture_history(&search_state, &board, takes_bishop, 40);
    }
    TEST_ASSERT_EQUAL_INT(CAPTURE_HISTORY_MAX, search_state.capture_history[P][c5][1]);
    TEST_ASSERT_EQUAL_INT(-CAPTURE_HISTORY_MAX, search_state.capture_history[P][e5][2]);
    TEST_ASSERT_TRUE(score_move(takes_knight, 0, &board, &search_state) > score_move(takes_bishop, 0, &board, &search_state));
}

void test_losing_capture_with_history_stays_under_quiets() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Qxf7+ loses the queen (SEE < 0): the best capture history can't lift it over a quiet move with
     * the worst quiet history. */
    setup_position("r1bqkbnr/pppp1ppp/2n5/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR w KQkq - 2 3", &board, &search_state, &limits, 1);
    const move_t quiet = parse_move("g1f3", &board);
    const move_t losing_capture = parse_move("h5f7", &board);
    for (int i = 0; i < 100000; i++) {
        set_quiet_history(&search_state, &board, quiet, -16384);
        update_capture_history(&search_state, &board, losing_capture, 40);
    }
    TEST_ASSERT_TRUE(score_move(losing_capture, 0, &board, &search_state) < score_move(quiet, 0, &board, &search_state));
}

void test_en_passant_scored_as_pawn_capture() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* White e5, black just played d7d5: exd6 e.p. and, for comparison, exf6 taking a pawn on f6. */
    setup_position("rnbqkbnr/ppp1p1pp/5p2/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3", &board, &search_state, &limits, 1);
    const move_t en_passant = parse_move("e5d6", &board);
    const move_t pawn_takes_pawn = parse_move("e5f6", &board);
    TEST_ASSERT_TRUE(get_enpassant_flag(en_passant));
    TEST_ASSERT_FALSE(get_enpassant_flag(pawn_takes_pawn));

    /* Used to score 0, below every quiet move with history. */
    TEST_ASSERT_EQUAL_INT(score_move(pawn_takes_pawn, 0, &board, &search_state),
                          score_move(en_passant, 0, &board, &search_state));

    /* Black side as well. */
    setup_position("rnbqkbnr/pppp1ppp/8/8/3Pp3/5P2/PPP3PP/RNBQKBNR b KQkq d3 0 3", &board, &search_state, &limits, 1);
    const move_t black_en_passant = parse_move("e4d3", &board);
    const move_t black_pawn_takes_pawn = parse_move("e4f3", &board);
    TEST_ASSERT_TRUE(get_enpassant_flag(black_en_passant));
    TEST_ASSERT_EQUAL_INT(score_move(black_pawn_takes_pawn, 0, &board, &search_state),
                          score_move(black_en_passant, 0, &board, &search_state));
}

void test_losing_capture_ordered_after_quiet_moves() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Qxd5 loses the queen for a pawn (c6 defends), Ke2 is a quiet move with no history. */
    setup_position("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1", &board, &search_state, &limits, 1);
    const int losing = score_move(parse_move("d1d5", &board), 0, &board, &search_state);
    const int quiet = score_move(parse_move("e1e2", &board), 0, &board, &search_state);
    TEST_ASSERT_TRUE(losing < quiet);

    /* Undefended, ordered as a normal capture. */
    setup_position("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1", &board, &search_state, &limits, 1);
    TEST_ASSERT_TRUE(score_move(parse_move("d1d5", &board), 0, &board, &search_state) >= 10000);
}

void test_reverse_futility_guards() {
    const int m3 = rfp_margin(3, false, false, true);
    TEST_ASSERT_EQUAL_INT(3 * RFP_MARGIN, m3);
    /* The margin: eval this far above beta is pruned, one less isn't. */
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, m3 - 1, 0, false, false, 1, m3));
    TEST_ASSERT_TRUE(can_reverse_futility_prune(3, m3, 0, false, false, 1, m3));
    /* Too deep, in check, PV node, root, mate bound. */
    TEST_ASSERT_FALSE(can_reverse_futility_prune(RFP_MAX_DEPTH + 1, 5000, 0, false, false, 1, m3));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, 5000, 0, true, false, 1, m3));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, 5000, 0, false, true, 1, m3));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, 5000, 0, false, false, 0, m3));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, MATE_VALUE, MATE_VALUE - 10, false, false, 1, m3));
    /* Each term makes the margin smaller, down to RFP_MIN_MARGIN. */
    int expected = 3 * RFP_MARGIN - RFP_IMPROVING;
    TEST_ASSERT_EQUAL_INT(expected > RFP_MIN_MARGIN ? expected : RFP_MIN_MARGIN, rfp_margin(3, true, false, true));
    expected = 3 * RFP_MARGIN - RFP_OPP_WORSENING;
    TEST_ASSERT_EQUAL_INT(expected > RFP_MIN_MARGIN ? expected : RFP_MIN_MARGIN, rfp_margin(3, false, true, true));
    expected = 3 * RFP_MARGIN - 3 * RFP_NO_TT;
    TEST_ASSERT_EQUAL_INT(expected > RFP_MIN_MARGIN ? expected : RFP_MIN_MARGIN, rfp_margin(3, false, false, false));
    TEST_ASSERT_TRUE(rfp_margin(1, true, true, false) >= RFP_MIN_MARGIN);
    /* The score: static eval moved RFP_BETA_BLEND / 1024 of the way to beta, between the two. */
    const int score = rfp_score(400, 0);
    TEST_ASSERT_TRUE(score >= 0 && score <= 400);
    TEST_ASSERT_EQUAL_INT((int) ((400LL * (1024 - RFP_BETA_BLEND)) / 1024), score);
    /* Opponent worsening: the two evals sum to more than the margin; never after a check (-INF). */
    TEST_ASSERT_TRUE(is_opp_worsening(RFP_OPP_WORSENING_MARGIN, 1));
    TEST_ASSERT_FALSE(is_opp_worsening(RFP_OPP_WORSENING_MARGIN, 0));
    TEST_ASSERT_FALSE(is_opp_worsening(5000, -INF));
}

void test_opp_can_win_material() {
    board_t board;
    /* Quiet start: nothing attacked. */
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    TEST_ASSERT_FALSE(opp_can_win_material(&board));
    /* A black pawn attacks the white knight on d4. */
    parse_fen("4k3/8/8/4p3/3N4/8/8/4K3 w - - 0 1", &board);
    TEST_ASSERT_TRUE(opp_can_win_material(&board));
    /* A black knight attacks the white rook on d1; a pawn attacking only a pawn doesn't count. */
    parse_fen("4k3/8/8/8/8/4n3/3P4/3RK3 w - - 0 1", &board);
    TEST_ASSERT_TRUE(opp_can_win_material(&board));
    parse_fen("4k3/8/8/8/8/4p3/3P4/4K3 w - - 0 1", &board);
    TEST_ASSERT_FALSE(opp_can_win_material(&board));
    /* A black rook attacks the white queen along the file; blocked, it doesn't. */
    parse_fen("3rk3/8/8/8/8/8/8/3QK3 w - - 0 1", &board);
    TEST_ASSERT_TRUE(opp_can_win_material(&board));
    parse_fen("3rk3/8/8/8/3P4/8/8/3QK3 w - - 0 1", &board);
    TEST_ASSERT_FALSE(opp_can_win_material(&board));
    /* Our own king doesn't block: the black rook attacks the white queen behind the white king. */
    parse_fen("3rk3/8/8/8/8/3K4/8/3Q4 w - - 0 1", &board);
    TEST_ASSERT_TRUE(opp_can_win_material(&board));
    /* From black's side: a white bishop attacks the black rook. */
    parse_fen("r3k3/8/8/8/8/8/6B1/4K3 b - - 0 1", &board);
    TEST_ASSERT_TRUE(opp_can_win_material(&board));
}

void test_improving() {
    /* Against two plies up, or four if that was in check (-INF); with neither, improving. */
    TEST_ASSERT_TRUE(is_improving(50, 40, -INF));
    TEST_ASSERT_FALSE(is_improving(40, 40, -INF));
    TEST_ASSERT_FALSE(is_improving(30, 40, 0));
    TEST_ASSERT_TRUE(is_improving(30, -INF, 20));
    TEST_ASSERT_FALSE(is_improving(10, -INF, 20));
    TEST_ASSERT_TRUE(is_improving(-500, -INF, -INF));
}





void test_quiescence_searches_promotion_with_negative_history() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* As test_quiescence_sees_quiet_queen_promotion, with a negative history for a8=Q: a queen
     * promotion is ordered with the winning captures whatever its history, so the qsearch still
     * searches it (it never sorts among the losing captures, which end the qsearch). */
    setup_position("8/P7/8/8/8/8/k7/7K w - - 0 1", &board, &search_state, &limits, 1);
    const move_t promotion = parse_move("a7a8q", &board);
    set_quiet_history(&search_state, &board, promotion, -16384);
    TEST_ASSERT_EQUAL_INT(QUEEN_PROMOTION_SCORE, score_move(promotion, 0, &board, &search_state));

    const int stand_pat = evaluation(&board, &search_state.nnue);
    TEST_ASSERT_TRUE(quiescence(-INF, INF, &board, &search_state, &limits) > stand_pat + 500);
}



void test_iir_reduction() {
    const move_t tt_move = encode_move(e2, e4, P, 0, 0, 1, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT(1, iir_reduction(IIR_MIN_DEPTH, 0));
    TEST_ASSERT_EQUAL_INT(1, iir_reduction(12, 0));
    TEST_ASSERT_EQUAL_INT(0, iir_reduction(IIR_MIN_DEPTH - 1, 0));
    TEST_ASSERT_EQUAL_INT(0, iir_reduction(12, tt_move));
}

void test_aspiration_search() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mate in 2 from a previous score of 0, the window has to widen all the way to the mate. */
    setup_position("r5k1/5ppp/8/8/8/8/4RPPP/4R1K1 w - - 0 1", &board, &search_state, &limits, 5);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, aspiration_search(&board, &search_state, &limits, 5, 0));

    /* A previous score far too high, the search fails low and widens down to the real score. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 5);
    int score = aspiration_search(&board, &search_state, &limits, 5, 800);
    TEST_ASSERT_TRUE(score > -200 && score < 200);
    TEST_ASSERT_EQUAL_UINT32(search_state.pv_table[0][0] != 0, 1);

    /* Shallow depth, full window, same as a plain full window search. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    int full = negamax(-INF, INF, 2, &board, &search_state, &limits);
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    TEST_ASSERT_EQUAL_INT(full, aspiration_search(&board, &search_state, &limits, 2, 800));
}

/* Doubled rooks on the e-file, Re8+ Rxe8 Rxe8#, mate in 2. */
#define SMP_MATE_IN_2_FEN "r5k1/5ppp/8/8/8/8/4RPPP/4R1K1 w - - 0 1"

void test_smp_search_finds_mate() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    setup_position(SMP_MATE_IN_2_FEN, &board, &search_state, &limits, 6);
    limits.threads = 4;
    move_t mv = find_best_move(&board, &search_state, &limits);

    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, search_state.score);
    TEST_ASSERT_EQUAL_INT(e2, get_source_square(mv));
    TEST_ASSERT_EQUAL_INT(e8, get_target_square(mv));
    /* The main thread finished, the helpers were stopped. */
    TEST_ASSERT_TRUE(limits.stop_search_flag);
}

void test_smp_matches_single_thread_move() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Back rank, Rd8#, every thread count must find it. */
    for (int threads = 1; threads <= 8; threads *= 2) {
        setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 5);
        limits.threads = threads;
        move_t mv = find_best_move(&board, &search_state, &limits);
        TEST_ASSERT_EQUAL_INT(d1, get_source_square(mv));
        TEST_ASSERT_EQUAL_INT(d8, get_target_square(mv));
    }
}

void test_smp_stopped_search_returns_legal_move() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Stopped before anything completes, helpers must exit and a legal move is still returned. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 20);
    limits.threads = 4;
    limits.stop_search_flag = true;
    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_NOT_EQUAL(0, mv);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_smp_time_limit_stops_helpers() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Already out of time, the main thread's first clock check stops every thread. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, MAX_SEARCH_DEPTH);
    limits.threads = 4;
    limits.time_set = true;
    limits.stop_time = 0;
    limits.soft_stop_time = UINT64_MAX;

    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_EQUAL_UINT64(1, limits.time_checks); /* Only the main thread checks the clock. */
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_pv_followed_only_on_pv_path() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* The previous iteration's PV is 1.e4 Nf6. Nf6 is also legal after White's other first moves,
     * but only the node after 1.e4 is on the PV: the root and that node order a PV move first.
     * The search after Nf6 is at depth 0 (qsearch), which used to leave the flag set for every
     * later sibling. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    search_state.pv_table[0][0] = parse_move("e2e4", &board);
    TEST_ASSERT_TRUE(make_move(&board, search_state.pv_table[0][0]));
    search_state.pv_table[0][1] = parse_move("g8f6", &board);
    pop_move(&board);
    search_state.follow_pv = true;
    negamax(-INF, INF, 2, &board, &search_state, &limits);
    TEST_ASSERT_EQUAL_UINT64(2, search_state.pv_nodes);

    /* Not following the PV, no node is on it. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    search_state.pv_table[0][0] = parse_move("e2e4", &board);
    search_state.follow_pv = false;
    negamax(-INF, INF, 2, &board, &search_state, &limits);
    TEST_ASSERT_EQUAL_UINT64(0, search_state.pv_nodes);
}








void test_pv_is_legal() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* The PV of every bench position is a line of legal moves from the root. */
    for (int i = 0; i < bench_position_count; i++) {
        setup_position(bench_positions[i], &board, &search_state, &limits, 6);
        limits.quiet = true;
        find_best_move(&board, &search_state, &limits);
        TEST_ASSERT_TRUE_MESSAGE(search_state.pv_length[0] > 0, bench_positions[i]);
        for (int ply = 0; ply < search_state.pv_length[0]; ply++) {
            move_buffer_t buffer;
            generate_moves(&board, &buffer);
            bool generated = false;
            for (int j = 0; j < buffer.index; j++) {
                if (buffer.moves[j] == search_state.pv_table[0][ply]) generated = true;
            }
            TEST_ASSERT_TRUE_MESSAGE(generated, bench_positions[i]);
            TEST_ASSERT_TRUE_MESSAGE(make_move(&board, search_state.pv_table[0][ply]), bench_positions[i]);
        }
    }
}

void test_mate_distance_scores() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mate in 2 (1.Nf6+ gxf6 2.Bxf7#), 3 plies from the root. */
    const char* fen = "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1";
    setup_position(fen, &board, &search_state, &limits, 5);
    limits.quiet = true;
    find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, search_state.score);

    /* After 1.Nf6+ Black is mated in 2 plies, whatever it plays. */
    tt_clear();
    init_search_state(&search_state);
    char command[256];
    snprintf(command, sizeof(command), "position fen %s moves d5f6", fen);
    parse_position(command, &board, &search_state);
    reset_time_control(&limits);
    limits.depth = 5;
    limits.quiet = true;
    find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE + 2, search_state.score);
}

void test_quiescence_stand_pat() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* No captures: the static eval inside the window; at or above beta the eval moved 519/1024 of the way
     * to beta (fail-soft, but damped). */
    setup_position("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const int eval = evaluation(&board, &search_state.nnue);
    TEST_ASSERT_EQUAL_INT(eval, quiescence(-INF, INF, &board, &search_state, &limits));
    const int beta = eval - 100;
    TEST_ASSERT_EQUAL_INT((eval * (1024 - QS_STAND_PAT_BLEND) + beta * QS_STAND_PAT_BLEND) / 1024,
                          quiescence(beta - 1, beta, &board, &search_state, &limits));
}

void test_quiescence_futility() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Nxd5 exd5 trades evenly (SEE 0, not >= 1): skipped when the stand pat plus QS_FUTILITY_MARGIN is at
     * or below alpha (only the qsearch node is counted), searched a point lower. */
    setup_position("4k3/8/4p3/3n4/8/4N3/8/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const int eval = evaluation(&board, &search_state.nnue);
    const int reach = eval + QS_FUTILITY_MARGIN;
    search_state.nodes = 0;
    quiescence(reach, reach + 1, &board, &search_state, &limits);
    TEST_ASSERT_EQUAL_UINT64(1, search_state.nodes);
    search_state.nodes = 0;
    quiescence(reach - 1, reach, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(search_state.nodes > 1);

    /* Nxd5 winning a pawn (SEE >= 1) is searched however far alpha is. */
    setup_position("4k3/8/8/3p4/8/4N3/8/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const int eval2 = evaluation(&board, &search_state.nnue);
    search_state.nodes = 0;
    quiescence(eval2 + 500, eval2 + 501, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(search_state.nodes > 1);
}

void test_quiescence_in_check() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mated, the qsearch used to stand pat. */
    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 1);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE, quiescence(-INF, INF, &board, &search_state, &limits));

    /* In check with a way out, ...Kf7: the quiet evasion is searched (the qsearch otherwise only
     * searches captures), so it isn't scored as mate. */
    setup_position("R5k1/6pp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 1);
    const int score = quiescence(-INF, INF, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(score > -MATE_SCORE && score < MATE_SCORE);
}

void test_fifty_move_rule_and_mate() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* The 100th half-move mates: a loss, not a draw. */
    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 100 80", &board, &search_state, &limits, 1);
    TEST_ASSERT_FALSE(is_draw(&board, &search_state));

    /* In check but not mated, or not in check: a draw. */
    setup_position("R5k1/6pp/8/8/8/8/5PPP/6K1 b - - 100 80", &board, &search_state, &limits, 1);
    TEST_ASSERT_TRUE(is_draw(&board, &search_state));
    setup_position("6k1/5ppp/8/8/8/8/5PPP/R5K1 b - - 100 80", &board, &search_state, &limits, 1);
    TEST_ASSERT_TRUE(is_draw(&board, &search_state));
}

void test_smp_helpers_grow_and_shrink() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* More helpers than before (a new, larger allocation), then fewer (reused). */
    const int threads[] = {2, 6, 3};
    for (int i = 0; i < 3; i++) {
        setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                       &board, &search_state, &limits, 6);
        limits.threads = threads[i];
        limits.quiet = true;
        const move_t mv = find_best_move(&board, &search_state, &limits);
        TEST_ASSERT_TRUE(make_move(&board, mv));
    }
}

void test_search_moves_restrict_root() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    const char* fen = "4kr2/8/8/3q4/8/8/3R4/4K3 w - - 0 1";

    /* Rxd5 wins the queen, the search plays it when it may. */
    setup_position(fen, &board, &search_state, &limits, 6);
    limits.quiet = true;
    parse_search_moves("go depth 6", &board, &limits);
    TEST_ASSERT_EQUAL_INT(0, limits.search_move_count);
    TEST_ASSERT_EQUAL_UINT32(parse_move("d2d5", &board), find_best_move(&board, &search_state, &limits));

    /* With 'searchmoves' it plays one of those. Ke1-f1 is illegal (the f8 rook) and dropped, and the list ends
     * at the next 'go' parameter. */
    setup_position(fen, &board, &search_state, &limits, 6);
    limits.quiet = true;
    parse_search_moves("go searchmoves e1f1 e1e2 e1d1 depth 6", &board, &limits);
    TEST_ASSERT_EQUAL_INT(2, limits.search_move_count);
    const move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(mv == parse_move("e1e2", &board) || mv == parse_move("e1d1", &board));
}

void test_node_limit_stops_search() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 'go nodes': stopped at the node that reaches the limit, whether or not it's a multiple of 2048 (the
     * batch check), so a budget below 2048 isn't overshot. */
    const uint64_t budgets[] = { 1000, 5000 };
    for (int i = 0; i < 2; i++) {
        setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                       &board, &search_state, &limits, MAX_SEARCH_DEPTH);
        limits.nodes = budgets[i];
        limits.quiet = true;
        const move_t mv = find_best_move(&board, &search_state, &limits);
        TEST_ASSERT_TRUE(limits.stop_search_flag);
        TEST_ASSERT_TRUE(search_state.nodes >= budgets[i] && search_state.nodes < budgets[i] + MAX_PLY);
        TEST_ASSERT_TRUE(make_move(&board, mv));
    }
}

void test_tt_lookup() {
    tt_clear();
    const uint64_t hash = 0x0123456789abcdefULL;
    const move_t stored = encode_move(g1, f3, N, 0, 0, 0, 0, 0, 0);
    move_t mv;
    int score, depth;
    tt_entry_type type;
    TEST_ASSERT_FALSE(tt_lookup(hash, 0, &mv, &score, &depth, &type));

    /* A shallow upper bound gives no cutoff at depth 8, but the lookup still returns it. */
    tt_save(hash, tt_alpha, 3, 0, -42, stored, TT_EVAL_NONE);
    move_t probe_move;
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(hash, 8, 0, -100, 100, &probe_move));
    TEST_ASSERT_TRUE(tt_lookup(hash, 0, &mv, &score, &depth, &type));
    TEST_ASSERT_EQUAL_UINT32(stored, mv);
    TEST_ASSERT_EQUAL_INT(-42, score);
    TEST_ASSERT_EQUAL_INT(3, depth);
    TEST_ASSERT_EQUAL_INT(tt_alpha, type);

    /* Mate scores come back relative to the root: mate 5 plies from the root found at ply 2 is
     * mate 3 plies from that node, so 7 plies from a root 4 plies above it. */
    tt_save(hash, tt_exact, 6, 2, MATE_VALUE - 5, stored, TT_EVAL_NONE);
    TEST_ASSERT_TRUE(tt_lookup(hash, 4, &mv, &score, &depth, &type));
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 7, score);
    TEST_ASSERT_EQUAL_INT(tt_exact, type);
}

void test_singular_extension_conditions() {
    /* From depth 3, a lower-bound or exact entry at most 3 plies shallower, not a mate score. */
    TEST_ASSERT_TRUE(can_singular_extend(8, 6, tt_beta, 50));
    TEST_ASSERT_TRUE(can_singular_extend(8, 8 - SE_TT_DEPTH_MARGIN, tt_exact, 50));
    TEST_ASSERT_TRUE(can_singular_extend(SE_MIN_DEPTH, SE_MIN_DEPTH, tt_beta, 50));
    TEST_ASSERT_FALSE(can_singular_extend(SE_MIN_DEPTH - 1, 6, tt_beta, 50));
    TEST_ASSERT_FALSE(can_singular_extend(8, 8 - SE_TT_DEPTH_MARGIN - 1, tt_beta, 50)); /* entry too shallow */
    TEST_ASSERT_FALSE(can_singular_extend(8, 6, tt_alpha, 50));                         /* only an upper bound */
    TEST_ASSERT_FALSE(can_singular_extend(8, 6, tt_none, 50));                          /* the eval only */
    TEST_ASSERT_FALSE(can_singular_extend(8, 6, tt_beta, MATE_VALUE - 10));

    /* The margin: depth units for a bound, (depth * 48 + 127) / 128 for an exact entry: 10 and 4 at depth 10. */
    TEST_ASSERT_EQUAL_INT(10, singular_margin_ref(10, false));
    TEST_ASSERT_EQUAL_INT(4, singular_margin_ref(10, true));
    /* s_beta: 10 units are 6.0 cp, 100 - 6 = 94; 4 units are 2.4 cp, and score < 97.6 is score < 98. */
    TEST_ASSERT_EQUAL_INT(94, singular_beta(100, 10));
    TEST_ASSERT_EQUAL_INT(98, singular_beta(100, 4));
    /* The search depth: max(1, (59 * depth - 215) / 128). */
    TEST_ASSERT_EQUAL_INT(1, singular_depth(3));
    TEST_ASSERT_EQUAL_INT(2, singular_depth(8));
    TEST_ASSERT_EQUAL_INT(3, singular_depth(12));
    TEST_ASSERT_EQUAL_INT(7, singular_depth(20));
}

void test_singular_result() {
    /* TT score 100, margin 10 units (6.0 cp): s_beta 94, in tenths 940; +2 below 940 - 84, +3 below 940 - 306. */
    TEST_ASSERT_EQUAL_INT(1, singular_extension(93, 100, 10, 0, 50, false, false, true));
    TEST_ASSERT_EQUAL_INT(1, singular_extension(86, 100, 10, 0, 50, false, false, true)); /* 860 > 856 */
    TEST_ASSERT_EQUAL_INT(2, singular_extension(85, 100, 10, 0, 50, false, false, true)); /* 850 < 856 */
    TEST_ASSERT_EQUAL_INT(2, singular_extension(64, 100, 10, 0, 50, false, false, true)); /* 640 > 634 */
    TEST_ASSERT_EQUAL_INT(3, singular_extension(63, 100, 10, 0, 50, false, false, true)); /* 630 < 634 */
    /* At PV nodes both thresholds are SE_PV_BONUS (153.6 cp) further. */
    TEST_ASSERT_EQUAL_INT(1, singular_extension(63, 100, 10, 0, 50, true, false, true));
    TEST_ASSERT_EQUAL_INT(2, singular_extension(-70, 100, 10, 0, 50, true, false, true)); /* -700 < -680 */
    /* Past ply 2 * root depth, singular but not extended. */
    TEST_ASSERT_EQUAL_INT(0, singular_extension(63, 100, 10, 0, 50, false, false, false));
    /* Not singular (score 94 reaches s_beta): the TT score at or above beta, -2 at PV nodes, -3 outside. */
    TEST_ASSERT_EQUAL_INT(-3, singular_extension(94, 100, 10, 0, 90, false, false, true));
    TEST_ASSERT_EQUAL_INT(-2, singular_extension(94, 100, 10, 0, 90, true, false, true));
    /* Below beta: -2 at a cut node; otherwise -1 if the TT score is at or below alpha, else 0. */
    TEST_ASSERT_EQUAL_INT(-2, singular_extension(94, 100, 10, 0, 150, false, true, true));
    TEST_ASSERT_EQUAL_INT(-1, singular_extension(94, 100, 10, 100, 150, false, false, true));
    TEST_ASSERT_EQUAL_INT(0, singular_extension(94, 100, 10, 50, 150, false, false, true));
}

void test_excluded_move_search() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    move_t mv;
    int score, depth;
    tt_entry_type type;

    /* Rd8# is the only mate in one. Without it the node fails low against a mate in one window,
     * with it it fails high. Fail-soft: the fail low returns the best score found, at or below alpha. */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 3);
    const move_t rd8 = parse_move("d1d8", &board);
    search_state.excluded_moves[0] = rd8;
    TEST_ASSERT_LESS_OR_EQUAL_INT(MATE_VALUE - 2, negamax(MATE_VALUE - 2, MATE_VALUE - 1, 3, &board, &search_state, &limits));
    /* A search without a move isn't the node's result, so it leaves nothing in the TT. */
    TEST_ASSERT_FALSE(tt_lookup(board.hash, 0, &mv, &score, &depth, &type));
    search_state.excluded_moves[0] = 0;
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 1, negamax(MATE_VALUE - 2, MATE_VALUE - 1, 3, &board, &search_state, &limits));

    /* Kxg2 is white's only legal move. Without it there's nothing left to play, which is not mate
     * or stalemate: the node fails low. */
    setup_position("7k/8/8/8/8/8/6q1/7K w - - 0 1", &board, &search_state, &limits, 4);
    search_state.excluded_moves[0] = parse_move("h1g2", &board);
    TEST_ASSERT_EQUAL_INT(-50, negamax(-50, -49, 4, &board, &search_state, &limits));
    TEST_ASSERT_FALSE(tt_lookup(board.hash, 0, &mv, &score, &depth, &type));
    search_state.excluded_moves[0] = 0;
    /* With it, the queen is won (a draw, 0): fail-soft returns that, not beta. */
    TEST_ASSERT_EQUAL_INT(0, negamax(-50, -49, 4, &board, &search_state, &limits));
}

void test_probcut_conditions() {
    /* The bound: beta plus the margin, less when improving. */
    TEST_ASSERT_EQUAL_INT(30 + PROBCUT_MARGIN, probcut_beta(30, false));
    TEST_ASSERT_EQUAL_INT(30 + PROBCUT_MARGIN - PROBCUT_IMPROVING, probcut_beta(30, true));
    TEST_ASSERT_TRUE(PROBCUT_IMPROVING < PROBCUT_MARGIN);
    /* The reduced search has a depth. */
    TEST_ASSERT_TRUE(PROBCUT_MIN_DEPTH > PROBCUT_REDUCTION);

    const int d = PROBCUT_MIN_DEPTH;
    const int pc_beta = probcut_beta(0, false);
    /* From the minimum depth, at a node that isn't the root, a PV node or in check, without a TT entry. */
    TEST_ASSERT_TRUE(can_probcut(d, 1, 0, false, false, false, 0, tt_none, 0, pc_beta));
    TEST_ASSERT_FALSE(can_probcut(d - 1, 1, 0, false, false, false, 0, tt_none, 0, pc_beta));
    TEST_ASSERT_FALSE(can_probcut(d, 0, 0, false, false, false, 0, tt_none, 0, pc_beta));
    TEST_ASSERT_FALSE(can_probcut(d, 1, 0, true, false, false, 0, tt_none, 0, pc_beta));
    TEST_ASSERT_FALSE(can_probcut(d, 1, 0, false, true, false, 0, tt_none, 0, pc_beta));
    /* Not when beta is a mate score. */
    TEST_ASSERT_FALSE(can_probcut(d, 1, MATE_VALUE - 10, false, false, false, 0, tt_none, 0, MATE_VALUE));
    TEST_ASSERT_FALSE(can_probcut(d, 1, -MATE_VALUE + 10, false, false, false, 0, tt_none, 0, pc_beta));

    /* A cutoff is worth the reduced search plus the move made before it. */
    const int saved = PROBCUT_TT_DEPTH(d);
    TEST_ASSERT_EQUAL_INT(d - PROBCUT_REDUCTION + 1, saved);

    /* A TT entry at least that deep with a score below the bound: the try would fail. */
    TEST_ASSERT_FALSE(can_probcut(d, 1, 0, false, false, true, saved, tt_alpha, pc_beta - 1, pc_beta));
    TEST_ASSERT_FALSE(can_probcut(d, 1, 0, false, false, true, d, tt_exact, pc_beta - 1, pc_beta));
    /* A score at the bound, a shallower entry, or one holding only the eval doesn't stop it. */
    TEST_ASSERT_TRUE(can_probcut(d, 1, 0, false, false, true, saved, tt_beta, pc_beta, pc_beta));
    TEST_ASSERT_TRUE(can_probcut(d, 1, 0, false, false, true, saved - 1, tt_alpha, pc_beta - 1, pc_beta));
    TEST_ASSERT_TRUE(can_probcut(d, 1, 0, false, false, true, saved, tt_none, 0, pc_beta));
}

void test_tunable_parameters() {
#ifdef TIKI_TUNE
    /* A tuning build: the parameters are variables, set by name (any case) and kept in range. */
    TEST_ASSERT_EQUAL_INT(117, tunable_count);
    const int saved = RFP_MARGIN;
    TEST_ASSERT_TRUE(set_tunable("rfp_margin", 10, 120));
    TEST_ASSERT_EQUAL_INT(120, RFP_MARGIN);
    TEST_ASSERT_TRUE(set_tunable("RFP_MARGIN", 10, 100000));
    TEST_ASSERT_EQUAL_INT(150, RFP_MARGIN);
    TEST_ASSERT_TRUE(set_tunable("RFP_MARGIN", 10, saved));
    TEST_ASSERT_EQUAL_INT(saved, RFP_MARGIN);
    /* The whole name must match. */
    TEST_ASSERT_FALSE(set_tunable("RFP_MARGI", 9, 120));
    /* Each default is inside its range. */
    for (int i = 0; i < tunable_count; i++) {
        TEST_ASSERT_TRUE(*tunables[i].value >= tunables[i].min && *tunables[i].value <= tunables[i].max);
    }
#else
    /* A normal build: the parameters are constants, there's nothing to set. */
    TEST_ASSERT_EQUAL_INT(0, tunable_count);
    TEST_ASSERT_FALSE(set_tunable("RFP_MARGIN", 10, 120));
#endif
}

void test_correction_history_learns_without_overshoot() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    setup_position("4k3/pp6/8/3p4/3P4/8/PP6/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    TEST_ASSERT_EQUAL_INT(0, eval_correction(&board, &search_state));

    /* The search keeps finding the position 40 cp better than its corrected eval: each update moves
     * the correction towards 40, never past it, and it gets there. */
    const int target = 40;
    int previous = 0;
    for (int i = 0; i < 40; i++) {
        update_correction(&search_state, &board, CORR_DEPTH_MAX, target - eval_correction(&board, &search_state));
        const int c = eval_correction(&board, &search_state);
        TEST_ASSERT_TRUE(c >= previous);
        TEST_ASSERT_TRUE(c <= target);
        previous = c;
    }
    TEST_ASSERT_TRUE(previous >= target - 2);

    /* Shallow results move it less than deep ones. */
    setup_position("4k3/pp6/8/3p4/3P4/8/PP6/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    update_correction(&search_state, &board, 1, 100);
    const int shallow = eval_correction(&board, &search_state);
    setup_position("4k3/pp6/8/3p4/3P4/8/PP6/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    update_correction(&search_state, &board, CORR_DEPTH_MAX, 100);
    TEST_ASSERT_TRUE(shallow > 0);
    TEST_ASSERT_TRUE(eval_correction(&board, &search_state) > shallow);
    /* 2 * CORR_DEPTH_MAX / CORR_RATE of the error (about half), rounding aside. */
    TEST_ASSERT_TRUE(eval_correction(&board, &search_state) <= 100 * 2 * CORR_DEPTH_MAX / CORR_RATE + 1);

    /* However large the errors, within +-CORR_MAX. */
    for (int i = 0; i < 200; i++) update_correction(&search_state, &board, CORR_DEPTH_MAX, 10000);
    TEST_ASSERT_EQUAL_INT(CORR_MAX, eval_correction(&board, &search_state));
    for (int i = 0; i < 400; i++) update_correction(&search_state, &board, CORR_DEPTH_MAX, -10000);
    TEST_ASSERT_EQUAL_INT(-CORR_MAX, eval_correction(&board, &search_state));
}

void test_correction_history_keys() {
    board_t board, swapped, other_side, other_piece;
    search_state_t search_state;
    limits_t limits;
    setup_position("4k3/8/8/3p4/3P4/8/2N5/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    /* The same squares with the pawns' colours swapped, the other side to move, a bishop for the knight. */
    parse_fen("4k3/8/8/3P4/3p4/8/2N5/4K3 w - - 0 1", &swapped);
    parse_fen("4k3/8/8/3p4/3P4/8/2N5/4K3 b - - 0 1", &other_side);
    parse_fen("4k3/8/8/3p4/3P4/8/2B5/4K3 w - - 0 1", &other_piece);
    update_correction(&search_state, &board, CORR_DEPTH_MAX, 60);
    TEST_ASSERT_TRUE(eval_correction(&board, &search_state) > 0);

    /* Swapped pawns: neither the pawn entry nor the pieces' (white's and black's are unchanged, the
     * pawn structure isn't), only the two piece entries are shared, so less than the full correction. */
    TEST_ASSERT_TRUE(eval_correction(&swapped, &search_state) < eval_correction(&board, &search_state));
    /* The other side to move has its own tables. */
    TEST_ASSERT_EQUAL_INT(0, eval_correction(&other_side, &search_state));
    /* A bishop instead of the knight: white's piece entry differs, the pawns and black's are shared. */
    TEST_ASSERT_TRUE(eval_correction(&other_piece, &search_state) < eval_correction(&board, &search_state));
}

/* The Lazy SMP vote (vote_best_thread) on made-up thread results: moves 1, 2, 3 stand for real ones. */
void test_vote_best_thread() {
    /* No thread completed a depth; one that did is chosen. */
    const thread_result_t none[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
    TEST_ASSERT_EQUAL_INT(-1, vote_best_thread(none, 2));
    const thread_result_t only_helper[2] = { { 0, 0, 0 }, { 2, 10, 5 } };
    TEST_ASSERT_EQUAL_INT(1, vote_best_thread(only_helper, 2));

    /* Two deeper, better scored helpers outvote the main thread and its agreeing helper. */
    const thread_result_t outvoted[4] = { { 1, 30, 14 }, { 2, 45, 15 }, { 1, 28, 14 }, { 2, 40, 15 } };
    TEST_ASSERT_EQUAL_INT(1, vote_best_thread(outvoted, 4));
    /* The same scores at the same depth: the main thread's move has more votes. */
    const thread_result_t agreed[4] = { { 1, 30, 14 }, { 2, 32, 14 }, { 1, 30, 14 }, { 1, 30, 14 } };
    TEST_ASSERT_EQUAL_INT(0, vote_best_thread(agreed, 4));
    /* A tie in votes and weight keeps the main thread's move. */
    const thread_result_t tie[2] = { { 1, 20, 10 }, { 2, 20, 10 } };
    TEST_ASSERT_EQUAL_INT(0, vote_best_thread(tie, 2));
    /* On a tie in votes, the thread with the higher weight: move 2's single deeper vote beats it. */
    const thread_result_t tie_votes[3] = { { 1, 0, 10 }, { 1, 0, 10 }, { 2, 0, 20 } };
    TEST_ASSERT_EQUAL_INT(2, vote_best_thread(tie_votes, 3));

    /* A win beats any number of votes, the fastest win among them. */
    const thread_result_t win[4] = { { 1, 50, 20 }, { 1, 50, 20 }, { 2, MATE_VALUE - 9, 12 }, { 3, MATE_VALUE - 5, 10 } };
    TEST_ASSERT_EQUAL_INT(3, vote_best_thread(win, 4));
    /* A thread that sees itself lost isn't chosen over one that doesn't, however it votes... */
    const thread_result_t lost_helper[3] = { { 1, 0, 10 }, { 2, -(MATE_VALUE - 7), 30 }, { 2, -(MATE_VALUE - 7), 30 } };
    TEST_ASSERT_EQUAL_INT(0, vote_best_thread(lost_helper, 3));
    /* ...a lost main thread gives way to one that isn't... */
    const thread_result_t lost_main[2] = { { 1, -(MATE_VALUE - 7), 30 }, { 2, -40, 10 } };
    TEST_ASSERT_EQUAL_INT(1, vote_best_thread(lost_main, 2));
    /* ...and when every thread is lost, the slowest loss. */
    const thread_result_t all_lost[3] = { { 1, -(MATE_VALUE - 5), 10 }, { 2, -(MATE_VALUE - 11), 12 },
                                          { 3, -(MATE_VALUE - 9), 12 } };
    TEST_ASSERT_EQUAL_INT(1, vote_best_thread(all_lost, 3));
}
