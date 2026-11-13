#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnue.h"

#if defined(__SSE2__) && !defined(TIKI_NO_SIMD)
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

/* Feature transformer weights, 64 byte aligned like the rest of the network (see nnue.h). */
#define NNUE_FT_BYTES ((size_t) NNUE_INPUTS * NNUE_HIDDEN * sizeof(int16_t))
static_assert(NNUE_FT_BYTES % 64 == 0, "aligned_alloc needs a multiple of the alignment");

/* Hidden layer weights, 256 KB, on the heap like the feature transformer (not in nnue_network_t, which
 * nnue_load_memory builds on the stack). */
#define NNUE_L1_COUNT ((size_t) NNUE_OUTPUT_BUCKETS * NNUE_L1 * NNUE_L1_INPUTS)
static_assert(NNUE_L1_COUNT % 64 == 0, "aligned_alloc needs a multiple of the alignment");

static void* allocate_aligned(size_t bytes) {
#if defined(_WIN32)
    return _aligned_malloc(bytes, 64);
#else
    return aligned_alloc(64, bytes);
#endif
}

static void free_aligned(void* pointer) {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    free(pointer);
#endif
}

static void free_network(nnue_network_t* net) {
    free_aligned(net->ft_weights);
    free_aligned(net->l1_weights);
    net->ft_weights = NULL;
    net->l1_weights = NULL;
}

/* A network with its weight arrays allocated (not initialised), false if either allocation failed
 * (then neither is kept). */
static bool allocate_network(nnue_network_t* net) {
    net->ft_weights = allocate_aligned(NNUE_FT_BYTES);
    net->l1_weights = allocate_aligned(NNUE_L1_COUNT);
    if (net->ft_weights == NULL || net->l1_weights == NULL) {
        free_network(net);
        return false;
    }
    return true;
}

bool nnue_allocate(void) {
    nnue_free();
    next_generation();
    memset(&nnue_net, 0, sizeof(nnue_net));
    if (!allocate_network(&nnue_net)) return false;
    memset(nnue_net.ft_weights, 0, NNUE_FT_BYTES);
    memset(nnue_net.l1_weights, 0, NNUE_L1_COUNT);
    return true;
}

void nnue_free(void) {
    free_network(&nnue_net);
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
#define NNUE_FILE_SIZE (8 + 3 * sizeof(uint32_t) + NNUE_FT_COUNT * sizeof(int16_t) + NNUE_HIDDEN * sizeof(int16_t) + \
                        NNUE_L1_COUNT * sizeof(int8_t) + sizeof(nnue_net.l1_bias) + sizeof(nnue_net.out_weights) + \
                        sizeof(nnue_net.out_bias))

/* Copies the hidden layer weights between the file's order ([bucket][unit][input], 'file') and memory's
 * (nnue_l1_index, 'memory'): into memory if 'load', else out to the file order. The one place the two
 * orders meet, so load and save can't disagree. */
static void convert_l1_weights(int8_t* memory, int8_t* file, bool load) {
    size_t f = 0;
    for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
        for (int u = 0; u < NNUE_L1; u++) {
            for (int i = 0; i < NNUE_L1_INPUTS; i++, f++) {
                if (load) memory[nnue_l1_index(b, u, i)] = file[f];
                else file[f] = memory[nnue_l1_index(b, u, i)];
            }
        }
    }
}

