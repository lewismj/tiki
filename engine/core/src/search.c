#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>

#include "search.h"
#include "evaluation.h"
#include "transposition.h"
#include "see.h"
#include "syzygy.h"
#include "ref_see.h"

quiet_history_t main_quiet_history;
corr_history_t main_corr_history;

/* Search verification (verify.h), a TIKI_VERIFY build only. VERIFY_PLAIN: every selective feature
 * off, the eval uncorrected and unadjusted, for the comparison with a plain alpha-beta search. */
#ifdef TIKI_VERIFY
#include "verify.h"
#include "uci.h"
#define VERIFY_PLAIN (verify_cfg.plain)
#else
#define VERIFY_PLAIN false
#endif

/* Whether a TT entry may cut off: always, but with VERIFY_PLAIN only from an entry of exactly this
 * depth with the TT on (verify plain-tt), never with it off (verify plain). */
static inline_always bool tt_cut_allowed(const tt_info_t* tt, int depth) {
#ifdef TIKI_VERIFY
    if (verify_cfg.plain) return verify_cfg.plain_tt && tt->depth == depth;
#endif
    (void) tt; (void) depth;
    return true;
}

/* The correction history indices of a position (corr_history_t), from the board's correction keys
 * (board_t.corr_keys, kept up to date by make_move): its pawn structure, then white's and black's
 * other pieces. Zobrist keys per piece and square, so colours and piece types all differ. */
static inline_always void corr_indices(const board_t* board, int indices[CORR_TABLES]) {
    for (int i = 0; i < CORR_TABLES; i++) indices[i] = (int) (board->corr_keys[i] >> (64 - CORR_BITS));
}

/* The correction from the position's entries (side to move): (pawn entry * CORR_PAWN_WEIGHT + the
 * other two * CORR_NONPAWN_WEIGHT) / 100, in cp, within +-CORR_MAX. */
static inline_always int correction_at(const search_state_t* search_state, int side, const int indices[CORR_TABLES]) {
    _Atomic int16_t (*t)[CORR_SIZE] = (*search_state->corr_history)[side];
    const int pawn = atomic_load_explicit(&t[0][indices[0]], memory_order_relaxed);
    const int white = atomic_load_explicit(&t[1][indices[1]], memory_order_relaxed);
    const int black = atomic_load_explicit(&t[2][indices[2]], memory_order_relaxed);
    const int sum = pawn * CORR_PAWN_WEIGHT + (white + black) * CORR_NONPAWN_WEIGHT;
    const int correction = sum / (100 * CORR_GRAIN);
    return correction > CORR_MAX ? CORR_MAX : (correction < -CORR_MAX ? -CORR_MAX : correction);
}

int eval_correction(const board_t* board, const search_state_t* search_state) {
    int indices[CORR_TABLES];
    corr_indices(board, indices);
    return correction_at(search_state, board->side, indices);
}

/**
 * Moves the position's entries towards 'error', the search's score less the corrected eval (so the
 * correction learns what's still missing): each entry by the same step, with gravity so it stays
 * within +-CORR_ENTRY_MAX. The step is scaled by the weights so that the correction itself moves by
 * 2 * error * min(depth, CORR_DEPTH_MAX) / CORR_RATE, whatever the weights: about half the error
 * with the defaults, never more than all of it (CORR_RATE >= 2 * CORR_DEPTH_MAX), so it can't
 * overshoot and oscillate.
 */
static inline_always void update_correction_at(search_state_t* search_state, int side, const int indices[CORR_TABLES],
                                               int depth, int error) {
    if (error > CORR_ERROR_MAX) error = CORR_ERROR_MAX;
    if (error < -CORR_ERROR_MAX) error = -CORR_ERROR_MAX;
    const int weight = depth < CORR_DEPTH_MAX ? depth : CORR_DEPTH_MAX;
    int total_weight = CORR_PAWN_WEIGHT + 2 * CORR_NONPAWN_WEIGHT;
    if (total_weight < 1) total_weight = 1;
    int bonus = error * CORR_GRAIN * weight * 200 / (CORR_RATE * total_weight);
    /* A step past an entry's bound means nothing (very small weights in a tuning run): capped, which
     * also keeps *entry * magnitude below int's range. With the defaults it's about half of it. */
    if (bonus > CORR_ENTRY_MAX) bonus = CORR_ENTRY_MAX;
    if (bonus < -CORR_ENTRY_MAX) bonus = -CORR_ENTRY_MAX;
    const int magnitude = bonus < 0 ? -bonus : bonus;
    for (int i = 0; i < CORR_TABLES; i++) {
        _Atomic int16_t* entry = &(*search_state->corr_history)[side][i][indices[i]];
        const int old = atomic_load_explicit(entry, memory_order_relaxed);
        int v = old + bonus - old * magnitude / CORR_ENTRY_MAX;
        if (v > CORR_ENTRY_MAX) v = CORR_ENTRY_MAX;
        if (v < -CORR_ENTRY_MAX) v = -CORR_ENTRY_MAX;
        atomic_store_explicit(entry, (int16_t) v, memory_order_relaxed);
    }
}

void update_correction(search_state_t* search_state, const board_t* board, int depth, int error) {
    int indices[CORR_TABLES];
    corr_indices(board, indices);
    update_correction_at(search_state, board->side, indices, depth, error);
}

/**
 * Move ordering score, highest first. Each category has its own band (search_constants.h): the TT
 * move, the PV move (while following the previous iteration's PV), winning and equal captures by
 * MVV-LVA plus their capture history, queen promotions, the killer, the counter move, quiet moves by
 * quiet_order_score, losing captures (with their capture history), then underpromotions (their
 * quiet score plus their band). 'qc': the node's quiet context, NULL where no quiet moves are scored
 * (qsearch out of check).
 */
static inline_always int score_move_with(move_t move, move_t tt_move, board_t* board, search_state_t* search_state,
                                         const quiet_ctx_t* qc) {
    if (tt_move && move == tt_move) return TT_MOVE_SCORE;

    if (search_state->score_pv && search_state->pv_table[0][search_state->ply] == move) {
        search_state->score_pv = false;
        return PV_MOVE_SCORE;
    }

    if (get_capture_flag(move)) {
        square target = get_target_square(move);
        piece piece = get_piece_moved(move);
        /* En passant, the target square is empty, the victim is the pawn beside it. */
        if (get_enpassant_flag(move)) {
            return CAPTURE_SCORE + mvv_lva[piece][board->side == white ? p : P] +
                   search_state->capture_history[piece][target][0] / CAPTURE_HISTORY_DIVISOR;
        }
        const int* pieces = (board->side == white) ? black_pieces : white_pieces;
        for (int i = 0; i < 6; i++) {
            if (is_bit_set(&board->pieces[pieces[i]], target)) {
                /* The capture's history ('i' is the victim's type) shifts it among the captures. */
                const int history = search_state->capture_history[piece][target][i] / CAPTURE_HISTORY_DIVISOR;
                /* Losing captures (SEE < 0) after the quiet moves. Only possible when the
                 * attacker is worth more than the victim, so SEE is skipped otherwise. */
                if (see_values[piece] > see_values[pieces[i]] && !see_ge(board, move, 0)) {
                    return BAD_CAPTURE_SCORE + mvv_lva[piece][pieces[i]] + history;
                }
                return CAPTURE_SCORE + mvv_lva[piece][pieces[i]] + history;
            }
        }
        return 0;
    } else {
        /* A queen promotion is tactical, it goes with the winning captures rather than the quiets. */
        const piece promoted = get_promoted_piece(move);
        if (promoted == Q || promoted == q) return QUEEN_PROMOTION_SCORE;
        if (!qc) return 0;
        if (!promoted) {
            if (search_state->killer_moves[search_state->ply] == move) return KILLER_SCORE;
            if (counter_move(qc) == move) return COUNTER_SCORE;
            return quiet_order_score(qc, move);
        }
        return quiet_order_score(qc, move) + (promoted == N || promoted == n ? UNDERPROMOTION_KNIGHT : UNDERPROMOTION_OTHER);
    }
}

int score_move(move_t move, move_t tt_move, board_t* board, search_state_t* search_state) {
    const quiet_ctx_t qc = quiet_ctx(search_state->quiet, board, search_state->ply, search_state->path_moves, false);
    return score_move_with(move, tt_move, board, search_state, &qc);
}


/* The type (0 pawn .. 5 king) of the piece a capture takes, on the board it's made from. */
static inline_always int captured_type(const board_t* board, move_t mv) {
    if (get_enpassant_flag(mv)) return 0;
    const piece victim = piece_on(board, get_target_square(mv), board->side ^ 1);
    return victim == none ? 0 : victim % 6;
}

/* A gravity update of the capture's capture history by a signed bonus, within +-CAPTURE_HISTORY_MAX. */
static inline_always void add_capture_history_bonus(search_state_t* search_state, const board_t* board, move_t mv,
                                                    int bonus) {
    int32_t* h = &search_state->capture_history[get_piece_moved(mv)][get_target_square(mv)][captured_type(board, mv)];
    const int v = *h + bonus - *h * (bonus < 0 ? -bonus : bonus) / CAPTURE_HISTORY_MAX;
    *h = v > CAPTURE_HISTORY_MAX ? CAPTURE_HISTORY_MAX : (v < -CAPTURE_HISTORY_MAX ? -CAPTURE_HISTORY_MAX : v);
#ifdef TIKI_SEARCH_STATS
    if (bonus > 0) search_state->stats.cap_bonuses++;
    else search_state->stats.cap_maluses++;
    if ((*h < 0 ? -*h : *h) >= CAPTURE_HISTORY_MAX * 9 / 10) search_state->stats.cap_near_bound_updates++;
#endif
}

#ifdef TIKI_SEARCH_STATS
/* The statistics' depth bands: 1-3, 4-6, 7+. */
static inline_always int stat_depth_band(int depth) {
    return depth <= 3 ? 0 : depth <= 6 ? 1 : 2;
}

/* A capture's capture history (its stored value, not the ordering term), on the board it's made from. */
static inline_always int stat_capture_history(const search_state_t* search_state, const board_t* board, move_t mv) {
    return search_state->capture_history[get_piece_moved(mv)][get_target_square(mv)][captured_type(board, mv)];
}

static inline_always int stat_history_bucket(int h) {
    return h < -1000 ? 0 : h > 1000 ? 2 : 1;
}

/* The node's captures other than the TT move, with their place among them as ordered and as they
 * would be with no capture history (ties keep move generation order, as pick_next_move does). */
typedef struct {
    int count;
    move_t moves[MOVE_BUFFER_SIZE];
    uint8_t rank[MOVE_BUFFER_SIZE];
    uint8_t rank_without[MOVE_BUFFER_SIZE];
    uint8_t rank_victim[MOVE_BUFFER_SIZE];   /* Experiment A's ranking (search_state.h). */
} stat_capture_ranks_t;

/* Experiment A's score for a capture, in the same band as its actual score ('score' tells which). */
static inline_always int stat_victim_score(int score, int victim_type, int history) {
    const int band = score < 0 ? BAD_CAPTURE_SCORE : CAPTURE_SCORE;
    return band + 4 * see_values[victim_type] + history / CAPTURE_HISTORY_DIVISOR;
}

static void stat_capture_ranks(stat_capture_ranks_t* ranks, const move_buffer_t* buffer, const int* scores,
                               move_t tt_move, const board_t* board, search_state_t* search_state, int depth,
                               bool is_pv_node) {
    int with[MOVE_BUFFER_SIZE], without[MOVE_BUFFER_SIZE], victim_scores[MOVE_BUFFER_SIZE];
    int victims[MOVE_BUFFER_SIZE];
    ranks->count = 0;
    for (int i = 0; i < buffer->index; i++) {
        const move_t mv = buffer->moves[i];
        if (!get_capture_flag(mv) || mv == tt_move) continue;
        const int n = ranks->count++;
        const int history = stat_capture_history(search_state, board, mv);
        ranks->moves[n] = mv;
        victims[n] = captured_type(board, mv);
        with[n] = scores[i];
        without[n] = scores[i] - history / CAPTURE_HISTORY_DIVISOR;
        victim_scores[n] = stat_victim_score(scores[i], victims[n], history);
    }
    for (int i = 0; i < ranks->count; i++) {
        int r = 1, r0 = 1, rv = 1;
        for (int j = 0; j < ranks->count; j++) {
            if (with[j] > with[i] || (with[j] == with[i] && j < i)) r++;
            if (without[j] > without[i] || (without[j] == without[i] && j < i)) r0++;
            if (victim_scores[j] > victim_scores[i] || (victim_scores[j] == victim_scores[i] && j < i)) rv++;
        }
        ranks->rank[i] = (uint8_t) r;
        ranks->rank_without[i] = (uint8_t) r0;
        ranks->rank_victim[i] = (uint8_t) rv;
    }
    if (ranks->count < 2) return;
    const int b = stat_depth_band(depth), p = is_pv_node, t = tt_move != 0;
    search_stats_t* stats = &search_state->stats;
    stats->cap_loops[b][p][t]++;
    stats->cap_moves[b][p][t] += (uint64_t) ranks->count;
    for (int i = 0; i < ranks->count; i++) {
        if (ranks->rank[i] != ranks->rank_without[i]) stats->cap_rank_changed[b][p][t]++;
        if (ranks->rank[i] == 1 && ranks->rank_without[i] != 1) stats->cap_top_changed[b][p][t]++;
        if (ranks->rank[i] == 1 && ranks->rank_victim[i] != 1) stats->victim_top_changed[b][p][t]++;
        for (int j = i + 1; j < ranks->count; j++) {
            if (victims[i] == victims[j] || victims[i] > 4 || victims[j] > 4) continue;
            const int lo = victims[i] < victims[j] ? victims[i] : victims[j];
            const int hi = victims[i] < victims[j] ? victims[j] : victims[i];
            stats->victim_pairs[lo][hi]++;
            if ((ranks->rank[i] < ranks->rank[j]) != (ranks->rank_victim[i] < ranks->rank_victim[j]))
                stats->victim_pair_flips[lo][hi]++;
        }
    }
}

/* A cutoff by a capture other than the TT move: its place as ordered and with no capture history. */
static void stat_capture_cut(const stat_capture_ranks_t* ranks, search_state_t* search_state, move_t mv, int depth,
                             bool is_pv_node, bool has_tt_move) {
    if (ranks->count < 2) return;
    for (int i = 0; i < ranks->count; i++) {
        if (ranks->moves[i] != mv) continue;
        const int b = stat_depth_band(depth), p = is_pv_node, t = has_tt_move;
        const int r = ranks->rank[i], r0 = ranks->rank_without[i], rv = ranks->rank_victim[i];
        search_state->stats.cap_cut_rank[b][p][t][r >= 3 ? 2 : r - 1]++;
        search_state->stats.cap_cut_rank_without[b][p][t][r0 >= 3 ? 2 : r0 - 1]++;
        search_state->stats.victim_cut_rank[b][p][t][rv >= 3 ? 2 : rv - 1]++;
        return;
    }
}
#endif

/* The capture history's bonus (and malus) at depth: min(CAPTURE_HIST_BONUS_SCALE * depth^2, CAPTURE_HIST_BONUS_MAX). */
static inline_always int capture_history_bonus(int depth) {
    const int bonus = CAPTURE_HIST_BONUS_SCALE * depth * depth;
    return bonus < CAPTURE_HIST_BONUS_MAX ? bonus : CAPTURE_HIST_BONUS_MAX;
}

