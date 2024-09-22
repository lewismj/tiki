#ifndef TIKI_TYPES_H
#define TIKI_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

#ifndef bool
    #define bool	_Bool
    #define true	1
    #define false	0
#endif


/**
 * BitBoard is an unsigned 64 bit integer.
 */
typedef uint64_t bitboard;

/**
 * Flags used for the print_bitboard function.
 */
typedef enum {
    show = 0x01,      /*  Display full bitboard as square matrix. */
    hex  = 0x02,      /*  Display the hexadecimal value of the bitboard(s). */
    min  = 0x04,      /*  On board, display just the board, no other information, e.g. castle rights etc. */
} showable;

/**
 * The chessboard squares.
 */
typedef enum {
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
        "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
        "none"
};

/**
 * Sides (colours)
 */
typedef enum { white , black, both } colour;

/**
 * Move.
 */
typedef uint32_t move_t;

/**
 * Pieces.
 */
typedef enum { P, N, B, R, Q, K, p, n, b, r, q, k, none } piece;

static int black_pieces[] = { p, n, b, r, q, k};
static int white_pieces[] = { P, N, B, R, Q, K };

static char* piece_to_str[] = {  "P",
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

static int char_to_piece[] = {  ['P'] = P,
                                ['N'] = N,
                                ['B'] = B,
                                ['R'] = R,
                                ['Q'] = Q,
                                ['K'] = K,
                                ['p'] = p,
                                ['n'] = n,
                                ['b'] = b,
                                ['r'] = r,
                                ['q'] = q,
                                ['k'] = k};

/**
 * Castling rights.
 */
typedef enum {
    white_king_side  = 01,
    white_queen_side = 02,
    black_king_side  = 04,
    black_queen_side = 010
} castle_rights;

/**
 * A mask function is a function that takes a square and returns a bitboard.
 * This is all the possible squares that the piece may move_t to, assuming no blockers.
 */
typedef bitboard (*mask_function)(square sq);

/**
 * A move_t function is a function that takes a square and a bitboard representing
 * blockers and returns a bitboard representing all the valid positions the piece
 * may move_t to.
 * Note, the 'attack_mask' functions provide the basic functions for move_t generation,
 * based on the magic numbers generated & mask generators on startup.S
 */
typedef bitboard (*move_function)(square s, const bitboard bitboard);

/** For King and Knight, the move_t function doesn't take into account blockers. */
typedef bitboard (*no_blocker_move_function)(square s);


#define inline_always inline __attribute__((always_inline))
#define align __attribute__((aligned(64)))

#endif