/* The hidden layer weights at 'data' + *at (file order) into 'out' (memory order), false if fewer are left. */
static bool take_l1_weights(const uint8_t* data, size_t size, size_t* at, int8_t* out) {
    if (size - *at < NNUE_L1_COUNT) return false;
    convert_l1_weights(out, (int8_t*) (data + *at), true);
    *at += NNUE_L1_COUNT;
    return true;
}

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
    nnue_network_t net = { 0 };
    char magic[8];
    uint32_t header[3];
    size_t at = 0;
    bool ok = allocate_network(&net) &&
              take(data, size, &at, magic, sizeof(magic)) &&
              memcmp(magic, NNUE_MAGIC, sizeof(magic)) == 0 &&
              take(data, size, &at, header, sizeof(header)) &&
              header[0] == NNUE_VERSION && header[1] == NNUE_INPUTS && header[2] == NNUE_HIDDEN &&
              take(data, size, &at, net.ft_weights, NNUE_FT_COUNT * sizeof(int16_t)) &&
              take(data, size, &at, net.ft_bias, sizeof(net.ft_bias)) &&
              take_l1_weights(data, size, &at, net.l1_weights) &&
              take(data, size, &at, net.l1_bias, sizeof(net.l1_bias)) &&
              take(data, size, &at, net.out_weights, sizeof(net.out_weights)) &&
              take(data, size, &at, net.out_bias, sizeof(net.out_bias)) &&
              at == size; /* Nothing after the network. */

    if (!ok) {
        free_network(&net);
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

    /* A tiki network of another version (the networks before the hidden layer are version 2): say so,
     * rather than leave only "could not load". Version 5 differs only in the output layer's scale. */
    uint32_t version;
    if (!ok && size >= 12 && memcmp(data, NNUE_MAGIC, 8) == 0 &&
        (memcpy(&version, data + 8, sizeof(version)), version != NNUE_VERSION)) {
        fprintf(stderr, "%s is a version %u network, this build of tiki reads version %d.%s\n",
                path, (unsigned) version, NNUE_VERSION,
                version == 5 ? " Convert it with the trainer (read_network, then write_network)." : "");
    }
    free(data);
    return ok;
}

bool nnue_save(const char* path) {
    if (!nnue_is_loaded() || !little_endian_host()) return false;

    /* The hidden layer weights back in the file's order, [bucket][unit][input]. */
    int8_t* l1_file = malloc(NNUE_L1_COUNT);
    if (l1_file == NULL) return false;
    convert_l1_weights(nnue_net.l1_weights, l1_file, false);

    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        free(l1_file);
        return false;
    }
    const uint32_t header[3] = { NNUE_VERSION, NNUE_INPUTS, NNUE_HIDDEN };
    const size_t ft_count = (size_t) NNUE_INPUTS * NNUE_HIDDEN;
    bool ok = fwrite(NNUE_MAGIC, 1, 8, file) == 8 &&
              fwrite(header, sizeof(uint32_t), 3, file) == 3 &&
              fwrite(nnue_net.ft_weights, sizeof(int16_t), ft_count, file) == ft_count &&
              fwrite(nnue_net.ft_bias, sizeof(int16_t), NNUE_HIDDEN, file) == NNUE_HIDDEN &&
              fwrite(l1_file, sizeof(int8_t), NNUE_L1_COUNT, file) == NNUE_L1_COUNT &&
              fwrite(nnue_net.l1_bias, sizeof(nnue_net.l1_bias), 1, file) == 1 &&
              fwrite(nnue_net.out_weights, sizeof(nnue_net.out_weights), 1, file) == 1 &&
              fwrite(nnue_net.out_bias, sizeof(nnue_net.out_bias), 1, file) == 1;
    free(l1_file);
    return fclose(file) == 0 && ok;
}

static inline const int16_t* feature_row(int feature) {
    return &nnue_net.ft_weights[(size_t) feature * NNUE_HIDDEN];
}

/*
 * The parts that run on every evaluation, in AVX2 (16 int16 per register) and plain C, with exactly
 * the same results (integer arithmetic either way): the accumulator update, the hidden layer's inputs
 * (pairwise products, 0..127) and its sums. The plain C versions are always compiled, as the reference
 * the tests check the SIMD ones against (nnue_output_scalar).
 */
#define NNUE_PAIRS (NNUE_HIDDEN / 2)            /* Hidden layer inputs per perspective. */
#define NNUE_GROUPS (NNUE_L1_INPUTS / 4)        /* Groups of 4 inputs (nnue_l1_index). */

/* in[j] = (x * y) >> 9 with x = clamp(v[j], 0, QA) and y = clamp(v[j + NNUE_PAIRS], 0, QA), 0..127:
 * one perspective's NNUE_HIDDEN accumulator values to its NNUE_PAIRS inputs. */
