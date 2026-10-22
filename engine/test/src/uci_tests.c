#include "unity.h"
#include "uci_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/uci.h"


void test_uci_command_tokens() {
    /* 'ucinewgame' must not be matched as 'uci'. */
    TEST_ASSERT_EQUAL_INT(uci_cmd_uci, parse_uci_command("uci"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_ucinewgame, parse_uci_command("ucinewgame"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_isready, parse_uci_command("isready"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_position, parse_uci_command("position startpos moves e2e4"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_go, parse_uci_command("go infinite"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_go, parse_uci_command("go"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_stop, parse_uci_command("stop"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_quit, parse_uci_command("quit"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_unknown, parse_uci_command("gopher"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_unknown, parse_uci_command("stopped"));
    TEST_ASSERT_EQUAL_INT(uci_cmd_unknown, parse_uci_command(""));
}

void test_uci_stop_does_not_quit() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);

    /* 'stop' with no search running is a no-op, the engine keeps running. */
    TEST_ASSERT_TRUE(handle_uci_command("stop", &board, &search_state, &limits));
    TEST_ASSERT_FALSE(limits.stop_engine_flag);
}

void test_uci_quit_exits() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);

    TEST_ASSERT_FALSE(handle_uci_command("quit", &board, &search_state, &limits));
    TEST_ASSERT_TRUE(limits.stop_engine_flag);
}

void test_uci_newgame_resets_position() {
    board_t board;
    board_t expected;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);

    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &expected);

    TEST_ASSERT_TRUE(handle_uci_command("position startpos moves g1f3 g8f6", &board, &search_state, &limits));
    TEST_ASSERT_NOT_EQUAL(expected.hash, board.hash);
    TEST_ASSERT_EQUAL_UINT16(2, search_state.repetition_index);

    TEST_ASSERT_TRUE(handle_uci_command("ucinewgame", &board, &search_state, &limits));
    TEST_ASSERT_EQUAL_UINT64(expected.hash, board.hash);
    TEST_ASSERT_EQUAL_INT(white, board.side);
    TEST_ASSERT_EQUAL_UINT16(0, search_state.repetition_index);
}

void test_go_infinite_limits() {
    board_t board;
    limits_t limits;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    /* Clock values are ignored with 'infinite', only 'stop' ends the search. */
    parse_go_limits("go infinite wtime 1000 btime 1000", &board, &limits);
    TEST_ASSERT_TRUE(limits.infinite);
    TEST_ASSERT_FALSE(limits.time_set);
    TEST_ASSERT_FALSE(limits.stop_search_flag);
    TEST_ASSERT_FALSE(limits.stop_engine_flag);
}

void test_go_clock_limits() {
    board_t board;
    limits_t limits;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    parse_go_limits("go wtime 60000 btime 60000 movestogo 10", &board, &limits);
    TEST_ASSERT_FALSE(limits.infinite);
    TEST_ASSERT_TRUE(limits.time_set);
    TEST_ASSERT_EQUAL_UINT64(limits.start_time + 60000 / 10 - 55, limits.stop_time);
}

void test_search_input_stop() {
    limits_t limits;
    reset_time_control(&limits);

    handle_search_input(&limits, "stop");
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_FALSE(limits.stop_engine_flag);
}

void test_search_input_quit() {
    limits_t limits;
    reset_time_control(&limits);

    handle_search_input(&limits, "quit");
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_TRUE(limits.stop_engine_flag);
}

void test_search_input_isready() {
    limits_t limits;
    reset_time_control(&limits);

    /* 'isready' is answered without interrupting the search. */
    handle_search_input(&limits, "isready");
    TEST_ASSERT_FALSE(limits.stop_search_flag);
    TEST_ASSERT_FALSE(limits.stop_engine_flag);
}
