#ifndef TIKI_TRANSPOSITION_H
#define TIKI_TRANSPOSITION_H

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
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
} tt_entry_type;

/**
 * Lock-less entry (Hyatt's XOR scheme), safe to share between search threads. The entry is two
 * 64-bit words, 'data' and 'check' = hash ^ data, each loaded and stored atomically. A torn entry
 * (words from two different saves) fails the check and is treated as a miss.
 *
 * Packed to 16 bytes, four entries per cache line. The low bits of the hash give the index
 * (the table size is a power of two), the full hash is kept in the check.
 *
 * 'data' layout:
 *   bits  0-24  move        (25 bits, see move_encoding.h)
 *   bits 25-45  score       (21 bits, signed)
 *   bits 46-53  depth
 *   bits 54-55  entry type
 *   bits 56-63  generation
 */
typedef struct {
    _Atomic uint64_t    check;
    _Atomic uint64_t    data;
} transposition_node_t;

_Static_assert(sizeof(transposition_node_t) == 16, "transposition_node_t should be 16 bytes");

#define TT_MOVE_BITS 25
#define TT_SCORE_BITS 21
#define TT_SCORE_SHIFT 25
#define TT_DEPTH_SHIFT 46
#define TT_TYPE_SHIFT 54
#define TT_GENERATION_SHIFT 56

_Static_assert(INF < (1 << (TT_SCORE_BITS - 1)), "scores must fit in the TT score field");

extern transposition_node_t* t_table;
extern size_t tt_size;
extern uint8_t tt_generation;


#define TT_NOT_FOUND 999999

#define TT_DEFAULT_MB 256
#define TT_MAX_MB 65536

/**
 * Allocates an empty table of 'mb' megabytes (clamped to 1..TT_MAX_MB), rounded down to a power
 * of two entries. If that fails the previous size is allocated again, returns false.
 */
bool init_transposition_table(size_t mb);
void free_transposition_table();


static inline_always uint64_t tt_pack(const move_t move,
                                      const int score,
                                      const int depth,
                                      const tt_entry_type entry_type,
                                      const uint8_t generation) {
    return ((uint64_t) move & ((1ULL << TT_MOVE_BITS) - 1)) |
           (((uint64_t) (uint32_t) score & ((1ULL << TT_SCORE_BITS) - 1)) << TT_SCORE_SHIFT) |
           ((uint64_t) (uint8_t) depth << TT_DEPTH_SHIFT) |
           ((uint64_t) entry_type << TT_TYPE_SHIFT) |
           ((uint64_t) generation << TT_GENERATION_SHIFT);
}

static inline_always move_t tt_data_move(const uint64_t data) {
    return (move_t) (data & ((1ULL << TT_MOVE_BITS) - 1));
}

static inline_always int tt_data_score(const uint64_t data) {
    /* Sign extend the 21-bit field. */
    const int32_t sign = 1 << (TT_SCORE_BITS - 1);
    const int32_t raw = (int32_t) ((data >> TT_SCORE_SHIFT) & ((1ULL << TT_SCORE_BITS) - 1));
    return (raw ^ sign) - sign;
}

static inline_always int tt_data_depth(const uint64_t data) {
    return (int) ((data >> TT_DEPTH_SHIFT) & 0xff);
}

static inline_always tt_entry_type tt_data_type(const uint64_t data) {
    return (tt_entry_type) ((data >> TT_TYPE_SHIFT) & 0x3);
}

static inline_always uint8_t tt_data_generation(const uint64_t data) {
    return (uint8_t) (data >> TT_GENERATION_SHIFT);
}

static inline_always transposition_node_t* tt_entry(const uint64_t position_hash) {
    return &t_table[position_hash & (tt_size - 1)];
}

/* Starts loading the position's entry into the cache, so it's there by the time it's probed. */
static inline_always void tt_prefetch(const uint64_t position_hash) {
    __builtin_prefetch(tt_entry(position_hash));
}

/**
 * Loads the entry's data, true if it belongs to this position (and wasn't torn by a concurrent
 * save). 'data' is written either way, so a save can see what it would replace.
 */
static inline_always bool tt_load(const uint64_t position_hash, uint64_t* data) {
    transposition_node_t* entry = tt_entry(position_hash);
    *data = atomic_load_explicit(&entry->data, memory_order_relaxed);
    const uint64_t check = atomic_load_explicit(&entry->check, memory_order_relaxed);
    return (check ^ *data) == position_hash;
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
    uint64_t data;
    *tt_move = 0;

    if (!tt_load(position_hash, &data)) return TT_NOT_FOUND;
    *tt_move = tt_data_move(data);

    if (tt_data_depth(data) >= depth) {
        /* Stored relative to this node, convert back to distance from the root. */
        int score = tt_data_score(data);
        if (score < -MATE_SCORE) score += ply;
        if (score > MATE_SCORE) score -= ply;

        const tt_entry_type entry_type = tt_data_type(data);
        if (entry_type == tt_exact) return score;
        if (entry_type == tt_alpha && score <= alpha) return alpha;
        if (entry_type == tt_beta && score >= beta) return beta;
    }
    return TT_NOT_FOUND;
}


/**
 * The stored entry for this position whatever its depth and bound, true if there is one. For
 * singular extensions, which need the score itself rather than a cutoff for a window. The score
 * is converted back to distance from the root, as in tt_probe.
 */
static inline_always bool tt_lookup(const uint64_t position_hash,
                                    const int ply,
                                    move_t* move,
                                    int* score,
                                    int* depth,
                                    tt_entry_type* entry_type) {
    uint64_t data;
    if (!tt_load(position_hash, &data)) return false;

    *move = tt_data_move(data);
    *score = tt_data_score(data);
    if (*score < -MATE_SCORE) *score += ply;
    if (*score > MATE_SCORE) *score -= ply;
    *depth = tt_data_depth(data);
    *entry_type = tt_data_type(data);
    return true;
}


static inline_always void tt_save(const uint64_t position_hash,
                                  const tt_entry_type hash_flag,
                                  const int depth,
                                  const int ply,
                                  int score,
                                  move_t move) {
    transposition_node_t* entry = tt_entry(position_hash);
    uint64_t old;
    const bool same_position = tt_load(position_hash, &old);

    /* Keep deeper entries from the current search, entries from earlier searches are replaced. */
    if (tt_data_generation(old) == tt_generation && tt_data_depth(old) > depth) return;

    /* A fail-low has no best move, keep the one already stored for this position. */
    if (move == 0 && same_position) move = tt_data_move(old);

    /* Store mate scores relative to this node, so they're valid at any ply. */
    if (score < -MATE_SCORE) score -= ply;
    if (score > MATE_SCORE) score += ply;

    const uint64_t data = tt_pack(move, score, depth, hash_flag, tt_generation);
    atomic_store_explicit(&entry->data, data, memory_order_relaxed);
    atomic_store_explicit(&entry->check, position_hash ^ data, memory_order_relaxed);
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
