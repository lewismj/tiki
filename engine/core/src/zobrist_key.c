#include "random.h"
#include "zobrist_key.h"

align zobrist_key_t zk_instance;

void init_zobrist_keys(uint32_t* rng_state) {
    /* Zobrist key for square/piece combinations. */
    for (unsigned int p = 0; p < 12; p++) { /* Loop over the pieces. */
        for (unsigned int sq = 0; sq < 64; sq++) {
            zk_instance.piece_keys[sq][p] = next_random_64(rng_state);
        }
    }

    /*
     * Zobrist key for square being en-passant, just generate key for every square: allow simple/fast indexing.
     */
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






