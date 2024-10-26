#include "unity.h"
#include "make_undo_move_tests.h"

#include "../../core/src/types.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/uci.h"


void test_make_move_hash1() {
    /*
     * Different board positions & moves, that yield same end position,
     * should have same hash values.
     * This test includes an en passant move.
     */

    board_t board1;
    parse_fen("3k4/8/8/8/3Pp3/8/4P3/4K3 b - d3 0 1", &board1);
    move_t e4d3 = encode_move(e4, d3, p, 0, 1, 0, 1, 0, 0);
    move_t e2d3 = encode_move(e2, d3, P, 0, 1, 0, 0, 0, 0);
    make_move(&board1, e4d3);
    make_move(&board1, e2d3);
    uint64_t expected = board1.hash;

    board_t board2;
    parse_fen("3k4/8/8/8/5n2/8/4P3/4K3 b - - 0 1", &board2);
    move_t f4d3 = encode_move(f4, d3, n, 0, 0, 0, 0, 0, 0);
    make_move(&board2, f4d3);
    make_move(&board2, e2d3);
    uint64_t actual = board2.hash;

    TEST_ASSERT_EQUAL_UINT64(expected, actual);
}

void test_make_move_hash2() {
    board_t board1;
    parse_fen("3k4/8/8/4p3/3P4/4P3/8/4K3 b - - 0 1", &board1);
    move_t e5d4 = encode_move(e5, d4, p, 0, 1, 0, 0, 0, 0);
    move_t e3d4 = encode_move(e3, d4, P, 0, 1, 0, 0, 0, 0);
    make_move(&board1, e5d4);
    make_move(&board1, e3d4);
    uint64_t expected = board1.hash;

    board_t board2;
    parse_fen("3k4/8/2n5/8/3P4/4P3/8/4K3 b - - 0 1", &board2);
    move_t c6d4 = encode_move(c6, d4, n, 0, 1, 0, 0, 0, 0);
    make_move(&board2, c6d4);
    TEST_ASSERT_NOT_EQUAL_UINT64(expected, board2.hash);
    make_move(&board2, e3d4);
    uint64_t actual = board2.hash;

    TEST_ASSERT_EQUAL_UINT64(expected, actual);
}

void test_make_move_hash3() {
    board_t board1;
    board_t board2;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board1);
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board2);


    move_t b2c3 = encode_move(b2, c3, N, 0, 0, 0, 0, 0, 0);
    move_t b8c5 = encode_move(b8, c5, n, 0, 0, 0, 0, 0, 0);
    move_t g1f4 = encode_move(g1, f4, N, 0, 0, 0, 0, 0, 0);
    move_t g8f6 = encode_move(g8, f6, n, 0, 0, 0, 0, 0, 0);

    make_move(&board1, b2c3);
    make_move(&board1, b8c5);
    make_move(&board1, g1f4);
    make_move(&board1, g8f6);

    make_move(&board2, g1f4);
    TEST_ASSERT_NOT_EQUAL_UINT64(board1.hash, board2.hash);
    make_move(&board2, g8f6);
    TEST_ASSERT_NOT_EQUAL_UINT64(board1.hash, board2.hash);
    make_move(&board2, b2c3);
    TEST_ASSERT_NOT_EQUAL_UINT64(board1.hash, board2.hash);
    make_move(&board2, b8c5);
    TEST_ASSERT_EQUAL_UINT64(board1.hash, board2.hash);
}


void test_undo_restores_state1() {
    static const char* fen[] = {
            "8/8/8/8/8/8/6k1/4K2R w K - 0 1",
            "8/1k6/8/5N2/8/4n3/8/2K5 w - - 0 1",
            "R6r/8/8/2K5/5k2/8/8/r6R w - - 0 1",
            "k7/8/3p4/8/8/4P3/8/7K b - - 0 1",
            "7k/8/8/p7/1P6/8/8/7K w - - 0 1",
            "7k/8/p7/8/8/1P6/8/7K w - - 0 1",
            "7k/8/8/1p6/P7/8/8/7K w - - 0 1",
            "7k/8/1p6/8/8/P7/8/7K w - - 0 1",
            "7k/8/8/p7/1P6/8/8/7K b - - 0 1",
            "7k/RR6/8/8/8/8/rr6/7K w - - 0 1",
            "3k4/3p4/8/8/8/8/8/R3K2R w KQ - 0 1",
            "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",
            "1r1k4/3p4/8/8/8/8/8/R3K2R w KQ - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/Pp2P3/2N2Q1p/1PPBBPPP/R3K2R b KQkq a3 0 1",
    };
    static int sz = 14;
    for (int i=0; i<sz; i++) {
        board_t board, original;
        parse_fen(fen[i], &original);
        parse_fen(fen[i], &board);
        TEST_ASSERT_EQUAL_UINT64(original.hash, board.hash);

        /*
         * Generate all candidate moves from this position, note this will include positions that we could filter,
         * e.g. move leaves king in check, ignore for the purposes of this test.
         */
        move_buffer_t buffer;
        buffer.index = 0;
        generate_moves(&board, &buffer);
        /* Check move-undo move always leave the board in consistent state. */
        for (int j=0; j<buffer.index; j++) {
            make_move(&board, buffer.moves[j]);
            TEST_ASSERT_NOT_EQUAL_UINT64(original.hash, board.hash);
            pop_move(&board);

            /* Restored board should have same hash. */
            TEST_ASSERT_EQUAL_UINT64(original.hash, board.hash);

            /* Check board state is the same. */
            for (int tmp = P; tmp <= k; tmp++) {
                TEST_ASSERT_EQUAL_UINT64(original.pieces[tmp], board.pieces[tmp]);
            }
            TEST_ASSERT_EQUAL_UINT64(original.occupancy[white], board.occupancy[white]);
            TEST_ASSERT_EQUAL_UINT64(original.occupancy[black], board.occupancy[black]);
            TEST_ASSERT_EQUAL_UINT64(original.occupancy[both], board.occupancy[both]);
            TEST_ASSERT_EQUAL_INT(original.castle_flag, board.castle_flag);
            TEST_ASSERT_EQUAL_INT(original.en_passant, board.en_passant);
           // TEST_ASSERT_EQUAL_INT(original.half_move, board.half_move);
            TEST_ASSERT_EQUAL_INT(original.fifty_move, board.fifty_move);
        }
    }
}