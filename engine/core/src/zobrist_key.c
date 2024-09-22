#include <stdalign.h>

#include "random.h"
#include "zobrist_key.h"

typedef struct {
    uint64_t piece_keys[64][12];
    uint64_t enpassant_keys[64];
    uint64_t castle_keys[16];
    uint64_t side_key;
} zobrist_key_t;

static alignas(64) zobrist_key_t zk_instance;

void init_zobrist_keys(uint32_t* rng_state) {
    /* Zobrist key for square/piece combinations. */
    for (unsigned int sq = 0; sq < 64; sq++) {
        for (unsigned int p = 0; p < 12; p++) { /* Loop over the pieces. */
            zk_instance.piece_keys[sq][p] = next_random_64(rng_state);
        }
    }

    /* Zobrist key for square being en-passant (Just generate key for every square: allow simple/fast indexing. */
    for (unsigned int sq = 0; sq < 64; sq++) {
        zk_instance.enpassant_keys[sq] = next_random_64(rng_state);
    }

    /* Zobrist key for Castle flag combinations. */
    for (unsigned int i = 0; i < 16; i++) {
        zk_instance.castle_keys[i] = next_random_64(rng_state);
    }

    /* Zobrist key for side to move. */
    zk_instance.side_key = next_random_64(rng_state);
}

void init_zobrist_key() {
    uint32_t rng_state = random_seed;
    init_zobrist_keys(&rng_state);
}

uint64_t get_piece_key(square s, piece p) {
    return zk_instance.piece_keys[s][p];
}

uint64_t get_enpassant_key(square s) {
    return zk_instance.enpassant_keys[s];
}

uint64_t get_castle_key(int castle_flag) {
    return zk_instance.castle_keys[castle_flag % 15];
}

uint64_t get_side_key() {
    return zk_instance.side_key;
}
