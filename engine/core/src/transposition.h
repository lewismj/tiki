#ifndef TIKI_TRANSPOSITION_H
#define TIKI_TRANSPOSITION_H

#include <stddef.h>
#include <stdatomic.h>
#include "types.h"
#include "search_constants.h"

/**
 * Even if we have multi-threaded search, we will only ever have a single instance of the transposition table.
 * The size must be configurable as UCI parameter.
 */

typedef enum {
    tt_exact = 1,
    tt_alpha,
    tt_beta
} hash_flag_t;

typedef struct align {
    uint64_t    position_hash;
    uint8_t     depth;
    hash_flag_t entry_type;
    uint8_t     score;
} transposition_node_t;

extern transposition_node_t* t_table;
extern size_t tt_size;

#define TT_NOT_FOUND 999999

/** Create the transposition table, specify maximum size in mega bytes. */
void init_transposition_table(unsigned short mb);
void free_transposition_table();

static int tt_num_reads = 0;


static inline_always int tt_probe(const uint64_t position_hash, const int depth, int alpha, int beta) {
    const size_t index = position_hash % tt_size;
    if (t_table[index].position_hash == position_hash && t_table[index].depth >= depth) {
        ++tt_num_reads;
        if (t_table[index].entry_type == tt_exact) return t_table[index].score;
        if (t_table[index].entry_type == tt_alpha && t_table[index].score <= alpha) return alpha;
        if (t_table[index].entry_type == tt_beta && t_table[index].score >= beta) return beta;
        --tt_num_reads;
    }
    return TT_NOT_FOUND;
}


static inline_always
void tt_save(const uint64_t position_hash, const hash_flag_t hash_flag, const move_t move, const int depth, int score) {
    const size_t index = position_hash % tt_size;
    t_table[index].position_hash = position_hash;
    t_table[index].entry_type = hash_flag;
    t_table[index].score = score;
    t_table[index].depth = depth;
}

#endif

