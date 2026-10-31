#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"
#include "input_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/search.h"
#include "../../core/src/uci.h"
#include "../../core/src/uci_input.h"


static int write_fd = -1;

/* Start the input thread on a pipe, the test writes commands to 'write_fd'. */
static void start_pipe_input() {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    write_fd = fds[1];
    input_start(fdopen(fds[0], "r"));
}

static void send_input(const char* lines) {
    TEST_ASSERT_EQUAL_INT((int) strlen(lines), (int) write(write_fd, lines, strlen(lines)));
}

static void close_input() {
    close(write_fd);
    write_fd = -1;
}

static void assert_pop(const char* expected) {
    char* line = input_pop();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING(expected, line);
    free(line);
}

/* The input thread runs concurrently, wait (up to 2s) for it to catch up. */
static void wait_for_pending(size_t n) {
    for (int i = 0; i < 2000 && input_pending() != n; i++) usleep(1000);
    TEST_ASSERT_EQUAL_size_t(n, input_pending());
}

static void wait_for_stop_flag(limits_t* limits) {
    for (int i = 0; i < 2000 && !limits->stop_search_flag; i++) usleep(1000);
    TEST_ASSERT_TRUE(limits->stop_search_flag);
}

/* End of input becomes 'quit', then the queue reports closed. */
static void finish_input() {
    close_input();
    assert_pop("quit");
    TEST_ASSERT_NULL(input_pop());
    input_join();
}


void test_input_queues_commands_in_order() {
    start_pipe_input();
    send_input("position startpos\nisready\ngo depth 1\n");

    assert_pop("position startpos");
    assert_pop("isready");
    assert_pop("go depth 1");
    finish_input();
}

void test_input_stop_during_search() {
    limits_t limits;
    reset_time_control(&limits);
    start_pipe_input();

    input_search_started(&limits);
    send_input("isready\nstop\n");
    wait_for_stop_flag(&limits);

    /* Handled on the input thread, not queued. */
    TEST_ASSERT_EQUAL_size_t(0, input_pending());
    TEST_ASSERT_FALSE(limits.stop_engine_flag);

    input_search_finished();
    finish_input();
}

void test_input_quit_during_search() {
    limits_t limits;
    reset_time_control(&limits);
    start_pipe_input();

    input_search_started(&limits);
    send_input("quit\n");
    wait_for_stop_flag(&limits);
    TEST_ASSERT_TRUE(limits.stop_engine_flag);
    input_search_finished();

    /* The input thread stops reading after 'quit'. */
    close_input();
    TEST_ASSERT_NULL(input_pop());
    input_join();
}

void test_input_commands_during_search_are_kept() {
    limits_t limits;
    reset_time_control(&limits);
    start_pipe_input();

    input_search_started(&limits);
    send_input("position startpos moves e2e4\ngo depth 1\n");
    wait_for_pending(2);
    TEST_ASSERT_FALSE(limits.stop_search_flag);
    input_search_finished();

    assert_pop("position startpos moves e2e4");
    assert_pop("go depth 1");
    finish_input();
}

