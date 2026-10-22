#ifndef TIKI_BENCH_H
#define TIKI_BENCH_H

#include <stdint.h>
#include "types.h"

#define BENCH_DEFAULT_DEPTH 8
#define BENCH_HASH_MB 16

/**
 * Fixed set of positions searched by 'bench'.
 */
extern const char* const bench_positions[];
extern const int bench_position_count;

/**
 * Search every bench position to 'depth', with the TT cleared before each one, and print the
 * total node count and nodes per second. The node count depends only on the search (and
 * evaluation), so it's a signature for changes that should not affect them. The TT is resized to
 * BENCH_HASH_MB for the run, and restored afterwards.
 *
 * Returns the total number of nodes searched.
 */
uint64_t bench(int depth);

#endif
