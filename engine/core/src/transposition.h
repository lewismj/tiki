#ifndef TIKI_TRANSPOSITION_H
#define TIKI_TRANSPOSITION_H

#include <stddef.h>
#include <string.h>
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

/**
 * Packed to 16 bytes, four entries per cache line. The low bits of the hash give the index
 * (the table size is a power of two), so only the upper 32 bits are stored as a check.
 */
typedef struct {
    uint32_t            key;
    move_t              move;
    int32_t             score;
    uint8_t             depth;
    uint8_t             entry_type;
    uint8_t             generation;
    uint8_t             padding;
} transposition_node_t;

_Static_assert(sizeof(transposition_node_t) == 16, "transposition_node_t should be 16 bytes");

extern transposition_node_t* t_table;
extern size_t tt_size;
extern uint8_t tt_generation;


#define TT_NOT_FOUND 999999

void init_transposition_table(unsigned short mb);
void free_transposition_table();


static inline_always uint32_t tt_key(const uint64_t position_hash) {
    return (uint32_t) (position_hash >> 32);
}

static inline_always transposition_node_t* tt_entry(const uint64_t position_hash) {
    return &t_table[position_hash & (tt_size - 1)];
}

/**
 * Returns the score if the entry gives a cutoff for this window, otherwise TT_NOT_FOUND.
 * The stored best move is written to 'tt_move' whenever the position matches, regardless of depth.
 */
static inline_always int tt_probe(const uint64_t position_hash,
                                  const int depth,
                                  const int ply,
                                  int alpha,
                                  int beta,
                                  move_t* tt_move) {
    const transposition_node_t* entry = tt_entry(position_hash);
    *tt_move = 0;

    if (entry->key != tt_key(position_hash)) return TT_NOT_FOUND;
    *tt_move = entry->move;

    if (entry->depth >= depth) {
        /* Stored relative to this node, convert back to distance from the root. */
        int score = entry->score;
        if (score < -MATE_SCORE) score += ply;
        if (score > MATE_SCORE) score -= ply;

        if (entry->entry_type == tt_exact) return score;
        if (entry->entry_type == tt_alpha && score <= alpha) return alpha;
        if (entry->entry_type == tt_beta && score >= beta) return beta;
    }
    return TT_NOT_FOUND;
}


static inline_always void tt_save(const uint64_t position_hash,
                                  const tt_entry_type hash_flag,
                                  const int depth,
                                  const int ply,
                                  int score,
                                  move_t move) {
    transposition_node_t* entry = tt_entry(position_hash);
    const uint32_t key = tt_key(position_hash);

    /* Keep deeper entries from the current search, entries from earlier searches are replaced. */
    if (entry->generation == tt_generation && entry->depth > depth) return;

    /* A fail-low has no best move, keep the one already stored for this position. */
    if (move == 0 && entry->key == key) move = entry->move;

    /* Store mate scores relative to this node, so they're valid at any ply. */
    if (score < -MATE_SCORE) score -= ply;
    if (score > MATE_SCORE) score += ply;

    entry->key = key;
    entry->move = move;
    entry->score = score;
    entry->depth = (uint8_t) depth;
    entry->entry_type = (uint8_t) hash_flag;
    entry->generation = tt_generation;
}

/**
 * Called at the start of each search ('go'). Entries are kept, but older ones become
 * preferred for replacement.
 */
static inline_always void tt_new_search() {
    ++tt_generation;
}

static inline_always void tt_clear() {
    memset(t_table, 0, tt_size * sizeof(transposition_node_t));
    tt_generation = 0;
}


#endif
