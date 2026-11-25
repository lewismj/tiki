#ifndef TIKI_QUIET_HISTORY_H
#define TIKI_QUIET_HISTORY_H

/**
 * Quiet move history, killers, counter moves, the node cache and the opponent's threat squares.
 * Counters are int16 in "history units" with gravity 16384; constants on them are in those units,
 * inputs in eval units (score differences) are in cp. The capture history is separate
 * (search_state.h).
 *
 * Piece types are 0..5 (pawn .. king, get_piece_moved % 6); "piece-to" is type * 64 + target square.
 */

#include <string.h>
#include "types.h"
#include "board.h"
#include "attack_mask.h"
#include "move_encoding.h"
#include "search_constants.h"

#define HISTORY_GRAVITY 16384    /* Counter update: v += d - v * |d| / this (no clamp, stays in int16). */
#define PAWN_HISTORY_SIZE 512    /* Pawn structure buckets (pawn key % this).                          */
#define CONT_PLIES 6             /* Continuation entries for the moves 1..6 plies back.               */
#define NODE_CACHE_SIZE 256      /* Positions near the root (ply < NODE_CACHE_MAX_PLY) with the...     */
#define NODE_CACHE_MOVES 32      /* ...nodes each move's search took.                                  */
#define NODE_CACHE_MAX_PLY 3
#define NODE_CACHE_MIN_NODES 256 /* The node cache term is used once a position's moves took more.     */
#define QUIETS_TRIED_MAX 32      /* Quiet moves remembered per node for the update.                     */

typedef int16_t piece_to_history_t[6 * 64];

typedef struct {
    move_t move;
    uint64_t nodes;
} node_cache_move_t;

typedef struct {
    uint64_t key;               /* The position's hash (0: empty). */
    uint32_t generation;        /* The search that last used it. */
    uint64_t nodes_sum;
    node_cache_move_t moves[NODE_CACHE_MOVES];
} node_cache_entry_t;

typedef struct {
    int16_t main[2][2][2][64 * 64];                 /* [side][from attacked][to attacked][from * 64 + to] */
    piece_to_history_t pawn[2][PAWN_HISTORY_SIZE];  /* [side][pawn key % size][piece-to]                  */
    piece_to_history_t cont[2][2][2][6][64];        /* [prev move captured][side][prev side][prev type][prev to] */
    move_t counter[2][6][64];                       /* [side][prev type][prev to]                         */
    node_cache_entry_t node_cache[NODE_CACHE_SIZE];
    uint32_t node_cache_generation;
} quiet_history_t;

/* The opponent's attacks: by pawns; by pawns and minors; by pawns,
 * minors and rooks; and every square they attack. The side to move's king isn't an obstacle for
 * their sliders. */
typedef struct {
    bitboard pawns;
    bitboard minors;
    bitboard rooks;
    bitboard all;
} threats_t;

static inline_always threats_t compute_threats(const board_t* board) {
    const colour us = (colour) board->side;
    const colour them = (colour) (us ^ 1);
    const int t = them == white ? 0 : 6;
    const bitboard occupancy = board->occupancy[both] & ~board->pieces[(us == white ? 0 : 6) + 5];
    threats_t threats;
    bitboard a = 0;
    for (bitboard b = board->pieces[t + 0]; b; b &= b - 1) a |= pawn_attack((square) trailing_zero_count(b), them);
    threats.pawns = a;
    for (bitboard b = board->pieces[t + 1]; b; b &= b - 1) a |= knight_attack((square) trailing_zero_count(b));
    for (bitboard b = board->pieces[t + 2]; b; b &= b - 1) a |= bishop_attack((square) trailing_zero_count(b), occupancy);
    threats.minors = a;
    for (bitboard b = board->pieces[t + 3]; b; b &= b - 1) a |= rook_attack((square) trailing_zero_count(b), occupancy);
    threats.rooks = a;
    for (bitboard b = board->pieces[t + 4]; b; b &= b - 1) a |= queen_attack((square) trailing_zero_count(b), occupancy);
    for (bitboard b = board->pieces[t + 5]; b; b &= b - 1) a |= king_attack((square) trailing_zero_count(b));
    threats.all = a;
    return threats;
}

