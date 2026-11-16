#ifndef TIKI_NNUE_H
#define TIKI_NNUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../types.h"
#include "../board.h"

/**
 * Tiki's NNUE: HalfKAv2_hm and PawnPairs features -> 512 x 2 accumulator -> pairwise products -> 512 -> 16
 * -> [CReLU, SqrCReLU] -> 32 -> [CReLU, SqrCReLU] -> 1 over both layers' activations, plus a skip from two of
 * the first layer's sums. The part after the accumulator (a layer stack) is one of 32 chosen by the number of
 * pieces on the board and which sides have a queen (output buckets, as Stockfish's SFNNv18).
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
 * PawnPairs (Stockfish's PP_3Wide), per perspective, after those: every unordered pair of pawns, of either
 * colour, whose files differ by at most one (levers, chains, phalanxes, rams, doubled pawns...).
 *   - A pawn's id = (its colour != perspective) * 48 + its oriented square - 8, the square oriented and
 *     mirrored as above (pawns are on ranks 2-7, oriented squares 8..55), so 0..95.
 *   - index = 22528 + hi * (hi - 1) / 2 + lo of the two ids, 4560 pairs: 27088 inputs in all. No king buckets.
 *   - At most C(16, 2) = 120 are active. Their rows are int8 (+-127, as Stockfish's), the pieces' int16 within
 *     +-505 (the trainer's clips), so the int16 accumulator can't overflow (33 * 505 + 120 * 127 < 32767), and a
 *     pawn move, which changes about 7 pairs per perspective (bench average), reads half the bytes.
 *   - They are in the same accumulator as the piece features: a move that changes no pawn changes no pair.
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
 *   h0[j] = l1_bias[b][j] + sum(in[i] * l1_w[b][j][i]), 16 sums (int8 weights scale QW = 64, QH = QI * QW
 *           = 8128 is 1.0).
 *   act(h): x = clamp(h, 0, QH), x >> 6 (CReLU, 0..127) for every sum, then (x * x) >> 19 (SqrCReLU, 0..126)
 *           for every sum: u8, 1.0 is 127 (Stockfish's shifts).
 *   a0 = act(h0) (32), h1[j] = l2_bias[b][j] + sum(a0[i] * l2_w[b][j][i]) (32, int8 weights at QW), a1 = act(h1)
 *   out = out_bias[b] + sum over the 96 of a0 ++ a1 times out_w[b][k] (int16 at QO = 2048, bias at 127 * QO)
 *   skip = h0[14] - h0[15]
 *   eval = (out * 64 + skip * QO) * SCALE / (QH * QO), int64 (out / (127 QO) + skip / QH, as QH = 127 * 64).
 *          The division truncates (C).
 *   Float model: a = pairwise_f(acc / QA), h0 = W1 a + b1, a0 = act_f(h0), a1 = act_f(W2 a0 + b2),
 *   y = w3 . (a0 ++ a1) + b3 + h0[14] - h0[15], eval = y * SCALE, act_f(x) = clamp(x, 0, 1) ++ clamp(x, 0, 1)^2
 *   * 127 / 128, with W1 = l1_w / QW, b1 = l1_bias / QH, W2 = l2_w / QW, b2 = l2_bias / QH, w3 = out_w / QO,
 *   b3 = out_bias / (127 * QO).
 *
 * File format (little endian), version 7:
 *   char magic[8] = "TIKINNUE", uint32 version, uint32 inputs (27088: pieces and pairs), uint32 hidden,
 *   int16 ft_weights[22528][hidden] (row per piece feature), int8 pair_weights[4560][hidden] (row per pawn pair),
 *   int16 ft_bias[hidden],
 *   int8 l1_weights[32][16][hidden] (per bucket and hidden unit, the side to move's inputs first),
 *   int32 l1_bias[32][16], int8 l2_weights[32][32][32], int32 l2_bias[32][32], int16 out_weights[32][96],
 *   int32 out_bias[32].
 * Earlier versions (6: no pawn pairs, one hidden layer 512 -> 16 -> 1; 5: the same at a coarser output
 * scale; 1-4 older still) are a different network. Only version 7 is loaded.
 */

#define NNUE_KING_BUCKETS 32
#define NNUE_PIECE_KINDS 11
#define NNUE_PIECE_SQUARES (NNUE_PIECE_KINDS * 64)              /* 704   */
#define NNUE_INPUTS (NNUE_KING_BUCKETS * NNUE_PIECE_SQUARES)    /* 22528 piece features */
#define NNUE_PAWN_IDS 96                                        /* Own and their pawns on ranks 2-7. */
#define NNUE_PAIR_FEATURES (NNUE_PAWN_IDS * (NNUE_PAWN_IDS - 1) / 2)    /* 4560 */
#define NNUE_TOTAL_INPUTS (NNUE_INPUTS + NNUE_PAIR_FEATURES)    /* 27088 */
#define NNUE_HIDDEN 512
#define NNUE_MAX_ACTIVE 32                                      /* One piece feature per piece. */
#define NNUE_MAX_PAIRS 120                                      /* C(16, 2): every pair of 16 pawns. */
#define NNUE_MAX_FEATURES (NNUE_MAX_ACTIVE + NNUE_MAX_PAIRS)
#define NNUE_L1 16                                              /* First hidden layer width, per bucket. */
#define NNUE_L1_INPUTS NNUE_HIDDEN                              /* 2 x 256 pairwise products */
#define NNUE_L2 32                                              /* Second hidden layer width, per bucket. */
#define NNUE_L2_INPUTS (2 * NNUE_L1)                            /* The first layer's two activations. */
#define NNUE_OUT_INPUTS (2 * NNUE_L1 + 2 * NNUE_L2)             /* 96: both layers' activations. */
#define NNUE_SKIP_PLUS 14                                       /* skip = h0[14] - h0[15] */
#define NNUE_SKIP_MINUS 15
#define NNUE_PIECE_BUCKETS 8
#define NNUE_OUTPUT_BUCKETS (NNUE_PIECE_BUCKETS * 4)            /* 32 layer stacks */

