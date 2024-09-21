#include "unity.h"

#include "perf_tests.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"


static int calc_perft(board_t* b, int depth)
{
    if (depth == 0 ) return 1;

    int sum = 0;
    move_buffer_t buffer;
    buffer.index = 0;
    generate_moves(b, &buffer);
    for (int i=0; i<buffer.index; i++) {
        int num_moves = 0;
        if (make_move(b, buffer.moves[i])) {
            num_moves += calc_perft(b, depth - 1);
        }
        pop_move(b);
        sum += num_moves;
    }

    return sum;
}


void full_perft_tests() {
    for (int i=0; i<num_perft_positions; i++) {
        printf("perft check position [%d]: %s ", i, perft_data[i].position);
        for (int j=0; j<6; j++) {
            if (perft_data[i].values[j] !=-1) {
                board_t board;
                unsafe_parse_fen(perft_data[i].position, &board);
                int num_moves = calc_perft(&board, j+1);
                if (perft_data[i].values[j] != num_moves) {
                    printf(" case [%d] failed, depth: %d, actual: %d, expected: %d\n", i, j, num_moves, perft_data[i].values[j]);
                }
                TEST_ASSERT_EQUAL_INT(perft_data[i].values[j], num_moves);
            }
        }
        printf("\n");
    }
}