static inline void hidden_inputs_scalar(uint8_t* in, const int16_t* values) {
    for (int j = 0; j < NNUE_PAIRS; j++) {
        const int x = values[j] < 0 ? 0 : values[j] > NNUE_QA ? NNUE_QA : values[j];
        const int y = values[j + NNUE_PAIRS] < 0 ? 0 : values[j + NNUE_PAIRS] > NNUE_QA ? NNUE_QA : values[j + NNUE_PAIRS];
        in[j] = (uint8_t) ((x * y) >> 9);
    }
}

/* sums[u] = sum(in[i] * weight[bucket][u][i]) over the NNUE_L1_INPUTS inputs, for each hidden unit u,
 * a group of 4 inputs at a time over the weights as stored (nnue_l1_index), skipping groups of zeros.
 * At most 512 * 127 * 128 in magnitude, int32. */
static inline void hidden_sums_scalar(int32_t* sums, const uint8_t* in, int bucket) {
    for (int u = 0; u < NNUE_L1; u++) sums[u] = 0;
    const int8_t* weights = nnue_net.l1_weights + nnue_l1_index(bucket, 0, 0);
    for (int g = 0; g < NNUE_GROUPS; g++) {
        const uint8_t* x = in + 4 * g;
        if ((x[0] | x[1] | x[2] | x[3]) == 0) continue;
        const int8_t* w = weights + (size_t) g * NNUE_L1 * 4;
        for (int u = 0; u < NNUE_L1; u++) {
            sums[u] += x[0] * w[4 * u] + x[1] * w[4 * u + 1] + x[2] * w[4 * u + 2] + x[3] * w[4 * u + 3];
        }
    }
}

/*
 * The SIMD versions, one code for every x86 level (TIKI_ARCH): vec_t is a register of NNUE_VEC_BYTES,
 * 64 with AVX-512 (BW), 32 with AVX2, 16 with SSE2. The hidden layer's dot products (uint8 inputs times
 * int8 weights, four at a time into int32) are one VNNI instruction (vpdpbusd) where the CPU has it,
 * otherwise maddubs + madd + add (SSSE3 and up); SSE2 alone has no maddubs, so its hidden layer sums are
 * the plain C ones. Every version gives exactly the scalar results.
 */
#if defined(TIKI_NO_SIMD)

/* Plain C (TIKI_ARCH general-64), below. */

#elif defined(__AVX512BW__)

#define NNUE_VEC_BYTES 64
typedef __m512i vec_t;
#define vec_load(p) _mm512_loadu_si512((const void*) (p))
#define vec_store(p, v) _mm512_storeu_si512((void*) (p), (v))
#define vec_add_16 _mm512_add_epi16
#define vec_sub_16 _mm512_sub_epi16
#define vec_max_16 _mm512_max_epi16
#define vec_min_16 _mm512_min_epi16
#define vec_slli_16 _mm512_slli_epi16
#define vec_mulhi_16 _mm512_mulhi_epi16
#define vec_set1_16 _mm512_set1_epi16
#define vec_set1_32 _mm512_set1_epi32
#define vec_zero _mm512_setzero_si512
#define vec_add_32 _mm512_add_epi32
#define NNUE_TILE_REGISTERS 16                  /* Of 32: the whole accumulator stays in registers. */

/* _mm512_packus_epi16 packs within each 128-bit lane (a0 b0 a1 b1 ... in 64-bit quarters), the
 * permute puts the quarters back in order (a0 a1 a2 a3 b0 b1 b2 b3). */
static inline vec_t vec_packus_16(vec_t a, vec_t b) {
    return _mm512_permutexvar_epi64(_mm512_set_epi64(7, 5, 3, 1, 6, 4, 2, 0), _mm512_packus_epi16(a, b));
}