/* A capture caused a beta cutoff ('board' is the position it was made from): capture_history_bonus. */
void update_capture_history(search_state_t* search_state, const board_t* board, move_t mv, int depth) {
    add_capture_history_bonus(search_state, board, mv, capture_history_bonus(depth));
}

/* A capture searched before the move that cut off didn't: the malus. */
void penalise_capture_history(search_state_t* search_state, const board_t* board, move_t mv, int depth) {
    add_capture_history_bonus(search_state, board, mv, -capture_history_bonus(depth));
}

/**
 * Scores every move at the node. The PV flag (set by negamax for this node) is cleared
 * afterwards: when the PV move is also the TT move it's never scored as the PV move, and the flag
 * mustn't carry over to whichever later node has a move matching the PV at its ply.
 */
void score_moves(const move_buffer_t* buffer, int* scores, move_t tt_move, board_t* board, search_state_t* search_state,
                 const quiet_ctx_t* qc) {
    for (int i = 0; i < buffer->index; i++) {
        scores[i] = score_move_with(buffer->moves[i], tt_move, board, search_state, qc);
    }
    search_state->score_pv = false;
}

/* Removes the moves scored below the counter move (quiet moves, losing captures, underpromotions) from
 * 'from' on, keeping the order of the others: the quiet stage ends (picker_skip_quiets). */
static inline_always void drop_quiet_moves(move_buffer_t* buffer, int* scores, int from) {
    int kept = from;
    for (int j = from; j < buffer->index; j++) {
        const move_t mv = buffer->moves[j];
        if (scores[j] < COUNTER_SCORE) continue;
        buffer->moves[kept] = mv;
        scores[kept] = scores[j];
        kept++;
    }
    buffer->index = kept;
}

/**
 * Selection sort one step at a time: swap the best scored move from i onwards into i and return it.
 * Most nodes cut off after one or two moves, so sorting the whole list up front is wasted work.
 * The scan is branchless (the comparison mispredicts often), ties keep the earlier move.
 */
move_t pick_next_move(move_buffer_t* buffer, int* scores, int i) {
    int best = i;
    int best_score = scores[i];
    for (int j = i + 1; j < buffer->index; j++) {
        const bool better = scores[j] > best_score;
        best = better ? j : best;
        best_score = better ? scores[j] : best_score;
    }
    if (best != i) {
        move_t mv = buffer->moves[best];
        buffer->moves[best] = buffer->moves[i];
        buffer->moves[i] = mv;
        int score = scores[best];
        scores[best] = scores[i];
        scores[i] = score;
    }
    return buffer->moves[i];
}


/* A beta cutoff, for the move ordering statistics. Nothing unless built with TIKI_SEARCH_STATS.
 * 'moves_searched' includes the cutting move (1: the first). */
static inline_always void count_cutoff(search_state_t* search_state, int moves_searched, bool is_quiet,
                                       int quiets_searched, bool is_capture, bool has_tt_move, bool is_tt_move) {
#ifdef TIKI_SEARCH_STATS
    search_stats_t* stats = &search_state->stats;
    stats->cutoffs++;
    if (moves_searched == 1) stats->first_move_cutoffs++;
    const int place = moves_searched <= 1 ? 0 : moves_searched == 2 ? 1 : moves_searched <= 4 ? 2 :
                      moves_searched <= 8 ? 3 : 4;
    stats->cutoff_at[place]++;
    if (is_capture) stats->capture_cutoffs++;
    if (has_tt_move) stats->cutoff_nodes_tt++;
    if (is_tt_move) stats->tt_move_cutoffs++;
    /* At nodes without a TT move, by the cutting move's kind. */
    if (!has_tt_move) stats->no_tt_cutoff_at[is_capture ? 0 : 1][place]++;
    if (is_quiet) {
        stats->quiet_cutoffs++;
        stats->quiets_before_cutoff += (uint64_t) quiets_searched;
    }
#else
    (void) search_state; (void) moves_searched; (void) is_quiet; (void) quiets_searched;
    (void) is_capture; (void) has_tt_move; (void) is_tt_move;
#endif
}

/* Statistics counters (TIKI_SEARCH_STATS), nothing otherwise. */
#ifdef TIKI_SEARCH_STATS
#define COUNT_STAT(search_state, field) ((search_state)->stats.field++)
#define STAT_EXIT(reason) (search_state->stats.exit_at_depth[(reason)][stat_entry_depth]++)
#else
#define COUNT_STAT(search_state, field) ((void) 0)
#define STAT_EXIT(reason) ((void) 0)
#endif


/* A move loop's quiet moves (TIKI_SEARCH_STATS): how many were searched, and whether the last of
 * them cut off ('quiets' is then its place among them). */
static inline_always void stat_quiet_node(search_state_t* search_state, int depth, bool is_pv_node, bool has_tt_move,
                                          int quiets, bool cut_by_quiet) {
#ifdef TIKI_SEARCH_STATS
    if (quiets == 0) return;
    const int b = stat_depth_band(depth), p = is_pv_node, t = has_tt_move;
    search_stats_t* stats = &search_state->stats;
    stats->quiet_nodes[b][p][t]++;
    stats->quiets_searched[b][p][t] += (uint64_t) quiets;
    if (cut_by_quiet) {
        stats->quiet_cut_nodes[b][p][t]++;
        stats->quiet_cut_rank[b][p][t][quiets <= 1 ? 0 : quiets == 2 ? 1 : quiets <= 4 ? 2 : quiets <= 8 ? 3 : 4]++;
    }
#else
    (void) search_state; (void) depth; (void) is_pv_node; (void) has_tt_move; (void) quiets; (void) cut_by_quiet;
#endif
}

/* A reduced search (TIKI_SEARCH_STATS): the move's place, the reduction, whether it beat alpha and,
 * if so, where the full depth search's score ('score') fell against the window. The nodes of the
 * reduced search and of the searches again at full depth (lmr_cost). */
static inline_always void stat_lmr(search_state_t* search_state, int depth, bool is_pv_node, int place, int reduction,
                                   bool beat_alpha, int score, int alpha, int beta, uint64_t reduced_nodes,
                                   uint64_t research_nodes) {
#ifdef TIKI_SEARCH_STATS
    uint64_t* cost = search_state->stats.lmr_cost[reduction >= 4 ? 3 : reduction - 1];
    cost[0] += reduced_nodes;
    if (beat_alpha) cost[score <= alpha ? 1 : 2] += research_nodes;
    const int pl = place <= 2 ? 0 : place == 3 ? 1 : place <= 5 ? 2 : place <= 8 ? 3 : place <= 16 ? 4 : 5;
    const int r = reduction >= 4 ? 3 : reduction - 1;
    uint64_t* cell = search_state->stats.lmr[stat_depth_band(depth)][is_pv_node][pl][r];
    cell[0]++;
    if (beat_alpha) {
        cell[1]++;
        cell[score <= alpha ? 2 : score < beta ? 3 : 4]++;
    }
#else
    (void) search_state; (void) depth; (void) is_pv_node; (void) place; (void) reduction; (void) beat_alpha;
    (void) score; (void) alpha; (void) beta; (void) reduced_nodes; (void) research_nodes;
#endif
}

#ifdef TIKI_SEARCH_STATS
/* A move's ordering band from its ordering score (search_constants.h): 0 TT or PV move, 1 good
 * capture or queen promotion, 2 killer, 3 counter move, 4 quiet move or underpromotion, 5 losing capture. */
static inline_always int stat_order_band(int score) {
    return score >= PV_MOVE_SCORE ? 0 : score > KILLER_SCORE ? 1 : score == KILLER_SCORE ? 2 :
           score == COUNTER_SCORE ? 3 : (score >= -QUIET_SCORE_SPAN || score < BAD_CAPTURE_SCORE - CAPTURE_HISTORY_SPAN_MAX) ? 4 : 5;
}

/* A move loop cut off (TIKI_SEARCH_STATS): 'cut_score' the cutting move's ordering score, 'loop_nodes'
 * the nodes the move loop took, 'wasted' those spent on the moves searched before it. */
static void stat_order_cut(search_state_t* search_state, int cut_score, int depth, bool cut_node, int moves_searched,
                           uint64_t wasted, uint64_t loop_nodes) {
    search_stats_t* stats = &search_state->stats;
    const int b = stat_depth_band(depth), t = cut_node, band = stat_order_band(cut_score);
    stats->ord_loops[b][t]++;
    stats->ord_cuts[b][t]++;
    if (moves_searched == 1) stats->ord_first[b][t]++;
    stats->ord_band[b][band]++;
    stats->ord_loop_nodes[b][1] += loop_nodes;
    stats->ord_wasted[b] += wasted;
    stats->ord_wasted_band[band] += wasted;
}

/* A move loop ended without a cutoff (TIKI_SEARCH_STATS): 'loop_nodes', the nodes it took. */
static inline_always void stat_order_loop_end(search_state_t* search_state, int depth, bool cut_node,
                                              uint64_t loop_nodes) {
    search_stats_t* stats = &search_state->stats;
    const int b = stat_depth_band(depth);
    stats->ord_loops[b][cut_node]++;
    stats->ord_loop_nodes[b][0] += loop_nodes;
}
#endif

/* The previous iteration's PV move at this ply if it's one of the node's moves, otherwise 0. */
static inline_always move_t find_pv_move(const move_buffer_t* buffer, const search_state_t* search_state) {
    const move_t pv_move = search_state->pv_table[0][search_state->ply];
    for (int i = 0; i < buffer->index; i++) {
        if (buffer->moves[i] == pv_move) return pv_move;
    }
    return 0;
}

/* Stops the search when out of time, or past the node limit ('go nodes', also checked at every node, count_node).
 * 'stop' and 'quit' set the flag from the input thread. */
static inline_always void check_limits(const search_state_t* search_state, limits_t* limits) {
    ++limits->time_checks;
    if (limits->time_set && clock_time_ms() > limits->stop_time) {
        limits->stop_search_flag = true;
    }
    if (limits->nodes && search_state->nodes >= limits->nodes) {
        limits->stop_search_flag = true;
    }
}

/**
 * Every 2048 nodes. The main thread checks the limits (its stop flag stops the helpers too), the
 * helpers publish their node count for the main thread's 'info' output.
 */
static inline_always void node_batch(search_state_t* search_state, limits_t* limits) {
    if (search_state->thread_id == 0) {
        check_limits(search_state, limits);
    } else {
        atomic_store_explicit(&search_state->shared_nodes, search_state->nodes, memory_order_relaxed);
    }
}

/* Counts a node: the limits every 2048 nodes, and a 'go nodes' limit exactly (the main thread's count), so a
 * small node budget isn't overshot by up to 2047 nodes. */
static inline_always void count_node(search_state_t* search_state, limits_t* limits) {
    if ((++search_state->nodes & 2047) == 0) {
        node_batch(search_state, limits);
    } else if (limits->nodes && search_state->thread_id == 0 && search_state->nodes >= limits->nodes) {
        limits->stop_search_flag = true;
    }
}

/* KvK, KNvK and KBvK (either side), neither side can mate. */
bool is_insufficient_material(const board_t* board) {
    if (board->pieces[P] | board->pieces[p] | board->pieces[R] | board->pieces[r] | board->pieces[Q] | board->pieces[q])
        return false;
    return pop_count(board->pieces[N] | board->pieces[n] | board->pieces[B] | board->pieces[b]) <= 1;
}

/* Knight, bishop, rook or queen for the side to move. Without one, zugzwang is likely. */
static inline_always bool has_non_pawn_material(const board_t* board) {
    return board->side == white ?
           (board->pieces[N] | board->pieces[B] | board->pieces[R] | board->pieces[Q]) != 0 :
           (board->pieces[n] | board->pieces[b] | board->pieces[r] | board->pieces[q]) != 0;
}

/* A mate or tablebase score: decisive, not an estimate to prune or learn from. */
static inline_always bool is_mate_score(int score) {
    return score >= TB_WIN_MIN || score <= -TB_WIN_MIN;
}

/**
 * Whether the side to move's position is improving: its network eval is above the one at its
 * previous node, two plies up, or four if that node was in check (no eval, -INF). With neither to
 * compare with, not improving.
 */
bool is_improving(int static_eval, int eval_2_plies_up, int eval_4_plies_up) {
    const int previous = eval_2_plies_up != -INF ? eval_2_plies_up : eval_4_plies_up;
    return previous != -INF && static_eval > previous;
}

/**
 * Whether to try null move pruning at this node. Not at the root or in PV nodes, not in check,
 * not straight after another null move (the position after a null move is at null_move_index),
 * not within a null move verification search (below nmp_min_ply), not with only king and pawns,
 * not when beta is a mate score, and only when the static eval is already at or above beta.
 */
bool can_null_move(const board_t* board,
                   const search_state_t* search_state,
                   int depth,
                   int beta,
                   int static_eval,
                   bool in_check,
                   bool is_pv_node) {
    if (depth < NMP_MIN_DEPTH || in_check || is_pv_node || search_state->ply == 0) return false;
    if (search_state->null_move_index != 0 && search_state->null_move_index == search_state->repetition_index) return false;
    if (search_state->ply < search_state->nmp_min_ply) return false;
    if (is_mate_score(beta) || !has_non_pawn_material(board)) return false;
    return static_eval >= beta;
}

/**
 * The null move search's depth: depth - R, R = NMP_R_BASE + depth * NMP_R_DEPTH_RATE + min((eval -
 * beta) / NMP_R_EVAL_DIV * PLY_GRAIN, NMP_R_EVAL_MAX), in 1/PLY_GRAIN ply and rounded down once, at
 * least 0 (qsearch). The eval term counts whole plies only (a ply per full NMP_R_EVAL_DIV above
 * beta); a fractional eval term hasn't been shown to help (to be SPRT'd on its own).
 * Larger the deeper the node and the further its eval is above beta (the more likely the cutoff).
 */
int null_move_depth(int depth, int static_eval, int beta) {
    const int64_t above = static_eval > beta ? (int64_t) static_eval - beta : 0;
    int64_t eval_term = above / NMP_R_EVAL_DIV * PLY_GRAIN;
    if (eval_term > NMP_R_EVAL_MAX) eval_term = NMP_R_EVAL_MAX;
    const int r = (int) ((NMP_R_BASE + (int64_t) depth * NMP_R_DEPTH_RATE + eval_term) / PLY_GRAIN);
    const int reduced = depth - r;
    return reduced > 0 ? reduced : 0;
}

/**
 * Whether the opponent (of the side to move) can win material straight away: one of their pawns
 * attacks one of our pieces, a knight or bishop attacks a rook or queen, or a rook attacks a queen.
 * Our king doesn't block their sliders (it can't step out of the line without leaving the piece
 * behind it attacked).
 */
bool opp_can_win_material(const board_t* board) {
    const int us = board->side;
    const int o = us == white ? 0 : 6;     /* our pieces, P..K or p..k */
    const int t = us == white ? 6 : 0;     /* theirs */
    const uint64_t occupancy = board->occupancy[both] & ~board->pieces[o + 5];
    uint64_t targets = board->pieces[o + 1] | board->pieces[o + 2] | board->pieces[o + 3] | board->pieces[o + 4];
    while (targets) {
        const int sq = trailing_zero_count(targets);
        targets &= targets - 1;
        if (pawn_attack((square) sq, (colour) us) & board->pieces[t + 0]) return true;
    }
    uint64_t heavy = board->pieces[o + 3] | board->pieces[o + 4];
    while (heavy) {
        const int sq = trailing_zero_count(heavy);
        heavy &= heavy - 1;
        if (knight_attack((square) sq) & board->pieces[t + 1]) return true;
        if (bishop_attack((square) sq, occupancy) & board->pieces[t + 2]) return true;
    }
    uint64_t queens = board->pieces[o + 4];
    while (queens) {
        const int sq = trailing_zero_count(queens);
        queens &= queens - 1;
        if (rook_attack((square) sq, occupancy) & board->pieces[t + 3]) return true;
    }
    return false;
}

