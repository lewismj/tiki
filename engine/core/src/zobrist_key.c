#include <stdlib.h>

#include "random.h"
#include "zobrist_key.h"

typedef struct {
    uint64_t** piece_keys;
    uint64_t* enpassant_keys;
    uint64_t* castle_keys;
    uint64_t side_key;
} zobrist_key_t;

static zobrist_key_t* zk_instance = NULL;


void free_zobrist_key_instance() {
    for (unsigned int sq=0; sq<64; sq++) {
        free(zk_instance->piece_keys[sq]);
    }
    free(zk_instance->piece_keys);
    free(zk_instance->enpassant_keys);
    free(zk_instance->castle_keys);
    free(zk_instance);
}

zobrist_key_t* create_zobrist_key(uint32_t* rng_state) {
    zk_instance = (zobrist_key_t*) malloc(sizeof(zobrist_key_t));

    if (zk_instance != NULL) {

        /* Zobrist key for square/piece combinations. */
        zk_instance->piece_keys = (uint64_t**) malloc(64 * sizeof(uint64_t*));
        for (unsigned int sq=0; sq<64; sq++) {
            zk_instance->piece_keys[sq] = (uint64_t*) malloc(12 * sizeof(uint64_t));
            for (unsigned int p=0; p<12; p++) { /* Loop over the pieces. */
                zk_instance->piece_keys[sq][p] = next_random_64(rng_state);
            }
        }

        /* Zobrist key for square being en-passant (Just generate key for every square: allow simple/fast indexing. */
        zk_instance->enpassant_keys = (uint64_t*) malloc(64 * sizeof(uint64_t));
        for (unsigned int sq=0; sq<64; sq++) {
            zk_instance->enpassant_keys[sq] = next_random_64(rng_state);
        }

        /* Zobrist key for Castle flag combinations. */
        zk_instance->castle_keys = (uint64_t*) malloc(16 * sizeof(uint64_t));
        for (unsigned int i=0; i<16; i++) {
            zk_instance->castle_keys[i]= next_random_64(rng_state);
        }

        /* Zobrist key for side to move. */
        zk_instance->side_key = next_random_64(rng_state);

        atexit(free_zobrist_key_instance);
    }
    return zk_instance;
}

void init_zobrist_key() {
    uint32_t rng_state = random_seed;
    create_zobrist_key(&rng_state);
}

uint64_t get_piece_key(square s, piece p) {
    return zk_instance->piece_keys[s][p];
}

uint64_t get_enpassant_key(square s) {
    return zk_instance->enpassant_keys[s];
}

uint64_t get_castle_key(int castle_flag) {
    return zk_instance->castle_keys[castle_flag % 15];
}

uint64_t get_side_key() {
    return zk_instance->side_key;
}
