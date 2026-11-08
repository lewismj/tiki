#ifndef TIKI_NNUE_H
#define TIKI_NNUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../types.h"
#include "../board.h"

/**
 * Tiki's NNUE: HalfKAv2_hm features -> 512 x 2 accumulator -> pairwise products -> 512 -> 16 -> CReLU
 * -> 1, the 512 -> 16 -> 1 part (a layer stack) one of 32 chosen by the number of pieces on the board
 * and which sides have a queen (output buckets, as Stockfish's SFNNv18).
 * The trainer (training/tiki_nnue) implements the same features and quantisation, any change
 * here must be made there too (training/tests check the two agree).
 *
 * Features (HalfKAv2_hm), per perspective (the side whose accumulator it is):
 *   - Squares are oriented so the perspective plays "up": tiki's square index (a8 = 0) as is for
 *     white, rank flipped (sq ^ 56) for black.
 *   - If the perspective's (oriented) king is on files e-h, every square is mirrored (sq ^ 7),
 *     so the king is always on files a-d ("_hm": horizontal mirror).
 *   - King bucket = rank * 4 + file of that king square, 32 buckets.
 *   - Piece kind: own pawn, their pawn, own knight, their knight, ... own queen, their queen
 *     (type * 2 + (colour != perspective)), and 10 for either king ("A": both kings share one).
 *   - index = bucket * 704 + kind * 64 + oriented square. 32 * 11 * 64 = 22528 inputs.
 *
 * Output bucket = piece bucket * 4 + (side to move has a queen ? 2 : 0) + (the other side has ? 1 : 0),
 * piece bucket = (pieces on the board, kings included, - 2) / 4: 2-5 pieces is 0 ... 30-32 is 7.
 * Both perspectives share the accumulator, only the layer stack depends on the bucket.
 *
 * Quantisation (what the trainer exports), for output bucket b:
 *   accumulator = ft_bias + sum of ft_weights rows for active features, int16, scale QA (255).
 *   in[i] = (x * y) >> 9 with x = clamp(acc[i], 0, QA), y = clamp(acc[i + 256], 0, QA), i < 256:
 *           the pairwise product of the accumulator's halves, 0..127 (scale QI = 127, rounded down), the side
 *           to move's 256 then the other side's (as Stockfish's pairwise CReLU).
 *   h[j] = clamp(l1_bias[b][j] + sum(in[i] * l1_w[b][j][i]), 0, QH) (CReLU, int8 weights scale QW = 64,
 *          QH = QI * QW = 8128 is 1.0).
 *   eval = (out_bias[b] + sum(h[j] * out_w[b][j])) * SCALE / (QH * QW). The division truncates (C).
 *   Float model: a = pairwise_f(acc / QA), h = CReLU_f(W1 a + b1), y = w2 . h + b2, eval = y * SCALE,
 *   with W1 = l1_w / QW, b1 = l1_bias / QH, w2 = out_w / QW, b2 = out_bias / (QH * QW).
 *
 * File format (little endian), version 5:
 *   char magic[8] = "TIKINNUE", uint32 version, uint32 inputs, uint32 hidden,
 *   int16 ft_weights[inputs][hidden] (row per feature), int16 ft_bias[hidden],
 *   int8 l1_weights[32][16][hidden] (per bucket and hidden unit, the side to move's inputs first),
 *   int32 l1_bias[32][16], int16 out_weights[32][16], int32 out_bias[32].
 * Earlier versions (1, 2: an output layer straight from the accumulator; 3: PSQT; 4: a hidden layer on
 * SCReLU inputs, never released) are a different network and aren't loaded.
 */

#define NNUE_KING_BUCKETS 32
#define NNUE_PIECE_KINDS 11
#define NNUE_PIECE_SQUARES (NNUE_PIECE_KINDS * 64)              /* 704   */
#define NNUE_INPUTS (NNUE_KING_BUCKETS * NNUE_PIECE_SQUARES)    /* 22528 */
#define NNUE_HIDDEN 512
#define NNUE_MAX_ACTIVE 32                                      /* One feature per piece. */
#define NNUE_L1 16                                              /* Hidden layer width, per bucket. */
#define NNUE_L1_INPUTS NNUE_HIDDEN                              /* 2 x 256 pairwise products */
#define NNUE_PIECE_BUCKETS 8
#define NNUE_OUTPUT_BUCKETS (NNUE_PIECE_BUCKETS * 4)            /* 32 layer stacks */