/* The opponent's last move cost them: this node's network eval and its parent's (each for its side to
 * move) sum to more than RFP_OPP_WORSENING_MARGIN. Not when the parent was in check (no eval, -INF). */
bool is_opp_worsening(int raw_eval, int parent_raw_eval) {
    return parent_raw_eval != -INF && raw_eval + parent_raw_eval > RFP_OPP_WORSENING_MARGIN;
}

/**
 * The reverse futility margin: RFP_MARGIN * depth, less RFP_IMPROVING when the position is improving
 * and the opponent can't win material ('improving_safe'), less RFP_OPP_WORSENING when the opponent's
 * last move cost them, less RFP_NO_TT * depth without a TT entry, at least RFP_MIN_MARGIN.
 */
int rfp_margin(int depth, bool improving_safe, bool opp_worsening, bool tt_hit) {
    const int margin = RFP_MARGIN * depth - (improving_safe ? RFP_IMPROVING : 0) -
                       (opp_worsening ? RFP_OPP_WORSENING : 0) - (tt_hit ? 0 : RFP_NO_TT * depth);
    return margin > RFP_MIN_MARGIN ? margin : RFP_MIN_MARGIN;
}

/* A pruned node's score (fail-soft): its static eval moved RFP_BETA_BLEND / 1024 of the way to beta. */
int rfp_score(int static_eval, int beta) {
    return (int) (((int64_t) static_eval * (1024 - RFP_BETA_BLEND) + (int64_t) beta * RFP_BETA_BLEND) / 1024);
}

/**
 * Reverse futility (static null move) pruning: at low depth, a static eval 'margin' (rfp_margin)
 * above beta is very unlikely to drop below it, so the node fails high without a search. Not in
 * check, PV nodes, at the root or near mate scores.
 */
bool can_reverse_futility_prune(int depth, int static_eval, int beta, bool in_check, bool is_pv_node, int ply,
                                int margin) {
    return ply > 0 && !in_check && !is_pv_node && depth <= RFP_MAX_DEPTH && !is_mate_score(beta) &&
           static_eval - margin >= beta;
}

/**
 * The depth a quiet move would be searched at after the quiet table's reduction alone, at least 1
 * (reference Search.cpp:1972-1974), for the move loop's pruning; the node's depth for other moves and
 * the first move. 'moves_made': legal moves made at the node before this one.
 */
int lmr_depth_estimate(int depth, int moves_made, bool is_quiet) {
    if (!is_quiet || depth < LMR_MIN_DEPTH || moves_made < 1) return depth;
    const int d = depth < LMR_TABLE_SIZE ? depth : LMR_TABLE_SIZE - 1;
    const int m = moves_made + 1 < LMR_TABLE_SIZE ? moves_made + 1 : LMR_TABLE_SIZE - 1;
    const int estimate = depth - (lmr_quiet_table[d][m] + PLY_GRAIN / 2) / PLY_GRAIN;
    return estimate > 1 ? estimate : 1;
}

/* Late move pruning: quiet moves (counted as picked, pruned or not) from this many on are skipped
 * (reference Search.cpp:153-158). 'depth': the node's, plus LMP_PV_SCALE at PV nodes. */
int lmp_threshold(int depth, bool improving) {
    return improving ? LMP_BASE + depth * depth : LMP_BASE + depth * depth / 2;
}

/* History pruning: a quiet move whose stat is below this is skipped (reference Search.cpp:160-163). */
int history_prune_threshold(int lmr_depth) {
    return -HIST_PRUNE_LINEAR * lmr_depth - HIST_PRUNE_QUAD * lmr_depth * lmr_depth;
}

/* Futility pruning: the margin over the corrected eval for a quiet move (reference Search.cpp:2000-2001,
 * 32 * lmr_depth^2 + stat / 383 in reference eval units, here cp). */
int futility_margin(int lmr_depth, int stat) {
    return FUTILITY_LMR_SQ * lmr_depth * lmr_depth / 10 + stat / FUTILITY_STAT_DIV;
}

/**
 * A capture's or promotion's class for the reductions, as the reference's ordering scores make it
 * (MoveOrderer.cpp:400-408, PromotionValues): 2 winning (attacker type below the victim's), 1 good
 * (same type, or reference SEE >= 0), 0 losing; a promotion without a capture, and any
 * underpromotion, is below the good captures (0).
 */
int ref_capture_class(const board_t* board, move_t mv) {
    const piece promoted = get_promoted_piece(mv);
    if (!get_capture_flag(mv) || (promoted && promoted != Q && promoted != q)) return 0;
    const int attacker = get_piece_moved(mv) % 6;
    const int victim = get_enpassant_flag(mv) ? 0 : captured_type(board, mv);
    if (attacker < victim) return 2;
    return attacker == victim || see_ge_ref(board, mv, 0) ? 1 : 0;
}

/**
 * Late move reduction in 1/PLY_GRAIN ply, rounded to whole plies (reference Search.cpp:2112-2177), not
 * yet limited to the new depth. 'move_number': the move's place among those made (1-based, > 1);
 * 'refutation': the TT, PV, killer or counter move (ordered at or above the counter move).
 */
int lmr_reduction(int depth, int move_number, int ply, bool is_pv_node, bool cut_node, bool improving, bool gives_check,
                  bool is_quiet, int stat, bool tt_capture, bool refutation, int capture_class, bool tt_deep) {
    const int d = depth < LMR_TABLE_SIZE ? depth : LMR_TABLE_SIZE - 1;
    const int m = move_number < LMR_TABLE_SIZE ? move_number : LMR_TABLE_SIZE - 1;
    int r;
    if (is_quiet) {
        r = lmr_quiet_table[d][m];
        if (!is_pv_node) r += LMR_QUIET_NON_PV;
        if (tt_capture) r += LMR_QUIET_TT_CAPTURE;
        if (refutation) r -= LMR_QUIET_REFUTATION;
        r -= (stat + LMR_STAT_OFFSET) / LMR_STAT_DIV;
        if (cut_node) r += LMR_QUIET_CUT_NODE;
        if (!improving) r += LMR_QUIET_NOT_IMPROVING;
        if (gives_check) r -= LMR_QUIET_CHECK;
    } else {
        r = lmr_capture_table[d][m];
        if (capture_class == 2) r -= LMR_CAPTURE_WINNING;
        if (capture_class == 0) r += LMR_CAPTURE_BAD;
        if (cut_node) r += LMR_CAPTURE_CUT_NODE;
        if (!improving) r += LMR_CAPTURE_NOT_IMPROVING;
        if (gives_check) r -= LMR_CAPTURE_CHECK;
    }
    if (is_pv_node) r -= PLY_GRAIN * depth / (1 + ply + depth);
    if (tt_deep) r -= LMR_TT_DEPTH;
    return (r + PLY_GRAIN / 2) / PLY_GRAIN;
}

/**
 * Internal iterative reduction: with no TT move the move ordering is poor, so search the node one
 * ply shallower (it's likely to be re-searched deeper later with a TT move from this search).
 */
int iir_reduction(int depth, move_t tt_move) {
    return (depth >= IIR_MIN_DEPTH && tt_move == 0) ? 1 : 0;
}

/* ProbCut's bound: beta plus the margin, less when the position is improving. */
int probcut_beta(int beta, bool improving) {
    return beta + PROBCUT_MARGIN - (improving ? PROBCUT_IMPROVING : 0);
}

/**
 * ProbCut: at a node deep enough that isn't the root, a PV node or in check, a
 * capture or promotion that beats pc_beta (probcut_beta) in a search PROBCUT_REDUCTION plies
 * shallower is taken to beat beta at full depth, and the node is cut off without searching it. Not
 * when beta is a mate score, and not when the TT already says the node is below pc_beta from a
 * search at least as deep as the entry ProbCut would save (PROBCUT_TT_DEPTH): the try would only fail.
 */
bool can_probcut(int depth, int ply, int beta, bool in_check, bool is_pv_node, bool tt_hit, int tt_depth,
                 tt_entry_type tt_type, int tt_score, int pc_beta) {
    if (ply == 0 || in_check || is_pv_node || depth < PROBCUT_MIN_DEPTH || is_mate_score(beta)) return false;
    return !(tt_hit && tt_type != tt_none && tt_depth >= PROBCUT_TT_DEPTH(depth) && tt_score < pc_beta);
}

/**
 * Singular extensions: whether the TT move is worth testing for being much better than every
 * other move. The entry must be deep enough and a lower bound or exact (an upper bound says
 * nothing about how good the move is), not a mate score, and not at the root. Below ply 2 *
 * root depth only, so a line of singular moves can't extend the search without end.
 */
bool can_singular_extend(int depth, int ply, int root_depth, move_t tt_move, int tt_depth, tt_entry_type tt_type,
                         int tt_score) {
    return ply > 0 && ply < 2 * root_depth && depth >= SE_MIN_DEPTH && tt_move != 0 &&
           tt_depth >= depth - SE_TT_DEPTH_MARGIN && tt_type != tt_alpha && !is_mate_score(tt_score);
}

/* The other moves must all score below this for the TT move to be singular. */
int singular_beta(int tt_score, int depth) {
    return tt_score - SE_MARGIN * depth / SE_MARGIN_GRAIN;
}

/**
 * What the search without the TT move ('score', against s_beta) says about the TT move:
 * - every other move fell below s_beta: the TT move is singular, extended;
 * - another move reached s_beta, so the TT move isn't singular. At a PV node nothing changes.
 *   Outside the PV, if s_beta >= beta another move already beats beta and the node fails high
 *   (multi-cut); otherwise, if the TT move's own stored score is at or above beta, it's searched a
 *   ply shallower, since it isn't the node's only good move.
 */
singular_result_t singular_result(int score, int s_beta, int tt_score, int beta, bool is_pv_node) {
    if (score < s_beta) return singular_extend;
    if (is_pv_node) return singular_none;
    if (s_beta >= beta) return singular_multi_cut;
    if (tt_score >= beta) return singular_reduce;
    return singular_none;
}

/* What came of a singular search, for the statistics. Nothing unless built with TIKI_SEARCH_STATS. */
static inline_always void count_singular(search_state_t* search_state, uint64_t nodes, singular_result_t result) {
#ifdef TIKI_SEARCH_STATS
    search_state->stats.singular_searches++;
    search_state->stats.singular_nodes += nodes;
    if (result == singular_extend) search_state->stats.singular_extensions++;
    if (result == singular_reduce) search_state->stats.singular_reductions++;
    if (result == singular_multi_cut) search_state->stats.multi_cuts++;
#else
    (void) search_state; (void) nodes; (void) result;
#endif
}

/* Whether the side to move is in check. */
static inline_always bool side_in_check(board_t* board) {
    return board->side == white ?
           is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K])) :
           is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));
}

/* Fifty moves without a capture or pawn move, unless that last move was checkmate. */
static bool is_fifty_move_draw(board_t* board) {
    if (board->fifty_move < 100) return false;
    return !side_in_check(board) || first_legal_move(board) != 0;
}

bool is_draw(board_t* board, search_state_t* search_state) {
    return is_fifty_move_draw(board) ||
           is_insufficient_material(board) ||
           is_repetition(search_state, search_state->ply);
}

/**
 * make_move / pop_move for the search, keeping the network's accumulators in step. An illegal move
 * is made without updating the accumulators (most moves are illegal in check), so it's undone with
 * pop_move alone.
 */
static inline_always bool search_make_move(board_t* board, search_state_t* search_state, move_t mv) {
    if (!make_move(board, mv)) return false;
    /* The child probes the TT first thing, the NNUE push overlaps the memory load. */
    tt_prefetch(board->hash);
    nnue_push(&search_state->nnue, board, mv);
    return true;
}

static inline_always void search_pop_move(board_t* board, search_state_t* search_state) {
    pop_move(board);
    nnue_pop(&search_state->nnue);
}

/**
 * The static eval refined by the node's TT entry: its score replaces the eval when the bound says
 * the true score is on that side of it (an exact score, a lower bound above the eval or an upper
 * bound below it). Not mate scores, which aren't an estimate of the position.
 */
int adjusted_eval(int eval, const tt_info_t* tt) {
    if (tt == NULL || tt->type == tt_none || is_mate_score(tt->score)) return eval;
    if (tt->type == tt_exact || (tt->type == tt_beta && tt->score > eval) || (tt->type == tt_alpha && tt->score < eval))
        return tt->score;
    return eval;
}

#ifdef TIKI_VERIFY
/* What a skipped move is (verify_classify), with the move made: a check, else a capture or promotion, else quiet. */
static verify_class_t verify_move_class(board_t* board_after, move_t mv) {
    if (side_in_check(board_after)) return vc_check;
    return get_capture_flag(mv) || get_promoted_piece(mv) ? vc_capture : vc_quiet;
}

/* A qsearch capture skipped (a losing capture, delta pruning): searched with the node's null window.
 * Wrong if it beats alpha (the node's result would change). */
static void verify_qmove(verify_feature_t feature, move_t mv, int alpha, int eval, board_t* board,
                         search_state_t* search_state, limits_t* limits) {
    char fen[128];
    board_to_fen(board, 1, fen, sizeof(fen));
    const uint64_t hash = board->hash;
    search_state->path_moves[search_state->ply] = mv;
    ++search_state->ply;
    if (!search_make_move(board, search_state, mv)) {
        --search_state->ply;
        pop_move(board);
        return;
    }
    const verify_class_t cls = verify_move_class(board, mv);
    verify_cfg.nesting++;
    const uint64_t nodes_before = search_state->nodes;
    const int score = -quiescence(-alpha - 1, -alpha, board, search_state, limits);
    verify_cfg.nesting--;
    --search_state->ply;
    search_pop_move(board, search_state);
    if (limits->stop_search_flag) return;
    char uci[6];
    move_to_uci(mv, uci);
    char context[160];
    snprintf(context, sizeof(context), "qsearch ply %d alpha %d eval %d searched %d move %s", search_state->ply, alpha,
             eval, score, uci);
    verify_record(feature, 0, score > alpha, score - alpha, search_state->nodes - nodes_before, fen, context);
    verify_classify(feature, 0, score > alpha, cls, mv, hash, alpha, score);
}
#endif

