#include "unity.h"
#include "search_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/search.h"
#include "../../core/src/transposition.h"
#include "../../core/src/uci.h"
#include "../../core/src/bench.h"


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