static inline_always int piece_type(move_t mv) { return get_piece_moved(mv) % 6; }
static inline_always int piece_to(move_t mv) { return piece_type(mv) * 64 + get_target_square(mv); }
static inline_always bool is_quiet_move(move_t mv) { return !get_capture_flag(mv) && !get_promoted_piece(mv); }

/**
 * What a node needs to score and update quiet moves, found once per node: its side, threats,
 * continuation rows for the moves 1..6 plies back (none past the root, NULL for a null move), its
 * pawn history row and node cache entry.
 */
typedef struct {
    quiet_history_t* tables;
    int side;
    threats_t threats;
    piece_to_history_t* cont[CONT_PLIES];
    piece_to_history_t* pawn;
    node_cache_entry_t* cache;
    move_t prev_move;           /* The move that led here (0: root or a null move). */
} quiet_ctx_t;

/* The continuation rows of the node at 'ply' whose side to move is 'side'; path[k] is the move made at ply k. */
static inline_always void cont_rows_at(quiet_history_t* tables, int side, int ply, const move_t* path,
                                       piece_to_history_t* rows[CONT_PLIES]) {
    for (int i = 0; i < CONT_PLIES; i++) {
        rows[i] = NULL;
        const int k = ply - 1 - i;
        if (k < 0) continue;
        const move_t prev = path[k];
        if (!prev) continue;
        const int prev_side = side ^ ((i + 1) & 1);
        rows[i] = &tables->cont[get_capture_flag(prev) ? 1 : 0][side][prev_side][piece_type(prev)][get_target_square(prev)];
    }
}

/* The node cache entry of a position: its own entry, else a new one
 * in a slot not used by this search, else none. */
static inline_always node_cache_entry_t* node_cache_entry(quiet_history_t* tables, uint64_t key) {
    node_cache_entry_t* entry = &tables->node_cache[key % NODE_CACHE_SIZE];
    if (entry->key == key) {
        entry->generation = tables->node_cache_generation;
        return entry;
    }
    if (entry->generation < tables->node_cache_generation) {
        memset(entry, 0, sizeof(*entry));
        entry->key = key;
        entry->generation = tables->node_cache_generation;
        return entry;
    }
    return NULL;
}

/* Nodes a move's search took: added to its slot, or replacing the
 * least visited slot that took fewer nodes than this. */
static inline_always void node_cache_add(node_cache_entry_t* entry, move_t mv, uint64_t nodes) {
    uint64_t min_nodes = UINT64_MAX;
    int min_index = -1;
    for (int i = 0; i < NODE_CACHE_MOVES; i++) {
        node_cache_move_t* m = &entry->moves[i];
        if (m->move == mv) {
            m->nodes += nodes;
            entry->nodes_sum += nodes;
            if (m->nodes >= UINT64_MAX / NODE_CACHE_MOVES) {
                entry->nodes_sum = 0;
                for (int j = 0; j < NODE_CACHE_MOVES; j++) {
                    entry->moves[j].nodes /= 2;
                    entry->nodes_sum += entry->moves[j].nodes;
                }
            }
            return;
        }
        if (!m->move || (m->nodes < min_nodes && m->nodes < nodes)) {
            min_nodes = m->nodes;
            min_index = i;
        }
    }
    if (min_index >= 0) {
        node_cache_move_t* m = &entry->moves[min_index];
        entry->nodes_sum -= m->nodes;
        entry->nodes_sum += nodes;
        m->move = mv;
        m->nodes = nodes;
    }
}

static inline_always quiet_ctx_t quiet_ctx(quiet_history_t* tables, const board_t* board, int ply, const move_t* path,
                                           bool with_cache) {
    quiet_ctx_t q;
    q.tables = tables;
    q.side = board->side;
    q.threats = compute_threats(board);
    cont_rows_at(tables, q.side, ply, path, q.cont);
    q.pawn = &tables->pawn[q.side][board->corr_keys[0] % PAWN_HISTORY_SIZE];
    q.cache = with_cache && ply < NODE_CACHE_MAX_PLY ? node_cache_entry(tables, board->hash) : NULL;
    q.prev_move = ply > 0 ? path[ply - 1] : 0;
    return q;
}