#define NNUE_QA 255
#define NNUE_QI 127                                             /* Hidden layer inputs, 0..127. */
#define NNUE_QW 64                                              /* Hidden and output weights. */
#define NNUE_QH (NNUE_QI * NNUE_QW)                             /* Hidden activation 1.0, 8128. */
#define NNUE_SCALE 400

#define NNUE_MAGIC "TIKINNUE"
#define NNUE_VERSION 5

/* The weights and accumulators are 64 byte aligned (align, rows of 512 bytes), so no SIMD load
 * splits a cache line. */
typedef struct {
    int16_t* ft_weights;                    /* [NNUE_INPUTS][NNUE_HIDDEN] */
    int16_t ft_bias[NNUE_HIDDEN] align;
    int8_t* l1_weights;                     /* In memory as nnue_l1_index (below), not the file's order */
    int32_t l1_bias[NNUE_OUTPUT_BUCKETS][NNUE_L1];
    int16_t out_weights[NNUE_OUTPUT_BUCKETS][NNUE_L1];
    int32_t out_bias[NNUE_OUTPUT_BUCKETS];
} nnue_network_t;

/*
 * Where hidden layer weight [bucket][unit][input] is in nnue_net.l1_weights. In memory the weights are
 * grouped by 4 inputs: for each group, the 16 units' 4 weights each (64 bytes, two AVX2 registers), so
 * the AVX2 layer adds a group of 4 inputs to all 16 sums at once, with no horizontal additions. The
 * file keeps [bucket][unit][input]; load and save convert.
 */
static inline size_t nnue_l1_index(int bucket, int unit, int input) {
    return (((size_t) bucket * (NNUE_L1_INPUTS / 4) + (size_t) (input / 4)) * NNUE_L1 + (size_t) unit) * 4 +
           (size_t) (input % 4);
}

typedef struct {
    int16_t values[2][NNUE_HIDDEN] align;   /* Indexed by perspective (white, black). */
} nnue_accumulator_t;

/**
 * Accumulators along the search path, updated lazily. Each made move pushes an entry recording
 * the features it adds and removes (at most two of each: a castle moves the king and the rook, a
 * capture removes the moved piece's old square and the captured piece). Nothing is computed until
 * a position is evaluated, as many moves are made and undone without one (illegal, pruned). Then
 * each perspective walks back to the nearest computed accumulator and applies the changes forward,
 * or, if that side's king moved on the way (every one of its features changes), refreshes from the
 * board. A null move changes no pieces, so it pushes nothing.
 */
#define NNUE_STACK_SIZE 128                 /* More than the deepest search path (MAX_PLY). */

typedef struct {
    piece pc;
    square sq;
} nnue_piece_square_t;

typedef struct {
    nnue_accumulator_t accumulator;
    bool computed[2];                       /* Per perspective, whether 'accumulator' is up to date. */
    bool refresh[2];                        /* This entry's move moved that perspective's king. */
    int8_t added_count;
    int8_t removed_count;
    nnue_piece_square_t added[2];
    nnue_piece_square_t removed[2];
} nnue_entry_t;

/**
 * Refresh cache ("Finny table"): when a king moves, every feature of its side changes, but most
 * pieces are where they were the last time that king stood on that square. Each entry holds a
 * perspective's accumulator for a king square and the pieces it was computed from, so a refresh
 * only applies the pieces that differ (usually a few) rather than adding all of them.
 */
typedef struct {
    int16_t values[NNUE_HIDDEN] align;
    bitboard pieces[12];
} nnue_cache_entry_t;

typedef struct {
    nnue_entry_t entries[NNUE_STACK_SIZE];  /* entries[0] is the search root. */
    int top;
    nnue_cache_entry_t cache[2][64];        /* [perspective][that side's king square] */
    uint32_t cache_generation;              /* The network the cache was built for (nnue_generation). */
} nnue_stack_t;