/* The qsearch node itself, already counted: by quiescence, or by negamax at depth 0. */
static int quiescence_node(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    if (search_state->ply == 0) nnue_reset_root(&search_state->nnue, board);
    if (search_state->ply > MAX_PLY-1) return evaluation(board, &search_state->nnue);

    /* The TT, saved at depth 0 by qsearch (and depth 0 negamax nodes, which are qsearch). Outside
     * the PV any bound that fits the window cuts off, as in negamax. */
    const bool is_pv_node = beta - alpha > 1;
    tt_info_t tt;
    const bool tt_hit = tt_read(board->hash, search_state->ply, &tt);
    if (!is_pv_node && tt_hit && tt_cut_allowed(&tt, 0)) {
        const int cutoff = tt_cutoff(&tt, 0, alpha, beta);
        if (cutoff != TT_NOT_FOUND) return cutoff;
    }

    /* In check there's no standing pat (the position may be lost), every evasion is searched.
     * Otherwise the stand pat is the static eval, from the TT if it's there and refined by the
     * TT score's bound (adjusted_eval); 'raw_eval' is the network's, the one saved. The correction
     * history (eval_correction) corrects it first, qsearch doesn't update it. */
    const bool in_check = side_in_check(board);
    int raw_eval = TT_EVAL_NONE;
    int eval = -INF;
    if (!in_check) {
        raw_eval = tt_hit && tt.eval != TT_EVAL_NONE ? tt.eval : evaluation(board, &search_state->nnue);
        eval = VERIFY_PLAIN ? raw_eval
                            : adjusted_eval(raw_eval + eval_correction(board, search_state), tt_hit ? &tt : NULL);
    }
    /* Fail-soft: the scores returned are the ones found (the stand pat, a capture's), which may lie
     * outside the window, never clamped to alpha or beta. */
    if (eval >= beta) {
        tt_save(board->hash, tt_beta, 0, search_state->ply, eval, 0, raw_eval);
        return eval;
    }
    const int alpha_orig = alpha;
    if (eval > alpha) alpha = eval;
    int best_score = eval;  /* -INF in check: every evasion is searched, no stand pat. */

    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    if (in_check) generate_moves(board, &buffer);
    else generate_captures(board, &buffer);
    quiet_ctx_t qctx;
    if (in_check) qctx = quiet_ctx(search_state->quiet, board, search_state->ply, search_state->path_moves, false);
    score_moves(&buffer, scores, tt_hit ? tt.move : 0, board, search_state, in_check ? &qctx : NULL);

    /* Out of check, captures and queen promotions only. */
    bool has_legal_moves = false;
    move_t best_move = 0;
    for (int i=0; i< buffer.index; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);
        tt_prefetch(hash_after_move(board, mv));

        if (!in_check && !VERIFY_PLAIN) {
            /* Sorted best first, so from here on every capture loses material (SEE < 0). Quiet queen
             * promotions (QUEEN_PROMOTION_SCORE) sort with the winning captures, before this. */
            if (scores[i] < 0 && get_capture_flag(mv)) {
#ifdef TIKI_VERIFY
                /* The first one skipped, the best ordered of them. */
                if (verify_take(vf_qs_bad, 0)) verify_qmove(vf_qs_bad, mv, alpha, eval, board, search_state, limits);
#endif
                break;
            }

            /* Delta pruning, even winning the piece can't bring the score up to alpha. Its optimistic
             * score still counts towards the best, so a fail low stays a valid upper bound. */
            if (!get_promoted_piece(mv)) {
                const int optimistic = eval + captured_value(board, mv) + DELTA_MARGIN;
                const bool delta_prune = optimistic <= alpha;
#ifdef TIKI_VERIFY
                if (verify_cfg.decisions && verify_cfg.nesting == 0 && optimistic <= alpha)
                    verify_rule_record(&verify_delta_rule, !delta_prune, board, mv);
#endif
                if (delta_prune) {
#ifdef TIKI_VERIFY
                    if (verify_take(vf_delta, 0)) verify_qmove(vf_delta, mv, alpha, eval, board, search_state, limits);
#endif
                    if (optimistic > best_score) best_score = optimistic;
                    continue;
                }
            }
        }

        search_state->path_moves[search_state->ply] = mv;
        ++search_state->ply;
        if (search_make_move(board, search_state, mv)) {
            has_legal_moves = true;
            int score = -quiescence(-beta, -alpha, board, search_state, limits);
            --search_state->ply;
            search_pop_move(board, search_state);

            if (limits->stop_search_flag) return STOPPED_SEARCH;

            if (score > best_score) best_score = score;
            if (score > alpha) {
                alpha = score;
                best_move = mv;
                if (score >= beta) {
                    tt_save(board->hash, tt_beta, 0, search_state->ply, score, mv, raw_eval);
                    return score;
                }
            }
        } else {
            --search_state->ply;
            pop_move(board);
        }
    }
    if (in_check && !has_legal_moves) return -MATE_VALUE + search_state->ply;
    /* An exact score if something (the stand pat or a move) raised alpha, otherwise an upper bound. */
    tt_save(board->hash, alpha > alpha_orig ? tt_exact : tt_alpha, 0, search_state->ply, best_score, best_move,
            raw_eval);
    return best_score;
}

int quiescence(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    count_node(search_state, limits);
    COUNT_STAT(search_state, qsearch_nodes);
    return quiescence_node(alpha, beta, board, search_state, limits);
}


/**
 * The move picker: negamax's moves, best first, generated in stages so that a node that cuts early
 * doesn't generate or score the rest. The order is score_move's, by band:
 *   1. the TT move, if it's possible here (move_is_pseudo_legal: a TT entry can be another position's);
 *   2. winning and equal captures and queen promotions (generate_noisy), by their scores;
 *   3. the killer, then the counter move, if they're possible here and quiet;
 *   4. the other quiet moves and underpromotions (generate_quiets), by quiet_order_score, generated only now;
 *   5. the losing captures (SEE < 0), left over from 2 (the reference orders these among the quiet
 *      moves; tiki's capture ordering stays until it's ported).
 * The bands don't overlap (search_constants.h), so it's the order of one sorted list, but for ties
 * within a band. Each move is returned once: the TT move and killers are skipped where they come up
 * again. In check and on the previous iteration's PV (or built with TIKI_SEARCH_STATS) everything is
 * generated and scored at once instead (picker_all), as before: evasions are few, the PV move is
 * ordered after the TT move and the statistics look at the whole list.
 * picker_skip_quiets (futility): the quiet stage ends, the quiet moves and losing captures left aren't
 * returned (nor generated); the counter move still is if its stage hasn't come yet.
 */
typedef enum {
    pick_tt, pick_gen_noisy, pick_good_noisy, pick_killer, pick_counter, pick_gen_quiets, pick_quiets,
    pick_bad_noisy, picker_all, pick_done
} pick_stage_t;

typedef struct {
    move_buffer_t* buffer;
    int* scores;
    board_t* board;
    search_state_t* search_state;
    const quiet_ctx_t* qc;   /* The node's quiet context (score_move_with).                              */
    move_t tt_move;          /* Returned by pick_tt (0 if it wasn't), skipped after.                    */
    move_t killers[2];       /* Killer and counter move, returned by their stages (0 if not), skipped after. */
    pick_stage_t stage;
    int noisy_next;          /* Next noisy move, [noisy_next, noisy_end) still to pick.                  */
    int noisy_end;
    int quiet_next;          /* Next quiet move, [quiet_next, buffer->index) still to pick.              */
    int score;               /* The ordering score of the move just returned.                            */
    move_t pv_move;          /* The PV move, when following the PV and it's in the list (all at once).   */
    bool skip_quiets;
} move_picker_t;

/* The best move in [i, end) to position i (as pick_next_move, ties to the first). */
static inline_always void pick_best(move_buffer_t* buffer, int* scores, int i, int end) {
    int best = i;
    int best_score = scores[i];
    for (int j = i + 1; j < end; j++) {
        const bool better = scores[j] > best_score;
        best = better ? j : best;
        best_score = better ? scores[j] : best_score;
    }
    if (best != i) {
        const move_t mv = buffer->moves[best];
        buffer->moves[best] = buffer->moves[i];
        buffer->moves[i] = mv;
        const int score = scores[best];
        scores[best] = scores[i];
        scores[i] = score;
    }
}

/* follow_pv: whether the node is on the previous iteration's PV, as it was on entry (the null move
 * and singular searches clear search_state->follow_pv before the moves are picked). */
static inline_always void picker_init(move_picker_t* picker, move_buffer_t* buffer, int* scores, board_t* board,
                                      search_state_t* search_state, move_t tt_move, bool all_at_once, bool follow_pv,
                                      const quiet_ctx_t* qc) {
    picker->buffer = buffer;
    picker->scores = scores;
    picker->board = board;
    picker->search_state = search_state;
    picker->qc = qc;
    picker->tt_move = tt_move;
    picker->killers[0] = search_state->killer_moves[search_state->ply];
    picker->killers[1] = counter_move(qc);
    picker->skip_quiets = false;
    picker->score = 0;
    picker->noisy_next = picker->noisy_end = picker->quiet_next = 0;
    picker->pv_move = 0;
    if (all_at_once) {
        /* As before staging: every move, the PV move (if it's in the list) after the TT move. */
        generate_moves(board, buffer);
        move_t pv_move = 0;
        if (follow_pv) {
            pv_move = find_pv_move(buffer, search_state);
            if (pv_move) search_state->pv_nodes++;
        }
        picker->pv_move = pv_move;
        search_state->score_pv = pv_move != 0;
        score_moves(buffer, scores, tt_move, board, search_state, qc);
        picker->stage = picker_all;
    } else {
        search_state->score_pv = false;
        picker->stage = pick_tt;
    }
}

/* The killer or counter move for its stage: possible here, quiet (they're only stored for quiet moves),
 * not the TT move. */
static inline_always bool picker_killer_ok(move_picker_t* picker, move_t killer) {
    if (!killer || killer == picker->tt_move || !is_quiet_move(killer)) return false;
    return move_is_pseudo_legal(picker->board, killer);
}

/* Futility pruning: the quiet stage ends (reference MovePicker::SkipQuiets): nothing scored below the
 * counter move is returned any more. */
static inline_always void picker_skip_quiets(move_picker_t* picker) {
    if (picker->skip_quiets) return;
    picker->skip_quiets = true;
    if (picker->stage == picker_all) {
        drop_quiet_moves(picker->buffer, picker->scores, picker->noisy_next);
    } else if (picker->stage == pick_gen_quiets || picker->stage == pick_quiets || picker->stage == pick_bad_noisy) {
        picker->stage = pick_done;
    }
}

/* The next move, 0 when there are none left (no move encodes as 0: its squares would be the same). */
static move_t picker_next(move_picker_t* picker) {
    move_buffer_t* buffer = picker->buffer;
    int* scores = picker->scores;
    for (;;) {
        switch (picker->stage) {
        case picker_all:
            /* noisy_next is the cursor over the whole list here. */
            if (picker->noisy_next >= buffer->index) return 0;
            pick_best(buffer, scores, picker->noisy_next, buffer->index);
            picker->score = scores[picker->noisy_next];
            return buffer->moves[picker->noisy_next++];

        case pick_tt:
            picker->stage = pick_gen_noisy;
            if (picker->tt_move && move_is_pseudo_legal(picker->board, picker->tt_move)) {
                picker->score = TT_MOVE_SCORE;
                return picker->tt_move;
            }
            picker->tt_move = 0;
            break;

        case pick_gen_noisy:
            /* Scored as score_move does, without the TT move (it's skipped instead). */
            generate_noisy(picker->board, buffer);
            picker->noisy_end = buffer->index;
            for (int i = 0; i < buffer->index; i++) {
                scores[i] = score_move_with(buffer->moves[i], 0, picker->board, picker->search_state, picker->qc);
            }
            picker->noisy_next = 0;
            picker->stage = pick_good_noisy;
            break;

        case pick_good_noisy:
            /* Down to the killers' band: what's left are the losing captures, for pick_bad_noisy. */
            while (picker->noisy_next < picker->noisy_end) {
                pick_best(buffer, scores, picker->noisy_next, picker->noisy_end);
                if (scores[picker->noisy_next] <= KILLER_SCORE) break;
                const move_t mv = buffer->moves[picker->noisy_next];
                picker->score = scores[picker->noisy_next++];
                if (mv == picker->tt_move) continue;
                return mv;
            }
            picker->stage = pick_killer;
            break;

        case pick_killer:
            picker->stage = pick_counter;
            if (picker_killer_ok(picker, picker->killers[0])) {
                picker->score = KILLER_SCORE;
                return picker->killers[0];
            }
            picker->killers[0] = 0;
            break;

        case pick_counter:
            /* A counter move equal to the killer is the killer's (score_move checks it first): if the killer
             * was returned it's still in killers[0]; if it wasn't, the same move fails the same tests. */
            picker->stage = pick_gen_quiets;
            if (picker->killers[1] != picker->killers[0] && picker_killer_ok(picker, picker->killers[1])) {
                picker->score = COUNTER_SCORE;
                return picker->killers[1];
            }
            picker->killers[1] = 0;
            break;

        case pick_gen_quiets:
            /* Appended after the noisy moves (the losing captures still among them). Once late move
             * pruning has started only promotions would be returned, so without a pawn on the 7th
             * (2nd for black) there's nothing to generate. */
            if (picker->skip_quiets) {
                picker->stage = pick_done;
                break;
            }
            picker->stage = pick_quiets;
            picker->quiet_next = picker->noisy_end;
            buffer->index = picker->noisy_end;
            generate_quiets(picker->board, buffer);
            for (int i = picker->noisy_end; i < buffer->index; i++) {
                scores[i] = score_move_with(buffer->moves[i], 0, picker->board, picker->search_state, picker->qc);
            }
            break;

        case pick_quiets:
            while (picker->quiet_next < buffer->index) {
                pick_best(buffer, scores, picker->quiet_next, buffer->index);
                const move_t mv = buffer->moves[picker->quiet_next];
                picker->score = scores[picker->quiet_next++];
                if (mv == picker->tt_move || mv == picker->killers[0] || mv == picker->killers[1]) continue;
                return mv;
            }
            picker->stage = pick_bad_noisy;
            break;

        case pick_bad_noisy:
            while (picker->noisy_next < picker->noisy_end) {
                pick_best(buffer, scores, picker->noisy_next, picker->noisy_end);
                const move_t mv = buffer->moves[picker->noisy_next];
                picker->score = scores[picker->noisy_next++];
                if (mv == picker->tt_move) continue;
                return mv;
            }
            picker->stage = pick_done;
            break;

        case pick_done:
            return 0;
        }
    }
}

/**
 * After a node's search, the correction history learns from its result (update_correction_at):
 * the score's error against the corrected eval, when the result says something about the eval.
 * Not after a capture or promotion (the score is the tactic's), not mate scores, and a bound only
 * on the side that shows an error: a fail high above the eval, a fail low below it. The caller
 * leaves out nodes in check (no eval) and singular extension searches (not the node's result).
 *
 * The error is against the correction the node used ('corrected_eval'). When other threads share
 * the table (Lazy SMP) it's against the correction as it is now instead: several threads finishing
 * the same positions would otherwise each add a step for the same error and overshoot. With one
 * thread the search is unchanged (the step size is tuned for it).
 */
static inline_always void learn_correction(search_state_t* search_state, int side, const int indices[CORR_TABLES],
                                           int depth, int score, int raw_eval, int corrected_eval,
                                           tt_entry_type bound, move_t best_move) {
    if (is_mate_score(score)) return;
    if (best_move && (get_capture_flag(best_move) || get_promoted_piece(best_move))) return;
    if (search_state->corr_shared) corrected_eval = raw_eval + correction_at(search_state, side, indices);
    if (bound == tt_beta && score <= corrected_eval) return;
    if (bound == tt_alpha && score >= corrected_eval) return;
    update_correction_at(search_state, side, indices, depth, score - corrected_eval);
}

#ifdef TIKI_VERIFY
static int negamax_node(int alpha, int beta, int depth, bool cut_node, board_t* board, search_state_t* search_state,
                        limits_t* limits);

/* The position as a FEN plus 'what', for the log of wrong decisions. */
static void verify_context(char* out, size_t size, const char* what, int depth, int ply, int alpha, int beta,
                           int static_eval, int claimed, int truth, move_t mv) {
    char uci[6] = "-";
    if (mv) move_to_uci(mv, uci);
    snprintf(out, size, "%s depth %d ply %d alpha %d beta %d eval %d claimed %d searched %d move %s", what, depth, ply,
             alpha, beta, static_eval, claimed, truth, uci);
}

