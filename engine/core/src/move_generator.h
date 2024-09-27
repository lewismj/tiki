#ifndef TIKI_MOVE_GENERATOR_H
#define TIKI_MOVE_GENERATOR_H

#include "types.h"
#include "board.h"
#include "attack_mask.h"
#include "move_encoding.h"
#include "board.h"

/**
 * As moves are generated to a particular depth, we store them in a fixed
 * size array, n.b. don't use a linked list as this would be unnecessarily
 * slow.
 */
typedef struct align {
    uint32_t moves[512];
    int index;
} move_buffer_t;


/**
 * Generate the set of moves given a board state 'board', and a buffer to store
 * those moves. The moves are stored starting at the 'index' position.
 *
 * @param board the board position state.
 * @param move_buffer the move_t buffer.
 */
void generate_moves(board_t* board, move_buffer_t* move_buffer);


/**
 * Update the move buffer with all the moves (if possible) of a specified piece type.
 * Used for bishop, rook, queen.
 *
 * @param move_piece         the piece type.
 * @param pieces    the bitboard of piece type move_piece.
 * @param not_self  the bitboard of opponent or empty.
 * @param opponent  the opponent bitboard.
 * @param fn        piece attack function.
 * @param move_buffer   the move_t buffer.
 */
static inline_always void generate_piece_move(  piece move_piece,
                                                bitboard pieces,
                                                const bitboard* not_self,
                                                const bitboard* opponent,
                                                const bitboard* both,
                                                const move_function move_fn,
                                                move_buffer_t* move_buffer) {
    /* n.b. tested, this is faster than two while loops of captures and non captures. */
    while (pieces) {
        square source = get_lsb_and_pop_bit(&pieces);
        bitboard attacks = move_fn(source, *both) & *not_self;
        while (attacks) {
            square target = get_lsb_and_pop_bit(&attacks);
            move_t mv = is_bit_set(opponent, target)
                        ? encode_move(source, target, move_piece, 0, 1, 0, 0, 0, 0)
                        : encode_move(source, target, move_piece, 0, 0, 0, 0, 0, 0);
            move_buffer->moves[move_buffer->index++] = mv;
        }
    }
}




#endif
