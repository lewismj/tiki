#ifndef TIKI_BOARD_H
#define TIKI_BOARD_H

#include <inttypes.h>
#include <stdbool.h>

#include "types.h"
#include "bitboard_ops.h"
#include "zobrist_key.h"

/**
 * Define the common board data structure.
 */

#define MaximumSearchDepthPly 64

/**
 * Represents the board (position) state of a game.
 */
typedef struct {
    bitboard* pieces;
    bitboard* occupancy;
    int castle_flag;
    square en_passant;
    colour side;
    int half_move;
    int full_move;
    uint64_t hash;
    /* Used for applying/undo moves to a board. */
    uint32_t undo[MaximumSearchDepthPly];
    int stack_ptr;
} board_t;


/**
 *   When making a move the castle flag needs to be updated, encoding is as follows:
 *
 *                              castling    move
 *                              right       update      binary  decimal
 *
 *  king & rooks didn't move:   111  &      1111    =   1111        15
 *
 *  white king moved:           1111 &      1100    =   1100        12
 *  white king's rook moved:    1111 &      1110    =   1110        14
 *  white queen's rook moved:   1111 &      1101    =   1101        13
 *
 *  black king moved:           1111 &      0011    =   1011        3
 *  black king's rook moved:    1111 &      1011    =   1011        11
 *  black queen's rook moved:   1111 &      0111    =  0111         7
 *
 *  From this, we can look at the board and pre-compute how the rights would change if a rook/king on a square is
 *  the source of a move. i.e. white queen's rook is at a1, if that moves on its own, we'd mask with 13 as above,
 *  for the whole chess board we have as follows:
 */
static int castling_update[] = {
        7, 15, 15, 15, 3, 15, 15, 11,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        13, 15, 15, 15, 12, 15, 15, 14
};

/**
 * unsafe in this context means no validation.
 * Engine is assuming client validates any FEN strings.
 *
 * @param fen
 * @param board
 * @return
 */
void unsafe_parse_fen(const char* fen, board_t* board);

/**
 *
 * @return
 */
board_t* new_board();

/**
 *
 * @param board
 */
void free_board(board_t* board);

/**
 *
 * @param board
 * @param options
 */
void print_board(board_t* board, showable options);

/**
 * Recomputes the Zobrist hash of the board state.
 * @param board the board state.
 */
static inline_always void recalculate_hash(board_t* board) {
    uint64_t hash = 0ULL;

    for (int p=0; p<12; p++) {
        bitboard b = board->pieces[p];
        while (b != 0ULL) {
            square s = get_lsb_and_pop_bit(&b);
            hash ^= get_piece_key(s,p);
        }
    }

    if (board->en_passant != none_sq) hash ^= get_enpassant_key(board->en_passant);
    hash ^= get_castle_key(board->castle_flag);

    if (board->side == black) hash ^= get_side_key();

    board->hash = hash;
}

static inline_always bool make_move(board_t* board, move_t move) {
    return false;
}

static inline_always void undo_move(board_t* board);

#endif