/**
 * A node-level decision (the node returned at or above beta without searching its moves): the same
 * node searched again without it ('feature' skipped at this node only, everything below as usual).
 * Wrong if that search ends below beta. 'depth' is the node's before IIR (negamax_node applies it).
 */
static void verify_node(verify_feature_t feature, int decision_depth, int alpha, int beta, int depth, bool cut_node,
                        int static_eval, int claimed, board_t* board, search_state_t* search_state, limits_t* limits) {
    char fen[128];
    board_to_fen(board, 1, fen, sizeof(fen));
    verify_cfg.nesting++;
    const uint64_t nodes_before = search_state->nodes;
    const bool follow_pv = search_state->follow_pv;
    search_state->follow_pv = false;
    verify_cfg.skip_feature = feature;
    verify_cfg.skip_ply = search_state->ply;
    const int truth = negamax_node(alpha, beta, depth, cut_node, board, search_state, limits);
    verify_cfg.skip_ply = -1;
    search_state->follow_pv = follow_pv;
    verify_cfg.nesting--;
    if (limits->stop_search_flag) return;
    char context[160];
    verify_context(context, sizeof(context), "node", decision_depth, search_state->ply, alpha, beta, static_eval, claimed,
                   truth, 0);
    verify_record(feature, decision_depth, truth < beta, beta - truth, search_state->nodes - nodes_before, fen, context);
    verify_classify(feature, decision_depth, truth < beta, vc_node, 0, board->hash, -beta, -truth);
}

/**
 * A move-level decision (a move skipped): the move searched at 'child_depth' with the node's null
 * window, as the move loop would have. Wrong if it beats alpha (the node's result would change).
 * 'made': the move is already made on the board (and its network update pushed), but the ply hasn't
 * moved on yet, as in the move loop's pruning after make. An illegal move isn't a decision.
 */
static void verify_move(verify_feature_t feature, move_t mv, bool made, int alpha, int depth, int child_depth,
                        bool cut_node, int static_eval, board_t* board, search_state_t* search_state, limits_t* limits) {
    /* The node's position (before the move), for the log. */
    if (made) pop_move(board);
    char fen[128];
    board_to_fen(board, 1, fen, sizeof(fen));
    const uint64_t hash = board->hash;
    if (!make_move(board, mv)) {
        pop_move(board);
        return;
    }
    const verify_class_t cls = verify_move_class(board, mv);
    if (!made) nnue_push(&search_state->nnue, board, mv);

    verify_cfg.nesting++;
    const uint64_t nodes_before = search_state->nodes;
    const bool follow_pv = search_state->follow_pv;
    search_state->path_moves[search_state->ply] = mv;
    ++search_state->ply;
    ++search_state->repetition_index;
    record_position(search_state, search_state->repetition_index, board->hash, board->fifty_move);
    search_state->follow_pv = false;
    const int score = -negamax_node(-alpha - 1, -alpha, child_depth, !cut_node, board, search_state, limits);
    --search_state->ply;
    --search_state->repetition_index;
    if (!made) {
        pop_move(board);
        nnue_pop(&search_state->nnue);
    }
    search_state->follow_pv = follow_pv;
    verify_cfg.nesting--;
    if (limits->stop_search_flag) return;
    char context[160];
    verify_context(context, sizeof(context), "move", depth, search_state->ply, alpha, alpha + 1, static_eval, alpha,
                   score, mv);
    verify_record(feature, depth, score > alpha, score - alpha, search_state->nodes - nodes_before, fen, context);
    verify_classify(feature, depth, score > alpha, cls, mv, hash, alpha, score);
}

/**
 * A logged node searched again (verify 'node' mode): as an interior node one ply below a root, null
 * window at 'alpha', 'depth' plies, an expected cut node. The accumulator is set up from the board, as
 * the root does it; the root depth (the extensions' limit) is the node's own depth.
 */
int verify_node_search(board_t* board, search_state_t* search_state, limits_t* limits, int alpha, int depth) {
    nnue_reset_root(&search_state->nnue, board);
    search_state->ply = 1;
    search_state->root_depth = depth;
    search_state->follow_pv = false;
    const int score = negamax_node(alpha, alpha + 1, depth, true, board, search_state, limits);
    search_state->ply = 0;
    return score;
}

/* Where a score falls against the window: 0 at or below alpha, 1 inside, 2 at or above beta. */
static int verify_window_class(int score, int alpha, int beta) {
    return score <= alpha ? 0 : score >= beta ? 2 : 1;
}

/**
 * A move searched again from its child (the move made, the ply moved on, as in the move loop) at
 * 'child_depth' with the node's null window: a reduced search that failed low, re-searched less
 * reduced. Wrong if it beats alpha.
 */
static void verify_research(verify_feature_t feature, int alpha, int child_depth, bool child_cut, int depth,
                            int static_eval, int claimed, move_t mv, bool follow_pv, uint64_t node_hash,
                            board_t* board, search_state_t* search_state, limits_t* limits) {
    char fen[128];
    board_to_fen(board, 1, fen, sizeof(fen));
    const verify_class_t cls = verify_move_class(board, mv);
    verify_cfg.nesting++;
    const uint64_t nodes_before = search_state->nodes;
    search_state->follow_pv = false;
    const int score = -negamax_node(-alpha - 1, -alpha, child_depth, child_cut, board, search_state, limits);
    search_state->follow_pv = follow_pv;
    verify_cfg.nesting--;
    if (limits->stop_search_flag) return;
    char context[160];
    verify_context(context, sizeof(context), "after the move", depth, search_state->ply - 1, alpha, alpha + 1,
                   static_eval, claimed, score, mv);
    verify_record(feature, depth, score > alpha, score - alpha, search_state->nodes - nodes_before, fen, context);
    verify_classify(feature, depth, score > alpha, cls, mv, node_hash, alpha, score);
}

/* A depth change checked both ways: 'changed' if the two scores fall differently against the window. */
static void verify_compare(verify_feature_t feature, int depth, int alpha, int beta, int without, int with,
                           uint64_t nodes, int static_eval, move_t mv, int ply, board_t* board) {
    char fen[128];
    board_to_fen(board, 1, fen, sizeof(fen));
    char context[160];
    verify_context(context, sizeof(context), "with / without", depth, ply, alpha, beta, static_eval, without, with, mv);
    verify_record(feature, depth, verify_window_class(with, alpha, beta) != verify_window_class(without, alpha, beta),
                  with - without, nodes, fen, context);
}

/**
 * A node-level depth change (IIR, the check extension): the node searched with it and without it
 * ('feature' skipped at this node), the shallower one first so the deeper one's TT entry doesn't
 * cut the other off. 'depth' is the node's as its parent gave it. Changed if the two results fall
 * differently against the window.
 */
static void verify_both_ways(verify_feature_t feature, int decision_depth, bool feature_shallower, int alpha, int beta,
                             int depth, bool cut_node, int static_eval, board_t* board, search_state_t* search_state,
                             limits_t* limits) {
    verify_cfg.nesting++;
    const uint64_t nodes_before = search_state->nodes;
    const bool follow_pv = search_state->follow_pv;
    int with = 0, without = 0;
    for (int run = 0; run < 2; run++) {
        const bool skip = (run == 0) != feature_shallower;
        search_state->follow_pv = false;
        if (skip) {
            verify_cfg.skip_feature = feature;
            verify_cfg.skip_ply = search_state->ply;
        }
        const int score = negamax_node(alpha, beta, depth, cut_node, board, search_state, limits);
        verify_cfg.skip_ply = -1;
        if (skip) without = score;
        else with = score;
    }
    /* The node's own PV row starts again (the runs used it). */
    search_state->pv_length[search_state->ply] = search_state->ply;
    search_state->follow_pv = follow_pv;
    verify_cfg.nesting--;
    if (limits->stop_search_flag) return;
    verify_compare(feature, decision_depth, alpha, beta, without, with, search_state->nodes - nodes_before, static_eval,
                   0, search_state->ply, board);
}
#endif

/**
 * The search of a node, 'depth' plies from the horizon, in the window (alpha, beta); negamax is the
 * root's. Fail-soft: returns the best score found, which may lie outside the window (at or above beta
 * a lower bound, at or below alpha an upper bound), or STOPPED_SEARCH once the search is stopped. A
 * PV node has a full window (beta - alpha > 1), every other node a null window.
 *
 * cut_node: the node is expected to fail high, set by the parent where it searches its moves. Never
 * in a PV node: a PV node's first child is a PV node, the null-window children alternate, a reduced
 * late move's child is a cut node (it should fail high, so the move fails low here), and the null
 * move's child and its verification aren't. LMR reduces more in a cut node (lmr_reduction).
 *
 * In order: draws and the ply limit, the check extension, the TT (a cutoff outside the PV), qsearch
 * at depth 0, the tablebases, IIR, the static eval (corrected, then adjusted by the TT score), the
 * node's pruning (reverse futility, null move, ProbCut), singular extensions, then the moves best
 * first with their pruning (late move, SEE, futility, history), LMR and PVS. The result is saved to
 * the TT and teaches the correction history; a move that raises alpha updates the quiet history, and
 * a cutoff the killers, the history maluses and the capture history.
 */
