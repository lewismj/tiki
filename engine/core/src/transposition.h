#ifndef TIKI_TRANSPOSITION_H
#define TIKI_TRANSPOSITION_H

#include <stddef.h>

#include "types.h"
#include "search_constants.h"

/**
 * Even if we have multi-threaded search, we will only ever have a single instance
 * of the transposition table.
 * The size must be configurable as UCI parameter.
 */

typedef enum {
    hash_flag_exact = 0x0,      /* Entry in t_table is exact evaluation.     */
    hash_flag_alpha = 0x2,      /* Entry in t_table, is alpha lower bound.   */
    hash_flag_beta  = 0x4       /* Entry in t_table, is beta upper bound.    */
} hash_flag_t;

typedef struct align {
    uint64_t position_hash;
    move_t best_move;
    int depth;
    int score;
    hash_flag_t hash_flag;
} transposition_node_t;

extern transposition_node_t* t_table;
extern size_t tt_size;

/** Create the transposition table, specify maximum size in mega bytes. */
void init_transposition_table(unsigned short mb);

static inline_always bool try_find_score(const uint64_t position_hash_key,
                                         const int depth,
                                         const int alpha,
                                         const int beta,
                                         const int ply,
                                         move_t* best_move,
                                         int* score) {
    const size_t index = position_hash_key % tt_size;
    if (position_hash_key == t_table[index].position_hash) {
        if (depth == t_table[index].depth) {

        }
        *best_move = t_table[index].best_move;
    }
    return false;
}

static inline_always void insert_position(const uint64_t hash_key,
                                          const hash_flag_t hash_flag,
                                          const move_t move,
                                          const int depth,
                                          int score,
                                          const int ply) {

    if (score < -mate_score) score -= ply;
    else if (score > mate_score) score += ply;

    transposition_node_t* node = &t_table[hash_key % tt_size];
    node->position_hash = hash_key;
    node->best_move = move;
    node->depth = depth;
    node->hash_flag = hash_flag;
    node->score = score;
}

#endif

