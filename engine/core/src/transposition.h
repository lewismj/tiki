#ifndef TIKI_TRANSPOSITION_H
#define TIKI_TRANSPOSITION_H

#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>

#include "types.h"
#include "search_constants.h"

/**
 * Even if we have multi-threaded search, we will only ever have a single instance of the transposition table.
 * The size must be configurable as UCI parameter.
 */

/**
 * Multithreading choice - pack/unpack the entry so that we can use an Atomic __uint128_t and
 * have atomic load/store. Or, use r/w lock. It isn't 100% that we would have any speed advantage by
 * making structure lock-less. Also relying on non-determinism (spawning threads for each depth when
 * iterative deepening may not yield any results?).
 */

typedef enum {
    tt_exact = 1,
    tt_alpha,
    tt_beta
} tt_entry_type;

typedef struct align {
    uint64_t            position_hash;      /* board hash value.                */
    uint32_t            move_and_type;      /* packed move and entry type.      */
    uint8_t             depth;              /* depth.                           */
    int                 score;              /* score.                           */
} transposition_node_t;

extern transposition_node_t* t_table;
extern size_t tt_size;


#define TT_NOT_FOUND 999999

void init_transposition_table(unsigned short mb);
void free_transposition_table();


static inline_always int tt_probe(const uint64_t position_hash,
                                  const int depth,
                                  const int ply,
                                  int alpha,
                                  int beta,
                                  move_t* best_move) {
    const size_t index = position_hash % tt_size;

    uint64_t current_position_hash = t_table[index].position_hash;
    uint8_t current_depth =t_table[index].depth;

    if (position_hash == current_position_hash && depth == current_depth) {
        *best_move = t_table[index].move_and_type;
        int score = t_table[index].score;

        if (score < -MATE_SCORE) score -= ply;
        if (score > MATE_SCORE) score += ply;

        if ((*best_move & 0x6000000) >> 25 == tt_exact) return score;
        if (((*best_move & 0x6000000) >> 25 == tt_alpha) && score <= alpha) return alpha;
        if (((*best_move & 0x6000000) >> 25 == tt_beta) && score >= beta) return beta;
    }

    return TT_NOT_FOUND;
}


static inline_always void tt_save(const uint64_t position_hash,
                                  const tt_entry_type hash_flag,
                                  const move_t move,
                                  const int depth,
                                  const int ply,
                                  int score) {

    const size_t index = position_hash % tt_size;
    uint64_t current_position_hash = t_table[index].position_hash;
    uint8_t current_depth = t_table[index].depth;

    if (position_hash == current_position_hash && current_depth >= depth) return;

    if (score < -MATE_SCORE) score -= ply;
    if (score > MATE_SCORE) score += ply;

    t_table[index].position_hash = position_hash;
    t_table[index].move_and_type = move | (hash_flag << 25);
    t_table[index].depth = depth;
    t_table[index].score = score;
}


#endif