#if defined(__AVX512VNNI__)
#define NNUE_SIMD_NAME "avx512-vnni"
#define vec_dpbusd _mm512_dpbusd_epi32
#else
#define NNUE_SIMD_NAME "avx512"
static inline vec_t vec_dpbusd(vec_t sum, vec_t x, vec_t w) {
    return _mm512_add_epi32(sum, _mm512_madd_epi16(_mm512_maddubs_epi16(x, w), _mm512_set1_epi16(1)));
}
#endif

#elif defined(__AVX2__)

#define NNUE_VEC_BYTES 32
typedef __m256i vec_t;
#define vec_load(p) _mm256_loadu_si256((const __m256i*) (p))
#define vec_store(p, v) _mm256_storeu_si256((__m256i*) (p), (v))
#define vec_add_16 _mm256_add_epi16
#define vec_sub_16 _mm256_sub_epi16
#define vec_max_16 _mm256_max_epi16
#define vec_min_16 _mm256_min_epi16
#define vec_slli_16 _mm256_slli_epi16
#define vec_mulhi_16 _mm256_mulhi_epi16
#define vec_set1_16 _mm256_set1_epi16
#define vec_set1_32 _mm256_set1_epi32
#define vec_zero _mm256_setzero_si256
#define vec_add_32 _mm256_add_epi32
#define NNUE_TILE_REGISTERS 8                   /* Of 16, the rest hold the rows' loads. */

/* Within each 128-bit half (a0 b0 a1 b1 in 64-bit quarters), the permute restores a0 a1 b0 b1. */
static inline vec_t vec_packus_16(vec_t a, vec_t b) {
    return _mm256_permute4x64_epi64(_mm256_packus_epi16(a, b), 0xD8);
}

#if defined(__AVXVNNI__)
#define NNUE_SIMD_NAME "avx-vnni"
#define vec_dpbusd _mm256_dpbusd_avx_epi32
#else
#define NNUE_SIMD_NAME "avx2"
static inline vec_t vec_dpbusd(vec_t sum, vec_t x, vec_t w) {
    return _mm256_add_epi32(sum, _mm256_madd_epi16(_mm256_maddubs_epi16(x, w), _mm256_set1_epi16(1)));
}
#endif

#elif defined(__SSE2__)

#define NNUE_VEC_BYTES 16
typedef __m128i vec_t;
#define vec_load(p) _mm_loadu_si128((const __m128i*) (p))
#define vec_store(p, v) _mm_storeu_si128((__m128i*) (p), (v))
#define vec_add_16 _mm_add_epi16
#define vec_sub_16 _mm_sub_epi16
#define vec_max_16 _mm_max_epi16
#define vec_min_16 _mm_min_epi16
#define vec_slli_16 _mm_slli_epi16
#define vec_mulhi_16 _mm_mulhi_epi16
#define vec_set1_16 _mm_set1_epi16
#define vec_set1_32 _mm_set1_epi32
#define vec_zero _mm_setzero_si128
#define vec_add_32 _mm_add_epi32
#define vec_packus_16 _mm_packus_epi16          /* One lane, already in order. */
#define NNUE_TILE_REGISTERS 8                   /* Of 16. */

#if defined(__SSSE3__)
#define NNUE_SIMD_NAME "ssse3"
static inline vec_t vec_dpbusd(vec_t sum, vec_t x, vec_t w) {
    return _mm_add_epi32(sum, _mm_madd_epi16(_mm_maddubs_epi16(x, w), _mm_set1_epi16(1)));
}
#else
#define NNUE_SIMD_NAME "sse2"
#define NNUE_SCALAR_SUMS                        /* No maddubs: plain C hidden layer sums. */
#endif

#endif

#if defined(NNUE_VEC_BYTES)

const char* nnue_simd(void) {
    return NNUE_SIMD_NAME;
}

/*
 * out = in + the 'added' rows - the 'removed' rows, over the whole accumulator ('out' may be 'in').
 * A tile of the accumulator stays in registers while every row is applied to it.
 */
#define NNUE_TILE (NNUE_TILE_REGISTERS * NNUE_VEC_BYTES / 2)
static_assert(NNUE_HIDDEN % NNUE_TILE == 0, "the accumulator is a whole number of tiles");