static inline_always int16_t* main_history_entry(const quiet_ctx_t* q, move_t mv) {
    const square from = get_source_square(mv), to = get_target_square(mv);
    return &q->tables->main[q->side][(q->threats.all >> from) & 1][(q->threats.all >> to) & 1][from * 64 + to];
}

/* The history value the reductions and the move loop's pruning read: main + continuation 1, 2 and 4
 * plies back. */
static inline_always int quiet_stat(const quiet_ctx_t* q, move_t mv) {
    const int pt = piece_to(mv);
    int stat = *main_history_entry(q, mv);
    if (q->cont[0]) stat += (*q->cont[0])[pt];
    if (q->cont[1]) stat += (*q->cont[1])[pt];
    if (q->cont[3]) stat += (*q->cont[3])[pt];
    return stat;
}

/* A quiet move's (or underpromotion's) ordering score. */
static inline_always int quiet_order_score(const quiet_ctx_t* q, move_t mv) {
    const int pt = piece_to(mv);
    const bitboard from = 1ULL << get_source_square(mv), to = 1ULL << get_target_square(mv);
    int score = *main_history_entry(q, mv);
    score += (*q->pawn)[pt] * PAWN_HIST_WEIGHT / 1024;
    if (q->cont[0]) score += (*q->cont[0])[pt];
    if (q->cont[1]) score += (*q->cont[1])[pt] * CONT_ORDER_WEIGHT_2 / 1024;
    if (q->cont[3]) score += (*q->cont[3])[pt] * CONT_ORDER_WEIGHT_4 / 1024;
    if (q->cont[5]) score += (*q->cont[5])[pt] * CONT_ORDER_WEIGHT_6 / 1024;
    switch (piece_type(mv)) {
    case 1:
    case 2:
        if (q->threats.pawns & from) score += THREAT_ESCAPE_MINOR;
        if (q->threats.pawns & to) score -= THREAT_ENTER_MINOR;
        break;
    case 3:
        if (q->threats.minors & from) score += THREAT_ESCAPE_ROOK;
        if (q->threats.minors & to) score -= THREAT_ENTER_ROOK;
        break;
    case 4:
        if (q->threats.rooks & from) score += THREAT_ESCAPE_QUEEN;
        if (q->threats.rooks & to) score -= THREAT_ENTER_QUEEN;
        break;
    default:
        break;
    }
    if (q->cache && q->cache->nodes_sum > NODE_CACHE_MIN_NODES) {
        for (int i = 0; i < NODE_CACHE_MOVES; i++) {
            if (q->cache->moves[i].move == mv) {
                score += (int) (NODE_CACHE_BONUS * q->cache->moves[i].nodes / q->cache->nodes_sum);
                break;
            }
        }
    }
    return score;
}

/* A history counter moved by 'delta', less the more it already is (gravity HISTORY_GRAVITY). */
static inline_always void history_counter_update(int16_t* counter, int delta) {
    const int v = *counter;
    *counter = (int16_t) (v + delta - v * (delta < 0 ? -delta : delta) / HISTORY_GRAVITY);
}

/* Continuation rows of a node, each by its weight. */
static inline_always void cont_history_update(piece_to_history_t* const rows[CONT_PLIES], move_t mv, int delta) {
    const int pt = piece_to(mv);
    if (rows[0]) history_counter_update(&(*rows[0])[pt], delta);
    if (rows[1]) history_counter_update(&(*rows[1])[pt], delta * CONT_UPDATE_WEIGHT_2 / 1024);
    if (rows[2]) history_counter_update(&(*rows[2])[pt], delta * CONT_UPDATE_WEIGHT_3 / 1024);
    if (rows[3]) history_counter_update(&(*rows[3])[pt], delta * CONT_UPDATE_WEIGHT_4 / 1024);
    if (rows[5]) history_counter_update(&(*rows[5])[pt], delta * CONT_UPDATE_WEIGHT_6 / 1024);
}

static inline_always int min_int(int a, int b) { return a < b ? a : b; }

