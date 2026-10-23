#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnue.h"


nnue_network_t nnue_net;

/* Where nnue_net came from, "" if it was built in code. */
static char loaded_path[1024];


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
    loaded_path[0] = '\0';
    memset(nnue_net.ft_bias, 0, sizeof(nnue_net.ft_bias));
    memset(nnue_net.out_weights, 0, sizeof(nnue_net.out_weights));
    nnue_net.out_bias = 0;
    return nnue_net.ft_weights != NULL;
}

void nnue_free(void) {
    free(nnue_net.ft_weights);
    nnue_net.ft_weights = NULL;
    loaded_path[0] = '\0';
}

const char* nnue_loaded_path(void) {
    return loaded_path;
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

    /* Read into a new network, the current one is only replaced once this one is complete. */
    const size_t ft_count = (size_t) NNUE_INPUTS * NNUE_HIDDEN;
    nnue_network_t net = { .ft_weights = malloc(ft_count * sizeof(int16_t)) };
    char magic[8];
    uint32_t header[3];
    bool ok = net.ft_weights != NULL &&
              fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
              memcmp(magic, NNUE_MAGIC, sizeof(magic)) == 0 &&
              fread(header, sizeof(uint32_t), 3, file) == 3 &&
              header[0] == NNUE_VERSION && header[1] == NNUE_INPUTS && header[2] == NNUE_HIDDEN &&
              fread(net.ft_weights, sizeof(int16_t), ft_count, file) == ft_count &&
              fread(net.ft_bias, sizeof(int16_t), NNUE_HIDDEN, file) == NNUE_HIDDEN &&
              fread(net.out_weights, sizeof(int16_t), 2 * NNUE_HIDDEN, file) == 2 * NNUE_HIDDEN &&
              fread(&net.out_bias, sizeof(int32_t), 1, file) == 1 &&
              fgetc(file) == EOF; /* Nothing after the network. */
    fclose(file);

    if (!ok) {
        free(net.ft_weights);
        return false;
    }
    nnue_free();
    nnue_net = net;
    snprintf(loaded_path, sizeof(loaded_path), "%s", path);
    return true;
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

static void refresh_perspective(const board_t* board, colour perspective, int16_t* values) {
    int features[NNUE_MAX_ACTIVE];
    memcpy(values, nnue_net.ft_bias, sizeof(nnue_net.ft_bias));
    int n = nnue_active_features(board, perspective, features);
    for (int f = 0; f < n; f++) {
        const int16_t* row = &nnue_net.ft_weights[(size_t) features[f] * NNUE_HIDDEN];
        for (int i = 0; i < NNUE_HIDDEN; i++) values[i] += row[i];
    }
}

void nnue_refresh(const board_t* board, nnue_accumulator_t* accumulator) {
    for (int c = white; c <= black; c++) refresh_perspective(board, c, accumulator->values[c]);
}

static inline int64_t screlu(int16_t x) {
    int64_t v = x < 0 ? 0 : x > NNUE_QA ? NNUE_QA : x;
    return v * v;
}

static int output(const nnue_accumulator_t* accumulator, colour side) {
    const int16_t* us = accumulator->values[side];
    const int16_t* them = accumulator->values[side ^ 1];

    /* Scalar reference, int64 so SCReLU (up to QA^2) times a weight can't overflow. */
    int64_t sum = 0;
    for (int i = 0; i < NNUE_HIDDEN; i++) {
        sum += screlu(us[i]) * nnue_net.out_weights[i];
        sum += screlu(them[i]) * nnue_net.out_weights[NNUE_HIDDEN + i];
    }
    int64_t result = sum / NNUE_QA + nnue_net.out_bias;
    return (int) (result * NNUE_SCALE / (NNUE_QA * NNUE_QB));
}

int nnue_evaluate(const board_t* board) {
    nnue_accumulator_t accumulator;
    nnue_refresh(board, &accumulator);
    return output(&accumulator, board->side);
}

void nnue_reset(nnue_stack_t* stack) {
    stack->top = 0;
    stack->entries[0].computed[white] = stack->entries[0].computed[black] = false;
}

static inline void add_change(nnue_piece_square_t* changes, int8_t* count, piece pc, square sq) {
    changes[*count].pc = pc;
    changes[(*count)++].sq = sq;
}

void nnue_push(nnue_stack_t* stack, const board_t* board, move_t move) {
    assert(stack->top + 1 < NNUE_STACK_SIZE);
    nnue_entry_t* entry = &stack->entries[++stack->top];
    const colour mover = board->side ^ 1;   /* make_move has already switched sides. */
    const piece moved = get_piece_moved(move);
    const square source = get_source_square(move);
    const square target = get_target_square(move);
    const piece promoted = get_promoted_piece(move);

    entry->computed[white] = entry->computed[black] = false;
    entry->refresh[white] = moved == K;
    entry->refresh[black] = moved == k;
    entry->added_count = entry->removed_count = 0;

    add_change(entry->removed, &entry->removed_count, moved, source);
    add_change(entry->added, &entry->added_count, promoted ? promoted : moved, target);

    if (get_capture_flag(move)) {
        if (get_enpassant_flag(move)) {
            add_change(entry->removed, &entry->removed_count,
                       mover == white ? p : P, mover == white ? target + 8 : target - 8);
        } else {
            add_change(entry->removed, &entry->removed_count, board->stack[board->stack_ptr - 1].captured_piece, target);
        }
    } else if (get_king_side_castle_flag(move) || get_queen_side_castle_flag(move)) {
        const piece rook = mover == white ? R : r;
        const bool king_side = get_king_side_castle_flag(move);
        add_change(entry->removed, &entry->removed_count, rook, king_side ? target + 1 : target - 2);
        add_change(entry->added, &entry->added_count, rook, king_side ? target - 1 : target + 1);
    }
}

/* Brings the top entry's 'perspective' accumulator up to date. */
static void update_perspective(nnue_stack_t* stack, const board_t* board, colour perspective) {
    nnue_entry_t* top = &stack->entries[stack->top];

    /* Back to the nearest computed accumulator, unless the king moved on the way. */
    int from = stack->top;
    while (from > 0 && !stack->entries[from].computed[perspective] && !stack->entries[from].refresh[perspective]) {
        from--;
    }
    if (!stack->entries[from].computed[perspective]) {
        refresh_perspective(board, perspective, top->accumulator.values[perspective]);
        top->computed[perspective] = true;
        return;
    }

    /* The king hasn't moved since 'from', so it's where it is now. */
    const square king = trailing_zero_count(board->pieces[perspective == white ? K : k]);
    for (int i = from + 1; i <= stack->top; i++) {
        nnue_entry_t* entry = &stack->entries[i];
        const int16_t* previous = stack->entries[i - 1].accumulator.values[perspective];
        int16_t* values = entry->accumulator.values[perspective];

        const int16_t* added[2];
        const int16_t* removed[2];
        for (int a = 0; a < entry->added_count; a++) {
            added[a] = &nnue_net.ft_weights[(size_t) nnue_feature_index(perspective, king, entry->added[a].pc,
                                                                        entry->added[a].sq) * NNUE_HIDDEN];
        }
        for (int r = 0; r < entry->removed_count; r++) {
            removed[r] = &nnue_net.ft_weights[(size_t) nnue_feature_index(perspective, king, entry->removed[r].pc,
                                                                          entry->removed[r].sq) * NNUE_HIDDEN];
        }

        /* Fused for the common cases, one pass over the accumulator. */
        if (entry->added_count == 1 && entry->removed_count == 1) {
            for (int j = 0; j < NNUE_HIDDEN; j++) values[j] = previous[j] + added[0][j] - removed[0][j];
        } else if (entry->added_count == 1) {
            for (int j = 0; j < NNUE_HIDDEN; j++) values[j] = previous[j] + added[0][j] - removed[0][j] - removed[1][j];
        } else {
            for (int j = 0; j < NNUE_HIDDEN; j++) {
                values[j] = previous[j] + added[0][j] + added[1][j] - removed[0][j] - removed[1][j];
            }
        }
        entry->computed[perspective] = true;
    }
}

int nnue_evaluate_incremental(nnue_stack_t* stack, const board_t* board) {
    nnue_entry_t* top = &stack->entries[stack->top];
    for (int c = white; c <= black; c++) {
        if (!top->computed[c]) update_perspective(stack, board, c);
    }
    return output(&top->accumulator, board->side);
}
