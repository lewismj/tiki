#ifndef TIKI_DATAGEN_H
#define TIKI_DATAGEN_H

#include <stdint.h>
#include <stdio.h>

/**
 * Self-play training data for the NNUE trainer (training/). Each game starts with a few random
 * legal moves, then tiki plays itself at a fixed depth. Quiet positions (not in check, best move
 * not a capture or promotion, not a mate score) are written one per line as
 *
 *     <fen> | <score> | <result>
 *
 * score is the search score in centipawns from white's point of view, and result the game result
 * for white: 1.0, 0.5 or 0.0. Games end on mate, stalemate, the draw rules (fifty moves,
 * insufficient material, repetition), a mate score, a score of +/-DATAGEN_WIN_SCORE for
 * DATAGEN_WIN_PLIES plies in a row (adjudicated win), or max_plies (adjudicated draw).
 */

#define DATAGEN_WIN_SCORE 2000
#define DATAGEN_WIN_PLIES 4
#define DATAGEN_HASH_MB 16

typedef struct {
    int games;
    int depth;          /* Search depth per move.                      */
    int random_plies;   /* Random legal moves at the start of a game.  */
    int max_plies;      /* Longer games are adjudicated a draw.        */
    uint32_t seed;      /* Same seed, same games (single threaded).    */
} datagen_options_t;

/* Plays the games and writes the positions to 'out', returns the number of positions written. */
uint64_t datagen(FILE* out, const datagen_options_t* options);

#endif
