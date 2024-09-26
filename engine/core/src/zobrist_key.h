#ifndef TIKI_ZOBRIST_KEY_H
#define TIKI_ZOBRIST_KEY_H

#include <inttypes.h>
#include "types.h"


typedef struct {
    uint64_t piece_keys[64][12];
    uint64_t enpassant_keys[64];
    uint64_t castle_keys[16];
    uint64_t side_key;
} zobrist_key_t;
extern zobrist_key_t zk_instance;

void init_zobrist_key();

static inline_always uint64_t get_piece_key(square s, piece p) {
    return zk_instance.piece_keys[s][p];
}

static inline_always uint64_t get_enpassant_key(square s) {
    return zk_instance.enpassant_keys[s];
}

static inline_always uint64_t get_castle_key(int castle_flag) {
    return zk_instance.castle_keys[castle_flag % 15];
}

static inline_always uint64_t get_side_key() {
    return zk_instance.side_key;
}

#endif