void test_input_stop_queued_before_search() {
    limits_t limits;
    reset_time_control(&limits);
    start_pipe_input();

    /* 'stop' read before the UCI loop has started the search, it still applies to that search. */
    send_input("go infinite\nstop\nposition startpos\n");
    assert_pop("go infinite");
    wait_for_pending(2);

    input_search_started(&limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_EQUAL_size_t(1, input_pending());
    input_search_finished();

    assert_pop("position startpos");
    finish_input();
}

void test_input_stop_for_later_search() {
    limits_t limits;
    reset_time_control(&limits);
    start_pipe_input();

    /* The 'stop' belongs to the second 'go'. */
    send_input("go depth 1\ngo infinite\nstop\n");
    assert_pop("go depth 1");
    wait_for_pending(2);

    input_search_started(&limits);
    TEST_ASSERT_FALSE(limits.stop_search_flag);
    input_search_finished();

    assert_pop("go infinite");
    input_search_started(&limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    input_search_finished();

    finish_input();
}

void test_input_wait_for_stop() {
    limits_t limits;
    reset_time_control(&limits);
    start_pipe_input();

    input_search_started(&limits);
    send_input("stop\n");
    input_wait_for_stop(&limits); /* Returns once the input thread has read 'stop'. */
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    input_search_finished();

    finish_input();
}

void test_uci_loop_keeps_commands_during_search() {
    board_t board, expected;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);

    /* All sent at once, the second 'position'/'go' arrive while the first search runs. */
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    const char* commands = "position startpos\ngo depth 3\nposition startpos moves e2e4\ngo depth 2\nquit\n";
    TEST_ASSERT_EQUAL_INT((int) strlen(commands), (int) write(fds[1], commands, strlen(commands)));
    close(fds[1]);

    uci_loop(fdopen(fds[0], "r"), &board, &search_state, &limits);

    search_state_t scratch;
    init_search_state(&scratch);
    parse_position("position startpos moves e2e4", &expected, &scratch);
    TEST_ASSERT_EQUAL_UINT64(expected.hash, board.hash);
    TEST_ASSERT_TRUE(limits.stop_engine_flag);
}

void test_uci_loop_go_infinite_stop() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);

    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    const char* commands = "position startpos\ngo infinite\nstop\nquit\n";
    TEST_ASSERT_EQUAL_INT((int) strlen(commands), (int) write(fds[1], commands, strlen(commands)));
    close(fds[1]);

    /* Would hang if 'stop' were lost. */
    uci_loop(fdopen(fds[0], "r"), &board, &search_state, &limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
}

void test_soft_time_limit() {
    board_t board;
    limits_t limits;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);

    /* A clock: soft and hard limits from the base time (limits.h), the soft one scaled in the search. */
    parse_go_limits("go wtime 60000 btime 60000", &board, &limits);
    TEST_ASSERT_TRUE(limits.time_set);
    TEST_ASSERT_TRUE(limits.adaptive);
    const uint64_t base = (60000 - MOVE_OVERHEAD_DEFAULT) / TM_MOVES_LEFT;
    TEST_ASSERT_EQUAL_UINT64(limits.start_time + base * TM_SOFT_PERCENT / 100, limits.soft_stop_time);
    TEST_ASSERT_EQUAL_UINT64(limits.start_time + base * TM_HARD_PERCENT / 100, limits.stop_time);
    TEST_ASSERT_TRUE(limits.soft_stop_time < limits.stop_time);

    /* 'movetime' isn't scaled, every depth started in time is searched. */
    parse_go_limits("go movetime 1000", &board, &limits);
    TEST_ASSERT_FALSE(limits.adaptive);
    TEST_ASSERT_EQUAL_UINT64(limits.stop_time, limits.soft_stop_time);
}

void test_stopped_search_returns_legal_move() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);
    limits.depth = 6;

    /* Stopped before depth 1 completes, used to return 0 ('a8a8'). */
    parse_position("position startpos", &board, &search_state);
    limits.stop_search_flag = true;
    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_NOT_EQUAL(0, mv);
    TEST_ASSERT_TRUE(make_move(&board, mv));
    pop_move(&board);

    /* Stalemate, no legal move. */
    parse_position("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", &board, &search_state);
    TEST_ASSERT_EQUAL_UINT32(0, first_legal_move(&board));
}

void test_input_stop_after_queued_go() {
    limits_t current, next;
    reset_time_control(&current);
    reset_time_control(&next);
    start_pipe_input();

    /* Read during a search, but the 'stop' is for the queued 'go', not the running search. */
    input_search_started(&current);
    send_input("go infinite\nstop\n");
    wait_for_pending(2);
    TEST_ASSERT_FALSE(current.stop_search_flag);
    input_search_finished();

    assert_pop("go infinite");
    input_search_started(&next);
    TEST_ASSERT_TRUE(next.stop_search_flag);
    input_search_finished();

    finish_input();
}
