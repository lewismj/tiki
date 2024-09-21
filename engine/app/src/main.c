#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <locale.h>

#include "../../core/src/types.h"
#include "../../core/src/bitboard_ops.h"
#include "../../core/src/bitboard_constants.h"
#include "../../core/src/random.h"
#include "../../core/src/mask_generator.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
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

        if (depth == 2) {
            square source = get_source_square(buffer.moves[i]);
            square target = get_target_square(buffer.moves[i]);
            printf("%s%s:%d\n",square_to_str[source], square_to_str[target], num_moves);
            if (source==a1 && target==a8) print_move(buffer.moves[i]);
        }



        if (make_move(b, buffer.moves[i])) {
            if (depth == 1) {
                square source = get_source_square(buffer.moves[i]);
                square target = get_target_square(buffer.moves[i]);
                printf("%s%s:%d\n",square_to_str[source], square_to_str[target], num_moves);
            }
            num_moves += calc_perft(b, depth - 1);
        }
        pop_move(b);

        sum += num_moves;
    }

    return sum;
}

int main(int argc, char* argv[]) {
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.
    printf("Tiki Chess\n\n");

    init_attack_table();
    init_zobrist_key();


    // fail [1] { "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {26,568,13744,314346,7594526,179862938}},
    // fail { "r3k2r/8/8/8/8/8/8/1R2K2R w Kkq - 0 1", {25,567,14095,328965,8153719,195629489}},
    // fail { "r3k2r/8/8/8/8/8/8/2R1K2R w Kkq - 0 1", {25,548,13502,312835,7736373,184411439}},
    // fail { "1r2k2r/8/8/8/8/8/8/R3K2R w KQk - 0 1", {26,583,14252,334705,8198901,198328929}},
    // fail { "2r1k2r/8/8/8/8/8/8/R3K2R w KQk - 0 1", {25,560,13592,317324,7710115,185959088}},
    // fail { "r3k1r1/8/8/8/8/8/8/R3K2R w KQq - 0 1", {25,560,13607,320792,7848606,190755813}},
    // fail { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", {48,2039,97862,4085603,193690690, -1}},
    // fail { "r3k2r/8/8/8/8/8/8/R3K1R1 w Qkq - 0 1", {25,547,13579,316214,7878456,189224276}},


    // [1] 574 vs 568
//    board_t b;
//    unsafe_parse_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", &b);
//    print_board(&b, min);
//    int p1 = calc_perft(&b, 2);
//    printf("p1: %d\n", p1);

//    move_t mv =1060920;
//    print_move(mv);
//    make_move(&b, mv);
//    print_board(&b, min);
    // Yields: R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1
    board_t b2;
    unsafe_parse_fen("R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1", &b2);
    print_board(&b2, show);
//    int p2 = calc_perft(&b2, 1);
//    printf("p2: %d\n", p2);
//    printf("************\n");


    return EXIT_SUCCESS;
}