#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnue.h"

#if defined(__AVX2__)
#include <immintrin.h>
#endif


nnue_network_t nnue_net;

/* Where nnue_net came from, "" if it was built in code. */
static char loaded_path[1024];

/* Bumped when nnue_net is replaced, 0 is never used (a new stack's cache_generation). */
static uint32_t generation;

static void next_generation(void) {
    if (++generation == 0) generation = 1;
}

uint32_t nnue_generation(void) {
    return generation;
}


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
    next_generation();
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

/* Size of a network in the file format, every valid file is exactly this long. */
#define NNUE_FT_COUNT ((size_t) NNUE_INPUTS * NNUE_HIDDEN)
#define NNUE_FILE_SIZE (8 + 3 * sizeof(uint32_t) + NNUE_FT_COUNT * sizeof(int16_t) + \
                        NNUE_HIDDEN * sizeof(int16_t) + 2 * NNUE_HIDDEN * sizeof(int16_t) + sizeof(int32_t))

/* Copies the next 'bytes' of 'data' to 'out', false if fewer are left. */
static bool take(const uint8_t* data, size_t size, size_t* at, void* out, size_t bytes) {
    if (size - *at < bytes) return false;
    memcpy(out, data + *at, bytes);
    *at += bytes;
    return true;
}

bool nnue_load_memory(const void* data, size_t size, const char* name) {
    if (!little_endian_host() || data == NULL) return false;

    /* Read into a new network, the current one is only replaced once this one is complete. */
    nnue_network_t net = { .ft_weights = malloc(NNUE_FT_COUNT * sizeof(int16_t)) };
    char magic[8];
    uint32_t header[3];
    size_t at = 0;
    bool ok = net.ft_weights != NULL &&
              take(data, size, &at, magic, sizeof(magic)) &&
              memcmp(magic, NNUE_MAGIC, sizeof(magic)) == 0 &&
              take(data, size, &at, header, sizeof(header)) &&
              header[0] == NNUE_VERSION && header[1] == NNUE_INPUTS && header[2] == NNUE_HIDDEN &&
              take(data, size, &at, net.ft_weights, NNUE_FT_COUNT * sizeof(int16_t)) &&
              take(data, size, &at, net.ft_bias, sizeof(net.ft_bias)) &&
              take(data, size, &at, net.out_weights, sizeof(net.out_weights)) &&
              take(data, size, &at, &net.out_bias, sizeof(net.out_bias)) &&
              at == size; /* Nothing after the network. */

    if (!ok) {
        free(net.ft_weights);
        return false;
    }
    nnue_free();
    nnue_net = net;
    snprintf(loaded_path, sizeof(loaded_path), "%s", name);
    next_generation();
    return true;
}

bool nnue_load(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;

    /* One byte more than a network, so a longer file is rejected too. */
    uint8_t* data = malloc(NNUE_FILE_SIZE + 1);
    const size_t size = data != NULL ? fread(data, 1, NNUE_FILE_SIZE + 1, file) : 0;
    fclose(file);

    const bool ok = size == NNUE_FILE_SIZE && nnue_load_memory(data, size, path);
    free(data);
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

static inline const int16_t* feature_row(int feature) {
    return &nnue_net.ft_weights[(size_t) feature * NNUE_HIDDEN];
}

/*
 * The two operations the network needs, SIMD with AVX2 (16 int16 per register) or plain C.
 * Both give exactly the same results: the accumulator is int16 arithmetic either way, and the
 * output layer sums in int64.
 */
#if defined(__AVX2__)

const char* nnue_simd(void) {
    return "avx2";
}

/* out = in + the 'added' rows - the 'removed' rows, over the whole accumulator ('out' may be 'in'). */
static inline void apply_rows(int16_t* out, const int16_t* in, const int16_t* const* added, int added_count,
                              const int16_t* const* removed, int removed_count) {
    for (int j = 0; j < NNUE_HIDDEN; j += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i*) (in + j));
        for (int a = 0; a < added_count; a++) v = _mm256_add_epi16(v, _mm256_loadu_si256((const __m256i*) (added[a] + j)));
        for (int r = 0; r < removed_count; r++) v = _mm256_sub_epi16(v, _mm256_loadu_si256((const __m256i*) (removed[r] + j)));
        _mm256_storeu_si256((__m256i*) (out + j), v);
    }
}