static int negamax_node(int alpha,
                        int beta,
                        int depth,
                        bool cut_node,
                        board_t* board,
                        search_state_t* search_state,
                        limits_t* limits) {

    /* Every node, including those that return early (TT, draw, pruning). Checked straight after
     * counting (count_node). */
    count_node(search_state, limits);
#ifdef TIKI_SEARCH_STATS
    const int stat_entry_depth = depth < 0 ? 0 : depth > 16 ? 16 : depth;
    search_state->stats.nodes_at_depth[stat_entry_depth]++;
    if (search_state->root_depth > 0) {
        const int net = search_state->ply + depth - search_state->root_depth;
        search_state->stats.ext_net[net < -10 ? 0 : net > 21 ? 31 : net + 10]++;
        const int q = search_state->ply * 4 / search_state->root_depth;
        search_state->stats.ply_quarters[q > 12 ? 12 : q]++;
    }
#endif

    search_state->pv_length[search_state->ply] = search_state->ply;

    /* The root, the accumulators start again from this position. */
    if (search_state->ply == 0) {
        nnue_reset_root(&search_state->nnue, board);
        search_state->best_move_nodes = 0;
        search_state->root_search_start = search_state->nodes;
    }

    /* Whether this node is on the previous iteration's PV, set by the parent for this node only. */
    const bool follow_pv = search_state->follow_pv;
    /* The depth the parent gave, before the check extension and IIR (node-level checks search again from it). */
    const int depth_param = depth;
    (void) depth_param;

#ifdef TIKI_VERIFY
    /* A node-level check (verify_node): this node is searched again without the decision it took. */
    const uint64_t verify_hash = board->hash;
    int verify_skip = -1;
    if (verify_cfg.skip_ply == search_state->ply) {
        verify_skip = verify_cfg.skip_feature;
        verify_cfg.skip_ply = -1;
    }
#define VERIFY_SKIPS(feature) (verify_skip == (feature))
#else
#define VERIFY_SKIPS(feature) false
#endif

    /* Not at the root, so a move is always returned. */
    if (search_state->ply > 0 && is_draw(board, search_state)) {
        STAT_EXIT(0);
        return 0;
    }

    /* The next ply's killer belongs to the nodes below this one (reference Search.cpp:1615-1616). */
    search_state->killer_moves[search_state->ply + 1] = 0;

    /* The deepest ply the tables hold. No moves are made from here, so no node is ever at MAX_PLY
     * (pv_length and pv_table are read at ply + 1). */
    if (search_state->ply >= MAX_PLY - 1) return evaluation(board, &search_state->nnue);

    /* Extend checks before the depth 0 test, so a check at the horizon is searched rather than
     * going straight to qsearch. Only below ply 2 * root depth, as the singular extensions: a long
     * series of checks otherwise extends the line ply after ply (in endgames 44% of the nodes were
     * past the nominal depth). Beyond it a check at the horizon goes to qsearch, which searches
     * every evasion in check (it doesn't stand pat), so a mate there is still found. */
    bool in_check = side_in_check(board);

    /* A singular extension search of this node without the TT move (see below). Its result isn't
     * the node's: it doesn't cut off from the TT, save to it, or prune the whole node. Its depth
     * comes from the node's, already extended for check, so it isn't extended again. */
    const move_t excluded_move = search_state->excluded_moves[search_state->ply];

    if (in_check && !excluded_move && search_state->ply < 2 * search_state->root_depth && !VERIFY_PLAIN &&
        !VERIFY_SKIPS(vf_check_ext)) {
        ++depth;
#ifdef TIKI_VERIFY
        if (search_state->ply > 0 && verify_take(vf_check_ext, depth_param))
            verify_both_ways(vf_check_ext, depth_param, false, alpha, beta, depth_param, cut_node, -INF, board,
                             search_state, limits);
#endif
    }

    /* Probed with the extended depth, the same depth the node is saved with. */
    bool is_pv_node = beta - alpha > 1;
    const int alpha_orig = alpha;
    tt_info_t tt;
    const bool tt_hit = tt_read(board->hash, search_state->ply, &tt);
    move_t tt_move = tt_hit ? tt.move : 0;
    if (search_state->ply && !is_pv_node && !excluded_move && tt_hit && tt_cut_allowed(&tt, depth)) {
        const int cutoff = tt_cutoff(&tt, depth, alpha, beta);
        if (cutoff != TT_NOT_FOUND) {
            if (depth > 0) STAT_EXIT(1);
            return cutoff;
        }
    }

    if (depth == 0) {
        return quiescence_node(alpha, beta, board, search_state, limits);
    }

    /* Tablebases (syzygy.h): after a capture or pawn move, with few enough pieces. Positions with as
     * many pieces as the largest tables only from SyzygyProbeDepth. The result is saved as a TT bound
     * (a win a lower bound, a loss an upper bound: a faster mate may exist, a draw exact) and
     * returned (fail-soft, the score itself): the node isn't searched for a faster mate. */
    if (search_state->ply > 0 && !excluded_move && syzygy_search_pieces > 0 && board->fifty_move == 0 &&
        board->castle_flag == 0) {
        const int pieces = pop_count(board->occupancy[both]);
        int wdl;
        if (pieces <= syzygy_search_pieces && (pieces < syzygy_search_pieces || depth >= syzygy_probe_depth) &&
            syzygy_probe_wdl(board, &wdl)) {
            const int ply = search_state->ply;
            const int tb_score = wdl > 0 ? TB_WIN_SCORE - ply : wdl < 0 ? -TB_WIN_SCORE + ply : 0;
            const tt_entry_type tb_type = wdl > 0 ? tt_beta : wdl < 0 ? tt_alpha : tt_exact;
            const int tb_depth = depth + 6 < MAX_PLY - 1 ? depth + 6 : MAX_PLY - 1;
            tt_save(board->hash, tb_type, tb_depth, ply, tb_score, 0, tt_hit ? tt.eval : TT_EVAL_NONE);
            STAT_EXIT(2);
            return tb_score;
        }
    }

    if (!VERIFY_PLAIN && !VERIFY_SKIPS(vf_iir)) {
        const int iir = iir_reduction(depth, tt_move);
#ifdef TIKI_VERIFY
        if (iir && search_state->ply > 0 && !excluded_move && verify_take(vf_iir, depth))
            verify_both_ways(vf_iir, depth, true, alpha, beta, depth_param, cut_node, -INF, board, search_state, limits);
#endif
        depth -= iir;
    }

    /* Not meaningful in check, every move is searched there anyway. The network's eval is taken
     * from the TT when it's there ('raw_eval', what's saved), corrected by the correction history
     * ('corrected_eval', learnt from below), then replaced by the TT score when its bound says the
     * true score is on that side of it (adjusted_eval). */
    int raw_eval = TT_EVAL_NONE;
    int corrected_eval = -INF;
    int corr_idx[CORR_TABLES];
    if (!in_check) {
        raw_eval = tt_hit && tt.eval != TT_EVAL_NONE ? tt.eval : evaluation(board, &search_state->nnue);
        /* A new position's eval is saved straight away, nodes pruned before their search keep it. */
        if (!tt_hit && !excluded_move) tt_save(board->hash, tt_none, 0, search_state->ply, 0, 0, raw_eval);
        corr_indices(board, corr_idx);
        corrected_eval = raw_eval + (VERIFY_PLAIN ? 0 : correction_at(search_state, board->side, corr_idx));
    }
    const int static_eval = in_check ? -INF : VERIFY_PLAIN ? corrected_eval : adjusted_eval(corrected_eval, tt_hit ? &tt : NULL);

    /* Improving: the network's eval against this side's previous node (is_improving). Every node
     * two or four plies up is an ancestor, so its entry was written before this node was reached.
     * Not straight after a null move: the side to move didn't get a move in between. */
    const int ply = search_state->ply;
    search_state->static_evals[ply] = in_check ? -INF : raw_eval;
    const bool after_null_move =
        search_state->null_move_index != 0 && search_state->null_move_index == search_state->repetition_index;
    const bool improving = !in_check && !after_null_move &&
                           is_improving(raw_eval, ply >= 2 ? search_state->static_evals[ply - 2] : -INF,
                                        ply >= 4 ? search_state->static_evals[ply - 4] : -INF);

    /* Reverse futility pruning (fail-soft, rfp_score; never a mate score). The threat test only when the
     * eval clears beta by the smallest margin, the rest can't be pruned anyway. */
    if (!excluded_move && !in_check && !is_pv_node && ply > 0 && depth <= RFP_MAX_DEPTH && !VERIFY_PLAIN &&
        !VERIFY_SKIPS(vf_rfp) && static_eval - RFP_MIN_MARGIN >= beta) {
        const bool improving_safe = improving && !opp_can_win_material(board);
        const bool opp_worsening = is_opp_worsening(raw_eval, search_state->static_evals[ply - 1]);
        const int margin = rfp_margin(depth, improving_safe, opp_worsening, tt_hit);
        if (can_reverse_futility_prune(depth, static_eval, beta, in_check, is_pv_node, ply, margin)) {
            STAT_EXIT(3);
            const int score = rfp_score(static_eval, beta);
#ifdef TIKI_VERIFY
            if (verify_take(vf_rfp, depth))
                verify_node(vf_rfp, depth, alpha_orig, beta, depth_param, cut_node, static_eval, score,
                            board, search_state, limits);
#endif
            return score;
        }
    }

    /* Null move pruning, searched at null_move_depth. */
    if (!excluded_move && !VERIFY_PLAIN && !VERIFY_SKIPS(vf_null) &&
        can_null_move(board, search_state, depth, beta, static_eval, in_check, is_pv_node)) {
        const int null_depth = null_move_depth(depth, static_eval, beta);
#ifdef TIKI_SEARCH_STATS
        search_state->stats.null_tried_at_depth[stat_entry_depth]++;
#endif
        search_state->path_moves[search_state->ply] = 0;
        make_null_move(board);
        search_state->repetition_check[++search_state->repetition_index] = board->hash;
        search_state->repetition_distance[search_state->repetition_index] = 0; /* Nothing repeats across it. */
        /* Repetitions can't span the null move, it isn't a real move. */
        const uint16_t null_move_index = search_state->null_move_index;
        search_state->null_move_index = search_state->repetition_index;
        search_state->follow_pv = false;
        ++search_state->ply;
        int score = -negamax_node(-beta, -beta + 1, null_depth, false, board, search_state, limits);
        --search_state->ply;
        search_state->null_move_index = null_move_index;
        --search_state->repetition_index;
        pop_null_move(board);
        /* A stopped null search returns -STOPPED_SEARCH, which would look like a cutoff. */
        if (limits->stop_search_flag) return STOPPED_SEARCH;
        if (score >= beta) {
            /* Fail-soft: the null search's score, but beta for a mate or tablebase score (a mate found
             * after passing isn't real). */
            const int null_score = is_mate_score(score) ? beta : score;
            if (depth < NMP_VERIFY_DEPTH || search_state->nmp_min_ply != 0) {
                STAT_EXIT(4);
#ifdef TIKI_VERIFY
                if (verify_take(vf_null, depth))
                    verify_node(vf_null, depth, alpha_orig, beta, depth_param, cut_node, static_eval, null_score, board,
                                search_state, limits);
#endif
                return null_score;
            }
            /* Deep, the cutoff is verified by searching this node normally at the null search's depth,
             * with null moves off for the first three quarters of it, so a zugzwang (where passing
             * would be best) can't cut off a large subtree. */
            search_state->nmp_min_ply = (uint8_t) (ply + 3 * null_depth / 4);
            const int verified = negamax_node(beta - 1, beta, null_depth, false, board, search_state, limits);
            search_state->nmp_min_ply = 0;
            if (limits->stop_search_flag) return STOPPED_SEARCH;
            if (verified >= beta) {
                STAT_EXIT(4);
#ifdef TIKI_VERIFY
                if (verify_take(vf_null, depth))
                    verify_node(vf_null, depth, alpha_orig, beta, depth_param, cut_node, static_eval, verified, board,
                                search_state, limits);
#endif
                return is_mate_score(verified) ? beta : verified;
            }
        }
    }

    /* ProbCut (can_probcut). The captures and promotions whose exchange alone brings the eval up to
     * pc_beta (SEE), best first, the TT move ahead of them: qsearch first, and only if that holds
     * the reduced search. A cutoff is saved as a lower bound at PROBCUT_TT_DEPTH (the reduced search plus
     * the move), and returned less the margin (fail-soft; a mate score as it is). */
    const int pc_beta = probcut_beta(beta, improving);
    if (!excluded_move && !VERIFY_PLAIN && !VERIFY_SKIPS(vf_probcut) &&
        can_probcut(depth, search_state->ply, beta, in_check, is_pv_node, tt_hit,
                                      tt_hit ? tt.depth : 0, tt_hit ? tt.type : tt_none, tt_hit ? tt.score : 0,
                                      pc_beta)) {
        move_buffer_t pc_buffer;
        int pc_scores[MOVE_BUFFER_SIZE];
        generate_noisy(board, &pc_buffer);
        for (int i = 0; i < pc_buffer.index; i++) {
            pc_scores[i] = score_move_with(pc_buffer.moves[i], tt_move, board, search_state, NULL);
        }
        const int pc_depth = depth - PROBCUT_REDUCTION;
        for (int i = 0; i < pc_buffer.index; i++) {
            pick_best(&pc_buffer, pc_scores, i, pc_buffer.index);
            const move_t mv = pc_buffer.moves[i];
            if (!see_ge(board, mv, pc_beta - static_eval)) continue;
            if (!search_make_move(board, search_state, mv)) {
                pop_move(board);
                continue;
            }
            search_state->path_moves[search_state->ply] = mv;
            ++search_state->ply;
            ++search_state->repetition_index;
            record_position(search_state, search_state->repetition_index, board->hash, board->fifty_move);
            search_state->follow_pv = false;
            int pc_score = -quiescence(-pc_beta, -pc_beta + 1, board, search_state, limits);
            if (pc_score >= pc_beta && pc_depth > 0 && !limits->stop_search_flag) {
                search_state->follow_pv = false;
                pc_score = -negamax_node(-pc_beta, -pc_beta + 1, pc_depth, !cut_node, board, search_state, limits);
            }
            --search_state->ply;
            --search_state->repetition_index;
            search_pop_move(board, search_state);
            if (limits->stop_search_flag) return STOPPED_SEARCH;
            if (pc_score >= pc_beta) {
                tt_save(board->hash, tt_beta, PROBCUT_TT_DEPTH(depth), search_state->ply, pc_score, mv, raw_eval);
                STAT_EXIT(5);
#ifdef TIKI_VERIFY
                if (verify_take(vf_probcut, depth))
                    verify_node(vf_probcut, depth, alpha_orig, beta, depth_param, cut_node, static_eval,
                                is_mate_score(pc_score) ? pc_score : pc_score - (pc_beta - beta), board, search_state,
                                limits);
#endif
                return is_mate_score(pc_score) ? pc_score : pc_score - (pc_beta - beta);
            }
        }
    }

    /**
     * Singular extensions: search the node without the TT move, at half the depth and with a null
     * window just under its TT score. If every other move fails low, the TT move is singular, much
     * better than the rest, and it's searched one ply deeper. Otherwise, outside the PV, the node
     * may fail high now (multi-cut) or the TT move be searched a ply shallower, see
     * singular_result. 'extension' only ever applies to the TT move.
     */
    int extension = 0;
    if (!excluded_move && tt_move && !VERIFY_PLAIN) {
        move_t stored_move;
        int tt_score, tt_depth;
        tt_entry_type tt_type;
        if (tt_lookup(board->hash, search_state->ply, &stored_move, &tt_score, &tt_depth, &tt_type) &&
            stored_move == tt_move &&
            can_singular_extend(depth, search_state->ply, search_state->root_depth, tt_move, tt_depth, tt_type,
                                tt_score)) {
            const int s_beta = singular_beta(tt_score, depth);
            search_state->excluded_moves[search_state->ply] = tt_move;
            search_state->follow_pv = false;
            const uint64_t nodes_before = search_state->nodes;
            const int score = negamax_node(s_beta - 1, s_beta, (depth - 1) / 2, cut_node, board, search_state, limits);
            search_state->excluded_moves[search_state->ply] = 0;
            /* The search without the TT move used this ply's PV row, the node starts its own. */
            search_state->pv_length[search_state->ply] = search_state->ply;
            if (limits->stop_search_flag) return STOPPED_SEARCH;

            const singular_result_t result = singular_result(score, s_beta, tt_score, beta, is_pv_node);
            count_singular(search_state, search_state->nodes - nodes_before, result);
            /* Fail-soft: another move reached s_beta (>= beta), a lower bound on the node. */
            if (result == singular_multi_cut && !VERIFY_SKIPS(vf_multicut)) {
                STAT_EXIT(6);
#ifdef TIKI_VERIFY
                if (verify_take(vf_multicut, depth))
                    verify_node(vf_multicut, depth, alpha_orig, beta, depth_param, cut_node, static_eval, s_beta, board,
                                search_state, limits);
#endif
                return s_beta;
            }
            if (result == singular_extend) extension = 1;
            else if (result == singular_reduce) extension = -SE_REDUCTION;
        }
    }

    /* The moves, best first (move_picker_t): staged, or all at once in check, on the previous
     * iteration's PV and in statistics builds. On the PV, its move is ordered first (after the TT
     * move) and its child is on the PV too. The node's quiet context (quiet_history.h) scores and
     * updates the quiet moves. */
    const quiet_ctx_t qc = quiet_ctx(search_state->quiet, board, ply, search_state->path_moves, true);
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
#ifdef TIKI_SEARCH_STATS
    const bool all_at_once = true;
#else
    const bool all_at_once = in_check || follow_pv;
#endif
    move_picker_t picker;
    picker_init(&picker, &buffer, scores, board, search_state, tt_move, all_at_once, follow_pv, &qc);
#ifdef TIKI_SEARCH_STATS
    search_state->stats.expanded_at_depth[stat_entry_depth]++;
#endif
    const move_t pv_move = picker.pv_move;
#ifdef TIKI_SEARCH_STATS
    stat_capture_ranks_t capture_ranks = { .count = 0 };
    if (!excluded_move) stat_capture_ranks(&capture_ranks, &buffer, scores, tt_move, board, search_state, depth, is_pv_node);
#endif

    /* In the move loop a node whose static eval is already at or above beta counts as improving
     * (reference Search.cpp:1872-1874). */
    const bool loop_improving = improving || (!in_check && raw_eval >= beta);
    const bool tt_capture = tt_move != 0 && (get_capture_flag(tt_move) || get_promoted_piece(tt_move));

    bool has_legal_moves = false;
    int num_moves_searched = 0; /* Legal moves made so far (the reference's moveIndex before the next). */
    int quiet_index = 0;        /* Quiet moves picked so far, pruned or not (quietMoveIndex). */
    int score;
    /* Fail-soft: the best score of the moves searched, returned whatever the window (alpha only moves
     * when a move scores above it). */
    int best_score = -INF;
    move_t best_move = 0;
    if (tt_move && !excluded_move) COUNT_STAT(search_state, tt_move_nodes);
    /* Quiet moves searched, for the quiet history update at a fail high (the cutting move among them),
     * and captures searched before a cutoff, for the capture history malus. */
    move_t quiets_tried[QUIETS_TRIED_MAX];
    int num_quiets_tried = 0;
    move_t captures_searched[MAX_CAPTURES_PENALISED];
    int num_captures_searched = 0;
#ifdef TIKI_SEARCH_STATS
    const uint64_t loop_nodes_start = search_state->nodes;   /* For stat_order_cut and stat_order_loop_end. */
#endif
    move_t mv;
    while ((mv = picker_next(&picker)) != 0) {
        if (mv == excluded_move) continue;
        /* The child's TT entry starts loading now, before the move is pruned or made (hash_after_move). */
        tt_prefetch(hash_after_move(board, mv));
        /* At a root in the tablebases, only the moves that keep its best result (syzygy_rank_root), and only
         * those given with 'go searchmoves'. */
        if (search_state->ply == 0 && (!syzygy_root_allows(mv) || !limits_root_allows(limits, mv))) continue;
        const bool is_capture = get_capture_flag(mv);
        const bool is_quiet = !is_capture && !get_promoted_piece(mv);
        const piece promoted = get_promoted_piece(mv);
        const bool is_underpromotion = promoted && promoted != Q && promoted != q;
        const int stat = is_quiet ? quiet_stat(&qc, mv) : 0;
        if (is_quiet) quiet_index++;

        /* Pruning before the move is made (reference Search.cpp:1968-2027): not at the root, and only once a
         * move has been searched (best_score was -INF) and the best score isn't a decisive loss. */
        const int lmr_depth = lmr_depth_estimate(depth, num_moves_searched, is_quiet);
        if (ply > 0 && best_score > -TB_WIN_MIN && !VERIFY_PLAIN) {
            if (is_quiet || is_underpromotion) {
                /* Late move pruning: in the quiet stage the rest of the moves go too. */
                if (quiet_index >= lmp_threshold(depth + LMP_PV_SCALE * is_pv_node, loop_improving)) {
#ifdef TIKI_VERIFY
                    if (verify_take(vf_lmp, depth))
                        verify_move(vf_lmp, mv, false, alpha, depth, depth - 1, cut_node, static_eval, board,
                                    search_state, limits);
#endif
                    if (picker.score < COUNTER_SCORE) break;
                    continue;
                }
                /* History pruning. */
                if (quiet_index > 1 && depth < HIST_PRUNE_MAX_DEPTH && stat < history_prune_threshold(lmr_depth)) {
#ifdef TIKI_VERIFY
                    if (verify_take(vf_history, depth))
                        verify_move(vf_history, mv, false, alpha, depth, depth - 1, cut_node, static_eval, board,
                                    search_state, limits);
#endif
                    continue;
                }
                /* Futility pruning: the quiet stage ends; the first quiet move is still searched. */
                if (!in_check && corrected_eval + futility_margin(lmr_depth, stat) < alpha) {
                    picker_skip_quiets(&picker);
                    if (quiet_index > 1) {
#ifdef TIKI_VERIFY
                        if (verify_take(vf_futility, depth))
                            verify_move(vf_futility, mv, false, alpha, depth, depth - 1, cut_node, static_eval, board,
                                        search_state, limits);
#endif
                        continue;
                    }
                }
            }
            /* SEE pruning, only for a move to a square the opponent attacks (reference SEE). */
            if ((qc.threats.all >> get_target_square(mv)) & 1) {
                const bool see_prune = is_capture ?
                    depth <= SEE_CAPTURE_DEPTH && ref_capture_class(board, mv) == 0 &&
                        !see_ge_ref(board, mv, -SEE_CAPTURE_MULT * depth) :
                    depth <= SEE_QUIET_DEPTH && !see_ge_ref(board, mv, -SEE_QUIET_MULT * lmr_depth - stat / SEE_QUIET_STAT_DIV);
                if (see_prune) {
#ifdef TIKI_VERIFY
                    if (verify_take(vf_see, depth))
                        verify_move(vf_see, mv, false, alpha, depth, depth - 1, cut_node, static_eval, board,
                                    search_state, limits);
#endif
                    continue;
                }
            }
        }

        /* The reduction's inputs that need the position before the move. */
        const int capture_class = is_quiet ? 0 : ref_capture_class(board, mv);
        const bool refutation = picker.score >= COUNTER_SCORE;

        if (!search_make_move(board, search_state, mv)) {
            pop_move(board);
            continue;
        }
        has_legal_moves = true;
        const int move_number = num_moves_searched + 1;

        /* At the root, the nodes this move takes, for time management (tm_scale) and the node cache. */
        const uint64_t nodes_before_move = search_state->nodes;

        /* Late move reduction (lmr_reduction): every move after the first, captures and promotions not
         * at PV nodes, limited to the new depth. */
        int reduction = 0;
        if (!VERIFY_PLAIN && depth >= LMR_MIN_DEPTH && move_number > 1 && (!is_pv_node || is_quiet)) {
            reduction = lmr_reduction(depth, move_number, ply, is_pv_node, cut_node, loop_improving, side_in_check(board),
                                      is_quiet, stat, tt_capture, refutation, capture_class, tt_hit && tt.depth >= depth);
        }
        /* The TT move a ply deeper if it's singular, shallower if it isn't but beat beta (see above). */
        int new_depth = depth - 1 + (mv == tt_move ? extension : 0);
        if (reduction > new_depth) reduction = new_depth;
        if (reduction < 0) reduction = 0;
#ifdef TIKI_SEARCH_STATS
        if (stat_entry_depth >= 2 && stat_entry_depth <= 4) {
            const bool stat_check = side_in_check(board);
            const int kind = (mv == tt_move || num_moves_searched == 0) ? 0 : stat_check ? 1 :
                             (!is_quiet && capture_class > 0) ? 2 : refutation ? 3 : !is_quiet ? 4 : 5;
            search_state->stats.move_kind[stat_entry_depth - 2][kind][reduction > 0]++;
        }
#endif

        /* Set before every search of the child, each one changes it further down the tree. */
        const bool child_on_pv = pv_move != 0 && mv == pv_move;
        search_state->path_moves[search_state->ply] = mv;
        ++search_state->ply;
        ++search_state->repetition_index;
        record_position(search_state, search_state->repetition_index, board->hash, board->fifty_move);

#ifdef TIKI_VERIFY
        /* A singular extension: the TT move searched without it too, first (the shallower search). */
        int se_unextended = 0;
        uint64_t se_nodes = 0;
        const bool se_check = move_number == 1 && mv == tt_move && extension > 0 && verify_take(vf_se_ext, depth);
        if (se_check) {
            verify_cfg.nesting++;
            const uint64_t nodes_before = search_state->nodes;
            search_state->follow_pv = false;
            se_unextended = -negamax_node(-beta, -alpha, depth - 1, !is_pv_node && !cut_node, board, search_state, limits);
            se_nodes = search_state->nodes - nodes_before;
            verify_cfg.nesting--;
        }
#endif
        if (move_number == 1 && mv == tt_move && !excluded_move) COUNT_STAT(search_state, tt_move_first);

        /* Principal variation search (reference Search.cpp:2179-2237): a reduced null-window search
         * (the child a cut node) that beats alpha is searched again at the new depth, a ply deeper if it
         * beat the best score by more than LMR_DEEPER_MARGIN, a ply shallower if not by the new depth's
         * worth, and only if that's deeper than the reduced search; otherwise (no reduction) a
         * null-window search at the new depth; at PV nodes the first move and any move inside the window
         * get the full window. */
        score = -INF;
        bool full_depth = !(is_pv_node && move_number == 1);
        if (reduction > 0) {
            const int reduced_depth = new_depth - reduction;
            search_state->follow_pv = child_on_pv;
            score = -negamax_node(-alpha - 1, -alpha, reduced_depth, true, board, search_state, limits);
            COUNT_STAT(search_state, lmr_searches);
            const bool reduced_beat_alpha = score > alpha;
            const uint64_t reduced_search_end = search_state->nodes;   /* For stat_lmr's node cost. */
#ifdef TIKI_VERIFY
            /* A reduced move that failed low isn't searched again: would it have beaten alpha at full depth? */
            if (score <= alpha && !limits->stop_search_flag) {
                const verify_feature_t f = cut_node ? vf_lmr_cutnode : vf_lmr;
                if (verify_take(f, depth))
                    verify_research(f, alpha, new_depth, !cut_node, depth, static_eval, score, mv, child_on_pv,
                                    verify_hash, board, search_state, limits);
            }
#endif
            if (score > alpha && ply > 0) {
                COUNT_STAT(search_state, lmr_fail_highs);
                new_depth += score > best_score + LMR_DEEPER_MARGIN && ply < 2 * search_state->root_depth;
                new_depth -= 10 * (score - best_score) < LMR_SHALLOWER_PER_PLY * new_depth;
                full_depth = new_depth > reduced_depth;
            } else {
                full_depth = false;
            }
            if (full_depth) {
                search_state->follow_pv = child_on_pv;
                score = -negamax_node(-alpha - 1, -alpha, new_depth, !cut_node, board, search_state, limits);
                if (score > alpha) COUNT_STAT(search_state, lmr_research_holds);
            }
            if (!excluded_move && !limits->stop_search_flag) {
                stat_lmr(search_state, depth, is_pv_node, move_number, reduction, reduced_beat_alpha, score, alpha, beta,
                         reduced_search_end - nodes_before_move, search_state->nodes - reduced_search_end);
            }
        } else if (full_depth) {
            search_state->follow_pv = child_on_pv;
            score = -negamax_node(-alpha - 1, -alpha, new_depth, !cut_node, board, search_state, limits);
        }
        if (is_pv_node && (move_number == 1 || (score > alpha && (ply == 0 || score < beta)))) {
            /* The TT move isn't dropped into qsearch here (reference Search.cpp:2227-2229). */
            if (mv == tt_move && search_state->root_depth > PV_TT_MOVE_MIN_ROOT_DEPTH && tt_hit && tt.depth > 1 &&
                new_depth < 1)
                new_depth = 1;
            search_state->follow_pv = child_on_pv;
            score = -negamax_node(-beta, -alpha, new_depth, false, board, search_state, limits);
        }
#ifdef TIKI_VERIFY
        if (se_check && !limits->stop_search_flag)
            verify_compare(vf_se_ext, depth, alpha, beta, se_unextended, score, se_nodes, static_eval, mv,
                           search_state->ply - 1, board);
        /* The TT move searched a ply shallower (not singular, but it beat beta) failed low. */
        if (move_number == 1 && mv == tt_move && extension < 0 && score <= alpha && !limits->stop_search_flag &&
            verify_take(vf_se_reduce, depth))
            verify_research(vf_se_reduce, alpha, depth - 1, !is_pv_node && !cut_node, depth, static_eval, score, mv,
                            child_on_pv, verify_hash, board, search_state, limits);
#endif

        ++num_moves_searched;
        --search_state->ply;
        --search_state->repetition_index;
        search_pop_move(board, search_state);

        if (limits->stop_search_flag) return STOPPED_SEARCH;

        /* Near the root, the nodes each move took (reference Search.cpp:2250-2256). */
        if (qc.cache) node_cache_add(qc.cache, mv, search_state->nodes - nodes_before_move);
        if (is_quiet && num_quiets_tried < QUIETS_TRIED_MAX) quiets_tried[num_quiets_tried++] = mv;

        if (score > best_score) best_score = score;
        if (score > alpha) {
            best_move = mv;
            if (search_state->ply == 0) search_state->best_move_nodes = search_state->nodes - nodes_before_move;

            search_state->pv_table[search_state->ply][search_state->ply] = mv;
            for (int j = search_state->ply + 1; j < search_state->pv_length[search_state->ply + 1]; j++) {
                search_state->pv_table[search_state->ply][j] = search_state->pv_table[search_state->ply + 1][j];
            }
            search_state->pv_length[search_state->ply] = search_state->pv_length[search_state->ply + 1];

            alpha = score;

            if (score >= beta) {
                count_cutoff(search_state, num_moves_searched, is_quiet, num_quiets_tried - (is_quiet ? 1 : 0), is_capture, tt_move != 0,
                             mv == tt_move);
                if (!excluded_move) {
                    tt_save(board->hash, tt_beta, depth, search_state->ply, score, mv, raw_eval);
                    if (!in_check) learn_correction(search_state, board->side, corr_idx, depth, score, raw_eval,
                                                    corrected_eval, tt_beta, mv);
                }
                /* The reference's update at a fail high (Search.cpp:2339-2350): a quiet best move updates
                 * the quiet history (quiet_history_update) and becomes the killer. */
                if (is_quiet) {
                    const int score_diff = score - beta < HIST_SCORE_DIFF_MAX ? score - beta : HIST_SCORE_DIFF_MAX;
                    quiet_history_update(&qc, quiets_tried, num_quiets_tried, mv, depth, score_diff);
                    search_state->killer_moves[search_state->ply] = mv;
                }
                /* The capture that cut is rewarded, the captures tried before it (whatever cut) lose. */
                if (is_capture) update_capture_history(search_state, board, mv, depth);
                for (int j = 0; j < num_captures_searched; j++) {
                    penalise_capture_history(search_state, board, captures_searched[j], depth);
                }
                if (!excluded_move) stat_quiet_node(search_state, depth, is_pv_node, tt_move != 0, num_quiets_tried, is_quiet);
#ifdef TIKI_SEARCH_STATS
                if (is_capture && mv != tt_move && !excluded_move)
                    stat_capture_cut(&capture_ranks, search_state, mv, depth, is_pv_node, tt_move != 0);
                if (!excluded_move)
                    stat_order_cut(search_state, picker.score, depth, cut_node, num_moves_searched,
                                   nodes_before_move - loop_nodes_start, search_state->nodes - loop_nodes_start);
#endif
                return score;
            }
            /* Alpha raised: the remaining moves are searched a ply shallower (reference Search.cpp:2317). */
            if (depth > ALPHA_IMPROVEMENT_MIN_DEPTH) depth--;
        }

        if (is_capture && num_captures_searched < MAX_CAPTURES_PENALISED) captures_searched[num_captures_searched++] = mv;
    }

    if (!excluded_move) stat_quiet_node(search_state, depth, is_pv_node, tt_move != 0, num_quiets_tried, false);
#ifdef TIKI_SEARCH_STATS
    if (!excluded_move && has_legal_moves)
        stat_order_loop_end(search_state, depth, cut_node, search_state->nodes - loop_nodes_start);
#endif

    /* Without the TT move there may be nothing else to play: no other move, so it's singular. */
    if (!has_legal_moves) {
        if (excluded_move) return alpha;
        return in_check ? -MATE_VALUE + search_state->ply : 0;
    }
    /* Every legal move pruned (futility, history, SEE): their scores are taken to be at most alpha, as
     * the pruning assumed, so alpha is the upper bound. */
    if (best_score == -INF) best_score = alpha;

    /* A fail low: the quiet move that led here gains in the parent's continuation history (reference
     * Search.cpp:2352-2358). */
    if (ply > 0 && best_score <= alpha_orig && qc.prev_move && is_quiet_move(qc.prev_move)) {
        piece_to_history_t* parent_rows[CONT_PLIES];
        cont_rows_at(search_state->quiet, board->side ^ 1, ply - 1, search_state->path_moves, parent_rows);
        const int bonus = depth * PRIOR_CMH_SCALE - PRIOR_CMH_BIAS;
        cont_history_update(parent_rows, qc.prev_move, bonus < PRIOR_CMH_CAP ? bonus : PRIOR_CMH_CAP);
    }
    if (excluded_move) return best_score;

    /* An exact score if a move scored inside the window (alpha moved, best_score == alpha), otherwise
     * an upper bound: best_score, at or below the original alpha. */
    if (alpha > alpha_orig) {
        tt_save(board->hash, tt_exact, depth, search_state->ply, best_score, best_move, raw_eval);
        if (!in_check) learn_correction(search_state, board->side, corr_idx, depth, best_score, raw_eval,
                                        corrected_eval, tt_exact, best_move);
    } else {
        tt_save(board->hash, tt_alpha, depth, search_state->ply, best_score, 0, raw_eval);
        if (!in_check) learn_correction(search_state, board->side, corr_idx, depth, best_score, raw_eval,
                                        corrected_eval, tt_alpha, 0);
    }

    return best_score;
}