/* The loaded network, a single instance like the TT. */
extern nnue_network_t nnue_net;

/* Feature index of piece 'pc' on 'sq', for 'perspective' whose king is on 'king'. */
int nnue_feature_index(colour perspective, square king, piece pc, square sq);

/* Writes the active feature indices for 'perspective' to 'features', returns how many. */
int nnue_active_features(const board_t* board, colour perspective, int* features);

/* Allocates (zeroed) weights, so a network can be built in code (tests). */
bool nnue_allocate(void);
void nnue_free(void);

/* Loads / saves the file format above. Load returns false on any mismatch, keeping the current network. */
bool nnue_load(const char* path);
bool nnue_save(const char* path);

/* Loads the file format above from memory, 'name' is then reported by nnue_loaded_path. */
bool nnue_load_memory(const void* data, size_t size, const char* name);

/* The network built into the binary (TIKI_EMBED_NETWORK in CMake, see nnue_embedded.c): its file
 * name (e.g. tiki-2026-10-23.nnue), or NULL if there isn't one. */
const char* nnue_embedded_name(void);

/* Loads the built in network, false if there isn't one. */
bool nnue_load_embedded(void);

/* The file the network was loaded from, "" if none or it was built in code (nnue_allocate). */
const char* nnue_loaded_path(void);

static inline bool nnue_is_loaded(void) {
    return nnue_net.ft_weights != NULL;
}

/* Accumulators from scratch. */
void nnue_refresh(const board_t* board, nnue_accumulator_t* accumulator);

/* Evaluation in centipawns from the side to move's point of view (refreshes the accumulator). */
int nnue_evaluate(const board_t* board);

/* Changes whenever the network is replaced (nnue_allocate, nnue_load), so caches built for the old one are rebuilt. */
uint32_t nnue_generation(void);

/* The accumulator and layer stack code compiled in (TIKI_ARCH): "avx512-vnni", "avx512", "avx-vnni", "avx2",
 * "ssse3", "sse2" or "scalar". */
const char* nnue_simd(void);

/* The piece count part of the output bucket, for 'pieces' pieces (kings included, so at least 2). */
static inline int nnue_piece_bucket(int pieces) {
    const int bucket = (pieces - 2) / 4;
    return bucket < 0 ? 0 : bucket >= NNUE_PIECE_BUCKETS ? NNUE_PIECE_BUCKETS - 1 : bucket;
}

/* The output bucket (layer stack): 'pieces' on the board, and whether the side to move and the other
 * side have a queen. */
static inline int nnue_output_bucket(int pieces, bool stm_queen, bool nstm_queen) {
    return nnue_piece_bucket(pieces) * 4 + (stm_queen ? 2 : 0) + (nstm_queen ? 1 : 0);
}

/* The output bucket of 'board', for its side to move. */
int nnue_board_bucket(const board_t* board);

/* The layer stack of 'bucket' on an accumulator, in centipawns for 'side': as nnue_evaluate uses it
 * (SIMD if compiled in) and the plain C reference. */
int nnue_output(const nnue_accumulator_t* accumulator, colour side, int bucket);
int nnue_output_scalar(const nnue_accumulator_t* accumulator, colour side, int bucket);

/* A new stack: empty, and its refresh cache to be built on first use. */
void nnue_stack_init(nnue_stack_t* stack);

/* Empties the stack, 'board' (whatever position it's next evaluated with) is the root. The refresh
 * cache is kept, it stays valid for the same network. */
void nnue_reset(nnue_stack_t* stack);

/* After make_move(board, move): records the move's feature changes (legal or not). */
void nnue_push(nnue_stack_t* stack, const board_t* board, move_t move);

/* With pop_move, discards the last push. */
static inline void nnue_pop(nnue_stack_t* stack) {
    stack->top--;
}

/* nnue_evaluate for the position at the top of the stack, which must be 'board', incrementally. */
int nnue_evaluate_incremental(nnue_stack_t* stack, const board_t* board);

#endif
