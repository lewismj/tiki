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

int nnue_pawn_id(colour perspective, square king, colour pawn_colour, square sq) {
    const int oriented_king = perspective == white ? king : king ^ 56;
    int oriented_sq = perspective == white ? sq : sq ^ 56;
    if ((oriented_king & 7) >= 4) oriented_sq ^= 7;     /* Mirrored as the piece features are. */
    return (pawn_colour != perspective) * 48 + oriented_sq - 8;
}

int nnue_pair_index(int id_a, int id_b) {
    const int hi = id_a > id_b ? id_a : id_b, lo = id_a > id_b ? id_b : id_a;
    return NNUE_INPUTS + hi * (hi - 1) / 2 + lo;
}

/* The squares a pawn on 'sq' pairs with: its own and the neighbouring files, less its own square. */
static inline bitboard pair_window(square sq) {
    const int file = sq & 7;
    const bitboard files = 0x0101010101010101ULL << file;
    return (files | (file > 0 ? files >> 1 : 0) | (file < 7 ? files << 1 : 0)) & ~(1ULL << sq);
}

/*
 * The pawn pairs that involve a pawn of 'changed' (per colour), each with a pawn of 'changed' or 'kept': with
 * 'changed' the pawns that left and 'kept' those that stayed, the pairs that went; with the pawns that arrived,
 * those that came. A pair of two changed pawns is written once (the first is taken out before its partners are
 * looked for). Writes feature indices to 'features' from 'n', returns the new count. Stockfish's way.
 */
static int pair_features(int* features, int n, colour perspective, square king, const bitboard changed[2],
                         const bitboard kept[2]) {
    /* nnue_pawn_id with the perspective's orientation worked out once: id = base[colour] + (sq ^ flip). */
    const int oriented_king = perspective == white ? king : king ^ 56;
    const int flip = (perspective == white ? 0 : 56) ^ ((oriented_king & 7) >= 4 ? 7 : 0);
    const int base[2] = { (perspective != white) * 48 - 8, (perspective != black) * 48 - 8 };
    bitboard rest[2] = { changed[white], changed[black] };
    for (int c = white; c <= black; c++) {
        while (rest[c]) {
            const square sq = get_lsb_and_pop_bit(&rest[c]);
            const int id = base[c] + (sq ^ flip);
            const bitboard window = pair_window(sq);
            for (int c2 = white; c2 <= black; c2++) {
                bitboard partners = (kept[c2] | rest[c2]) & window;
                while (partners) {
                    const int other = base[c2] + (get_lsb_and_pop_bit(&partners) ^ flip);
                    const int hi = id > other ? id : other, lo = id > other ? other : id;
                    features[n++] = NNUE_INPUTS + hi * (hi - 1) / 2 + lo;
                }
            }
        }
    }
    return n;
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
    /* Every pair of the pawns on the board: all of them "changed" from none. */
    const bitboard pawns[2] = { board->pieces[P], board->pieces[p] };
    const bitboard none[2] = { 0, 0 };
    return pair_features(features, n, perspective, king, pawns, none);
}

/* Feature transformer weights, 64 byte aligned like the rest of the network (see nnue.h). */
#define NNUE_FT_BYTES ((size_t) NNUE_INPUTS * NNUE_HIDDEN * sizeof(int16_t))
static_assert(NNUE_FT_BYTES % 64 == 0, "aligned_alloc needs a multiple of the alignment");
#define NNUE_PAIR_BYTES ((size_t) NNUE_PAIR_FEATURES * NNUE_HIDDEN)
static_assert(NNUE_PAIR_BYTES % 64 == 0, "aligned_alloc needs a multiple of the alignment");

/* Hidden layer weights, 256 KB, on the heap like the feature transformer (not in nnue_network_t, which
 * nnue_load_memory builds on the stack). */
#define NNUE_L1_COUNT ((size_t) NNUE_OUTPUT_BUCKETS * NNUE_L1 * NNUE_L1_INPUTS)
static_assert(NNUE_L1_COUNT % 64 == 0, "aligned_alloc needs a multiple of the alignment");
#define NNUE_L2_COUNT ((size_t) NNUE_OUTPUT_BUCKETS * NNUE_L2 * NNUE_L2_INPUTS)
static_assert(NNUE_L2_COUNT % 64 == 0, "aligned_alloc needs a multiple of the alignment");

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
    free_aligned(net->pair_weights);
    free_aligned(net->l1_weights);
    free_aligned(net->l2_weights);
    net->ft_weights = NULL;
    net->pair_weights = NULL;
    net->l1_weights = NULL;
    net->l2_weights = NULL;
}

