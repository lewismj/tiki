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
    TEST_ASSERT_TRUE(contains_repetition(&search_state, board.hash, board.fifty_move));
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
    TEST_ASSERT_TRUE(contains_repetition(&search_state, board.hash, board.fifty_move));
}

#define H 0x1234ULL

/* History of hashes, the last entry is the current position. */
static void set_history(search_state_t* search_state, const uint64_t* hashes, int n) {
    init_search_state(search_state);
    for (int i = 0; i < n; i++) search_state->repetition_check[i] = hashes[i];
    search_state->repetition_index = (uint16_t) (n - 1);
}

void test_repetition_same_side_to_move() {
    search_state_t search_state;

    /* Four plies back, same side to move. */
    const uint64_t four_back[] = { H, 1, 2, 3, H };
    set_history(&search_state, four_back, 5);
    TEST_ASSERT_TRUE(contains_repetition(&search_state, H, 50));

    /* An odd distance is the other side to move, never a repetition. */
    const uint64_t odd[] = { 1, H, 2, 3, H };
    set_history(&search_state, odd, 5);
    TEST_ASSERT_FALSE(contains_repetition(&search_state, H, 50));

    /* Two plies back can't repeat (each side made one move), so it isn't checked. */
    const uint64_t two_back[] = { 1, 2, H, 3, H };
    set_history(&search_state, two_back, 5);
    TEST_ASSERT_FALSE(contains_repetition(&search_state, H, 50));
}

void test_repetition_limited_by_fifty_move() {
    search_state_t search_state;
    const uint64_t history[] = { H, 1, 2, 3, 4, 5, H };
    set_history(&search_state, history, 7);

    /* The match is 6 plies back, only found if there's no irreversible move since. */
    TEST_ASSERT_TRUE(contains_repetition(&search_state, H, 6));
    TEST_ASSERT_FALSE(contains_repetition(&search_state, H, 5));
}

void test_repetition_limited_by_null_move() {
    search_state_t search_state;
    const uint64_t history[] = { H, 1, 2, 3, 4, 5, H };
    set_history(&search_state, history, 7);

    /* A null move after the match, the repetition would span it. */
    search_state.null_move_index = 2;
    TEST_ASSERT_FALSE(contains_repetition(&search_state, H, 50));

    search_state.null_move_index = 0;
    TEST_ASSERT_TRUE(contains_repetition(&search_state, H, 50));
}

void test_insufficient_material() {
    static const char* const draws[] = {
            "8/8/8/4k3/8/8/8/4K3 w - - 0 1",        /* KvK  */
            "8/8/8/4k3/8/8/8/4KN2 w - - 0 1",       /* KNvK */
            "8/8/8/4k3/8/8/8/4KB2 b - - 0 1",       /* KBvK */
            "8/8/8/4kn2/8/8/8/4K3 w - - 0 1",       /* KvKN */
            "8/8/8/4kb2/8/8/8/4K3 b - - 0 1",       /* KvKB */
    };
    static const char* const not_draws[] = {
            "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1",      /* KPvK  */
            "8/8/8/4k3/8/8/8/4KR2 w - - 0 1",       /* KRvK  */
            "8/8/8/4k3/8/8/8/3QK3 w - - 0 1",       /* KQvK  */
            "8/8/8/4k3/8/8/8/3NKN2 w - - 0 1",      /* KNNvK */
            "8/8/8/4kb2/8/8/8/4KB2 w - - 0 1",      /* KBvKB */
            "8/8/8/4kp2/8/8/8/4KN2 w - - 0 1",      /* KNvKP */
    };
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);

    for (size_t i = 0; i < sizeof(draws) / sizeof(draws[0]); i++) {
        parse_fen(draws[i], &board);
        TEST_ASSERT_TRUE_MESSAGE(is_insufficient_material(&board), draws[i]);
        TEST_ASSERT_TRUE_MESSAGE(is_draw(&board, &search_state), draws[i]);
    }
    for (size_t i = 0; i < sizeof(not_draws) / sizeof(not_draws[0]); i++) {
        parse_fen(not_draws[i], &board);
        TEST_ASSERT_FALSE_MESSAGE(is_insufficient_material(&board), not_draws[i]);
        TEST_ASSERT_FALSE_MESSAGE(is_draw(&board, &search_state), not_draws[i]);
    }
}

void test_search_scores_insufficient_material_as_draw() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    init_search_state(&search_state);
    reset_time_control(&limits);

    /* Taking the last black pawn leaves KNvK, the search scores it as a draw. */
    parse_position("position fen 8/8/8/8/8/8/4p3/4KN1k w - - 0 1", &board, &search_state);
    move_t take = parse_move("e1e2", &board);
    TEST_ASSERT_NOT_EQUAL(0, take);
    TEST_ASSERT_TRUE(make_move(&board, take));
    TEST_ASSERT_TRUE(is_insufficient_material(&board));

    /* Searched from the child, a draw is detected at once (ply > 0). */
    search_state.repetition_check[++search_state.repetition_index] = board.hash;
    search_state.ply = 1;
    TEST_ASSERT_EQUAL_INT(0, negamax(-INF, INF, 4, &board, &search_state, &limits));
}
