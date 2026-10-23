#ifndef TIKI_SHIM_H
#define TIKI_SHIM_H

/**
 * Plain C functions over the engine for the C++ micro-benchmarks. The engine headers are C only
 * (static inline functions, _Static_assert, stdatomic.h), so C++ code can't include them, it
 * calls these instead.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tiki_position tiki_position;

/* Tables, a TT of 'hash_mb' and the network (TIKI_EVALFILE, exits without one), call once first. */
void tiki_init(unsigned short hash_mb);

tiki_position* tiki_position_new(const char* fen);
void tiki_position_free(tiki_position* position);

/* Static evaluation of the position, the network with its accumulators from scratch. */
int tiki_evaluate(const tiki_position* position);

/* Number of pseudo-legal moves generated. */
int tiki_generate_moves(tiki_position* position);

/* Make and undo every generated move, returns the number of legal moves. */
int tiki_make_undo_all(tiki_position* position);

uint64_t tiki_perft(tiki_position* position, int depth);

/* Fixed depth search from a cleared TT, returns the node count. */
uint64_t tiki_search(tiki_position* position, int depth);

#ifdef __cplusplus
}
#endif

#endif
