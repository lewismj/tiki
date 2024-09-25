#ifndef TIKI_BOARD_H
#define TIKI_BOARD_H

#include <stdalign.h>
#include <inttypes.h>
#include <stdbool.h>

#include "types.h"
#include "bitboard_ops.h"
#include "move_encoding.h"
#include "zobrist_key.h"
#include "attack_mask.h"


/**
 * Structure used for undo-ing moves, note we create a fixed size array and store
 * the values, try to make undo as fast as possible. We store at most some maximum ply depth
 * number of entries.
 */
typedef struct  {
    move_t move;
    int captured_piece;
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


/**
 *  For pawns, we do the following, say we have a white pawn on square s, is it attacked by a black pawn?
 *  We look at the white pawn attacks and see if there is a black pawn on those squares.
 *  For everything else within the function, the logic is just is Colour c attacking square s, use the
 *  BitBoard of the piece for the given colour.
 */

/**
 * Return true if the given square 's' is attacked by any piece of colour 'c'.
 *
 * @param board the board position.
 * @param c     the colour of the attacker.
 * @param s     the square that might be attacked.
 * @return  true, if square attacked; false otherwise.
 */

/*
static inline_always bool is_square_attacked(board_t* board, colour c, square s) {
    bitboard queens = c == white ? board->pieces[Q] : board->pieces[q];

    bitboard rooks = c == white ? board->pieces[R] : board->pieces[r];
    bitboard rook_attacks = rook_attack(s, board->occupancy[both]);
    if (rook_attacks & (rooks|queens)) return true;

    bitboard bishops = c == white ? board->pieces[B] : board->pieces[b];
    bitboard bishop_attacks = bishop_attack(s, board->occupancy[both]);
    if (bishop_attacks & (bishops|queens)) return true;

    bitboard knights = c == white ? board->pieces[N] : board->pieces[n];
    if (knight_attack(s) & knights) return true;

    bitboard pawns = c == white ? board->pieces[P] : board->pieces[p];
    bitboard pawn_attacks = c == white ? pawn_attack(s, black) : pawn_attack(s,white);
    if (pawn_attacks & pawns) return true;

    bitboard kings = c == white ? board->pieces[K] : board->pieces[k];
    bitboard king_attacks = king_attack(s);
    return king_attacks & kings;
}
 */


static inline_always bool is_square_attacked_black(board_t* board, square s) {
    if (knight_attack(s) & board->pieces[n]) return true;

    bitboard rook_attacks = rook_attack(s, board->occupancy[both]);
    if (rook_attacks & (board->pieces[r]|board->pieces[q])) return true;

    bitboard bishop_attacks = bishop_attack(s, board->occupancy[both]);
    if (bishop_attacks & (board->pieces[b]|board->pieces[q])) return true;

    if (pawn_attack(s,white) & board->pieces[p]) return true;

    return king_attack(s) & board->pieces[k];
}

static inline_always bool is_square_attacked_white(board_t* board, square s) {
    if (knight_attack(s) & board->pieces[N]) return true;

    bitboard rook_attacks = rook_attack(s, board->occupancy[both]);
    if (rook_attacks & (board->pieces[R]|board->pieces[Q])) return true;

    bitboard bishop_attacks = bishop_attack(s, board->occupancy[both]);
    if (bishop_attacks & (board->pieces[B]|board->pieces[Q])) return true;

    if (pawn_attack(s,black) & board->pieces[P]) return true;

    return king_attack(s) & board->pieces[K];
}


static inline_always bool make_move(board_t* board, move_t move) {
    /*
     * Put move and associate state onto the stack, put enough
     * information onto the stack, so that it can be popped in an undo op.
     */
    board->stack[board->stack_ptr].move = move;
    board->stack[board->stack_ptr].castle_flag = board->castle_flag;
    board->stack[board->stack_ptr].en_passant = board->en_passant;
    board->stack[board->stack_ptr].half_move = board->half_move;
    board->stack[board->stack_ptr].full_move = board->full_move;
    board->stack[board->stack_ptr].hash = board->hash;

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
    pop_bit(&board->occupancy[board->side], source);

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
            /*
             * Move is en passant capture.
             * We've moved the pawn from source to target at the start, but we need to remove the
             * opposing pawn, since it wasn't at the target square (post capture) in en-passant.
             */
            if (board->side == white) {
                square sq = target + 8;
                pop_bit(&board->pieces[p], sq);
                pop_bit(&board->occupancy[opponent], sq);
                board->hash ^= get_piece_key(sq, p);
            } else {
                square sq = target - 8;
                pop_bit(&board->pieces[P], sq);
                pop_bit(&board->occupancy[opponent], sq);
                board->hash ^= get_piece_key(sq, P);
            }
        } /* en passant. */
        else {
            /* Process capture. Find the piece we're capturing and pop the bit. */
            int *pieces = board->side == white ? black_pieces : white_pieces;
            for (int i=0; i<=5; i++) {
                piece captured_piece = pieces[i];
                if (!is_bit_set(&board->pieces[captured_piece],target)) continue;
                board->stack[board->stack_ptr].captured_piece = captured_piece;
                pop_bit(&board->pieces[captured_piece], target);
                pop_bit(&board->occupancy[opponent], target);
                board->hash ^= get_piece_key(target, captured_piece);
                break;
            }
        }
    }

    int double_push_flag = get_double_push_flag(move);
    int king_side_castle_flag = get_king_side_castle_flag(move);
    int queen_side_castle_flag = get_queen_side_castle_flag(move);
    int promoted = get_promoted_piece(move);

    /* Double push of pawn. */
    if (double_push_flag) {
        square sq = board->side == white ? target + 8 : target - 8;
        board->en_passant = sq;
        board->hash ^= get_enpassant_key(sq);
    } else if (king_side_castle_flag) {
        /* King side castling. */
        int rook = board->side == white ? R : r;
        int from = target +1;
        int to = target -1;
        pop_bit(&board->pieces[rook], from);
        pop_bit(&board->occupancy[board->side], from);
        set_bit(&board->pieces[rook], to);
        set_bit(&board->occupancy[board->side], to);
        board->hash ^= get_piece_key(from, rook);
        board->hash ^= get_piece_key(to, rook);
    } else if (queen_side_castle_flag) {
        /* Queen side castling. */
        int rook = board->side == white ? R: r;
        int from = target -2;
        int to = target +1;
        pop_bit(&board->pieces[rook], from);
        pop_bit(&board->occupancy[board->side], from);
        set_bit(&board->pieces[rook], to);
        set_bit(&board->occupancy[board->side], to);
        board->hash ^= get_piece_key(from, rook);
        board->hash ^= get_piece_key(to, rook);

    } else if (promoted) {
        /* Here promoted is piece, but if promoted=0, indicated no promotion. */
        int pawn = board->side == white ? P : p;
        pop_bit(&board->pieces[pawn], target);
        pop_bit(&board->occupancy[board->side], target);
        board->hash ^= get_piece_key(target, pawn);

        set_bit(&board->pieces[promoted], target);
        set_bit(&board->occupancy[board->side], target);
        board->hash ^= get_piece_key(target, promoted);
    }

    board->castle_flag &= castling_update[source];
    board->castle_flag &= castling_update[target];
    board->hash ^= get_castle_key(board->castle_flag);

    board->occupancy[both] = board->occupancy[white] | board->occupancy[black];
    board->side = opponent;
    board->hash ^= get_side_key();

    square king_sq = board->side == white ?
            trailing_zero_count(board->pieces[k]) :
            trailing_zero_count(board->pieces[K]);
    board->stack_ptr++;

    return board->side == white ?
        !is_square_attacked_white(board, king_sq) :
        !is_square_attacked_black(board, king_sq);
}