/*
 * sum(SCReLU(values[i]) * weights[i]) for one half of the output layer. SCReLU is up to QA^2 =
 * 65025 and a weight up to 32767 in magnitude, so each product is computed and summed in int64
 * (_mm256_mul_epi32 multiplies the even int32 lanes into int64, the odd ones are shifted down).
 */
static inline __m256i output_half(__m256i sum, const int16_t* values, const int16_t* weights) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16(NNUE_QA);
    for (int j = 0; j < NNUE_HIDDEN; j += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i*) (values + j));
        v = _mm256_min_epi16(_mm256_max_epi16(v, zero), qa);
        const __m256i w = _mm256_loadu_si256((const __m256i*) (weights + j));
        for (int half = 0; half < 2; half++) {
            const __m256i v32 = _mm256_cvtepi16_epi32(half ? _mm256_extracti128_si256(v, 1) : _mm256_castsi256_si128(v));
            const __m256i w32 = _mm256_cvtepi16_epi32(half ? _mm256_extracti128_si256(w, 1) : _mm256_castsi256_si128(w));
            const __m256i squared = _mm256_mullo_epi32(v32, v32);
            sum = _mm256_add_epi64(sum, _mm256_mul_epi32(squared, w32));
            sum = _mm256_add_epi64(sum, _mm256_mul_epi32(_mm256_srli_epi64(squared, 32), _mm256_srli_epi64(w32, 32)));
        }
    }
    return sum;
}

static inline int64_t output_sum(const int16_t* us, const int16_t* them) {
    __m256i sum = output_half(_mm256_setzero_si256(), us, nnue_net.out_weights);
    sum = output_half(sum, them, nnue_net.out_weights + NNUE_HIDDEN);
    int64_t lanes[4];
    _mm256_storeu_si256((__m256i*) lanes, sum);
    return lanes[0] + lanes[1] + lanes[2] + lanes[3];
}

#else

const char* nnue_simd(void) {
    return "scalar";
}

static inline void apply_rows(int16_t* out, const int16_t* in, const int16_t* const* added, int added_count,
                              const int16_t* const* removed, int removed_count) {
    for (int j = 0; j < NNUE_HIDDEN; j++) {
        int v = in[j];
        for (int a = 0; a < added_count; a++) v += added[a][j];
        for (int r = 0; r < removed_count; r++) v -= removed[r][j];
        out[j] = (int16_t) v;
    }
}

static int64_t output_sum_scalar(const int16_t* us, const int16_t* them);

static inline int64_t output_sum(const int16_t* us, const int16_t* them) {
    return output_sum_scalar(us, them);
}

#endif

static inline int64_t screlu(int16_t x) {
    int64_t v = x < 0 ? 0 : x > NNUE_QA ? NNUE_QA : x;
    return v * v;
}

/* Plain C reference, int64 so SCReLU (up to QA^2) times a weight can't overflow. */
static int64_t output_sum_scalar(const int16_t* us, const int16_t* them) {
    int64_t sum = 0;
    for (int i = 0; i < NNUE_HIDDEN; i++) {
        sum += screlu(us[i]) * nnue_net.out_weights[i];
        sum += screlu(them[i]) * nnue_net.out_weights[NNUE_HIDDEN + i];
    }
    return sum;
}

static inline int scale_output(int64_t sum) {
    int64_t result = sum / NNUE_QA + nnue_net.out_bias;
    return (int) (result * NNUE_SCALE / (NNUE_QA * NNUE_QB));
}

int nnue_output(const nnue_accumulator_t* accumulator, colour side) {
    return scale_output(output_sum(accumulator->values[side], accumulator->values[side ^ 1]));
}

int nnue_output_scalar(const nnue_accumulator_t* accumulator, colour side) {
    return scale_output(output_sum_scalar(accumulator->values[side], accumulator->values[side ^ 1]));
}

