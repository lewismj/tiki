#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "../../core/src/types.h"
#include "../../core/src/bitboard_ops.h"
#include "../../core/src/bitboard_constants.h"
#include "../../core/src/random.h"
#include "../../core/src/mask_generator.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"


int main(int argc, char* argv[]) {
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.
    printf("Tiki Chess\n\n");

    init_attack_table();
    init_zobrist_key();

    board_t* board = new_board();
    //unsafe_parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1 ", board);
    unsafe_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", board);
    print_board(board, show | hex);
    move_buffer_t buffer;
    buffer.index = 0;
    generate_moves(board, &buffer);
    printf("move index: %d\n", buffer.index);
    for (int i=0; i < buffer.index; i++) print_move(buffer.moves[i]);
    free_board(board);


//    bitboard occupancy = 0ULL;
//    set_bit(&occupancy,d6);
//    set_bit(&occupancy,f6);
//    set_bit(&occupancy,f4);
//    set_bit(&occupancy,a5);
//    set_bit(&occupancy,d2);
//    set_bit(&occupancy,b4);
//    set_bit(&occupancy,b2);
//    bitboard attacks = rook_attack(d4, occupancy);
//    print_bitboard(&attacks, show | hex);
//
//    attacks = bishop_attack(d4, occupancy);

//    print_bitboard(&attacks, show | hex);


//    bitboard attack_mask = create_knight_attack_mask(d4);
//    print_bitboard(&attack_mask, show | hex);
//
//    printf("index: %d\n", trailing_zero_count(attack_mask));
//    printf("pop count: %d\n", pop_count(attack_mask));

//    uint32_t state = random_seed;
//    for (int i=0; i<10; i++) {
//        uint64_t next = next_random_64(&state);
//        printf("0x%" PRIx64 ",\n", next);
//    }

//    printf("rook blockers, d4:\n");
//    bitboard blockers = create_bishop_blocker_mask(g1);
//    print_bitboard(&blockers, show | hex);
//
//    bitboard attack_mask = create_rook_attack_mask(d2, blockers);
//    printf("rook attack mask, d2 with above blockers:\n");
//    print_bitboard(&attack_mask, show | hex);
//
//    attack_mask = create_bishop_attack_mask(d2, blockers);
//    printf("bishop attack mask, d2 with above blockers:\n");
//    print_bitboard(&attack_mask, show | hex);

//    printf("test:\n");
//    bitboard b = 0ULL;
//   // set_bit(&b,d4);
//    unsigned int rank = a8 / 8;
//    unsigned int file = a8 % 8;
//    b |= 1ULL << (rank * 8 + file);
//    rank = d4 / 8;
//    file = d4 % 8 ;
//    b |= 1ULL << (rank * 8 + file);
//    print_bitboard(&b, show | hex);


    return EXIT_SUCCESS;
}