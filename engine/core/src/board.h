#ifndef TIKI_BOARD_H
#define TIKI_BOARD_H

#include <inttypes.h>
#include <stdbool.h>

#include "types.h"
#include "bitboard_ops.h"
#include "move_encoding.h"
#include "zobrist_key.h"


/**
 * Structure used for undo-ing moves, note we create a fixed size array and store
 * the values, try to make undo as fast as possible. We store at most some maximum ply depth
 * number of entries.
 */
typedef struct {
    move_t move;
    int castle_flag;
    int en_passant;
    int half_move;
    int full_move;
    uint64_t hash;
} undo_meta_t;

#define MAX_DEPTH 16

/**
 * Represents the board (position) state of a game.
 * Typically engine will create a single instance.
 */
typedef struct {
    bitboard pieces[12];
    bitboard occupancy[3];
    int castle_flag;
    square en_passant;
    colour side;
    int half_move;
    int full_move;
    uint64_t hash;
    /* Used for applying/undo moves to a board. */
    undo_meta_t stack[MAX_DEPTH];
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
    /*
     * Put move and associate state onto the stack, put enough
     * information onto the stack so it can be popped in an undo op.
     */
    board->stack[board->stack_ptr].move = move;
    board->stack[board->stack_ptr].castle_flag = board->castle_flag;
    board->stack[board->stack_ptr].en_passant = board->en_passant;
    board->stack[board->stack_ptr].half_move = board->half_move;
    board->stack[board->stack_ptr].full_move = board->full_move;

    /* Retrieve move properties. */
    square source = get_source_square(move);
    square target = get_target_square(move);
    int move_piece = get_move_piece(move);
    int capture_flag = get_capture_flag(move);
    int enpassant_flag = get_enpassant_flag(move);
    int opponent = board->side == white ? black : white;

    /* Move the piece from the source square to the target square. */
    pop_bit(&board->pieces[move_piece], source);
    pop_bit(&board->occupancy[board->side], source);
    set_bit(&board->pieces[move_piece], target);
    set_bit(&board->occupancy[board->side], target);

    board->hash ^= get_piece_key(source, move_piece);
    board->hash ^= get_piece_key(target, move_piece);

    /* Update half move counter, if necessary. */
    if ( move_piece == P || move_piece == p || capture_flag) {
        board->half_move = 0;
    } else {
        board->half_move++;
    }

    /* A square is only en passant for one move... */
    if (board->en_passant != none_sq) board->hash ^= get_enpassant_key(board->en_passant);
    board->en_passant = none_sq;

    /* Captures. */
    if (capture_flag) {
        if (enpassant_flag) {
            /* Move is en passant capture.
             *      We've moved the pawn from source to target at the start, but we need to remove the
             *      opposing pawn, since it wasn't at the target square (post capture) in en-passant.
             */
            if (board->side == white) {

            } else {

            }
        } /* en passant. */

    }

    /* Double push of pawn. */


    board->stack_ptr++;
    return false;
}

static inline_always void undo_move(board_t* board);

#endif
