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
 * making structure lock-less.
 */

typedef enum {
    tt_exact = 1,
    tt_alpha,
    tt_beta
} tt_entry_type;

typedef struct align {
    uint64_t            position_hash;
    uint8_t             depth;
    tt_entry_type       entry_type;
    uint8_t             score;
    pthread_rwlock_t    lock;  /* Alternative is Atomic __uint128_t and pack the above, use atomic load/store etc. */
} transposition_node_t;

extern transposition_node_t* t_table;
extern size_t tt_size;


#define TT_NOT_FOUND 999999

void init_transposition_table(unsigned short mb);
void free_transposition_table();


static inline_always int tt_probe(const uint64_t position_hash, const int depth, int alpha, int beta) {
    const size_t index = position_hash % tt_size;

    pthread_rwlock_rdlock(&t_table->lock);
    int res = TT_NOT_FOUND;
    if (t_table[index].position_hash == position_hash && t_table[index].depth >= depth) {
        if (t_table[index].entry_type == tt_exact) res = t_table[index].score;
        else if (t_table[index].entry_type == tt_alpha && t_table[index].score <= alpha) res = alpha;
        else if (t_table[index].entry_type == tt_beta && t_table[index].score >= beta) res = beta;
    }
    pthread_rwlock_unlock(&t_table->lock);
    return res;
}


static inline_always
void tt_save(const uint64_t position_hash, const tt_entry_type hash_flag, const int depth, int score) {
    const size_t index = position_hash % tt_size;
    pthread_rwlock_wrlock(&t_table->lock);
    t_table[index].position_hash = position_hash;
    t_table[index].entry_type = hash_flag;
    t_table[index].score = score;
    t_table[index].depth = depth;
    pthread_rwlock_unlock(&t_table->lock);
}


#endif