static inline void apply_rows(int16_t* out, const int16_t* in, const int16_t* const* added, int added_count,
                              const int16_t* const* removed, int removed_count) {
    enum { lanes = NNUE_VEC_BYTES / 2 };
    for (int t = 0; t < NNUE_HIDDEN; t += NNUE_TILE) {
        vec_t v[NNUE_TILE_REGISTERS];
        for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_load(in + t + lanes * k);
        for (int a = 0; a < added_count; a++) {
            const int16_t* row = added[a] + t;
            for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_add_16(v[k], vec_load(row + lanes * k));
        }
        for (int r = 0; r < removed_count; r++) {
            const int16_t* row = removed[r] + t;
            for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_sub_16(v[k], vec_load(row + lanes * k));
        }
        for (int k = 0; k < NNUE_TILE_REGISTERS; k++) vec_store(out + t + lanes * k, v[k]);
    }
}

/*
 * hidden_inputs_scalar, a register of inputs at a time, as Stockfish does it: x (clamped to 0..QA)
 * shifted left 7 is at most 32640, so it fits int16, and mulhi by y gives (x * 128 * y) >> 16 =
 * (x * y) >> 9 in one instruction. y is only clamped above (to QA): a negative y gives a negative
 * product, which packus saturates to 0, the same as clamping it to 0 first. Results 0..127, so they
 * pack to uint8 exactly.
 */
static_assert((NNUE_QA << 7) <= INT16_MAX, "x << 7 must fit int16");
static_assert(NNUE_PAIRS % NNUE_VEC_BYTES == 0, "a register of inputs a step");

static inline vec_t scaled_x(const int16_t* values) {
    return vec_slli_16(vec_min_16(vec_max_16(vec_load(values), vec_zero()), vec_set1_16(NNUE_QA)), 7);
}

static inline vec_t capped_y(const int16_t* values) {
    return vec_min_16(vec_load(values), vec_set1_16(NNUE_QA));
}

static inline void hidden_inputs(uint8_t* in, const int16_t* values) {
    enum { lanes = NNUE_VEC_BYTES / 2 };
    for (int j = 0; j < NNUE_PAIRS; j += NNUE_VEC_BYTES) {
        const vec_t p = vec_mulhi_16(scaled_x(values + j), capped_y(values + NNUE_PAIRS + j));
        const vec_t q = vec_mulhi_16(scaled_x(values + j + lanes), capped_y(values + NNUE_PAIRS + j + lanes));
        vec_store(in + j, vec_packus_16(p, q));
    }
}

#if defined(NNUE_SCALAR_SUMS)

static inline void hidden_sums(int32_t* sums, const uint8_t* in, int bucket) {
    hidden_sums_scalar(sums, in, bucket);
}

#else

/*
 * hidden_sums_scalar. The weights are stored by group of 4 inputs (nnue_l1_index): 16 units x 4 weights,
 * 64 bytes, NNUE_SUM_VECS registers (one with AVX-512, two with AVX2, four with SSE). A group's 4 inputs
 * are broadcast to every 32-bit lane and dot-multiplied (vec_dpbusd) by each unit's 4 weights into that
 * unit's int32 lane: each lane of the sums is one unit's sum, in order, with no horizontal additions.
 * Without VNNI maddubs adds pairs into int16 first, at most 2 * 127 * 128 = 32512, so it never saturates
 * and every version gives the same sums. NNUE_CHAINS groups a step into separate sums (four registers of
 * sums in flight), added at the end.
 *
 * Every group is computed: skipping the groups of 4 zero inputs (Stockfish's sparse first layer) was
 * slower, as with these networks over 80% of groups have a non-zero input (35% of the inputs are
 * non-zero, spread out). Reordering the accumulator after training so zeros share groups could change
 * that.
 */
#define NNUE_SUM_VECS (NNUE_L1 * 4 / NNUE_VEC_BYTES)
#define NNUE_CHAINS (4 / NNUE_SUM_VECS)
static_assert(2 * NNUE_QI * 128 <= INT16_MAX, "maddubs pairs must not saturate");
static_assert(NNUE_L1 * 4 % NNUE_VEC_BYTES == 0, "a group's weights are whole registers");
static_assert(NNUE_GROUPS % NNUE_CHAINS == 0, "NNUE_CHAINS groups a step");

