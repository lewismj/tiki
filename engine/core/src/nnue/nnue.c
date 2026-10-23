#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnue.h"


nnue_network_t nnue_net;


int nnue_feature_index(colour perspective, square king, piece pc, square sq) {
    /* Orient so the perspective plays up the board (a8 = 0, so black is rank flipped). */
    int oriented_king = perspective == white ? king : king ^ 56;
    int oriented_sq = perspective == white ? sq : sq ^ 56;

    /* Horizontal mirror, the king is always on files a-d. */
    if ((oriented_king & 7) >= 4) {
        oriented_king ^= 7;
        oriented_sq ^= 7;
    }
    int bucket = (oriented_king >> 3) * 4 + (oriented_king & 7);

    int type = pc % 6;
    colour pc_colour = pc >= p ? black : white;
    int kind = type == 5 ? 10 : type * 2 + (pc_colour != perspective);

    return bucket * NNUE_PIECE_SQUARES + kind * 64 + oriented_sq;
}

int nnue_active_features(const board_t* board, colour perspective, int* features) {
    square king = trailing_zero_count(board->pieces[perspective == white ? K : k]);
    int n = 0;
    for (int pc = P; pc <= k; pc++) {
        bitboard pieces = board->pieces[pc];
        while (pieces) {
            square sq = get_lsb_and_pop_bit(&pieces);
            features[n++] = nnue_feature_index(perspective, king, pc, sq);
        }
    }
    return n;
}

bool nnue_allocate(void) {
    nnue_free();
    nnue_net.ft_weights = calloc((size_t) NNUE_INPUTS * NNUE_HIDDEN, sizeof(int16_t));
    memset(nnue_net.ft_bias, 0, sizeof(nnue_net.ft_bias));
    memset(nnue_net.out_weights, 0, sizeof(nnue_net.out_weights));
    nnue_net.out_bias = 0;
    return nnue_net.ft_weights != NULL;
}

void nnue_free(void) {
    free(nnue_net.ft_weights);
    nnue_net.ft_weights = NULL;
}

bool nnue_is_loaded(void) {
    return nnue_net.ft_weights != NULL;
}

/* The format is little endian, read and written directly on a little endian host. */
static bool little_endian_host(void) {
    const uint16_t one = 1;
    return *(const uint8_t*) &one == 1;
}

bool nnue_load(const char* path) {
    if (!little_endian_host()) return false;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;

    char magic[8];
    uint32_t header[3];
    bool ok = fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
              memcmp(magic, NNUE_MAGIC, sizeof(magic)) == 0 &&
              fread(header, sizeof(uint32_t), 3, file) == 3 &&
              header[0] == NNUE_VERSION && header[1] == NNUE_INPUTS && header[2] == NNUE_HIDDEN &&
              nnue_allocate();

    const size_t ft_count = (size_t) NNUE_INPUTS * NNUE_HIDDEN;
    ok = ok &&
         fread(nnue_net.ft_weights, sizeof(int16_t), ft_count, file) == ft_count &&
         fread(nnue_net.ft_bias, sizeof(int16_t), NNUE_HIDDEN, file) == NNUE_HIDDEN &&
         fread(nnue_net.out_weights, sizeof(int16_t), 2 * NNUE_HIDDEN, file) == 2 * NNUE_HIDDEN &&
         fread(&nnue_net.out_bias, sizeof(int32_t), 1, file) == 1 &&
         fgetc(file) == EOF; /* Nothing after the network. */

    fclose(file);
    if (!ok) nnue_free();
    return ok;
}

bool nnue_save(const char* path) {
    if (!nnue_is_loaded() || !little_endian_host()) return false;
    FILE* file = fopen(path, "wb");
    if (file == NULL) return false;

    const uint32_t header[3] = { NNUE_VERSION, NNUE_INPUTS, NNUE_HIDDEN };
    const size_t ft_count = (size_t) NNUE_INPUTS * NNUE_HIDDEN;
    bool ok = fwrite(NNUE_MAGIC, 1, 8, file) == 8 &&
              fwrite(header, sizeof(uint32_t), 3, file) == 3 &&
              fwrite(nnue_net.ft_weights, sizeof(int16_t), ft_count, file) == ft_count &&
              fwrite(nnue_net.ft_bias, sizeof(int16_t), NNUE_HIDDEN, file) == NNUE_HIDDEN &&
              fwrite(nnue_net.out_weights, sizeof(int16_t), 2 * NNUE_HIDDEN, file) == 2 * NNUE_HIDDEN &&
              fwrite(&nnue_net.out_bias, sizeof(int32_t), 1, file) == 1;
    return fclose(file) == 0 && ok;
}

void nnue_refresh(const board_t* board, nnue_accumulator_t* accumulator) {
    int features[NNUE_MAX_ACTIVE];
    for (int c = white; c <= black; c++) {
        int16_t* values = accumulator->values[c];
        memcpy(values, nnue_net.ft_bias, sizeof(nnue_net.ft_bias));
        int n = nnue_active_features(board, c, features);
        for (int f = 0; f < n; f++) {
            const int16_t* row = &nnue_net.ft_weights[(size_t) features[f] * NNUE_HIDDEN];
            for (int i = 0; i < NNUE_HIDDEN; i++) values[i] += row[i];
        }
    }
}

static inline int64_t screlu(int16_t x) {
    int64_t v = x < 0 ? 0 : x > NNUE_QA ? NNUE_QA : x;
    return v * v;
}

int nnue_evaluate(const board_t* board) {
    nnue_accumulator_t accumulator;
    nnue_refresh(board, &accumulator);

    const int16_t* us = accumulator.values[board->side];
    const int16_t* them = accumulator.values[board->side ^ 1];

    /* Scalar reference, int64 so SCReLU (up to QA^2) times a weight can't overflow. */
    int64_t sum = 0;
    for (int i = 0; i < NNUE_HIDDEN; i++) {
        sum += screlu(us[i]) * nnue_net.out_weights[i];
        sum += screlu(them[i]) * nnue_net.out_weights[NNUE_HIDDEN + i];
    }
    int64_t output = sum / NNUE_QA + nnue_net.out_bias;
    return (int) (output * NNUE_SCALE / (NNUE_QA * NNUE_QB));
}
