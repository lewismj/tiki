#include <string.h>

#include "unity.h"
#include "repetition_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/search.h"
#include "../../core/src/uci.h"

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"


static uint64_t start_hash() {
    board_t board;
    parse_fen(START_FEN, &board);
    return board.hash;
}

void test_position_resets_history() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    /* GUIs send the whole game with each 'position', history must not be appended twice. */
    parse_position("position startpos moves g1f3 g8f6", &board, &search_state);
    TEST_ASSERT_EQUAL_UINT16(2, search_state.repetition_index);
    parse_position("position startpos moves g1f3 g8f6", &board, &search_state);
    TEST_ASSERT_EQUAL_UINT16(2, search_state.repetition_index);
}

void test_position_records_root() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    parse_position("position startpos moves g1f3 g8f6", &board, &search_state);
    TEST_ASSERT_EQUAL_UINT64(start_hash(), search_state.repetition_check[0]);
    TEST_ASSERT_EQUAL_UINT64(board.hash, search_state.repetition_check[search_state.repetition_index]);

    parse_position("position startpos", &board, &search_state);
    TEST_ASSERT_EQUAL_UINT16(0, search_state.repetition_index);
    TEST_ASSERT_EQUAL_UINT64(start_hash(), search_state.repetition_check[0]);
}

void test_position_irreversible_move_clears_history() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    parse_position("position startpos moves g1f3 g8f6 e2e4", &board, &search_state);
    TEST_ASSERT_EQUAL_UINT16(0, search_state.repetition_index);
    TEST_ASSERT_EQUAL_UINT64(board.hash, search_state.repetition_check[0]);
}

void test_position_detects_game_repetition() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    /* Knights out and back, the start position has occurred before. */
    parse_position("position startpos moves g1f3 g8f6 f3g1 f6g8", &board, &search_state);
    TEST_ASSERT_EQUAL_UINT64(start_hash(), board.hash);
    TEST_ASSERT_TRUE(contains_repetition(&search_state, board.hash));
    TEST_ASSERT_TRUE(is_draw(&board, &search_state));

    parse_position("position startpos moves g1f3 g8f6", &board, &search_state);
    TEST_ASSERT_FALSE(is_draw(&board, &search_state));
}

void test_position_long_history() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    /* 300 reversible plies, beyond the range of the old uint8_t index. */
    static char command[4096];
    strcpy(command, "position startpos moves");
    for (int i = 0; i < 75; i++) strcat(command, " g1f3 g8f6 f3g1 f6g8");

    parse_position(command, &board, &search_state);
    TEST_ASSERT_EQUAL_UINT16(300, search_state.repetition_index);
    TEST_ASSERT_EQUAL_UINT64(start_hash(), search_state.repetition_check[0]);
    TEST_ASSERT_EQUAL_UINT64(board.hash, search_state.repetition_check[300]);
}

void test_go_preserves_history() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);

    parse_position("position startpos moves g1f3 g8f6 f3g1 f6g8", &board, &search_state);
    parse_go("go depth 2", &board, &search_state, &limits);

    TEST_ASSERT_EQUAL_UINT16(4, search_state.repetition_index);
    TEST_ASSERT_EQUAL_UINT64(start_hash(), search_state.repetition_check[0]);
    TEST_ASSERT_EQUAL_UINT64(board.hash, search_state.repetition_check[4]);
}

void test_fifty_move_draw() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    /* 100 half-moves without a pawn move or capture is a draw. */
    parse_position("position fen 7k/8/8/8/8/8/8/K6R w - - 99 1 moves h1h2", &board, &search_state);
    TEST_ASSERT_EQUAL_INT(100, board.fifty_move);
    TEST_ASSERT_TRUE(is_draw(&board, &search_state));

    parse_position("position fen 7k/8/8/8/8/8/8/K6R w - - 98 1 moves h1h2", &board, &search_state);
    TEST_ASSERT_EQUAL_INT(99, board.fifty_move);
    TEST_ASSERT_FALSE(is_draw(&board, &search_state));
}

void test_position_history_overflow() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    /* 1000 reversible plies, the buffer must keep room for the search path. */
    static char command[8192];
    strcpy(command, "position startpos moves");
    for (int i = 0; i < 250; i++) strcat(command, " g1f3 g8f6 f3g1 f6g8");

    parse_position(command, &board, &search_state);
    TEST_ASSERT_TRUE(search_state.repetition_index < MAX_MOVES - MAX_PLY);
    TEST_ASSERT_EQUAL_UINT64(board.hash, search_state.repetition_check[search_state.repetition_index]);
    TEST_ASSERT_TRUE(contains_repetition(&search_state, board.hash));
}