static inline void hidden_sums(int32_t* sums, const uint8_t* in, int bucket) {
    const int8_t* weights = nnue_net.l1_weights + nnue_l1_index(bucket, 0, 0);
    vec_t acc[NNUE_CHAINS][NNUE_SUM_VECS];
    for (int c = 0; c < NNUE_CHAINS; c++) {
        for (int k = 0; k < NNUE_SUM_VECS; k++) acc[c][k] = vec_zero();
    }
    for (int group = 0; group < NNUE_GROUPS; group += NNUE_CHAINS) {
        for (int c = 0; c < NNUE_CHAINS; c++) {
            int32_t four;
            memcpy(&four, in + 4 * (group + c), sizeof(four));
            const vec_t x = vec_set1_32(four);
            const int8_t* w = weights + (size_t) (group + c) * NNUE_L1 * 4;
            for (int k = 0; k < NNUE_SUM_VECS; k++) acc[c][k] = vec_dpbusd(acc[c][k], x, vec_load(w + NNUE_VEC_BYTES * k));
        }
    }
    for (int k = 0; k < NNUE_SUM_VECS; k++) {
        vec_t total = acc[0][k];
        for (int c = 1; c < NNUE_CHAINS; c++) total = vec_add_32(total, acc[c][k]);
        vec_store(sums + k * (NNUE_VEC_BYTES / 4), total);
    }
}

#endif

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

static inline void hidden_inputs(uint8_t* in, const int16_t* values) {
    hidden_inputs_scalar(in, values);
}

static inline void hidden_sums(int32_t* sums, const uint8_t* in, int bucket) {
    hidden_sums_scalar(sums, in, bucket);
}

#endif

/* The rest of bucket 'bucket's layer stack from the hidden layer sums: bias, CReLU, the output layer,
 * in centipawns (nnue.h). int64: a clamped activation (up to QH) times an int16 weight, 16 of them (at
 * most 16 * 8128 * 32768 + 2^31, times SCALE, well inside int64). */
static inline int stack_output(const int32_t* sums, int bucket) {
    int64_t out = nnue_net.out_bias[bucket];
    for (int u = 0; u < NNUE_L1; u++) {
        int64_t h = (int64_t) sums[u] + nnue_net.l1_bias[bucket][u];
        h = h < 0 ? 0 : h > NNUE_QH ? NNUE_QH : h;
        out += h * nnue_net.out_weights[bucket][u];
    }
    return (int) (out * NNUE_SCALE / ((int64_t) NNUE_QH * NNUE_QO));
}

int nnue_output(const nnue_accumulator_t* accumulator, colour side, int bucket) {
    uint8_t in[NNUE_L1_INPUTS];
    int32_t sums[NNUE_L1];
    hidden_inputs(in, accumulator->values[side]);
    hidden_inputs(in + NNUE_PAIRS, accumulator->values[side ^ 1]);
    hidden_sums(sums, in, bucket);
    return stack_output(sums, bucket);
}

int nnue_output_scalar(const nnue_accumulator_t* accumulator, colour side, int bucket) {
    uint8_t in[NNUE_L1_INPUTS];
    int32_t sums[NNUE_L1];
    hidden_inputs_scalar(in, accumulator->values[side]);
    hidden_inputs_scalar(in + NNUE_PAIRS, accumulator->values[side ^ 1]);
    hidden_sums_scalar(sums, in, bucket);
    return stack_output(sums, bucket);
}

int nnue_board_bucket(const board_t* board) {
    const colour side = board->side;
    return nnue_output_bucket(pop_count(board->occupancy[both]), board->pieces[side == white ? Q : q] != 0,
                              board->pieces[side == white ? q : Q] != 0);
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
    return nnue_output(&accumulator, board->side, nnue_board_bucket(board));
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
    return nnue_output(&top->accumulator, board->side, nnue_board_bucket(board));
}
