#ifndef TIKI_TRANSPOSITION_H
#define TIKI_TRANSPOSITION_H

#include <stddef.h>
#include <stdatomic.h>
#include "types.h"
#include "search_constants.h"

/**
 * Even if we have multi-threaded search, we will only ever have a single instance
 * of the transposition table.
 * The size must be configurable as UCI parameter.
 */


/**
 * Flag to encode the entry type. Given that we use 24 bits to encode a move in move_t (uint32_t),
 * we can use bits 25-26 to encode the entry type, without having to introduce new variable,
 * that would increase the memory requirement.
 *
 * i.e.
 * to encode: move_and_type = move |= (hash_flag << 25)
 * to decode: hash_flag = (move_encoding & 0x6000000) >> 25;
 */
typedef enum {
    tt_exact,
    tt_alpha,
    tt_beta
} hash_flag_t;

typedef struct align {
    _Atomic uint64_t position_hash;
    _Atomic uint32_t move_and_type;
    _Atomic uint8_t depth;
    _Atomic int8_t score;
} transposition_node_t;

extern transposition_node_t* t_table;
extern size_t tt_size;

#define TT_NOT_FOUND 999999

/** Create the transposition table, specify maximum size in mega bytes. */
void init_transposition_table(unsigned short mb);

static inline_always int tt_probe(const uint64_t position_hash,
                                  const int depth,
                                  int alpha,
                                  int beta,
                                  move_t* best_move) {
    const size_t index = position_hash % tt_size;

    uint64_t current_position_hash = atomic_load(&t_table[index].position_hash);
    /* Don't worry too much about read hash, write depth/read depth inconsistency. */
    uint8_t current_depth = atomic_load(&t_table[index].depth);

    if (position_hash == current_position_hash && depth == current_depth) {
        *best_move = atomic_load(&t_table[index].move_and_type);
        int score = atomic_load(&t_table[index].score);
        if ((*best_move & 0x6000000) >> 25 == tt_exact) return score;
        if (((*best_move & 0x6000000) >> 25 == tt_alpha) && score <= alpha) return alpha;
        if (((*best_move & 0x6000000) >> 25 == tt_beta) && score >= beta) return beta;
    }

    return TT_NOT_FOUND;
}


static inline_always void tt_save(const uint64_t position_hash,
                                  const hash_flag_t hash_flag,
                                  const move_t move,
                                  const int depth,
                                  int score) {
    const size_t index = position_hash % tt_size;
    uint64_t current_position_hash = atomic_load(&t_table[index].position_hash);
    uint8_t current_depth = atomic_load(&t_table[index].depth);

    if (position_hash == current_position_hash && current_depth >= depth) return;

    transposition_node_t new_node = {
            .position_hash = position_hash,
            .move_and_type = move | (hash_flag << 25),
            .depth = depth,
            .score = score
    };

    if (atomic_compare_exchange_strong(&t_table[index].position_hash,
                                       &current_position_hash,
                                       position_hash)) {
        atomic_store(&t_table[index].move_and_type, new_node.move_and_type);
        atomic_store(&t_table[index].depth, new_node.depth);
        atomic_store(&t_table[index].score, new_node.score);
    } else {
        current_position_hash = atomic_load(&t_table[index].position_hash);
        current_depth = atomic_load(&t_table[index].depth);

        if (position_hash == current_position_hash && current_depth < depth) {
            atomic_store(&t_table[index].move_and_type, new_node.move_and_type);
            atomic_store(&t_table[index].depth, new_node.depth);
            atomic_store(&t_table[index].score, new_node.score);
        }
    }
}

#endif

