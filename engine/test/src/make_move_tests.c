#include "unity.h"
#include "make_move_tests.h"

#include "../../core/src/types.h"
#include "../../core/src/board.h"

void test_correct_hash0() {
    /*
     * Different board positions & moves, that yield same end position,
     * should have same hash values.
     * This test includes an en passant move.
     */

    board_t board1;
    unsafe_parse_fen("3k4/8/8/8/3Pp3/8/4P3/4K3 b - d3 0 1", &board1);
    move_t e4d3 = encode_move(e4, d3, p, 0, 1, 0, 1, 0, 0);
    move_t e2d3 = encode_move(e2, d3, P, 0, 1, 0, 0, 0, 0);
    make_move(&board1, e4d3);
    make_move(&board1, e2d3);
    uint64_t expected = board1.hash;

    board_t board2;
    unsafe_parse_fen("3k4/8/8/8/5n2/8/4P3/4K3 b - - 0 1", &board2);
    move_t f4d3 = encode_move(f4, d3, n, 0, 0, 0, 0, 0, 0);
    make_move(&board2, f4d3);
    make_move(&board2, e2d3);
    uint64_t actual = board2.hash;

    TEST_ASSERT_EQUAL_UINT64(expected, actual);
}

void test_correct_hash1() {
    board_t board1;
    unsafe_parse_fen("3k4/8/8/4p3/3P4/4P3/8/4K3 b - - 0 1", &board1);
    move_t e5d4 = encode_move(e5, d4, p, 0, 1, 0, 0, 0, 0);
    move_t e3d4 = encode_move(e3, d4, P, 0, 1, 0, 0, 0, 0);
    make_move(&board1, e5d4);
    make_move(&board1, e3d4);
    uint64_t expected = board1.hash;

    board_t board2;
    unsafe_parse_fen("3k4/8/2n5/8/3P4/4P3/8/4K3 b - - 0 1", &board2);
    move_t c6d4 = encode_move(c6, d4, n, 0, 1, 0, 0, 0, 0);
    make_move(&board2, c6d4);
    TEST_ASSERT_NOT_EQUAL_UINT64(expected, board2.hash);
    make_move(&board2, e3d4);
    uint64_t actual = board2.hash;

    TEST_ASSERT_EQUAL_UINT64(expected, actual);
}

void test_correct_hash2() {


}