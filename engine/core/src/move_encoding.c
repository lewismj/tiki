#include <stdio.h>

#include "move_encoding.h"

void print_move(move m) {
    printf("move: %d\n", m);
    printf("source: %s\n", square_to_str[get_source_square(m)]);
    printf("target: %s\n", square_to_str[get_target_square(m)]);
    printf("piece moved: %s\n",piece_to_str[get_move_piece(m)]);
    printf("promoted piece: %s\n", piece_to_str[get_promoted_piece(m)]);
    printf("en-passant: %s\n", get_enpassant_flag(m) != 0 ? "true" : "false");
    printf("capture: %s\n", get_capture_flag(m) != 0 ? "true" : "false");
    printf("double push: %s\n", get_double_push_flag(m) !=0 ? "true" : "false");
    printf("king side castle :%s\n", get_capture_flag(m) != 0 ? "true" : "false");
    printf("queen side castle :%s\n", get_capture_flag(m) != 0 ? "true" : "false");
}