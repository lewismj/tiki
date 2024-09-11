#ifndef TIKI_TYPES_H
#define TIKI_TYPES_H

#include <stdint.h>


/**
 * BitBoard is an unsigned 64 bit integer.
 */
typedef uint64_t bitboard;

/**
 * Flags used for the print_bitboard function.
 */
typedef enum {
    show = 01,      /*  Display full bitboard as square matrix. */
    hex = 02,       /*  Display the hexadecimal value of the bitboard(s). */
} showable;

/**
 * The chessboard squares.
 */
typedef enum : int {
    a8, b8, c8, d8, e8, f8, g8, h8,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a1, b1, c1, d1, e1, f1, g1, h1,
    none_sq
} square;

static char* square_to_str[] = {
        "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8",
        "a7", "b7", "c7", "d7", "e7", "f7", "g7", "h7",
        "a6", "b6", "c6", "d6", "e6", "f6", "g6", "h6",
        "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5",
        "a4", "b4", "c4", "d4", "e4", "f4", "g4", "h4",
        "a3", "b3", "c3", "d3", "e3", "f3", "g3", "h3",
        "a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2",
        "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1", "none"
};

/**
 * Sides (colours)
 */
typedef enum : int { white = 01, black = 02 } colour;

/**
 * Move.
 */
typedef uint32_t move;

/**
 * Pieces, none is used in the move encoding for empty 'promoted piece' field.
 */
typedef enum : int { P, N, B, R, Q, K, p, n, b, r, q, k, none } piece;

static char* piece_to_str[] = {"P",
                                "N",
                                "B",
                                "R",
                                "Q",
                                "K",
                                "p",
                                "n",
                                "b",
                                "r",
                                "q",
                                "k",
                                "none"};

/**
 * Castling rights.
 */
typedef enum {
    white_king  = 01,
    white_queen = 02,
    black_king  = 04,
    black_queen = 010
} castle_rights;

/**
 * A mask function is a function that takes a square and returns a bitboard.
 * This is all the possible squares that the piece may move to, assuming no blockers.
 */
typedef bitboard (*mask_function)(int square_index);

/**
 * A move function is a function that takes a square and a bitboard representing
 * blockers and returns a bitboard representing all the valid positions the piece
 * may move to. Not used 'in game', just for generating magic numbers etc.
 */
typedef bitboard (*move_function)(square s, const bitboard bitboard);

#endif