/* A network with its weight arrays allocated (not initialised), false if any allocation failed
 * (then none is kept). */
static bool allocate_network(nnue_network_t* net) {
    net->ft_weights = allocate_aligned(NNUE_FT_BYTES);
    net->pair_weights = allocate_aligned(NNUE_PAIR_BYTES);
    net->l1_weights = allocate_aligned(NNUE_L1_COUNT);
    net->l2_weights = allocate_aligned(NNUE_L2_COUNT);
    if (net->ft_weights == NULL || net->pair_weights == NULL || net->l1_weights == NULL || net->l2_weights == NULL) {
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
    memset(nnue_net.pair_weights, 0, NNUE_PAIR_BYTES);
    memset(nnue_net.l1_weights, 0, NNUE_L1_COUNT);
    memset(nnue_net.l2_weights, 0, NNUE_L2_COUNT);
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
#define NNUE_FILE_SIZE (8 + 3 * sizeof(uint32_t) + NNUE_FT_COUNT * sizeof(int16_t) + NNUE_PAIR_BYTES + \
                        NNUE_HIDDEN * sizeof(int16_t) + \
                        NNUE_L1_COUNT * sizeof(int8_t) + sizeof(nnue_net.l1_bias) + NNUE_L2_COUNT * sizeof(int8_t) + \
                        sizeof(nnue_net.l2_bias) + sizeof(nnue_net.out_weights) + sizeof(nnue_net.out_bias))

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

/* convert_l1_weights for the second hidden layer ([bucket][unit][input] and nnue_l2_index). */
static void convert_l2_weights(int8_t* memory, int8_t* file, bool load) {
    size_t f = 0;
    for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
        for (int u = 0; u < NNUE_L2; u++) {
            for (int i = 0; i < NNUE_L2_INPUTS; i++, f++) {
                if (load) memory[nnue_l2_index(b, u, i)] = file[f];
                else file[f] = memory[nnue_l2_index(b, u, i)];
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

static bool take_l2_weights(const uint8_t* data, size_t size, size_t* at, int8_t* out) {
    if (size - *at < NNUE_L2_COUNT) return false;
    convert_l2_weights(out, (int8_t*) (data + *at), true);
    *at += NNUE_L2_COUNT;
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
              header[0] == NNUE_VERSION && header[1] == NNUE_TOTAL_INPUTS && header[2] == NNUE_HIDDEN &&
              take(data, size, &at, net.ft_weights, NNUE_FT_COUNT * sizeof(int16_t)) &&
              take(data, size, &at, net.pair_weights, NNUE_PAIR_BYTES) &&
              take(data, size, &at, net.ft_bias, sizeof(net.ft_bias)) &&
              take_l1_weights(data, size, &at, net.l1_weights) &&
              take(data, size, &at, net.l1_bias, sizeof(net.l1_bias)) &&
              take_l2_weights(data, size, &at, net.l2_weights) &&
              take(data, size, &at, net.l2_bias, sizeof(net.l2_bias)) &&
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

    /* A tiki network of another version (those before pawn pairs and the second hidden layer are version 6):
     * say so, rather than leave only "could not load". */
    uint32_t version;
    if (!ok && size >= 12 && memcmp(data, NNUE_MAGIC, 8) == 0 &&
        (memcpy(&version, data + 8, sizeof(version)), version != NNUE_VERSION)) {
        fprintf(stderr, "%s is a version %u network, this build of tiki reads version %d.\n",
                path, (unsigned) version, NNUE_VERSION);
    }
    free(data);
    return ok;
}

bool nnue_save(const char* path) {
    if (!nnue_is_loaded() || !little_endian_host()) return false;

    /* The hidden layers' weights back in the file's order, [bucket][unit][input]. */
    int8_t* l1_file = malloc(NNUE_L1_COUNT);
    int8_t* l2_file = malloc(NNUE_L2_COUNT);
    FILE* file = l1_file != NULL && l2_file != NULL ? fopen(path, "wb") : NULL;
    if (file == NULL) {
        free(l1_file);
        free(l2_file);
        return false;
    }
    convert_l1_weights(nnue_net.l1_weights, l1_file, false);
    convert_l2_weights(nnue_net.l2_weights, l2_file, false);
    const uint32_t header[3] = { NNUE_VERSION, NNUE_TOTAL_INPUTS, NNUE_HIDDEN };
    const size_t ft_count = (size_t) NNUE_INPUTS * NNUE_HIDDEN;
    bool ok = fwrite(NNUE_MAGIC, 1, 8, file) == 8 &&
              fwrite(header, sizeof(uint32_t), 3, file) == 3 &&
              fwrite(nnue_net.ft_weights, sizeof(int16_t), ft_count, file) == ft_count &&
              fwrite(nnue_net.pair_weights, 1, NNUE_PAIR_BYTES, file) == NNUE_PAIR_BYTES &&
              fwrite(nnue_net.ft_bias, sizeof(int16_t), NNUE_HIDDEN, file) == NNUE_HIDDEN &&
              fwrite(l1_file, sizeof(int8_t), NNUE_L1_COUNT, file) == NNUE_L1_COUNT &&
              fwrite(nnue_net.l1_bias, sizeof(nnue_net.l1_bias), 1, file) == 1 &&
              fwrite(l2_file, sizeof(int8_t), NNUE_L2_COUNT, file) == NNUE_L2_COUNT &&
              fwrite(nnue_net.l2_bias, sizeof(nnue_net.l2_bias), 1, file) == 1 &&
              fwrite(nnue_net.out_weights, sizeof(nnue_net.out_weights), 1, file) == 1 &&
              fwrite(nnue_net.out_bias, sizeof(nnue_net.out_bias), 1, file) == 1;
    free(l1_file);
    free(l2_file);
    return fclose(file) == 0 && ok;
}

static inline const int16_t* feature_row(int feature) {
    return &nnue_net.ft_weights[(size_t) feature * NNUE_HIDDEN];
}

static inline const int8_t* pair_row(int feature) {
    return &nnue_net.pair_weights[(size_t) (feature - NNUE_INPUTS) * NNUE_HIDDEN];
}

/* The rows an accumulator update adds and subtracts: piece features' (int16) and pawn pairs' (int8). */
typedef struct {
    int added_count, removed_count, pairs_added_count, pairs_removed_count;
    const int16_t* added[NNUE_MAX_ACTIVE];
    const int16_t* removed[NNUE_MAX_ACTIVE];
    const int8_t* pairs_added[NNUE_MAX_PAIRS];
    const int8_t* pairs_removed[NNUE_MAX_PAIRS];
} row_changes_t;

/* Adds a feature's row to 'changes' as added ('add') or removed, by its kind. */
static inline void change_feature(row_changes_t* changes, int feature, bool add) {
    if (feature < NNUE_INPUTS) {
        if (add) changes->added[changes->added_count++] = feature_row(feature);
        else changes->removed[changes->removed_count++] = feature_row(feature);
    } else {
        if (add) changes->pairs_added[changes->pairs_added_count++] = pair_row(feature);
        else changes->pairs_removed[changes->pairs_removed_count++] = pair_row(feature);
    }
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
 * one perspective's NNUE_HIDDEN accumulator values v (the piece features' and the pawn pairs' sum, within
 * int16, see nnue.h) to its NNUE_PAIRS inputs. */
static inline void hidden_inputs_scalar(uint8_t* in, const int16_t* values) {
    for (int j = 0; j < NNUE_PAIRS; j++) {
        const int vx = values[j], vy = values[j + NNUE_PAIRS];
        const int x = vx < 0 ? 0 : vx > NNUE_QA ? NNUE_QA : vx;
        const int y = vy < 0 ? 0 : vy > NNUE_QA ? NNUE_QA : vy;
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

/* sums[u] = sum(in[i] * l2 weight[bucket][u][i]) over the NNUE_L2_INPUTS activations, for each unit of the
 * second hidden layer, a group of 4 inputs at a time over the weights as stored (nnue_l2_index). */
static inline void l2_sums_scalar(int32_t* sums, const uint8_t* in, int bucket) {
    for (int u = 0; u < NNUE_L2; u++) sums[u] = 0;
    const int8_t* weights = nnue_net.l2_weights + nnue_l2_index(bucket, 0, 0);
    for (int g = 0; g < NNUE_L2_INPUTS / 4; g++) {
        const uint8_t* x = in + 4 * g;
        const int8_t* w = weights + (size_t) g * NNUE_L2 * 4;
        for (int u = 0; u < NNUE_L2; u++) {
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
#define vec_load_i8(p) _mm512_cvtepi8_epi16(_mm256_loadu_si256((const __m256i*) (p)))   /* 32 int8 to int16 */
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
#define vec_load_i8(p) _mm256_cvtepi8_epi16(_mm_loadu_si128((const __m128i*) (p)))      /* 16 int8 to int16 */
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
/* 8 int8 to int16 with SSE2 alone: each byte doubled into a 16-bit lane, then shifted down arithmetically. */
static inline vec_t vec_load_i8(const int8_t* p) {
    const __m128i x = _mm_loadl_epi64((const __m128i*) p);
    return _mm_srai_epi16(_mm_unpacklo_epi8(x, x), 8);
}
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
 * out = in + the added rows - the removed rows of 'changes', over the whole accumulator ('out' may be 'in').
 * A tile of the accumulator stays in registers while every row is applied to it; the pawn pairs' int8 rows
 * are sign-extended as they're loaded.
 */
#define NNUE_TILE (NNUE_TILE_REGISTERS * NNUE_VEC_BYTES / 2)
static_assert(NNUE_HIDDEN % NNUE_TILE == 0, "the accumulator is a whole number of tiles");

static inline void apply_rows(int16_t* out, const int16_t* in, const row_changes_t* changes) {
    enum { lanes = NNUE_VEC_BYTES / 2 };
    for (int t = 0; t < NNUE_HIDDEN; t += NNUE_TILE) {
        vec_t v[NNUE_TILE_REGISTERS];
        for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_load(in + t + lanes * k);
        for (int a = 0; a < changes->added_count; a++) {
            const int16_t* row = changes->added[a] + t;
            for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_add_16(v[k], vec_load(row + lanes * k));
        }
        for (int r = 0; r < changes->removed_count; r++) {
            const int16_t* row = changes->removed[r] + t;
            for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_sub_16(v[k], vec_load(row + lanes * k));
        }
        for (int a = 0; a < changes->pairs_added_count; a++) {
            const int8_t* row = changes->pairs_added[a] + t;
            for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_add_16(v[k], vec_load_i8(row + lanes * k));
        }
        for (int r = 0; r < changes->pairs_removed_count; r++) {
            const int8_t* row = changes->pairs_removed[r] + t;
            for (int k = 0; k < NNUE_TILE_REGISTERS; k++) v[k] = vec_sub_16(v[k], vec_load_i8(row + lanes * k));
        }
        for (int k = 0; k < NNUE_TILE_REGISTERS; k++) vec_store(out + t + lanes * k, v[k]);
    }
}

/*
 * hidden_inputs_scalar, a register of inputs at a time: x (clamped to 0..QA)
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

static inline void l2_sums(int32_t* sums, const uint8_t* in, int bucket) {
    l2_sums_scalar(sums, in, bucket);
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

/* l2_sums_scalar the same way: a group's 32 units x 4 weights are NNUE_L2_SUM_VECS registers. The inputs are
 * 0..127, as the first layer's, so maddubs can't saturate here either. */
#define NNUE_L2_SUM_VECS (NNUE_L2 * 4 / NNUE_VEC_BYTES)
static_assert(NNUE_L2 * 4 % NNUE_VEC_BYTES == 0, "a group's weights are whole registers");

static inline void l2_sums(int32_t* sums, const uint8_t* in, int bucket) {
    const int8_t* weights = nnue_net.l2_weights + nnue_l2_index(bucket, 0, 0);
    vec_t acc[NNUE_L2_SUM_VECS];
    for (int k = 0; k < NNUE_L2_SUM_VECS; k++) acc[k] = vec_zero();
    for (int group = 0; group < NNUE_L2_INPUTS / 4; group++) {
        int32_t four;
        memcpy(&four, in + 4 * group, sizeof(four));
        const vec_t x = vec_set1_32(four);
        const int8_t* w = weights + (size_t) group * NNUE_L2 * 4;
        for (int k = 0; k < NNUE_L2_SUM_VECS; k++) acc[k] = vec_dpbusd(acc[k], x, vec_load(w + NNUE_VEC_BYTES * k));
    }
    for (int k = 0; k < NNUE_L2_SUM_VECS; k++) vec_store(sums + k * (NNUE_VEC_BYTES / 4), acc[k]);
}

#endif

#else

const char* nnue_simd(void) {
    return "scalar";
}

static inline void apply_rows(int16_t* out, const int16_t* in, const row_changes_t* changes) {
    for (int j = 0; j < NNUE_HIDDEN; j++) {
        int v = in[j];
        for (int a = 0; a < changes->added_count; a++) v += changes->added[a][j];
        for (int r = 0; r < changes->removed_count; r++) v -= changes->removed[r][j];
        for (int a = 0; a < changes->pairs_added_count; a++) v += changes->pairs_added[a][j];
        for (int r = 0; r < changes->pairs_removed_count; r++) v -= changes->pairs_removed[r][j];
        out[j] = (int16_t) v;
    }
}

static inline void hidden_inputs(uint8_t* in, const int16_t* values) {
    hidden_inputs_scalar(in, values);
}

static inline void hidden_sums(int32_t* sums, const uint8_t* in, int bucket) {
    hidden_sums_scalar(sums, in, bucket);
}

static inline void l2_sums(int32_t* sums, const uint8_t* in, int bucket) {
    l2_sums_scalar(sums, in, bucket);
}

#endif

/* A hidden layer's 'n' sums (with their biases, scale QH) to its 2n activations (nnue.h): each sum clamped to
 * 0..QH, then >> 6 (CReLU, 0..127) for all of them, then (x * x) >> 19 (SqrCReLU, 0..126). */
static_assert(NNUE_QH == NNUE_QU * 64, "CReLU's >> 6 maps QH to 127");
static_assert((int64_t) NNUE_QH * NNUE_QH <= INT32_MAX, "the square of a clamped sum fits int32");
static inline void activations(uint8_t* restrict out, const int32_t* restrict h, int n) {
    /* Two plain loops over int32, which the compiler vectorises for the CPU level built for. */
    for (int i = 0; i < n; i++) {
        const int32_t x = h[i] < 0 ? 0 : h[i] > NNUE_QH ? NNUE_QH : h[i];
        out[i] = (uint8_t) (x >> 6);
    }
    for (int i = 0; i < n; i++) {
        const int32_t x = h[i] < 0 ? 0 : h[i] > NNUE_QH ? NNUE_QH : h[i];
        out[n + i] = (uint8_t) ((x * x) >> 19);
    }
}

/*
 * The rest of bucket 'bucket's layer stack from the first hidden layer's sums, in centipawns (nnue.h), the
 * second layer's sums by SIMD ('simd') or the plain C reference: everything else is the same code. int32
 * until the end: a sum is at most 512 * 127 * 128 (first layer) or 32 * 127 * 128 plus its bias (the trainer's
 * biases are a few QH; a file with biases near 2^31 would overflow), a square at most QH * QH, the output's
 * sum at most 96 * 127 * 32768 plus its bias; the final scaling is int64.
 */
static inline int stack_output(const int32_t* sums, int bucket, bool simd) {
    int32_t h0[NNUE_L1], h1[NNUE_L2];
    uint8_t a[NNUE_OUT_INPUTS] align;
    int32_t sums1[NNUE_L2] align;
    for (int u = 0; u < NNUE_L1; u++) h0[u] = sums[u] + nnue_net.l1_bias[bucket][u];
    activations(a, h0, NNUE_L1);
    if (simd) l2_sums(sums1, a, bucket);
    else l2_sums_scalar(sums1, a, bucket);
    for (int u = 0; u < NNUE_L2; u++) h1[u] = sums1[u] + nnue_net.l2_bias[bucket][u];
    activations(a + NNUE_L2_INPUTS, h1, NNUE_L2);

    const int16_t* weights = nnue_net.out_weights[bucket];
    int32_t out = nnue_net.out_bias[bucket];
    for (int i = 0; i < NNUE_OUT_INPUTS; i++) out += a[i] * weights[i];
    const int64_t skip = (int64_t) h0[NNUE_SKIP_PLUS] - h0[NNUE_SKIP_MINUS];
    return (int) (((int64_t) out * (NNUE_QH / NNUE_QU) + skip * NNUE_QO) * NNUE_SCALE /
                  ((int64_t) NNUE_QH * NNUE_QO));
}

int nnue_output(const nnue_accumulator_t* accumulator, colour side, int bucket) {
    uint8_t in[NNUE_L1_INPUTS];
    int32_t sums[NNUE_L1] align;
    hidden_inputs(in, accumulator->values[side]);
    hidden_inputs(in + NNUE_PAIRS, accumulator->values[side ^ 1]);
    hidden_sums(sums, in, bucket);
    return stack_output(sums, bucket, true);
}

int nnue_output_scalar(const nnue_accumulator_t* accumulator, colour side, int bucket) {
    uint8_t in[NNUE_L1_INPUTS];
    int32_t sums[NNUE_L1];
    hidden_inputs_scalar(in, accumulator->values[side]);
    hidden_inputs_scalar(in + NNUE_PAIRS, accumulator->values[side ^ 1]);
    hidden_sums_scalar(sums, in, bucket);
    return stack_output(sums, bucket, false);
}

int nnue_board_bucket(const board_t* board) {
    const colour side = board->side;
    return nnue_output_bucket(pop_count(board->occupancy[both]), board->pieces[side == white ? Q : q] != 0,
                              board->pieces[side == white ? q : Q] != 0);
}

/* From scratch: the bias plus every active feature's row. */
static void refresh_perspective(const board_t* board, colour perspective, int16_t* values) {
    int features[NNUE_MAX_FEATURES];
    row_changes_t changes;
    changes.added_count = changes.removed_count = changes.pairs_added_count = changes.pairs_removed_count = 0;
    const int n = nnue_active_features(board, perspective, features);
    for (int f = 0; f < n; f++) change_feature(&changes, features[f], true);
    apply_rows(values, nnue_net.ft_bias, &changes);
}

/* A perspective's pawn ids worked out once (nnue_pawn_id): id = base[colour] + (sq ^ flip). */
typedef struct {
    int flip;
    int base[2];
} pair_orientation_t;

static inline pair_orientation_t pair_orientation(colour perspective, square king) {
    const int oriented_king = perspective == white ? king : king ^ 56;
    const pair_orientation_t o = { (perspective == white ? 0 : 56) ^ ((oriented_king & 7) >= 4 ? 7 : 0),
                                   { (perspective != white) * 48 - 8, (perspective != black) * 48 - 8 } };
    return o;
}

/* The weight row of the pair of pawn ids 'a' and 'b' (nnue_pair_index, less NNUE_INPUTS). */
static inline const int8_t* pair_ids_row(int a, int b) {
    const int hi = a > b ? a : b, lo = a > b ? b : a;
    return &nnue_net.pair_weights[(size_t) (hi * (hi - 1) / 2 + lo) * NNUE_HIDDEN];
}

/*
 * pair_features' pairs (those of a pawn of 'changed' with a pawn of 'changed' or 'kept', each once) as weight
 * rows, for 'n' perspectives (1 or 2) at once: the pairs are found once and each perspective only maps them to
 * its ids. Appends to rows[v] from *counts[v].
 */
static inline_always void pair_rows(int n, const pair_orientation_t* o, const int8_t** rows[2], int* counts[2],
                                    const bitboard changed[2], const bitboard kept[2]) {
    bitboard rest[2] = { changed[white], changed[black] };
    for (int c = white; c <= black; c++) {
        while (rest[c]) {
            const square sq = get_lsb_and_pop_bit(&rest[c]);
            const bitboard window = pair_window(sq);
            int id[2];
            for (int v = 0; v < n; v++) id[v] = o[v].base[c] + (sq ^ o[v].flip);
            for (int c2 = white; c2 <= black; c2++) {
                bitboard partners = (kept[c2] | rest[c2]) & window;
                while (partners) {
                    const square sq2 = get_lsb_and_pop_bit(&partners);
                    for (int v = 0; v < n; v++) {
                        rows[v][(*counts[v])++] = pair_ids_row(id[v], o[v].base[c2] + (sq2 ^ o[v].flip));
                    }
                }
            }
        }
    }
}

/* The pawn pairs that changed between the pawns 'before' and 'after' (per colour), for 'n' perspectives (1 or 2,
 * orientations 'o'), into changes[v]: those of the pawns that left removed, those of the pawns that arrived
 * added. */
static inline_always void change_pairs(int n, const pair_orientation_t* o, row_changes_t* changes[2],
                                       const bitboard before[2], const bitboard after[2]) {
    const bitboard gone[2] = { before[white] & ~after[white], before[black] & ~after[black] };
    const bitboard arrived[2] = { after[white] & ~before[white], after[black] & ~before[black] };
    const bitboard kept[2] = { before[white] & after[white], before[black] & after[black] };
    const int8_t** removed[2] = { changes[0]->pairs_removed, changes[n - 1]->pairs_removed };
    int* removed_counts[2] = { &changes[0]->pairs_removed_count, &changes[n - 1]->pairs_removed_count };
    const int8_t** added[2] = { changes[0]->pairs_added, changes[n - 1]->pairs_added };
    int* added_counts[2] = { &changes[0]->pairs_added_count, &changes[n - 1]->pairs_added_count };
    pair_rows(n, o, removed, removed_counts, gone, kept);
    pair_rows(n, o, added, added_counts, arrived, kept);
}

static inline bool pawns_changed(const bitboard before[2], const bitboard after[2]) {
    return ((before[white] ^ after[white]) | (before[black] ^ after[black])) != 0;
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

/* Every cache entry back to the empty board (the bias, no pieces, so no pawn pairs), for the current network. */
static void init_cache(nnue_stack_t* stack) {
    for (int c = white; c <= black; c++) {
        for (int sq = 0; sq < 64; sq++) {
            memcpy(stack->cache[c][sq].values, nnue_net.ft_bias, sizeof(nnue_net.ft_bias));
            memset(stack->cache[c][sq].pieces, 0, sizeof(stack->cache[c][sq].pieces));
        }
    }
    stack->cache_generation = nnue_generation();
}

/* refresh_perspective through the cache: only the pieces, and the pawn pairs, that differ from the cached
 * position. The entry is for this king square, so its pairs were mirrored the same way. */
static void refresh_cached(nnue_stack_t* stack, const board_t* board, colour perspective, int16_t* values) {
    if (stack->cache_generation != nnue_generation()) init_cache(stack);

    const square king = trailing_zero_count(board->pieces[perspective == white ? K : k]);
    nnue_cache_entry_t* entry = &stack->cache[perspective][king];

    /* At most 32 pieces were on the board then, and 32 are now; at most 120 pawn pairs either way. */
    row_changes_t changes;
    changes.added_count = changes.removed_count = changes.pairs_added_count = changes.pairs_removed_count = 0;
    const bitboard pawns_before[2] = { entry->pieces[P], entry->pieces[p] };
    const bitboard pawns_after[2] = { board->pieces[P], board->pieces[p] };
    if (pawns_changed(pawns_before, pawns_after)) {
        const pair_orientation_t o = pair_orientation(perspective, king);
        row_changes_t* out[2] = { &changes, &changes };
        change_pairs(1, &o, out, pawns_before, pawns_after);
    }
    for (int pc = P; pc <= k; pc++) {
        const bitboard now = board->pieces[pc];
        bitboard gone = entry->pieces[pc] & ~now;
        bitboard arrived = now & ~entry->pieces[pc];
        while (gone) changes.removed[changes.removed_count++] = feature_row(nnue_feature_index(perspective, king, pc, get_lsb_and_pop_bit(&gone)));
        while (arrived) changes.added[changes.added_count++] = feature_row(nnue_feature_index(perspective, king, pc, get_lsb_and_pop_bit(&arrived)));
        entry->pieces[pc] = now;
    }
    apply_rows(entry->values, entry->values, &changes);
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

    /* The pawns now, and before the move: the moved pawn back on its square (and off the target, unless it
     * promoted), and a captured pawn back. */
    entry->pawns_after[white] = board->pieces[P];
    entry->pawns_after[black] = board->pieces[p];
    entry->pawns_before[white] = entry->pawns_after[white];
    entry->pawns_before[black] = entry->pawns_after[black];
    if (moved == P || moved == p) {
        entry->pawns_before[mover] |= 1ULL << source;
        if (!promoted) entry->pawns_before[mover] &= ~(1ULL << target);
    }

    if (get_capture_flag(move)) {
        if (get_enpassant_flag(move)) {
            const square taken = mover == white ? target + 8 : target - 8;
            add_change(entry->removed, &entry->removed_count, mover == white ? p : P, taken);
            entry->pawns_before[mover ^ 1] |= 1ULL << taken;
        } else {
            const piece captured = board->stack[board->stack_ptr - 1].captured_piece;
            add_change(entry->removed, &entry->removed_count, captured, target);
            if (captured == P || captured == p) entry->pawns_before[mover ^ 1] |= 1ULL << target;
        }
    } else if (get_king_side_castle_flag(move) || get_queen_side_castle_flag(move)) {
        const piece rook = mover == white ? R : r;
        const bool king_side = get_king_side_castle_flag(move);
        add_change(entry->removed, &entry->removed_count, rook, king_side ? target + 1 : target - 2);
        add_change(entry->added, &entry->added_count, rook, king_side ? target - 1 : target + 1);
    }
}

/* The entry 'perspective' updates from: the nearest one whose accumulator is computed, back from the top, unless
 * its king moved on the way (then the entry the walk stopped at, not computed: refresh). */
static inline int update_start(const nnue_stack_t* stack, colour perspective) {
    int from = stack->top;
    while (from > 0 && !stack->entries[from].computed[perspective] && !stack->entries[from].refresh[perspective]) {
        from--;
    }
    return from;
}

/* Entry's piece rows for 'perspective' (king on 'king') into 'changes', which it starts (no pairs yet). */
static inline void piece_changes(row_changes_t* changes, const nnue_entry_t* entry, colour perspective, square king) {
    changes->added_count = entry->added_count;
    changes->removed_count = entry->removed_count;
    changes->pairs_added_count = changes->pairs_removed_count = 0;
    for (int a = 0; a < entry->added_count; a++) {
        changes->added[a] = feature_row(nnue_feature_index(perspective, king, entry->added[a].pc, entry->added[a].sq));
    }
    for (int r = 0; r < entry->removed_count; r++) {
        changes->removed[r] = feature_row(nnue_feature_index(perspective, king, entry->removed[r].pc, entry->removed[r].sq));
    }
}

/* Applies entries from + 1 .. top forward for 'n' perspectives (1: just perspectives[0]; 2: white and black),
 * each computed at 'from' and its king not moved since, so where it is now. A pawn move's pairs are found once
 * for both. */
static inline_always void update_forward(nnue_stack_t* stack, const board_t* board, int n, const colour* perspectives,
                                         int from) {
    square kings[2];
    pair_orientation_t o[2];
    for (int v = 0; v < n; v++) {
        kings[v] = trailing_zero_count(board->pieces[perspectives[v] == white ? K : k]);
        o[v] = pair_orientation(perspectives[v], kings[v]);
    }
    for (int i = from + 1; i <= stack->top; i++) {
        nnue_entry_t* entry = &stack->entries[i];
        row_changes_t changes[2];
        row_changes_t* out[2] = { &changes[0], &changes[n - 1] };
        for (int v = 0; v < n; v++) piece_changes(&changes[v], entry, perspectives[v], kings[v]);
        if (pawns_changed(entry->pawns_before, entry->pawns_after)) {
            change_pairs(n, o, out, entry->pawns_before, entry->pawns_after);
        }
        for (int v = 0; v < n; v++) {
            const colour c = perspectives[v];
            apply_rows(entry->accumulator.values[c], stack->entries[i - 1].accumulator.values[c], &changes[v]);
            entry->computed[c] = true;
        }
    }
}

/*
 * After the top entry's 'perspective' was refreshed: entries top - 1 down to 'to' from the one above, each move's
 * changes undone (its added rows removed, its removed rows added, its pawn pairs back), as Stockfish does. The
 * king hasn't moved after 'to', so it's where it is now. Otherwise the next position evaluated below 'to' would
 * walk back to it, still not computed, and refresh again (about half the refreshes in bench were such repeats).
 */
static void update_backward(nnue_stack_t* stack, const board_t* board, colour perspective, int to) {
    const square king = trailing_zero_count(board->pieces[perspective == white ? K : k]);
    const pair_orientation_t o = pair_orientation(perspective, king);
    for (int i = stack->top - 1; i >= to; i--) {
        const nnue_entry_t* next = &stack->entries[i + 1];
        row_changes_t changes;
        row_changes_t* out[2] = { &changes, &changes };
        changes.added_count = next->removed_count;
        changes.removed_count = next->added_count;
        changes.pairs_added_count = changes.pairs_removed_count = 0;
        for (int r = 0; r < next->removed_count; r++) {
            changes.added[r] = feature_row(nnue_feature_index(perspective, king, next->removed[r].pc, next->removed[r].sq));
        }
        for (int a = 0; a < next->added_count; a++) {
            changes.removed[a] = feature_row(nnue_feature_index(perspective, king, next->added[a].pc, next->added[a].sq));
        }
        if (pawns_changed(next->pawns_before, next->pawns_after)) {
            change_pairs(1, &o, out, next->pawns_after, next->pawns_before);
        }
        apply_rows(stack->entries[i].accumulator.values[perspective], next->accumulator.values[perspective], &changes);
        stack->entries[i].computed[perspective] = true;
    }
}

/* Brings the top entry's 'perspective' accumulator up to date. */
static void update_perspective(nnue_stack_t* stack, const board_t* board, colour perspective) {
    nnue_entry_t* top = &stack->entries[stack->top];
    const int from = update_start(stack, perspective);
    if (!stack->entries[from].computed[perspective]) {
        refresh_cached(stack, board, perspective, top->accumulator.values[perspective]);
        top->computed[perspective] = true;
        update_backward(stack, board, perspective, from);
        return;
    }
    update_forward(stack, board, 1, &perspective, from);
}

void nnue_reset_root(nnue_stack_t* stack, const board_t* board) {
    nnue_reset(stack);
    nnue_entry_t* root = &stack->entries[0];
    for (int c = white; c <= black; c++) {
        refresh_cached(stack, board, c, root->accumulator.values[c]);
        root->computed[c] = true;
    }
}

int nnue_evaluate_incremental(nnue_stack_t* stack, const board_t* board) {
    nnue_entry_t* top = &stack->entries[stack->top];
    /* Usually both perspectives update from the same entry: then together, finding each pawn move's pairs once. */
    if (!top->computed[white] && !top->computed[black]) {
        const int from = update_start(stack, white);
        if (from == update_start(stack, black) && stack->entries[from].computed[white] &&
            stack->entries[from].computed[black]) {
            static const colour both[2] = { white, black };
            update_forward(stack, board, 2, both, from);
        }
    }
    for (int c = white; c <= black; c++) {
        if (!top->computed[c]) update_perspective(stack, board, c);
    }
    return nnue_output(&top->accumulator, board->side, nnue_board_bucket(board));
}
