#ifndef TIKI_MOVE_GENERATOR_H
#define TIKI_MOVE_GENERATOR_H

#include "types.h"
#include "board.h"
#include "attack_mask.h"
#include "move_encoding.h"


/**
 * As moves are generated to a particular depth, we store them in a fixed
 * size array, n.b. don't use a linked list as this would be unnecessarily
 * slow.
 */
typedef struct  {
    uint32_t moves[1024];
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
 * Return true if the given square 's' is attacked by any piece of colour 'c'.
 *
 * @param board the board position.
 * @param c     the colour of the attacker.
 * @param s     the square that might be attacked.
 * @return  true, if square attacked; false otherwise.
 */
static inline_always bool is_square_attacked(board_t* board, colour c, square s) {
    /**
     *  For pawns, we do the following, say we have a white pawn on square s, is it attacked by a black pawn?
     *  We look at the white pawn attacks and see if there is a black pawn on those squares.
     *  For everything else within the function, the logic is just is Colour c attacking square s, use the
     *  BitBoard of the piece for the given colour.
     */

    bitboard rooks = c == white ? board->pieces[R] : board->pieces[r];
    bitboard rook_attacks = rook_attack(s, board->occupancy[both]);
    if (rook_attacks & rooks) return true;

    bitboard bishops = c == white ? board->pieces[B] : board->pieces[b];
    bitboard bishop_attacks = bishop_attack(s, board->occupancy[both]);
    if (bishop_attacks & bishops) return true;

    bitboard knights = c == white ? board->pieces[N] : board->pieces[n];
    if (knight_attack(s) & knights) return true;

    bitboard queens = c == white ? board->pieces[Q] : board->pieces[q];
    bitboard queen_attacks = rook_attacks | bishop_attacks;
    if (queen_attacks & queens) return true;

    bitboard pawns = c == white ? board->pieces[P] : board->pieces[p];
    bitboard pawn_attacks = c == white ? pawn_attack(s, black) : pawn_attack(s,white);
    if (pawn_attacks & pawns) return true;

    bitboard kings = c == white ? board->pieces[K] : board->pieces[k];
    bitboard king_attacks = king_attack(s);
    return king_attacks & kings;
}

/**
 * Update the move buffer with all the moves (if possible) of a specified piece type.
 * Used for bishop, rook, queen.
 *
 * @param p         the piece type.
 * @param pieces    the bitboard of piece type p.
 * @param not_self  the bitboard of opponent or empty.
 * @param opponent  the opponent bitboard.
 * @param fn        piece attack function.
 * @param move_buffer   the move_t buffer.
 */
static inline_always void generate_piece_move(  piece p,
                                                bitboard pieces,
                                                const bitboard* not_self,
                                                const bitboard* opponent,
                                                const bitboard* both,
                                                const move_function move_fn,
                                                move_buffer_t* move_buffer) {

    while (pieces) {
        square source = get_lsb_and_pop_bit(&pieces);
        bitboard attacks = (move_fn(source, *both) & *not_self);
        while (attacks) {
            square target = get_lsb_and_pop_bit(&attacks);
            move_t mv = is_bit_set(opponent, target)
                    ? encode_capture(source, target, p)
                    : encode_no_capture(source, target, p);
            move_buffer->moves[move_buffer->index++] = mv;
        }
    }

}




#endif
