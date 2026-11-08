#ifndef TIKI_DATAGEN_H
#define TIKI_DATAGEN_H

#include <stdint.h>
#include <stdio.h>

/**
 * Self-play training data for the NNUE trainer (training/). Each game starts with a few random
 * legal moves, then tiki plays itself at a fixed depth. Quiet positions (not in check, best move
 * not a capture or promotion, not a mate score) are written in one of two formats:
 *
 * - datagen_text, one per line:
 *
 *       <fen> | <score> | <result>
 *
 *   score is the search score in centipawns from white's point of view, and result the game result
 *   for white: 1.0, 0.5 or 0.0.
 *
 * - datagen_binary, the trainer's own format (training/tiki_nnue/binary.py): after the file's
 *   DATAGEN_BINARY_MAGIC header, DATAGEN_RECORD_SIZE bytes per position, little endian:
 *
 *       uint16 pieces[32]   piece * 64 + square for each piece in square order (a8 first, tiki's
 *                           piece order P N B R Q K p n b r q k), then 0xFFFF
 *       int16  score        the search score for the side to move
 *       uint8  result       2 / 1 / 0, a win / draw / loss for the side to move
 *       uint8  side         the side to move, 0 white, 1 black
 *
 *   datagen() writes only the records; datagen_to_file writes the header to a new file.
 *
 * Games end on mate, stalemate, the draw rules (fifty moves,
 * insufficient material, repetition), a mate score, a score of +/-DATAGEN_WIN_SCORE for
 * DATAGEN_WIN_PLIES plies in a row (adjudicated win), or max_plies (adjudicated draw).
 */

#define DATAGEN_WIN_SCORE 2000
#define DATAGEN_WIN_PLIES 4
#define DATAGEN_HASH_MB 16
#define DATAGEN_BINARY_MAGIC "TIKIBIN1"
#define DATAGEN_MAGIC_SIZE 8
#define DATAGEN_RECORD_SIZE 68

typedef enum { datagen_text, datagen_binary } datagen_format_t;

typedef struct {
    int games;
    int depth;          /* Search depth per move.                      */
    int random_plies;   /* Random legal moves at the start of a game.  */
    int max_plies;      /* Longer games are adjudicated a draw.        */
    uint32_t seed;      /* Same seed, same games (single threaded).    */
    datagen_format_t format;
} datagen_options_t;

/* Plays the games and writes the positions to 'out', returns the number of positions written. */
uint64_t datagen(FILE* out, const datagen_options_t* options);

#endif