/* From scratch: the bias plus every active feature's row. */
static void refresh_perspective(const board_t* board, colour perspective, int16_t* values) {
    int features[NNUE_MAX_ACTIVE];
    const int16_t* rows[NNUE_MAX_ACTIVE];
    int n = nnue_active_features(board, perspective, features);
    for (int f = 0; f < n; f++) rows[f] = feature_row(features[f]);
    apply_rows(values, nnue_net.ft_bias, rows, n, NULL, 0);
}

void nnue_refresh(const board_t* board, nnue_accumulator_t* accumulator) {
    for (int c = white; c <= black; c++) refresh_perspective(board, c, accumulator->values[c]);
}

int nnue_evaluate(const board_t* board) {
    nnue_accumulator_t accumulator;
    nnue_refresh(board, &accumulator);
    return nnue_output(&accumulator, board->side);
}

void nnue_stack_init(nnue_stack_t* stack) {
    stack->cache_generation = 0;
    nnue_reset(stack);
}

void nnue_reset(nnue_stack_t* stack) {
    stack->top = 0;
    stack->entries[0].computed[white] = stack->entries[0].computed[black] = false;
}

/* Every cache entry back to the empty board (the bias, no pieces), for the current network. */
static void init_cache(nnue_stack_t* stack) {
    for (int c = white; c <= black; c++) {
        for (int sq = 0; sq < 64; sq++) {
            memcpy(stack->cache[c][sq].values, nnue_net.ft_bias, sizeof(nnue_net.ft_bias));
            memset(stack->cache[c][sq].pieces, 0, sizeof(stack->cache[c][sq].pieces));
        }
    }
    stack->cache_generation = nnue_generation();
}

/* refresh_perspective through the cache: only the pieces that differ from the cached position. */
static void refresh_cached(nnue_stack_t* stack, const board_t* board, colour perspective, int16_t* values) {
    if (stack->cache_generation != nnue_generation()) init_cache(stack);

    const square king = trailing_zero_count(board->pieces[perspective == white ? K : k]);
    nnue_cache_entry_t* entry = &stack->cache[perspective][king];

    /* At most 32 pieces were on the board then, and 32 are now. */
    const int16_t* added[NNUE_MAX_ACTIVE];
    const int16_t* removed[NNUE_MAX_ACTIVE];
    int added_count = 0, removed_count = 0;
    for (int pc = P; pc <= k; pc++) {
        const bitboard now = board->pieces[pc];
        bitboard gone = entry->pieces[pc] & ~now;
        bitboard arrived = now & ~entry->pieces[pc];
        while (gone) removed[removed_count++] = feature_row(nnue_feature_index(perspective, king, pc, get_lsb_and_pop_bit(&gone)));
        while (arrived) added[added_count++] = feature_row(nnue_feature_index(perspective, king, pc, get_lsb_and_pop_bit(&arrived)));
        entry->pieces[pc] = now;
    }
    apply_rows(entry->values, entry->values, added, added_count, removed, removed_count);
    memcpy(values, entry->values, sizeof(entry->values));
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
        refresh_cached(stack, board, perspective, top->accumulator.values[perspective]);
        top->computed[perspective] = true;
        return;
    }

    /* The king hasn't moved since 'from', so it's where it is now. */
    const square king = trailing_zero_count(board->pieces[perspective == white ? K : k]);
    for (int i = from + 1; i <= stack->top; i++) {
        nnue_entry_t* entry = &stack->entries[i];
        const int16_t* added[2];
        const int16_t* removed[2];
        for (int a = 0; a < entry->added_count; a++) {
            added[a] = feature_row(nnue_feature_index(perspective, king, entry->added[a].pc, entry->added[a].sq));
        }
        for (int r = 0; r < entry->removed_count; r++) {
            removed[r] = feature_row(nnue_feature_index(perspective, king, entry->removed[r].pc, entry->removed[r].sq));
        }
        apply_rows(entry->accumulator.values[perspective], stack->entries[i - 1].accumulator.values[perspective],
                   added, entry->added_count, removed, entry->removed_count);
        entry->computed[perspective] = true;
    }
}

int nnue_evaluate_incremental(nnue_stack_t* stack, const board_t* board) {
    nnue_entry_t* top = &stack->entries[stack->top];
    for (int c = white; c <= black; c++) {
        if (!top->computed[c]) update_perspective(stack, board, c);
    }
    return nnue_output(&top->accumulator, board->side);
}