/**
 * A node failed high with a quiet best move: its counter move is
 * stored; then, unless the update is uncertain (one quiet tried at depth < 2), the best move gets the
 * bonus and every other quiet tried the malus, in the main, pawn and continuation histories. The
 * sizes grow with depth and with how far the score beat beta ('score_diff', cp, at most
 * HIST_SCORE_DIFF_MAX); the score diff terms are per cp in 1/64.
 */
static inline_always void quiet_history_update(const quiet_ctx_t* q, const move_t* tried, int count, move_t best,
                                               int depth, int score_diff) {
    if (q->prev_move) q->tables->counter[q->side][piece_type(q->prev_move)][get_target_square(q->prev_move)] = best;
    if (count <= 1 && depth < 2) return;
    const int hist_bonus = min_int(HIST_BONUS_OFFSET + HIST_BONUS_LINEAR * depth + HIST_BONUS_SCORE_DIFF * score_diff / 64,
                                   HIST_BONUS_LIMIT);
    const int hist_malus = -min_int(HIST_MALUS_OFFSET + HIST_MALUS_LINEAR * depth + HIST_MALUS_SCORE_DIFF * score_diff / 64,
                                    HIST_MALUS_LIMIT);
    const int cont_bonus = min_int(CONT_BONUS_OFFSET + CONT_BONUS_LINEAR * depth + CONT_BONUS_SCORE_DIFF * score_diff / 64,
                                   CONT_BONUS_LIMIT);
    const int cont_malus = -min_int(CONT_MALUS_OFFSET + CONT_MALUS_LINEAR * depth + CONT_MALUS_SCORE_DIFF * score_diff / 64,
                                    CONT_MALUS_LIMIT);
    history_counter_update(main_history_entry(q, best), hist_bonus);
    history_counter_update(&(*q->pawn)[piece_to(best)], hist_bonus);
    cont_history_update(q->cont, best, cont_bonus);
    for (int i = 0; i < count; i++) {
        if (tried[i] == best) continue;
        history_counter_update(main_history_entry(q, tried[i]), hist_malus);
        history_counter_update(&(*q->pawn)[piece_to(tried[i])], hist_malus);
        cont_history_update(q->cont, tried[i], cont_malus);
    }
}

/* The counter move for the move that led to the node (0: none). */
static inline_always move_t counter_move(const quiet_ctx_t* q) {
    return q->prev_move ? q->tables->counter[q->side][piece_type(q->prev_move)][get_target_square(q->prev_move)] : 0;
}

/* Every counter of a table to 'value'. */
static inline_always void fill_history(int16_t* table, size_t count, int16_t value) {
    for (size_t i = 0; i < count; i++) table[i] = value;
}

/* 'ucinewgame': the histories to their start values, counter moves and the node cache cleared. */
static inline_always void quiet_history_clear(quiet_history_t* tables) {
    fill_history(&tables->main[0][0][0][0], sizeof(tables->main) / sizeof(int16_t), (int16_t) HIST_CLEAR_MAIN);
    fill_history(&tables->pawn[0][0][0], sizeof(tables->pawn) / sizeof(int16_t), (int16_t) HIST_CLEAR_PAWN);
    fill_history(&tables->cont[0][0][0][0][0][0], sizeof(tables->cont) / sizeof(int16_t), (int16_t) HIST_CLEAR_CONT);
    memset(tables->counter, 0, sizeof(tables->counter));
    memset(tables->node_cache, 0, sizeof(tables->node_cache));
    tables->node_cache_generation = 0;
}

/* Each 'go': main and pawn history x 7/8,
 * continuation history kept, a new node cache generation. */
static inline_always void quiet_history_new_search(quiet_history_t* tables) {
    int16_t* m = &tables->main[0][0][0][0];
    for (size_t i = 0; i < sizeof(tables->main) / sizeof(int16_t); i++) m[i] = (int16_t) (m[i] * 7 / 8);
    int16_t* p = &tables->pawn[0][0][0];
    for (size_t i = 0; i < sizeof(tables->pawn) / sizeof(int16_t); i++) p[i] = (int16_t) (p[i] * 7 / 8);
    tables->node_cache_generation++;
}

#endif
