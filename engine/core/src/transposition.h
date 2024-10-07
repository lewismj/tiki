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
    tt_exact    = 0x1,
    tt_alpha    = 0x2,
    tt_beta     = 0x4
} hash_flag_t;

typedef struct align {
    uint64_t position_hash;
    uint32_t move_and_type;
    uint8_t depth;
    int score;
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
    if (position_hash == t_table[index].position_hash) {
        *best_move = t_table[index].move_and_type;
        if (depth == t_table[index].depth) {
            int score = t_table[index].score;
            if ((t_table[index].move_and_type & 0x6000000) >> 25 & tt_exact) {
                return score;
            }
            if ( ((t_table[index].move_and_type & 0x6000000) >> 25 & tt_alpha) && score <= alpha) {
                return alpha;
            }
            if ( ((t_table[index].move_and_type & 0x6000000) >> 25 & tt_beta) && score >= beta) {
                return beta;
            }
        }
    }
    return TT_NOT_FOUND;
}

static inline_always void tt_save(const uint64_t position_hash,
                                  const hash_flag_t hash_flag,
                                  const move_t move,
                                  const int depth,
                                  int score)
                                  {

    const size_t index = position_hash % tt_size;
    if (position_hash == t_table[index].position_hash && t_table[index].depth >= depth) return;
    t_table[index].position_hash = position_hash;
    t_table[index].move_and_type = move | (hash_flag << 25);
    t_table[index].depth = depth;
    t_table[index].score = score;
}

#endif

