#ifndef TIKI_NNUE_H
#define TIKI_NNUE_H

#include <stdbool.h>
#include <stdint.h>

#include "../types.h"
#include "../board.h"

/**
 * Tiki's NNUE: HalfKAv2_hm features -> 256 x 2 accumulator -> SCReLU -> 1 output.
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
 * Quantisation (what the trainer exports):
 *   accumulator = ft_bias + sum of ft_weights rows for active features, int16, scale QA.
 *   SCReLU(x) = clamp(x, 0, QA)^2.
 *   eval = (sum(SCReLU(acc_stm) * out_w[0..255]) + sum(SCReLU(acc_nstm) * out_w[256..511])) / QA
 *          + out_bias, then * SCALE / (QA * QB). Integer division truncates (C semantics).
 *   Float model: y = SCReLU_f(acc_f) . w + b with acc_f = acc / QA, w = out_w / QB,
 *   b = out_bias / (QA * QB), and eval in centipawns = y * SCALE.
 *
 * File format (little endian):
 *   char magic[8] = "TIKINNUE", uint32 version, uint32 inputs, uint32 hidden,
 *   int16 ft_weights[inputs][hidden] (row per feature), int16 ft_bias[hidden],
 *   int16 out_weights[2 * hidden] (side to move half first), int32 out_bias.
 */

#define NNUE_KING_BUCKETS 32
#define NNUE_PIECE_KINDS 11
#define NNUE_PIECE_SQUARES (NNUE_PIECE_KINDS * 64)              /* 704   */
#define NNUE_INPUTS (NNUE_KING_BUCKETS * NNUE_PIECE_SQUARES)    /* 22528 */
#define NNUE_HIDDEN 256
#define NNUE_MAX_ACTIVE 32                                      /* One feature per piece. */

#define NNUE_QA 255
#define NNUE_QB 64
#define NNUE_SCALE 400

#define NNUE_MAGIC "TIKINNUE"
#define NNUE_VERSION 1

typedef struct {
    int16_t* ft_weights;                    /* [NNUE_INPUTS][NNUE_HIDDEN] */
    int16_t ft_bias[NNUE_HIDDEN];
    int16_t out_weights[2 * NNUE_HIDDEN];   /* Side to move, then the other side. */
    int32_t out_bias;
} nnue_network_t;

typedef struct {
    int16_t values[2][NNUE_HIDDEN];         /* Indexed by perspective (white, black). */
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
    int16_t values[NNUE_HIDDEN];
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

/* "avx2" or "scalar", the accumulator and output code compiled in (TIKI_USE_AVX2). */
const char* nnue_simd(void);

/* The output layer, as nnue_evaluate uses it (SIMD if compiled in) and the plain C reference. */
int nnue_output(const nnue_accumulator_t* accumulator, colour side);
int nnue_output_scalar(const nnue_accumulator_t* accumulator, colour side);

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
