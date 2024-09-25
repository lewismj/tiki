#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <stdalign.h>

#include "../../core/src/types.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/evaluation/evaluation_mask.h"


static int perft(board_t* b, int depth) {
    if (depth == 0 ) return 1;

    int sum = 0;
    align move_buffer_t buffer;
    buffer.index = 0;
    generate_moves(b, &buffer);
    for (int i=0; i<buffer.index; i++) {
        int num_moves = 0;
        if (make_move(b, buffer.moves[i])) num_moves += perft(b, depth - 1);
        pop_move(b);

        sum += num_moves;
    }

    return sum;
}


void generate_white_pawn_moves_fast(board_t* board, move_buffer_t* move_buffer) {
    /* Pawn moves one rank forward. */
    bitboard wp_one_step = (board->pieces[P] >> 8) & ~board->occupancy[both];
    print_bitboard(&wp_one_step, show);
    square target_sq;

    /* One step and not rank 8. rank8 = 0xff, not rank8 = 0xffffffffffffff00 */
    bitboard targets = wp_one_step & 0xffffffffffffff00;
    while (targets) {
        target_sq = get_lsb_and_pop_bit(&targets);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 8, target_sq, P, 0, 0, 0, 0, 0, 0);
    }
    /* One step and promotion, i.e. one step and rank 8. */
    targets = wp_one_step & 0xff;
    while (targets) {
        target_sq = get_lsb_and_pop_bit(&targets);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 8, target_sq, P, Q, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 8, target_sq, P, R, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 8, target_sq, P, B, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 8, target_sq, P, N, 0, 0, 0, 0, 0);
    }

    /* Double push? Rank 4 = 0xff00000000ULL */
    bitboard wp_double_push = (wp_one_step >> 8) & ~board->occupancy[both];
    targets = wp_double_push & 0xff00000000ULL;
    while (targets) {
        target_sq = get_lsb_and_pop_bit(&targets);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 16, target_sq, P, 0, 0, 1, 0, 0, 0);
    }

    /* Left attack, mask = 0x7F7F7F7F7F7F7F7FULL */
    bitboard capturing_left = ((board->pieces[P] & 0x7F7F7F7F7F7F7F7FULL) >> 9) & board->occupancy[black];

    /* Capturing Left, without promotion. */
    targets = capturing_left & 0xffffffffffffff00;
    while (targets) {
        target_sq = get_lsb_and_pop_bit(&targets);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 9, target_sq, P, 0, 1, 0, 0, 0, 0);
    }

    /* Capturing Left, with promotion. */
    targets = capturing_left & 0xff;
    while (targets) {
        target_sq = get_lsb_and_pop_bit(&targets);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 9, target_sq, P, Q, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 9, target_sq, P, R, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 9, target_sq, P, B, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 9, target_sq, P, N, 1, 0, 0, 0, 0);
    }

    /* Right attack, mask = 0xFEFEFEFEFEFEFEFEULL */
    bitboard capturing_right = ((board->pieces[P] & 0xFEFEFEFEFEFEFEFEULL) >> 7) & board->occupancy[black];

    /* Capturing Right, without promotion. */
    targets = capturing_right & 0xffffffffffffff00;
    while (targets) {
        target_sq = get_lsb_and_pop_bit(&targets);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 7, target_sq, P, 0, 1, 0, 0, 0, 0);
    }

    /* Capturing Right, with promotion. */
    targets = capturing_right & 0xff;
    while (targets) {
        target_sq = get_lsb_and_pop_bit(&targets);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 7, target_sq, P, Q, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 7, target_sq, P, R, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 7, target_sq, P, B, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(target_sq + 7, target_sq, P, N, 1, 0, 0, 0, 0);
    }
    /* Missing en-passant. */
    
}

int main(int argc, char* argv[]) {
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.
    printf("Tiki Chess\n\n");

    printf("Initializing tables ...\n");
    init_attack_table();
    init_zobrist_key();
    init_evaluation_masks();
    printf(" done\n");

    alignas(64) board_t board;

    //unsafe_parse_fen("n1n5/PPPk4/3r1r2/4P3/8/8/3PKppp/5N1N b - - 0 1", &board);
    unsafe_parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1\"",&board);
   //print_board(&board, min);

    move_buffer_t buffer1;
    buffer1.index = 0;
    generate_white_pawn_moves(&board, &buffer1);
    printf("Number of white pawn moves = [%d]\n", buffer1.index);
    printf("moves from slow:\n");
    for (int i=0; i<buffer1.index; i++) {
        print_move(buffer1.moves[i], min);
    }

    buffer1.index =0;
    generate_white_pawn_moves_fast(&board, &buffer1);
    printf("Number of white pawn moves = [%d]\n", buffer1.index);
    printf("moves from fast:\n");
    for (int i=0; i<buffer1.index; i++) {
        print_move(buffer1.moves[i], min);
    }


//    struct timeval start, end;
//    // Use elapsed time not clock time here:
//    printf("Starting Perft.\n");
//    double sum = 0;
//    for (int i =0; i<10; i++) {
//        gettimeofday(&start, NULL);
//        int pn = perft(&board, 6);
//        gettimeofday(&end, NULL);
//        printf("Perft (startpos) :%d\n", pn);
//        long seconds = end.tv_sec - start.tv_sec;
//        long microseconds = end.tv_usec - start.tv_usec;
//        double elapsed = seconds * 1000.0 + microseconds / 1000.0;
//        sum+=elapsed/1000;
//        printf("Elapsed time: %.3f sec\n", elapsed / 1000);
//    }
//    printf("Average elapsed time: %.3f sec\n", sum/10);

    return EXIT_SUCCESS;
}