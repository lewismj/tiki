#ifndef TIKI_BOARD_H
#define TIKI_BOARD_H

#include <inttypes.h>

#include "types.h"
#include "bitboard_ops.h"
#include "zobrist_key.h"

/**
 * Define the common board data structure.
 */

#define MaximumSearchDepthPly 64

/**
 *
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
    uint32_t undo[MaximumSearchDepthPly];
} board_t;

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

#endif