/* The root's search (a PV node, so not a cut node), also the tests'. Its depth is the root depth
 * (singular and check extensions are bounded by it), as aspiration_search sets it. */
int negamax(int alpha, int beta, int depth, board_t* board, search_state_t* search_state, limits_t* limits) {
    search_state->root_depth = (uint8_t) depth;
    return negamax_node(alpha, beta, depth, false, board, search_state, limits);
}

/* Used when no iteration completed (stopped very early), 0 if there are no legal moves. */
move_t first_legal_move(board_t* board) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        bool legal = make_move(board, buffer.moves[i]);
        pop_move(board);
        if (legal) return buffer.moves[i];
    }
    return 0;
}

/* The only legal move, 0 if there are none or more than one. */
move_t only_legal_move(board_t* board) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    move_t only = 0;
    for (int i = 0; i < buffer.index; i++) {
        bool legal = make_move(board, buffer.moves[i]);
        pop_move(board);
        if (!legal) continue;
        if (only) return 0;
        only = buffer.moves[i];
    }
    return only;
}

/* The fallback move when no depth completed: the first legal move the root restriction allows
 * (syzygy_rank_root). Not first_legal_move, which the search uses at any node (the fifty-move rule's
 * checkmate test), where the root's restriction doesn't apply. */
static move_t first_root_move(board_t* board, const limits_t* limits) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        bool legal = make_move(board, buffer.moves[i]);
        pop_move(board);
        if (legal && syzygy_root_allows(buffer.moves[i]) && limits_root_allows(limits, buffer.moves[i])) {
            return buffer.moves[i];
        }
    }
    return 0;
}

/**
 * Root search at 'depth' in a window around the previous depth's score. On a fail the failing
 * side is widened, doubling each time, until the window is wider than ASPIRATION_MAX_DELTA and the
 * full window is used. Shallow depths and mate scores use the full window straight away.
 */