static inline_always void pop_move(board_t* board) {
    if (--board->stack_ptr == -1) return;
    undo_meta_t* undo_element = &board->stack[board->stack_ptr];
    move_t undo_move = undo_element->move;

    piece undo_piece = get_move_piece(undo_move);
    colour old_side_index = board->side == white ? black : white;

    square undo_source = get_source_square(undo_move);
    square undo_target = get_target_square(undo_move);

    /* First move the piece back, before replacing any capture etc. */
    set_bit(&board->pieces[undo_piece], undo_source);
    set_bit(&board->occupancy[old_side_index], undo_source);
    pop_bit(&board->pieces[undo_piece], undo_target);
    pop_bit(&board->occupancy[old_side_index], undo_target);

    int capture_flag = get_capture_flag(undo_move);
    int enpassant_flag = get_enpassant_flag(undo_move);

    if (capture_flag) {
        if (enpassant_flag) {
            if (old_side_index == white) {
                int target = undo_target + 8;
                set_bit(&board->pieces[p], target);
                set_bit(&board->occupancy[black], target);
            } else {
                int target = undo_target - 8;
                set_bit(&board->pieces[P], target);
                set_bit(&board->occupancy[white], target);
            }
        } else {
            piece captured_piece = undo_element->captured_piece;
            set_bit(&board->pieces[captured_piece], undo_target);
            set_bit(&board->occupancy[board->side], undo_target);
        }
    }

    int king_side_castle_flag = get_king_side_castle_flag(undo_move);
    int queen_side_castle_flag = get_queen_side_castle_flag(undo_move);
    int promoted_piece = get_promoted_piece(undo_move); /* 0 indicates no promotion. */

    if (king_side_castle_flag) {
        piece rook = old_side_index == white ? R : r;
        int from = undo_target + 1;
        int to = undo_target - 1;
        set_bit(&board->pieces[rook], from);
        set_bit(&board->occupancy[old_side_index], from);
        pop_bit(&board->pieces[rook], to);
        pop_bit(&board->occupancy[old_side_index], to);
    } else if (queen_side_castle_flag) {
        piece rook = old_side_index == white ? R : r;
        int from = undo_target - 2;
        int to = undo_target + 1;
        set_bit(&board->pieces[rook], from);
        set_bit(&board->occupancy[old_side_index], from);
        pop_bit(&board->pieces[rook], to);
        pop_bit(&board->occupancy[old_side_index], to);
    } else if (promoted_piece) {
        pop_bit(&board->pieces[promoted_piece], undo_target);
        pop_bit(&board->occupancy[old_side_index], undo_target);
    }

    /* Reset flags and counters. */
    board->castle_flag = undo_element->castle_flag;
    board->en_passant = undo_element->en_passant;
    board->half_move = undo_element->half_move;
    board->full_move = undo_element->full_move;
    board->hash = undo_element->hash;

    board->occupancy[both] = board->occupancy[white] | board->occupancy[black];
    board->side = board->side == white ? black : white;
}

#endif