#define NNUE_QA 255
#define NNUE_QI 127                                             /* Hidden layer inputs, 0..127. */
#define NNUE_QW 64                                              /* Hidden layer weights. */
#define NNUE_QH (NNUE_QI * NNUE_QW)                             /* Hidden activation 1.0, 8128. */
#define NNUE_QO 2048                                            /* Output layer weights. */
#define NNUE_QU 127                                             /* Hidden layer activations (u8), 1.0. */
#define NNUE_SCALE 400

#define NNUE_MAGIC "TIKINNUE"
#define NNUE_VERSION 7

/* The weights and accumulators are 64 byte aligned (align, rows of 512 bytes), so no SIMD load
 * splits a cache line. */
typedef struct {
    int16_t* ft_weights;                    /* [NNUE_INPUTS][NNUE_HIDDEN], the piece features */
    int8_t* pair_weights;                   /* [NNUE_PAIR_FEATURES][NNUE_HIDDEN], the pawn pairs */
    int16_t ft_bias[NNUE_HIDDEN] align;
    int8_t* l1_weights;                     /* In memory as nnue_l1_index (below), not the file's order */
    int32_t l1_bias[NNUE_OUTPUT_BUCKETS][NNUE_L1];
    int8_t* l2_weights;                     /* In memory as nnue_l2_index (below), not the file's order */
    int32_t l2_bias[NNUE_OUTPUT_BUCKETS][NNUE_L2];
    int16_t out_weights[NNUE_OUTPUT_BUCKETS][NNUE_OUT_INPUTS];
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

/* The same for the second hidden layer: by group of 4 inputs, the 32 units' 4 weights each (128 bytes). */
static inline size_t nnue_l2_index(int bucket, int unit, int input) {
    return (((size_t) bucket * (NNUE_L2_INPUTS / 4) + (size_t) (input / 4)) * NNUE_L2 + (size_t) unit) * 4 +
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
 *
 * The accumulators hold the pawn pairs too (as Stockfish's): an entry whose move changed the pawns also applies
 * the pairs that went and came, in the same pass as its pieces. Each entry keeps both sides' pawns before and
 * after its move for that. The pairs are mirrored with the king's half, so a king move (a refresh) is the only
 * thing that changes all of them, and the refresh cache handles that.
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
    bitboard pawns_before[2];               /* [colour] */
    bitboard pawns_after[2];
} nnue_entry_t;

/**
 * Refresh cache ("Finny table"): when a king moves, every feature of its side changes, but most
 * pieces are where they were the last time that king stood on that square. Each entry holds a
 * perspective's accumulator for a king square and the pieces it was computed from, so a refresh
 * only applies the pieces that differ (usually a few) rather than adding all of them, and the pawn
 * pairs that differ (a king square is always in the same half, so the pairs' mirroring never changes).
 */
typedef struct {
    int16_t values[NNUE_HIDDEN] align;
    bitboard pieces[12];
} nnue_cache_entry_t;

typedef struct {
    nnue_entry_t entries[NNUE_STACK_SIZE];  /* entries[0] is the search root. */
    int top;
    nnue_cache_entry_t cache[2][64];        /* [perspective][that side's king square] */
    uint32_t cache_generation;              /* The network the caches were built for (nnue_generation). */
} nnue_stack_t;

/* The loaded network, a single instance like the TT. */
extern nnue_network_t nnue_net;

/* Feature index of piece 'pc' on 'sq', for 'perspective' whose king is on 'king'. */
int nnue_feature_index(colour perspective, square king, piece pc, square sq);

/* A pawn's PawnPairs id (0..95) and the feature index of a pair of them (two different ids), for
 * 'perspective' whose king is on 'king'. */
int nnue_pawn_id(colour perspective, square king, colour pawn_colour, square sq);
int nnue_pair_index(int id_a, int id_b);

/* Writes the active feature indices for 'perspective' to 'features' (room for NNUE_MAX_FEATURES): the
 * pieces', then the pawn pairs'. Returns how many. */
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

/* nnue_reset with 'board' the root, its accumulators computed now (from the refresh cache): every walk back
 * from the search tree then ends at the root at worst, rather than refreshing each root move's subtree. */
void nnue_reset_root(nnue_stack_t* stack, const board_t* board);

/* After make_move(board, move): records the move's feature changes (legal or not). */
void nnue_push(nnue_stack_t* stack, const board_t* board, move_t move);

/* With pop_move, discards the last push. */
static inline void nnue_pop(nnue_stack_t* stack) {
    stack->top--;
}

/* nnue_evaluate for the position at the top of the stack, which must be 'board', incrementally. */
int nnue_evaluate_incremental(nnue_stack_t* stack, const board_t* board);

#endif
