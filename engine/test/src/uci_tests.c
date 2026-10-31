#include <stdio.h>

#include "unity.h"
#include "uci_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/uci.h"
#include "../../core/src/search.h"
#include "../../core/src/transposition.h"


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
    TEST_ASSERT_EQUAL_INT(uci_cmd_setoption, parse_uci_command("setoption name Hash value 64"));
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

void test_go_nodes_limits() {
    board_t board;
    limits_t limits;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    /* A node limit with no depth or clock: searched to the depth limit until the nodes run out. */
    parse_go_limits("go nodes 5000", &board, &limits);
    TEST_ASSERT_EQUAL_UINT64(5000, limits.nodes);
    TEST_ASSERT_EQUAL_INT(MAX_SEARCH_DEPTH, limits.depth);
    TEST_ASSERT_FALSE(limits.time_set);

    /* No limit unless given. */
    parse_go_limits("go depth 5", &board, &limits);
    TEST_ASSERT_EQUAL_UINT64(0, limits.nodes);
}

void test_go_clock_limits() {
    board_t board;
    limits_t limits;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    parse_go_limits("go wtime 60000 btime 60000 movestogo 10", &board, &limits);
    TEST_ASSERT_FALSE(limits.infinite);
    TEST_ASSERT_TRUE(limits.time_set);
    /* 'movestogo' divides the clock by the moves left in the time control instead of the estimate. */
    TEST_ASSERT_EQUAL_UINT64(limits.start_time + (60000 - MOVE_OVERHEAD_DEFAULT) / 10 * TM_HARD_PERCENT / 100,
                             limits.stop_time);
}

/* The time a 'go' command allows the side to move (white, from the start position). */
static uint64_t move_budget(const char* command) {
    board_t board;
    limits_t limits;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    parse_go_limits(command, &board, &limits);
    TEST_ASSERT_TRUE(limits.time_set);
    return limits.stop_time - limits.start_time;
}

void test_go_low_clock_limits() {
    const uint64_t overhead = MOVE_OVERHEAD_DEFAULT;

    /* Bullet: the hard limit, a multiple of the base time (a share of the clock less the overhead, plus
     * most of the increment). */
    TEST_ASSERT_EQUAL_UINT64(((1000 - overhead) / TM_MOVES_LEFT + 10 * TM_INCREMENT_PERCENT / 100) * TM_HARD_PERCENT / 100,
                             move_budget("go wtime 1000 btime 1000 winc 10 binc 10"));

    /* A low clock: never more than half of what's left, whatever the increment. It used to get 55 ms
     * here with a 10 ms increment, and lost on time. */
    TEST_ASSERT_EQUAL_UINT64((40 - overhead) / 2, move_budget("go wtime 40 btime 40 winc 10 binc 10"));
    TEST_ASSERT_EQUAL_UINT64((1000 - overhead) / 2, move_budget("go wtime 1000 btime 1000 winc 5000 binc 5000"));

    /* Less than the overhead left: a moment, the search still returns a move. */
    TEST_ASSERT_EQUAL_UINT64(1, move_budget("go wtime 10 btime 10 winc 10 binc 10"));

    /* The last move before the time control can use all of it, 'movestogo 0' is taken as 1. */
    TEST_ASSERT_EQUAL_UINT64(1000 - overhead, move_budget("go wtime 1000 btime 1000 movestogo 1"));
    TEST_ASSERT_EQUAL_UINT64(1000 - overhead, move_budget("go wtime 1000 btime 1000 movestogo 0"));

    /* 'movetime' uses all of it. */
    TEST_ASSERT_EQUAL_UINT64(1000 - overhead, move_budget("go movetime 1000"));
}

void test_move_overhead_option() {
    parse_setoption("setoption name Move Overhead value 100");
    TEST_ASSERT_EQUAL_UINT64(1000 - 100, move_budget("go movetime 1000"));
    parse_setoption("setoption name Move Overhead value 999999");
    TEST_ASSERT_EQUAL_UINT64(1, move_budget("go movetime 1000"));     /* Capped at MOVE_OVERHEAD_MAX. */

    char command[64];
    snprintf(command, sizeof(command), "setoption name Move Overhead value %d", MOVE_OVERHEAD_DEFAULT);
    parse_setoption(command);
    TEST_ASSERT_EQUAL_UINT64(1000 - MOVE_OVERHEAD_DEFAULT, move_budget("go movetime 1000"));
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

void test_setoption_hash() {
    parse_setoption("setoption name Hash value 4");
    TEST_ASSERT_EQUAL_size_t(4 * 0x100000, tt_size * sizeof(transposition_node_t));

    /* Not a power of two, rounded down. */
    parse_setoption("setoption name hash value 3");
    TEST_ASSERT_EQUAL_size_t(2 * 0x100000, tt_size * sizeof(transposition_node_t));

    /* Out of range, clamped to 1MB. */
    parse_setoption("setoption name Hash value 0");
    TEST_ASSERT_EQUAL_size_t(0x100000, tt_size * sizeof(transposition_node_t));
}

void test_setoption_threads() {
    parse_setoption("setoption name Threads value 4");
    TEST_ASSERT_EQUAL_INT(4, get_search_threads());

    parse_setoption("setoption name threads value 0");
    TEST_ASSERT_EQUAL_INT(1, get_search_threads());

    parse_setoption("setoption name Threads value 100000");
    TEST_ASSERT_EQUAL_INT(MAX_SEARCH_THREADS, get_search_threads());

    /* Unknown options and a missing value are ignored. */
    parse_setoption("setoption name Threads");
    parse_setoption("setoption name No Such Option value 10");
    TEST_ASSERT_EQUAL_INT(MAX_SEARCH_THREADS, get_search_threads());

    parse_setoption("setoption name Threads value 1");
    TEST_ASSERT_EQUAL_INT(1, get_search_threads());
}
