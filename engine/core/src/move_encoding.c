#include <stdio.h>

#include "move_encoding.h"

void print_move(move_t m, showable_t options) {
    if (options & show) {
        printf("%s%s:", square_to_str[get_source_square(m)], square_to_str[get_target_square(m)]);
        printf("%c:", piece_to_char[get_piece_moved(m)]);
        printf("%c:", get_promoted_piece(m) == 0 ? '-' : piece_to_char[get_promoted_piece(m)]);
        printf("%c:", get_enpassant_flag(m) != 0 ? 't' : 'f');
        printf("%c:", get_capture_flag(m) != 0 ? 't' : 'f');
        printf("%c:", get_double_push_flag(m) != 0 ? 't' : 'f');
        printf("%c:", get_king_side_castle_flag(m) != 0 ? 't' : 'f');
        printf("%c ", get_queen_side_castle_flag(m) != 0 ? 't' : 'f');
    }
    if (options & min) {
        piece maybe_promoted = get_promoted_piece(m);
        if (maybe_promoted == 0 )
            printf("%s%s",
                   square_to_str[get_source_square(m)],
                   square_to_str[get_target_square(m)]);
        else
            printf("%s%s%c",
                   square_to_str[get_source_square(m)],
                   square_to_str[get_target_square(m)],
                   piece_to_char[maybe_promoted]);
    }
}