int aspiration_search(board_t* board, search_state_t* search_state, limits_t* limits,
                      int depth, int previous_score) {
    int delta = ASPIRATION_WINDOW;
    int alpha = -INF;
    int beta = INF;
    if (depth >= ASPIRATION_MIN_DEPTH && !is_mate_score(previous_score) && !VERIFY_PLAIN) {
        alpha = previous_score - delta;
        beta = previous_score + delta;
    }

    search_state->root_depth = (uint8_t) depth;
    while (true) {
        search_state->follow_pv = true;
        int score = negamax(alpha, beta, depth, board, search_state, limits);
        if (limits->stop_search_flag) return score;

        if (score <= alpha && alpha > -INF) {
            alpha -= delta;
        } else if (score >= beta && beta < INF) {
            beta += delta;
        } else {
            return score;
        }
        delta *= 2;
        if (delta > ASPIRATION_MAX_DELTA) {
            alpha = -INF;
            beta = INF;
        }
    }
}

/**
 * Lazy SMP helper: its own board and search state, the TT is shared. It searches the same root
 * as the main thread and its results reach the main thread through the TT and the shared
 * correction history (corr_history_t). Odd helpers
 * search one ply deeper, so the threads spread over more of the tree.
 */
typedef struct {
    board_t board;
    search_state_t search_state;
    limits_t* limits;
    pthread_t thread;
    thread_result_t result;        /* Its last completed depth, for the vote (read after it's joined). */
    uint32_t pv[MAX_PLY];          /* That depth's PV, printed if its move is chosen. */
    uint8_t pv_length;
} search_helper_t;

/**
 * The helpers, allocated once and reused by later searches rather than allocated each time.
 * Grown when 'Threads' needs more. Only one search runs at a time.
 */
static search_helper_t* helpers_kept = NULL;
static int helpers_allocated = 0;

/* The search state is 64 byte aligned (align), which malloc and realloc don't guarantee. */
static search_helper_t* allocate_helpers(int count) {
    const size_t size = (size_t) count * sizeof(search_helper_t);
#if defined(_WIN32)
    return _aligned_malloc(size, 64);
#else
    return aligned_alloc(64, size);
#endif
}

/* Frees 'count' helpers and their continuation history tables (the correction history is shared). */
static void free_helpers(search_helper_t* helpers, int count) {
    if (helpers == NULL) return;
    for (int i = 0; i < count; i++) free(helpers[i].search_state.quiet);
#if defined(_WIN32)
    _aligned_free(helpers);
#else
    free(helpers);
#endif
}

static void* helper_search(void* arg) {
    search_helper_t* helper = arg;
    limits_t* limits = helper->limits;

    /* The rest of clear_pv_table (see start_helpers), on this thread so the helpers set up in
     * parallel. */
    clear_search_tables(&helper->search_state);
    nnue_reset(&helper->search_state.nnue);

    const int extra = helper->search_state.thread_id & 1;
    int score = 0;

    for (int i = 1; i <= limits->depth && !limits->stop_search_flag; i++) {
        const int depth = i + extra <= limits->depth ? i + extra : limits->depth;
        score = aspiration_search(&helper->board, &helper->search_state, limits, depth, score);
        /* A stopped depth is incomplete (its PV partly overwritten): the vote takes the last one before. */
        const search_state_t* state = &helper->search_state;
        if (limits->stop_search_flag || !state->pv_length[0]) continue;
        helper->result = (thread_result_t) { state->pv_table[0][0], score, depth };
        helper->pv_length = state->pv_length[0];
        memcpy(helper->pv, state->pv_table[0], helper->pv_length * sizeof(helper->pv[0]));
    }
    atomic_store_explicit(&helper->search_state.shared_nodes, helper->search_state.nodes, memory_order_relaxed);
    return NULL;
}

static uint64_t total_nodes(const search_state_t* search_state, const search_helper_t* helpers, int helper_count) {
    uint64_t nodes = search_state->nodes;
    for (int i = 0; i < helper_count; i++) {
        nodes += atomic_load_explicit(&helpers[i].search_state.shared_nodes, memory_order_relaxed);
    }
    return nodes;
}

static void print_info(int score, int depth, uint64_t nodes, const uint32_t* pv, int pv_length, const limits_t* limits) {
    const uint64_t time = clock_time_ms() - limits->start_time;
    if (score > -MATE_VALUE && score < -MATE_SCORE) {
        printf("info score mate %d depth %d nodes %" PRIu64 " time %" PRIu64 " nps %" PRIu64 " pv ",
               -(score + MATE_VALUE) / 2 - 1, depth, nodes, time, nodes * 1000 / (time ? time : 1));
    } else if (score > MATE_SCORE && score < MATE_VALUE) {
        printf("info score mate %d depth %d nodes %" PRIu64 " time %" PRIu64 " nps %" PRIu64 " pv ",
               (MATE_VALUE - score) / 2 + 1, depth, nodes, time, nodes * 1000 / (time ? time : 1));
    } else if (is_mate_score(score)) {
        /* A tablebase win or loss: shown as 200 pawns less the plies to the tablebase position. */
        const int cp = score > 0 ? 20000 - (TB_WIN_SCORE - score) : -20000 + (TB_WIN_SCORE + score);
        printf("info score cp %d depth %d nodes %" PRIu64 " time %" PRIu64 " nps %" PRIu64 " pv ",
               cp, depth, nodes, time, nodes * 1000 / (time ? time : 1));
    } else {
        printf("info score cp %d depth %d nodes %" PRIu64 " time %" PRIu64 " nps %" PRIu64 " pv ",
               score, depth, nodes, time, nodes * 1000 / (time ? time : 1));
    }

    for (int count = 0; count < pv_length; count++) {
        print_move(pv[count], min);
        printf(" ");
    }
    printf("\n");
}

/**
 * Starts limits->threads - 1 helpers on copies of the root position, returns the number started
 * (fewer if memory or thread creation fails). Each helper sets up its own search state, only the
 * game history for repetitions (up to the root), the move history and the scalars are copied from
 * the main thread's.
 */
static int start_helpers(board_t* board, search_state_t* search_state, limits_t* limits, search_helper_t** helpers) {
    int count = limits->threads - 1;
    *helpers = helpers_kept;
    search_state->corr_shared = false; /* Set below once a helper shares the correction history. */
    if (count <= 0) return 0;

    /* Nothing in a helper carries over between searches (history is the main thread's), so a larger
     * set starts afresh. */
    if (count > helpers_allocated) {
        search_helper_t* grown = allocate_helpers(count);
        /* Each helper has its own quiet history (quiet_history_t, 3.3 MB); the correction history is the
         * main thread's, shared (set for each search below). */
        int tables = 0;
        while (grown != NULL && tables < count) {
            quiet_history_t* table = malloc(sizeof(quiet_history_t));
            if (table == NULL) break;
            init_search_state_with(&grown[tables].search_state, table, search_state->corr_history);
            tables++;
        }
        if (grown == NULL || tables < count) {
            if (grown != NULL) free_helpers(grown, tables);
            count = helpers_allocated;
        } else {
            free_helpers(helpers_kept, helpers_allocated);
            helpers_kept = grown;
            helpers_allocated = count;
        }
    }
    *helpers = helpers_kept;

    int started = 0;
    for (; started < count; started++) {
        search_helper_t* helper = &(*helpers)[started];
        memcpy(&helper->board, board, sizeof(board_t));
        search_state_t* state = &helper->search_state;
        memcpy(state->repetition_check, search_state->repetition_check,
               (search_state->repetition_index + 1) * sizeof(search_state->repetition_check[0]));
        memcpy(state->repetition_distance, search_state->repetition_distance,
               (search_state->repetition_index + 1) * sizeof(search_state->repetition_distance[0]));
        state->repetition_index = search_state->repetition_index;
        state->null_move_index = search_state->null_move_index;
        state->nmp_min_ply = 0;
        state->nodes = 0;
        state->pv_nodes = 0;
        state->ply = 0;
        state->score = 0;
        state->score_pv = false;
        state->follow_pv = true;
        state->thread_id = started + 1;
        memcpy(state->capture_history, search_state->capture_history, sizeof(state->capture_history));
        memcpy(state->quiet, search_state->quiet, sizeof(quiet_history_t));
        state->corr_history = search_state->corr_history; /* Shared, see corr_history_t. */
        state->corr_shared = true;
        helper->result.move = 0; /* No depth completed yet. */
        helper->pv_length = 0;
        atomic_init(&state->shared_nodes, 0);
        helper->limits = limits;
        if (pthread_create(&helper->thread, NULL, helper_search, helper) != 0) break;
    }
    search_state->corr_shared = started > 0;
    return started;
}

static void stop_helpers(limits_t* limits, search_helper_t* helpers, int count) {
    if (count > 0) {
        /* With 'go infinite' the helpers carry on until 'stop', as the main thread waits for it anyway. */
        if (!limits->infinite) limits->stop_search_flag = true;
        for (int i = 0; i < count; i++) pthread_join(helpers[i].thread, NULL);
    }
}

/* By how many depths in a row the best move has stayed the same (capped at the last entry): more
 * time when it just changed (TM_UNSTABLE_PERCENT, tunable, 2.5 by default), less when it has held. */
static const double tm_stability_scale[TM_STABILITY_MAX + 1] = { 0.0 /* TM_UNSTABLE_PERCENT */, 1.2, 0.9, 0.8, 0.75 };

double tm_scale(int stability, uint64_t best_move_nodes, uint64_t root_nodes) {
    if (stability < 0) stability = 0;
    if (stability > TM_STABILITY_MAX) stability = TM_STABILITY_MAX;
    const double stability_scale = stability == 0 ? TM_UNSTABLE_PERCENT / 100.0 : tm_stability_scale[stability];
    /* The best move's share of the root's nodes: near 1 it's clearly best, low when others came close. */
    const double fraction = root_nodes ? (double) best_move_nodes / (double) root_nodes : 1.0;
    const double nodes_scale = (1.5 - (fraction > 1.0 ? 1.0 : fraction)) * (TM_NODES_PERCENT / 100.0);
    return stability_scale * nodes_scale;
}

int vote_best_thread(const thread_result_t* results, int count) {
    int best = -1;
    int min_score = INF;
    for (int i = 0; i < count; i++) {
        if (!results[i].move) continue;
        if (best < 0) best = i;
        if (results[i].score < min_score) min_score = results[i].score;
    }
    if (best < 0) return -1;

    int64_t weights[MAX_SEARCH_THREADS];
    int64_t votes[MAX_SEARCH_THREADS];
    for (int i = 0; i < count; i++) {
        weights[i] = results[i].move
                   ? (int64_t) (results[i].score - min_score + VOTE_SCORE_OFFSET) * results[i].depth : 0;
    }
    for (int i = 0; i < count; i++) {
        votes[i] = 0;
        for (int j = 0; j < count; j++) {
            if (results[j].move == results[i].move) votes[i] += weights[j];
        }
    }

    for (int i = 0; i < count; i++) {
        if (!results[i].move || i == best) continue;
        const int score = results[i].score;
        const int best_score = results[best].score;
        const bool win = score >= TB_WIN_MIN;
        const bool loss = score <= -TB_WIN_MIN;
        const bool best_win = best_score >= TB_WIN_MIN;
        const bool best_loss = best_score <= -TB_WIN_MIN;
        if (best_win || win) {
            /* A win beats anything that isn't one, a faster win a slower one. */
            if (win && (!best_win || score > best_score)) best = i;
        } else if (best_loss) {
            /* Out of a loss by anything that isn't one, a slower loss beats a faster one. */
            if (score > best_score) best = i;
        } else if (!loss && (votes[i] > votes[best] || (votes[i] == votes[best] && weights[i] > weights[best]))) {
            best = i;
        }
    }
    return best;
}

/*
 * Lazy SMP: after the helpers have stopped, the vote's choice between the main thread's last
 * completed depth ('main_result') and theirs. Prints the chosen thread's 'info' line if it isn't the
 * main thread (whose line was the last printed), and returns its move.
 */
static move_t vote_move(search_state_t* search_state, const limits_t* limits, const search_helper_t* helpers,
                        int helper_count, thread_result_t main_result) {
    thread_result_t results[MAX_SEARCH_THREADS];
    int count = 0;
    results[count++] = main_result;
    for (int i = 0; i < helper_count && count < MAX_SEARCH_THREADS; i++) results[count++] = helpers[i].result;

    const int best = vote_best_thread(results, count);
    if (best <= 0) return main_result.move;

    const search_helper_t* winner = &helpers[best - 1];
    search_state->score = winner->result.score;
    if (!limits->quiet) {
        print_info(winner->result.score, winner->result.depth, total_nodes(search_state, helpers, helper_count),
                   winner->pv, winner->pv_length, limits);
    }
    return winner->result.move;
}

move_t find_best_move(board_t* board, search_state_t* search_state, limits_t* limits) {
    int score = 0;

    int depth = limits->depth;
    move_t move = 0;
    move_t previous_move = 0;
    int stability = 0;
    int completed_depth = 0;

    /* On the clock, a forced move is played at once, saving the time for later moves. Not with
     * 'movetime', a depth or node limit or 'infinite' (analysis, bench), which search as asked. */
    if (limits->adaptive) {
        const move_t only = only_legal_move(board);
        if (only) return only;
    }

    search_state->thread_id = 0;
    /* Before the helpers start: they read the root restriction it sets. */
    syzygy_rank_root(board);
    search_helper_t* helpers;
    const int helper_count = start_helpers(board, search_state, limits, &helpers);

    for (int i = 1; i <= depth; i++) {
        score = aspiration_search(board, search_state, limits, i, score);

        /* The search may have been stopped, the PV table is then partly overwritten so keep
         * the move from the last completed depth. */
        if (limits->stop_search_flag) break;

        move = search_state->pv_table[0][0];
        search_state->score = score;
        completed_depth = i;

        if (search_state->pv_length[0] && !limits->quiet) {
            print_info(score, i, total_nodes(search_state, helpers, helper_count), search_state->pv_table[0],
                       search_state->pv_length[0], limits);
        }

        stability = move == previous_move ? stability + 1 : 0;
        previous_move = move;

        /* Soft limit: no new depth after it. With a clock it's scaled by how settled the search is,
         * from TM_SCALE_MIN_DEPTH on, and never later than the hard limit. */
        if (limits->time_set) {
            uint64_t soft_stop = limits->soft_stop_time;
            if (limits->adaptive && i >= TM_SCALE_MIN_DEPTH) {
                /* The last root search's (an aspiration fail searches the root again). */
                const double scale = tm_scale(stability, search_state->best_move_nodes,
                                              search_state->nodes - search_state->root_search_start);
                soft_stop = limits->start_time +
                            (uint64_t) ((double) (limits->soft_stop_time - limits->start_time) * scale);
                if (soft_stop > limits->stop_time) soft_stop = limits->stop_time;
            }
            if (clock_time_ms() > soft_stop) break;
        }
    }

    stop_helpers(limits, helpers, helper_count);
    /* With helpers the threads vote on the move (vote_best_thread); one thread plays its own. */
    if (helper_count > 0) {
        const thread_result_t main_result = { move, search_state->score, completed_depth };
        move = vote_move(search_state, limits, helpers, helper_count, main_result);
    }
    if (!move) move = first_root_move(board, limits);
    syzygy_end_search();
    return move;
}

void print_bestmove(move_t mv) {

    /* No legal moves, UCI null move. */
    if (mv == 0) {
        printf("bestmove 0000\n");
        return;
    }

    char uci[6];
    printf("bestmove %s\n", move_to_uci(mv, uci));
}
