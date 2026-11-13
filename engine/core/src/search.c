#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "search.h"
#include "evaluation.h"
#include "transposition.h"
#include "see.h"
#include "syzygy.h"

cont_history_t main_cont_history;
corr_history_t main_corr_history;

/* The correction history indices of a position (corr_history_t), from the board's correction keys
 * (board_t.corr_keys, kept up to date by make_move): its pawn structure, then white's and black's
 * other pieces. Zobrist keys per piece and square, so colours and piece types all differ. */
static inline_always void corr_indices(const board_t* board, int indices[CORR_TABLES]) {
    for (int i = 0; i < CORR_TABLES; i++) indices[i] = (int) (board->corr_keys[i] >> (64 - CORR_BITS));
}

/* The correction from the position's entries (side to move): (pawn entry * CORR_PAWN_WEIGHT + the
 * other two * CORR_NONPAWN_WEIGHT) / 100, in cp, within +-CORR_MAX. */
static inline_always int correction_at(const search_state_t* search_state, int side, const int indices[CORR_TABLES]) {
    const int16_t (*t)[CORR_SIZE] = (*search_state->corr_history)[side];
    const int sum = t[0][indices[0]] * CORR_PAWN_WEIGHT + (t[1][indices[1]] + t[2][indices[2]]) * CORR_NONPAWN_WEIGHT;
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
        int16_t* entry = &(*search_state->corr_history)[side][i][indices[i]];
        int v = *entry + bonus - *entry * magnitude / CORR_ENTRY_MAX;
        if (v > CORR_ENTRY_MAX) v = CORR_ENTRY_MAX;
        if (v < -CORR_ENTRY_MAX) v = -CORR_ENTRY_MAX;
        *entry = (int16_t) v;
    }
}

void update_correction(search_state_t* search_state, const board_t* board, int depth, int error) {
    int indices[CORR_TABLES];
    corr_indices(board, indices);
    update_correction_at(search_state, board->side, indices, depth, error);
}

/* A move's key in the continuation history (cont_history_t). */
static inline_always int cont_key(move_t mv) {
    return get_piece_moved(mv) * 64 + get_target_square(mv);
}

/* The move made 'back' plies before the node at 'ply' (0 if none: before the root, or a null move). */
static inline_always move_t move_back(const search_state_t* search_state, int ply, int back) {
    return ply >= back ? search_state->path_moves[ply - back] : 0;
}

/**
 * The continuation history rows at a node: for each of the moves 1, 2 and 4 plies back, its row
 * (indexed by a quiet move's cont_key), or cont_zero_row when there's no such move. Found once per
 * node (node_cont_rows) rather than once per quiet move. The rows are pointers into the table, so
 * reads see updates made after they're found. row[0] == NULL: not found yet (score_moves).
 */
typedef struct {
    const int16_t* row[CONT_PLIES];
} cont_rows_t;

static const int16_t cont_zero_row[CONT_KEYS]; /* Never written, all 0. */

static inline_always cont_rows_t node_cont_rows(const search_state_t* search_state) {
    cont_rows_t rows;
    for (int i = 0; i < CONT_PLIES; i++) {
        const move_t previous = move_back(search_state, search_state->ply, cont_ply_back(i));
        rows.row[i] = previous ? (*search_state->cont_history)[i][cont_key(previous)] : cont_zero_row;
    }
    return rows;
}

/* quiet_history with the node's rows (node_cont_rows). */
static inline_always int quiet_history_at(const search_state_t* search_state, const cont_rows_t* rows, move_t mv) {
    const int key = cont_key(mv);
    int sum = search_state->history_moves[get_piece_moved(mv)][get_target_square(mv)];
    for (int i = 0; i < CONT_PLIES; i++) sum += rows->row[i][key];
    return sum;
}

/**
 * A quiet move's history at this node: its history plus its continuation history after the moves
 * 1, 2 and 4 plies back (those there are), within +-(CONT_PLIES + 1) * HISTORY_MAX. A sum, not a
 * mean: with no continuation entries yet it's the history alone, so LMR (lmr_reduction) and history
 * pruning see the same values they did before continuation history.
 */
int quiet_history(const search_state_t* search_state, move_t mv) {
    const cont_rows_t rows = node_cont_rows(search_state);
    return quiet_history_at(search_state, &rows, mv);
}

/**
 * Move ordering score, highest first. Each category has its own band (search_constants.h): the TT
 * move, the PV move (while following the previous iteration's PV), winning and equal captures by
 * MVV-LVA plus their capture history, queen promotions, the two killers, quiet moves (and
 * underpromotions) by quiet_history (scaled into the history band), then losing captures (also with
 * their capture history).
 */
static inline_always int score_move_with(move_t move, move_t tt_move, board_t* board, search_state_t* search_state,
                                         cont_rows_t* rows) {
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
            return CAPTURE_SCORE + search_const.mvv_lva[piece][board->side == white ? p : P] +
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
                    return BAD_CAPTURE_SCORE + search_const.mvv_lva[piece][pieces[i]] + history;
                }
                return CAPTURE_SCORE + search_const.mvv_lva[piece][pieces[i]] + history;
            }
        }
        return 0;
    } else {
        /* A queen promotion is tactical, it goes with the winning captures rather than the quiets. */
        const piece promoted = get_promoted_piece(move);
        if (promoted == Q || promoted == q) return QUEEN_PROMOTION_SCORE;
        if (search_state->killer_moves[0][search_state->ply] == move) return KILLER_1_SCORE;
        else if (search_state->killer_moves[1][search_state->ply] == move) return KILLER_2_SCORE;
        /* The rows are found at the node's first quiet move (none in most qsearch nodes). */
        if (!rows->row[0]) *rows = node_cont_rows(search_state);
        return quiet_history_at(search_state, rows, move) / QUIET_ORDER_DIVISOR;
    }
}

int score_move(move_t move, move_t tt_move, board_t* board, search_state_t* search_state) {
    cont_rows_t rows = { .row = { NULL } };
    return score_move_with(move, tt_move, board, search_state, &rows);
}


/**
 * 'Gravity' update of a history score by a signed bonus: h += bonus - h * |bonus| / HISTORY_MAX.
 * The change shrinks as h approaches +-HISTORY_MAX and h never passes it, so history stays
 * within the quiet move range however deep or long the search runs.
 */
static inline_always int history_gravity(int h, int bonus) {
    h += bonus - h * (bonus < 0 ? -bonus : bonus) / HISTORY_MAX;
    return h > HISTORY_MAX ? HISTORY_MAX : (h < -HISTORY_MAX ? -HISTORY_MAX : h);
}

/* Adds a signed bonus to the move's history, and to its continuation history after the moves 1, 2
 * and 4 plies back (those there are), at the node's ply. */
static inline_always void add_history_bonus(search_state_t* search_state, move_t mv, int bonus) {
    int32_t* h = &search_state->history_moves[get_piece_moved(mv)][get_target_square(mv)];
    *h = history_gravity(*h, bonus);
    const int key = cont_key(mv);
    for (int i = 0; i < CONT_PLIES; i++) {
        const move_t previous = move_back(search_state, search_state->ply, cont_ply_back(i));
        if (!previous) continue;
        int16_t* c = &(*search_state->cont_history)[i][cont_key(previous)][key];
        *c = (int16_t) history_gravity(*c, bonus);
    }
}

/* The type (0 pawn .. 5 king) of the piece a capture takes, on the board it's made from. */
static inline_always int captured_type(const board_t* board, move_t mv) {
    if (get_enpassant_flag(mv)) return 0;
    const piece victim = piece_on(board, get_target_square(mv), board->side ^ 1);
    return victim == none ? 0 : victim % 6;
}

/* As add_history_bonus, for the capture's capture history, within +-CAPTURE_HISTORY_MAX. */
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

static inline_always int history_bonus(int depth) {
    const int bonus = HIST_BONUS_SCALE * depth * depth;
    return bonus < HIST_BONUS_MAX ? bonus : HIST_BONUS_MAX;
}

/* Quiet move raised alpha, bonus min(HIST_BONUS_SCALE * depth^2, HIST_BONUS_MAX) (history_bonus). */
void update_history(search_state_t* search_state, move_t mv, int depth) {
    add_history_bonus(search_state, mv, history_bonus(depth));
}

/**
 * History malus: a quiet move searched before the quiet move that caused a cutoff didn't cut, so
 * its history is lowered by the same amount (history_bonus). Without it history only rises, and a move that's often tried
 * but rarely good keeps a high score.
 */
void penalise_history(search_state_t* search_state, move_t mv, int depth) {
    add_history_bonus(search_state, mv, -history_bonus(depth));
}

/* A capture caused a beta cutoff ('board' is the position it was made from): the quiet history's
 * bonus (history_bonus). */
void update_capture_history(search_state_t* search_state, const board_t* board, move_t mv, int depth) {
    add_capture_history_bonus(search_state, board, mv, history_bonus(depth));
}

/* A capture searched before the move that cut off didn't: the quiet history's malus. */
void penalise_capture_history(search_state_t* search_state, const board_t* board, move_t mv, int depth) {
    add_capture_history_bonus(search_state, board, mv, -history_bonus(depth));
}

/* A non-capture caused a cutoff: it becomes the first killer at this ply, the first moves down to
 * second. */
void store_killer(search_state_t* search_state, move_t mv) {
    search_state->killer_moves[1][search_state->ply] = search_state->killer_moves[0][search_state->ply];
    search_state->killer_moves[0][search_state->ply] = mv;
}

/**
 * Scores every move at the node. The PV flag (set by negamax for this node) is cleared
 * afterwards: when the PV move is also the TT move it's never scored as the PV move, and the flag
 * mustn't carry over to whichever later node has a move matching the PV at its ply.
 */
void score_moves(const move_buffer_t* buffer, int* scores, move_t tt_move, board_t* board, search_state_t* search_state) {
    cont_rows_t rows = { .row = { NULL } };
    for (int i = 0; i < buffer->index; i++) {
        scores[i] = score_move_with(buffer->moves[i], tt_move, board, search_state, &rows);
    }
    search_state->score_pv = false;
}

/* Removes the quiet moves (not captures, not promotions) from 'from' on, keeping the order of the
 * others (late move pruning, which skips them all once it starts). */
static inline_always void drop_quiet_moves(move_buffer_t* buffer, int* scores, int from) {
    int kept = from;
    for (int j = from; j < buffer->index; j++) {
        const move_t mv = buffer->moves[j];
        if (!get_capture_flag(mv) && !get_promoted_piece(mv)) continue;
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
#else
#define COUNT_STAT(search_state, field) ((void) 0)
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
 * if so, where the full depth search's score ('score') fell against the window. */
static inline_always void stat_lmr(search_state_t* search_state, int depth, bool is_pv_node, int place, int reduction,
                                   bool beat_alpha, int score, int alpha, int beta) {
#ifdef TIKI_SEARCH_STATS
    const int pl = place <= 5 ? 0 : place <= 8 ? 1 : place <= 16 ? 2 : 3;
    const int r = reduction >= 4 ? 3 : reduction - 1;
    uint64_t* cell = search_state->stats.lmr[stat_depth_band(depth)][is_pv_node][pl][r];
    cell[0]++;
    if (beat_alpha) {
        cell[1]++;
        cell[score <= alpha ? 2 : score < beta ? 3 : 4]++;
    }
#else
    (void) search_state; (void) depth; (void) is_pv_node; (void) place; (void) reduction; (void) beat_alpha;
    (void) score; (void) alpha; (void) beta;
#endif
}

/* The previous iteration's PV move at this ply if it's one of the node's moves, otherwise 0. */
static inline_always move_t find_pv_move(const move_buffer_t* buffer, const search_state_t* search_state) {
    const move_t pv_move = search_state->pv_table[0][search_state->ply];
    for (int i = 0; i < buffer->index; i++) {
        if (buffer->moves[i] == pv_move) return pv_move;
    }
    return 0;
}

/* Stops the search when out of time, or past the node limit ('go nodes', so up to 2047 over).
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
 * compare with, it counts as improving (the cautious side: less pruning).
 */
bool is_improving(int static_eval, int eval_2_plies_up, int eval_4_plies_up) {
    const int previous = eval_2_plies_up != -INF ? eval_2_plies_up : eval_4_plies_up;
    return previous == -INF || static_eval > previous;
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
 * The null move search's depth: depth - R, R = NMP_R_BASE + depth / NMP_R_DEPTH_DIV + min((eval -
 * beta) / NMP_R_EVAL_DIV, NMP_R_EVAL_MAX), at least 0 (qsearch). Larger the deeper the node and the
 * further its eval is above beta (the more likely the cutoff).
 */
int null_move_depth(int depth, int static_eval, int beta) {
    int eval_term = (static_eval - beta) / NMP_R_EVAL_DIV;
    if (eval_term > NMP_R_EVAL_MAX) eval_term = NMP_R_EVAL_MAX;
    const int reduced = depth - (NMP_R_BASE + depth / NMP_R_DEPTH_DIV + eval_term);
    return reduced > 0 ? reduced : 0;
}

/**
 * Reverse futility (static null move) pruning: at low depth, a static eval this far above beta
 * is very unlikely to drop below it, so the node fails high without a search. The margin is
 * RFP_IMPROVING smaller when the position is improving, a rising eval is likelier to stay above
 * beta. Not in check, PV nodes, at the root or near mate scores.
 */
bool can_reverse_futility_prune(int depth, int static_eval, int beta, bool in_check, bool is_pv_node, int ply,
                                bool improving) {
    return ply > 0 && !in_check && !is_pv_node && depth <= RFP_MAX_DEPTH && !is_mate_score(beta) &&
           static_eval - (RFP_MARGIN * depth - (improving ? RFP_IMPROVING : 0)) >= beta;
}

/**
 * Futility pruning: at low depth, a static eval this far below alpha (base + margin * depth) means
 * a quiet move is very unlikely to raise it, so quiet moves (after the first) are skipped.
 * Captures, promotions and checks are still searched.
 */
bool is_futile(int depth, int static_eval, int alpha, bool in_check, bool is_pv_node) {
    return !in_check && !is_pv_node && depth <= FUTILITY_MAX_DEPTH && !is_mate_score(alpha) &&
           static_eval + FUTILITY_BASE + FUTILITY_MARGIN * depth <= alpha;
}

/**
 * Late move pruning: at low depth, once base + depth^2 moves have been searched (LMP_NOT_IMPROVING_PCT
 * of them, rounded up, when the position isn't improving), the remaining quiet moves (sorted last,
 * after the killers and the best history) are very unlikely to raise alpha, so they're skipped.
 * Captures and promotions are still searched.
 */
bool can_late_move_prune(int depth, int moves_searched, int alpha, bool in_check, bool is_pv_node, bool improving) {
    if (in_check || is_pv_node || depth > LMP_MAX_DEPTH || is_mate_score(alpha)) return false;
    int limit = LMP_BASE + depth * depth;
    if (!improving) limit = (limit * LMP_NOT_IMPROVING_PCT + 99) / 100;
    return moves_searched >= limit;
}

/**
 * SEE pruning: at low depth, a move that loses this much material on its target square (static
 * exchange evaluation) is very unlikely to raise alpha, so it's skipped. Quiet moves lose
 * -SEE_QUIET_MARGIN * depth, captures -SEE_CAPTURE_MARGIN * depth^2 (a capture's threat often
 * pays for some material). Never the first move, so the node always has a score.
 */
bool can_see_prune(int depth, int moves_searched, int alpha, bool in_check, bool is_pv_node) {
    return !in_check && !is_pv_node && depth <= SEE_PRUNE_MAX_DEPTH && !is_mate_score(alpha) &&
           moves_searched > 0;
}

int see_prune_threshold(int depth, bool is_quiet) {
    return is_quiet ? -SEE_QUIET_MARGIN * depth : -SEE_CAPTURE_MARGIN * depth * depth;
}

/**
 * Whether a move is reduced: quiet moves (not the killers) and losing captures (SEE < 0) late in
 * the list at enough depth. Checks and moves made in check are searched at full depth.
 */
bool can_reduce(int depth, int moves_searched, bool is_quiet, bool gives_check, bool in_check, bool is_killer,
                bool is_bad_capture) {
    return moves_searched >= LMR_MIN_MOVES && depth >= LMR_MIN_DEPTH && !gives_check && !in_check &&
           ((is_quiet && !is_killer) || is_bad_capture);
}

/**
 * History pruning: at low depth, a quiet move whose history (quiet_history: the history plus the
 * continuation history) is below -HIST_PRUNE_MARGIN * depth has rarely been good here, so it's
 * skipped. Not the first move, the killers, in check, PV nodes or near mate scores; checks are
 * excluded by the caller (it's decided after the move is made).
 * Measured over game sequences (depth 10, history kept between moves), quiet moves searched at
 * depth <= 4 in non-PV nodes: history is mostly positive there (median +3200 at depth 1, -100 at
 * depth 4), so -1000 * depth pruned only 0.5% of them. Below -300 * depth: 5.5% of them, which
 * cut 1.1% of the time against 5.9% for all.
 */
bool can_history_prune(int depth, int history, int moves_searched, int alpha, bool in_check, bool is_pv_node,
                       bool is_killer) {
    return !in_check && !is_pv_node && !is_killer && moves_searched > 0 && depth <= HIST_PRUNE_MAX_DEPTH &&
           !is_mate_score(alpha) && history < -HIST_PRUNE_MARGIN * depth;
}

/**
 * Late move reduction for a quiet move, from the log table, one ply less in PV nodes, and adjusted
 * by the move's history (quiet_history, the history plus the continuation history):
 * history / LMR_HISTORY_DIVISOR plies less for a move that has often been good, more for one that
 * hasn't. The history alone averages about 900 in magnitude (bench), so a move well above or below
 * average moves a ply. Clamped so the reduced search still has at least one ply (depth - 1 - r >= 1).
 * With the history alone, against the same search without it (both with persistent history): SPRT
 * +15.0 +- 7.4 after 3388 games, H1 accepted [0, 5].
 * LMR_CUT_NODE plies more at an expected cut node (cut_node, see negamax_node): a late move there
 * is expected to fail low, the cutoff should come from an earlier move. LMR_NOT_IMPROVING plies
 * more when the position isn't improving (is_improving), a late move is then less likely to help.
 */
int lmr_reduction(int depth, int moves_searched, bool is_pv_node, int history, bool cut_node, bool improving) {
    int r = lmr_table[depth < LMR_TABLE_SIZE ? depth : LMR_TABLE_SIZE - 1]
                     [moves_searched < LMR_TABLE_SIZE ? moves_searched : LMR_TABLE_SIZE - 1];
    if (is_pv_node) r--;
    if (cut_node) r += LMR_CUT_NODE;
    if (!improving) r += LMR_NOT_IMPROVING;
    r -= history / LMR_HISTORY_DIVISOR;
    if (r > depth - 2) r = depth - 2;
    return r > 0 ? r : 0;
}

/**
 * Internal iterative reduction: with no TT move the move ordering is poor, so search the node one
 * ply shallower (it's likely to be re-searched deeper later with a TT move from this search).
 */
int iir_reduction(int depth, move_t tt_move) {
    return (depth >= IIR_MIN_DEPTH && tt_move == 0) ? 1 : 0;
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
    return tt_score - SE_MARGIN * depth;
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

/* The qsearch node itself, already counted: by quiescence, or by negamax at depth 0. */
static int quiescence_node(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    if (search_state->ply == 0) nnue_reset(&search_state->nnue);
    if (search_state->ply > MAX_PLY-1) return evaluation(board, &search_state->nnue);

    /* The TT, saved at depth 0 by qsearch (and depth 0 negamax nodes, which are qsearch). Outside
     * the PV any bound that fits the window cuts off, as in negamax. */
    const bool is_pv_node = beta - alpha > 1;
    tt_info_t tt;
    const bool tt_hit = tt_read(board->hash, search_state->ply, &tt);
    if (!is_pv_node && tt_hit) {
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
        eval = adjusted_eval(raw_eval + eval_correction(board, search_state), tt_hit ? &tt : NULL);
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
    score_moves(&buffer, scores, tt_hit ? tt.move : 0, board, search_state);

    /* Out of check, captures and queen promotions only. */
    bool has_legal_moves = false;
    move_t best_move = 0;
    for (int i=0; i< buffer.index; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);
        tt_prefetch(hash_after_move(board, mv));

        if (!in_check) {
            /* Sorted best first, so from here on every capture loses material (SEE < 0). Quiet queen
             * promotions (QUEEN_PROMOTION_SCORE) sort with the winning captures, before this. */
            if (scores[i] < 0 && get_capture_flag(mv)) break;

            /* Delta pruning, even winning the piece can't bring the score up to alpha. Its optimistic
             * score still counts towards the best, so a fail low stays a valid upper bound. */
            if (!get_promoted_piece(mv)) {
                const int optimistic = eval + captured_value(board, mv) + DELTA_MARGIN;
                if (optimistic <= alpha) {
                    if (optimistic > best_score) best_score = optimistic;
                    continue;
                }
            }
        }

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
    if ((++search_state->nodes & 2047) == 0) node_batch(search_state, limits);
    return quiescence_node(alpha, beta, board, search_state, limits);
}


/**
 * The move picker: negamax's moves, best first, generated in stages so that a node that cuts early
 * doesn't generate or score the rest. The order is score_move's, by band:
 *   1. the TT move, if it's possible here (move_is_pseudo_legal: a TT entry can be another position's);
 *   2. winning and equal captures and queen promotions (generate_noisy), by their scores;
 *   3. the two killers, if they're possible here and quiet (not a capture or queen promotion);
 *   4. the other quiet moves (generate_quiets), by their history, generated only now;
 *   5. the losing captures (SEE < 0), left over from 2.
 * The bands don't overlap (search_constants.h), so it's the order of one sorted list, but for ties
 * within a band. Each move is returned once: the TT move and killers are skipped where they come up
 * again. In check and on the previous iteration's PV (or built with TIKI_SEARCH_STATS) everything is
 * generated and scored at once instead (picker_all), as before: evasions are few, the PV move is
 * ordered after the TT move and the statistics look at the whole list.
 * picker_skip_quiets: late move pruning has started, the quiet moves left aren't returned (nor
 * generated), only captures and promotions.
 */
typedef enum {
    pick_tt, pick_gen_noisy, pick_good_noisy, pick_killer_1, pick_killer_2, pick_gen_quiets, pick_quiets,
    pick_bad_noisy, picker_all, pick_done
} pick_stage_t;

typedef struct {
    move_buffer_t* buffer;
    int* scores;
    board_t* board;
    search_state_t* search_state;
    cont_rows_t rows;        /* For the quiet moves' scores (score_move_with), found when first needed. */
    move_t tt_move;          /* Returned by pick_tt (0 if it wasn't), skipped after.                    */
    move_t killers[2];       /* Returned by the killer stages (0 if they weren't), skipped after.        */
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
                                      search_state_t* search_state, move_t tt_move, bool all_at_once, bool follow_pv) {
    picker->buffer = buffer;
    picker->scores = scores;
    picker->board = board;
    picker->search_state = search_state;
    picker->rows.row[0] = NULL;
    picker->tt_move = tt_move;
    picker->killers[0] = search_state->killer_moves[0][search_state->ply];
    picker->killers[1] = search_state->killer_moves[1][search_state->ply];
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
        score_moves(buffer, scores, tt_move, board, search_state);
        picker->stage = picker_all;
    } else {
        search_state->score_pv = false;
        picker->stage = pick_tt;
    }
}

/* A killer for stage 3: possible here, quiet in score_move's sense (not a capture or a queen
 * promotion, those are scored in their own bands), not the TT move, not already returned. */
static inline_always bool picker_killer_ok(move_picker_t* picker, move_t killer) {
    if (!killer || killer == picker->tt_move || get_capture_flag(killer)) return false;
    const piece promoted = get_promoted_piece(killer);
    if (promoted == Q || promoted == q) return false;
    if (picker->skip_quiets && !promoted) return false;
    return move_is_pseudo_legal(picker->board, killer);
}

/* Late move pruning has started: the quiet moves left won't be searched (drop_quiet_moves). */
static inline_always void picker_skip_quiets(move_picker_t* picker) {
    if (picker->skip_quiets) return;
    picker->skip_quiets = true;
    if (picker->stage == picker_all) {
        drop_quiet_moves(picker->buffer, picker->scores, picker->noisy_next);
    } else if (picker->stage == pick_quiets) {
        drop_quiet_moves(picker->buffer, picker->scores, picker->quiet_next);
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
                scores[i] = score_move_with(buffer->moves[i], 0, picker->board, picker->search_state, &picker->rows);
            }
            picker->noisy_next = 0;
            picker->stage = pick_good_noisy;
            break;

        case pick_good_noisy:
            /* Down to the killers' band: what's left are the losing captures, for pick_bad_noisy. */
            while (picker->noisy_next < picker->noisy_end) {
                pick_best(buffer, scores, picker->noisy_next, picker->noisy_end);
                if (scores[picker->noisy_next] <= KILLER_1_SCORE) break;
                const move_t mv = buffer->moves[picker->noisy_next];
                picker->score = scores[picker->noisy_next++];
                if (mv == picker->tt_move) continue;
                return mv;
            }
            picker->stage = pick_killer_1;
            break;

        case pick_killer_1:
            picker->stage = pick_killer_2;
            if (picker_killer_ok(picker, picker->killers[0])) {
                picker->score = KILLER_1_SCORE;
                return picker->killers[0];
            }
            picker->killers[0] = 0;
            break;

        case pick_killer_2:
            /* The same move twice in the killers is the first one's (score_move checks it first): if the
             * first was returned it's still in killers[0]; if it wasn't, the same move fails the same tests. */
            picker->stage = pick_gen_quiets;
            if (picker->killers[1] != picker->killers[0] && picker_killer_ok(picker, picker->killers[1])) {
                picker->score = KILLER_2_SCORE;
                return picker->killers[1];
            }
            picker->killers[1] = 0;
            break;

        case pick_gen_quiets:
            /* Appended after the noisy moves (the losing captures still among them). Once late move
             * pruning has started only promotions would be returned, so without a pawn on the 7th
             * (2nd for black) there's nothing to generate. */
            picker->stage = pick_quiets;
            picker->quiet_next = picker->noisy_end;
            buffer->index = picker->noisy_end;
            if (picker->skip_quiets) {
                const bitboard promoting = picker->board->side == white ?
                    picker->board->pieces[P] & bitboard_const.rank_masks[6] :
                    picker->board->pieces[p] & bitboard_const.rank_masks[1];
                if (!promoting) break;
            }
            generate_quiets(picker->board, buffer);
            for (int i = picker->noisy_end; i < buffer->index; i++) {
                scores[i] = score_move_with(buffer->moves[i], 0, picker->board, picker->search_state, &picker->rows);
            }
            if (picker->skip_quiets) drop_quiet_moves(buffer, scores, picker->quiet_next);
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
 */
static inline_always void learn_correction(search_state_t* search_state, int side, const int indices[CORR_TABLES],
                                           int depth, int score, int corrected_eval, tt_entry_type bound,
                                           move_t best_move) {
    if (is_mate_score(score)) return;
    if (best_move && (get_capture_flag(best_move) || get_promoted_piece(best_move))) return;
    if (bound == tt_beta && score <= corrected_eval) return;
    if (bound == tt_alpha && score >= corrected_eval) return;
    update_correction_at(search_state, side, indices, depth, score - corrected_eval);
}

/**
 * The search of a node (negamax is the root's). cut_node: the node is expected to fail high (a
 * cut node), set by the parent as Stockfish does, see where it searches its moves. Never in a PV
 * node: a PV node's children with the full window are PV nodes, the null-window ones alternate,
 * a reduced late move's child is a cut node (it should fail high, so the move fails low here),
 * and the null move's child and its verification aren't. Used by LMR (lmr_reduction).
 */
static int negamax_node(int alpha,
                        int beta,
                        int depth,
                        bool cut_node,
                        board_t* board,
                        search_state_t* search_state,
                        limits_t* limits) {

    /* Every node, including those that return early (TT, draw, pruning). Checked straight after
     * counting, so the check runs once per 2048 counted nodes. */
    if ((++search_state->nodes & 2047) == 0) node_batch(search_state, limits);

    search_state->pv_length[search_state->ply] = search_state->ply;

    /* The root, the accumulators start again from this position. */
    if (search_state->ply == 0) {
        nnue_reset(&search_state->nnue);
        search_state->best_move_nodes = 0;
    }

    /* Whether this node is on the previous iteration's PV, set by the parent for this node only. */
    const bool follow_pv = search_state->follow_pv;

    /* Not at the root, so a move is always returned. */
    if (search_state->ply > 0 && is_draw(board, search_state))
        return 0;

    /* The deepest ply the tables hold. No moves are made from here, so no node is ever at MAX_PLY
     * (pv_length and pv_table are read at ply + 1). */
    if (search_state->ply >= MAX_PLY - 1) return evaluation(board, &search_state->nnue);

    /* Extend checks before the depth 0 test, so a check at the horizon is searched rather than
     * going straight to qsearch. Only below ply 2 * root depth, as the singular extensions: a long
     * series of checks otherwise extends the line ply after ply (in endgames 44% of the nodes were
     * past the nominal depth). Beyond it a check at the horizon goes to qsearch, which searches
     * every evasion in check (it doesn't stand pat), so a mate there is still found. */
    bool in_check = side_in_check(board);

    if (in_check && search_state->ply < 2 * search_state->root_depth) ++depth;

    /* A singular extension search of this node without the TT move (see below). Its result isn't
     * the node's: it doesn't cut off from the TT, save to it, or prune the whole node. */
    const move_t excluded_move = search_state->excluded_moves[search_state->ply];

    /* Probed with the extended depth, the same depth the node is saved with. */
    bool is_pv_node = beta - alpha > 1;
    const int alpha_orig = alpha;
    tt_info_t tt;
    const bool tt_hit = tt_read(board->hash, search_state->ply, &tt);
    move_t tt_move = tt_hit ? tt.move : 0;
    if (search_state->ply && !is_pv_node && !excluded_move && tt_hit) {
        const int cutoff = tt_cutoff(&tt, depth, alpha, beta);
        if (cutoff != TT_NOT_FOUND) return cutoff;
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
            return tb_score;
        }
    }

    depth -= iir_reduction(depth, tt_move);

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
        corrected_eval = raw_eval + correction_at(search_state, board->side, corr_idx);
    }
    const int static_eval = in_check ? -INF : adjusted_eval(corrected_eval, tt_hit ? &tt : NULL);

    /* Improving: the network's eval against this side's previous node (is_improving). Every node
     * two or four plies up is an ancestor, so its entry was written before this node was reached. */
    const int ply = search_state->ply;
    search_state->static_evals[ply] = in_check ? -INF : raw_eval;
    const bool improving = !in_check && is_improving(raw_eval, ply >= 2 ? search_state->static_evals[ply - 2] : -INF,
                                                     ply >= 4 ? search_state->static_evals[ply - 4] : -INF);

    /* Fail-soft: the static eval that's this far above beta (can_reverse_futility_prune: never a mate score). */
    if (!excluded_move &&
        can_reverse_futility_prune(depth, static_eval, beta, in_check, is_pv_node, search_state->ply, improving))
        return static_eval;

    /* Null move pruning, searched at null_move_depth. */
    if (!excluded_move && can_null_move(board, search_state, depth, beta, static_eval, in_check, is_pv_node)) {
        const int null_depth = null_move_depth(depth, static_eval, beta);
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
            if (depth < NMP_VERIFY_DEPTH || search_state->nmp_min_ply != 0) return null_score;
            /* Deep, the cutoff is verified by searching this node normally at the null search's depth,
             * with null moves off for the first three quarters of it, so a zugzwang (where passing
             * would be best) can't cut off a large subtree. */
            search_state->nmp_min_ply = (uint8_t) (ply + 3 * null_depth / 4);
            const int verified = negamax_node(beta - 1, beta, null_depth, false, board, search_state, limits);
            search_state->nmp_min_ply = 0;
            if (limits->stop_search_flag) return STOPPED_SEARCH;
            if (verified >= beta) return is_mate_score(verified) ? beta : verified;
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
    if (!excluded_move && tt_move) {
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
            /* Fail-soft: another move reached s_beta (>= beta), a lower bound on the node (as Stockfish). */
            if (result == singular_multi_cut) return s_beta;
            if (result == singular_extend) extension = 1;
            else if (result == singular_reduce) extension = -SE_REDUCTION;
        }
    }

    /* The moves, best first (move_picker_t): staged, or all at once in check, on the previous
     * iteration's PV and in statistics builds. On the PV, its move is ordered first (after the TT
     * move) and its child is on the PV too. */
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
#ifdef TIKI_SEARCH_STATS
    const bool all_at_once = true;
#else
    const bool all_at_once = in_check || follow_pv;
#endif
    move_picker_t picker;
    picker_init(&picker, &buffer, scores, board, search_state, tt_move, all_at_once, follow_pv);
    const move_t pv_move = picker.pv_move;
#ifdef TIKI_SEARCH_STATS
    stat_capture_ranks_t capture_ranks = { .count = 0 };
    if (!excluded_move) stat_capture_ranks(&capture_ranks, &buffer, scores, tt_move, board, search_state, depth, is_pv_node);
#endif

    const bool futile = is_futile(depth, static_eval, alpha, in_check, is_pv_node);

    bool has_legal_moves = false;
    int num_moves_searched = 0;
    int score;
    /* Fail-soft: the best score of the moves searched, returned whatever the window (alpha only moves
     * when a move scores above it). */
    int best_score = -INF;
    move_t best_move = 0;
    if (tt_move && !excluded_move) COUNT_STAT(search_state, tt_move_nodes);
    /* Quiet moves searched without a cutoff, for the history malus when a later quiet move cuts, and
     * captures, for the capture history malus when any later move cuts. */
    move_t quiets_searched[MAX_QUIETS_PENALISED];
    int num_quiets_searched = 0;
    move_t captures_searched[MAX_CAPTURES_PENALISED];
    int num_captures_searched = 0;
    int quiets_tried = 0; /* Quiet moves searched (not pruned), for the statistics. */
    /* This node's continuation history rows: the moves before it don't change while its moves are searched. */
    const cont_rows_t cont_rows = node_cont_rows(search_state);
    move_t mv;
    while ((mv = picker_next(&picker)) != 0) {
        if (mv == excluded_move) continue;
        /* The child's TT entry starts loading now, before the move is pruned or made (hash_after_move). */
        tt_prefetch(hash_after_move(board, mv));
        /* At a root in the tablebases, only the moves that keep its best result (syzygy_rank_root). */
        if (search_state->ply == 0 && !syzygy_root_allows(mv)) continue;
        bool is_capture = get_capture_flag(mv);
        const bool is_quiet = !is_capture && !get_promoted_piece(mv);

        /* Late move pruning, before making the move: a legal move has been searched already, so
         * whether this one is legal doesn't matter. Once it prunes one quiet move it prunes every
         * later one (it's never in PV nodes, where alpha would move; elsewhere alpha only moves on a
         * cutoff, which returns, and the moves searched only grow), so the quiet moves left are
         * skipped by the picker from here on (not even generated, if it hasn't got to them yet). */
        if (is_quiet && can_late_move_prune(depth, num_moves_searched, alpha, in_check, is_pv_node, improving)) {
            picker_skip_quiets(&picker);
            continue;
        }
        /* A losing capture (SEE < 0, ordered below the quiet moves), not the TT move: reduced like a quiet move. */
        const bool is_bad_capture = is_capture && mv != tt_move && picker.score < 0;
        /* Killers are for this ply, checked before ++ply. A quiet move's history is read at this node
         * (history pruning and LMR). */
        const bool is_killer = mv == search_state->killer_moves[0][search_state->ply] ||
                               mv == search_state->killer_moves[1][search_state->ply];
        const int history = is_quiet ? quiet_history_at(search_state, &cont_rows, mv) : 0;
        const bool history_prunable = is_quiet &&
            can_history_prune(depth, history, num_moves_searched, alpha, in_check, is_pv_node, is_killer);

        /* SEE pruning, also before making the move. Promotions are always searched. */
        if (!get_promoted_piece(mv) && can_see_prune(depth, num_moves_searched, alpha, in_check, is_pv_node) &&
            !see_ge(board, mv, see_prune_threshold(depth, is_quiet))) continue;

        /* Futility and history pruning apply to quiet moves that don't give check (below). For a move
         * of a piece other than the king that's decided here, before the move is made: whether it
         * gives check (move_gives_check) and, since only a legal move is pruned (an illegal one is
         * just skipped, as when it's made), whether it's legal (quiet_move_is_legal: the node isn't in
         * check when either pruning applies). Most quiet moves at these nodes are pruned, and were
         * made and unmade only to find that out. King moves are still made first. */
        const bool prune_candidate = (futile && num_moves_searched > 0 && is_quiet) || history_prunable;
        const bool check_known = prune_candidate && get_piece_moved(mv) != K && get_piece_moved(mv) != k;
        const bool known_check = check_known && move_gives_check(board, mv);
        if (check_known && !known_check && quiet_move_is_legal(board, mv)) {
            if (futile) {
                const int futility_value = static_eval + FUTILITY_BASE + FUTILITY_MARGIN * depth;
                if (futility_value > best_score) best_score = futility_value;
            }
            continue;
        }

        if (!search_make_move(board, search_state, mv)) {
            pop_move(board);
            continue;
        }
        has_legal_moves = true;

        /* Only needed when futility, history pruning or LMR might apply, it costs an attack test. */
        const bool may_prune = (is_quiet || is_bad_capture) && num_moves_searched > 0 &&
                               (futile || history_prunable ||
                                (num_moves_searched >= LMR_MIN_MOVES && depth >= LMR_MIN_DEPTH && !in_check));
        const bool gives_check = check_known ? known_check : may_prune && side_in_check(board);
        assert(!check_known || known_check == side_in_check(board));

        /* Futility and history pruning, quiet moves that don't give check. The first move is always
         * searched. */
        if (((futile && num_moves_searched > 0 && is_quiet) || history_prunable) && !gives_check) {
            /* Fail-soft: a futility-pruned move is assumed to score at most the futility value (at or
             * below alpha), so it counts towards the best score, as Stockfish does; otherwise a fail
             * low would return, and save as an upper bound, the lower score of the moves searched. */
            if (futile && is_quiet) {
                const int futility_value = static_eval + FUTILITY_BASE + FUTILITY_MARGIN * depth;
                if (futility_value > best_score) best_score = futility_value;
            }
            search_pop_move(board, search_state);
            continue;
        }

        if (is_quiet) quiets_tried++;
#ifdef TIKI_SEARCH_STATS
        /* The capture history read from the position the capture is made from: undone for the read. */
        int stat_bucket = -1;
        if (is_capture && mv != tt_move && !excluded_move) {
            pop_move(board);
            stat_bucket = stat_history_bucket(stat_capture_history(search_state, board, mv));
            make_move(board, mv);
            search_state->stats.cap_searched_by_history[stat_bucket]++;
        }
#endif

        /* At the root, the nodes this move takes, for time management (tm_scale). */
        const uint64_t nodes_before_move = search_state->nodes;

        /* Late move reduction: quiet moves adjusted by their history, losing captures by the table alone.
         * The killers by at most LMR_KILLER_MAX (by default 0, never reduced). */
        int reduction =
            can_reduce(depth, num_moves_searched, is_quiet, gives_check, in_check, is_killer, is_bad_capture) ?
            lmr_reduction(depth, num_moves_searched, is_pv_node, history, cut_node, improving) : 0;
        if (LMR_KILLER_MAX > 0 && is_killer &&
            can_reduce(depth, num_moves_searched, is_quiet, gives_check, in_check, false, is_bad_capture)) {
            const int r = lmr_reduction(depth, num_moves_searched, is_pv_node, history, cut_node, improving);
            reduction = r < LMR_KILLER_MAX ? r : LMR_KILLER_MAX;
        }
        /* The TT move a ply deeper if it's singular, shallower if it isn't but beat beta (see above). */
        const int new_depth = depth - 1 + (mv == tt_move ? extension : 0);

        /* Set before every search of the child, each one changes it further down the tree. */
        const bool child_on_pv = pv_move != 0 && mv == pv_move;
        search_state->path_moves[search_state->ply] = mv;
        ++search_state->ply;
        ++search_state->repetition_index;
        record_position(search_state, search_state->repetition_index, board->hash, board->fifty_move);

        search_state->follow_pv = child_on_pv;
        /* The child's node type (negamax_node): the first move's child is a PV node in a PV node,
         * otherwise the opposite of this node, as are the null-window searches at full depth. A
         * reduced search's child is a cut node. Full-window searches are PV nodes. */
        if (num_moves_searched == 0 ) {
            if (mv == tt_move && !excluded_move) COUNT_STAT(search_state, tt_move_first);
            score = -negamax_node(-beta, -alpha, new_depth, !is_pv_node && !cut_node, board, search_state, limits);
        } else {
            /* Principal variation search, null window (reduced for late moves). A reduced move that
             * beats alpha is searched again at full depth with the null window, then with the full
             * window if it's still inside it. */
            score = -negamax_node(-alpha - 1, -alpha, new_depth - reduction, reduction > 0 || !cut_node, board,
                                  search_state, limits);
            const bool reduced_beat_alpha = reduction > 0 && score > alpha;
            if (reduction > 0) COUNT_STAT(search_state, lmr_searches);
            if (score > alpha && reduction > 0) {
                COUNT_STAT(search_state, lmr_fail_highs);
                search_state->follow_pv = child_on_pv;
                score = -negamax_node(-alpha - 1, -alpha, new_depth, !cut_node, board, search_state, limits);
                if (score > alpha) COUNT_STAT(search_state, lmr_research_holds);
            }
            if (score > alpha && score < beta) {
                search_state->follow_pv = child_on_pv;
                score = -negamax_node(-beta, -alpha, new_depth, false, board, search_state, limits);
            }
            if (reduction > 0 && !excluded_move && !limits->stop_search_flag) {
                stat_lmr(search_state, depth, is_pv_node, num_moves_searched + 1, reduction, reduced_beat_alpha, score,
                         alpha, beta);
            }
        }

        ++num_moves_searched;
        --search_state->ply;
        --search_state->repetition_index;
        search_pop_move(board, search_state);

        if (limits->stop_search_flag) return STOPPED_SEARCH;

        if (score > best_score) best_score = score;
        if (score > alpha) {
            best_move = mv;
            if (search_state->ply == 0) search_state->best_move_nodes = search_state->nodes - nodes_before_move;

            search_state->pv_table[search_state->ply][search_state->ply] = mv;
            for (int j = search_state->ply + 1; j < search_state->pv_length[search_state->ply + 1]; j++) {
                search_state->pv_table[search_state->ply][j] = search_state->pv_table[search_state->ply + 1][j];
            }
            search_state->pv_length[search_state->ply] = search_state->pv_length[search_state->ply + 1];

            if (!is_capture) update_history(search_state, mv, depth);

            alpha = score;

            if (score >= beta) {
                count_cutoff(search_state, num_moves_searched, is_quiet, num_quiets_searched, is_capture, tt_move != 0,
                             mv == tt_move);
                if (!excluded_move) {
                    tt_save(board->hash, tt_beta, depth, search_state->ply, score, mv, raw_eval);
                    if (!in_check) learn_correction(search_state, board->side, corr_idx, depth, score, corrected_eval,
                                                    tt_beta, mv);
                }
                if (!is_capture) store_killer(search_state, mv);
                if (is_quiet) {
                    for (int j = 0; j < num_quiets_searched; j++) penalise_history(search_state, quiets_searched[j], depth);
                }
                /* The capture that cut is rewarded, the captures tried before it (whatever cut) lose. */
                if (is_capture) update_capture_history(search_state, board, mv, depth);
                for (int j = 0; j < num_captures_searched; j++) {
                    penalise_capture_history(search_state, board, captures_searched[j], depth);
                }
                if (!excluded_move) stat_quiet_node(search_state, depth, is_pv_node, tt_move != 0, quiets_tried, is_quiet);
#ifdef TIKI_SEARCH_STATS
                if (is_capture && mv != tt_move && !excluded_move) {
                    stat_capture_cut(&capture_ranks, search_state, mv, depth, is_pv_node, tt_move != 0);
                    if (stat_bucket >= 0) search_state->stats.cap_cut_by_history[stat_bucket]++;
                }
#endif
                return score;
            }
        }

        if (is_quiet && num_quiets_searched < MAX_QUIETS_PENALISED) quiets_searched[num_quiets_searched++] = mv;
        if (is_capture && num_captures_searched < MAX_CAPTURES_PENALISED) captures_searched[num_captures_searched++] = mv;
    }

    if (!excluded_move) stat_quiet_node(search_state, depth, is_pv_node, tt_move != 0, quiets_tried, false);

    /* Without the TT move there may be nothing else to play: no other move, so it's singular. */
    if (!has_legal_moves) {
        if (excluded_move) return alpha;
        return in_check ? -MATE_VALUE + search_state->ply : 0;
    }
    /* Every legal move pruned (futility, history, SEE): their scores are taken to be at most alpha, as
     * the pruning assumed, so alpha is the upper bound. */
    if (best_score == -INF) best_score = alpha;
    if (excluded_move) return best_score;

    /* An exact score if a move scored inside the window (alpha moved, best_score == alpha), otherwise
     * an upper bound: best_score, at or below the original alpha. */
    if (alpha > alpha_orig) {
        tt_save(board->hash, tt_exact, depth, search_state->ply, best_score, best_move, raw_eval);
        if (!in_check) learn_correction(search_state, board->side, corr_idx, depth, best_score, corrected_eval, tt_exact,
                                        best_move);
    } else {
        tt_save(board->hash, tt_alpha, depth, search_state->ply, best_score, 0, raw_eval);
        if (!in_check) learn_correction(search_state, board->side, corr_idx, depth, best_score, corrected_eval, tt_alpha,
                                        0);
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

/* The fallback move when no depth completed: the first legal move the root restriction allows
 * (syzygy_rank_root). Not first_legal_move, which the search uses at any node (the fifty-move rule's
 * checkmate test), where the root's restriction doesn't apply. */
static move_t first_root_move(board_t* board) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        bool legal = make_move(board, buffer.moves[i]);
        pop_move(board);
        if (legal && syzygy_root_allows(buffer.moves[i])) return buffer.moves[i];
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
    if (depth >= ASPIRATION_MIN_DEPTH && !is_mate_score(previous_score)) {
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
 * as the main thread and its results reach the main thread only through the TT. Odd helpers
 * search one ply deeper, so the threads spread over more of the tree.
 */
typedef struct {
    board_t board;
    search_state_t search_state;
    limits_t* limits;
    pthread_t thread;
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

/* Frees 'count' helpers and their continuation history tables. */
static void free_helpers(search_helper_t* helpers, int count) {
    if (helpers == NULL) return;
    for (int i = 0; i < count; i++) {
        free(helpers[i].search_state.cont_history);
        free(helpers[i].search_state.corr_history);
    }
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

static void print_info(int score, int depth, uint64_t nodes, const search_state_t* search_state, const limits_t* limits) {
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

    for (int count = 0; count < search_state->pv_length[0]; count++) {
        print_move(search_state->pv_table[0][count], min);
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
    if (count <= 0) return 0;

    /* Nothing in a helper carries over between searches (history is the main thread's), so a larger
     * set starts afresh. */
    if (count > helpers_allocated) {
        search_helper_t* grown = allocate_helpers(count);
        /* Each helper has its own continuation and correction history (cont_history_t, 3.5 MB,
         * corr_history_t, 192 KB). */
        int tables = 0;
        while (grown != NULL && tables < count) {
            cont_history_t* table = malloc(sizeof(cont_history_t));
            corr_history_t* corrections = malloc(sizeof(corr_history_t));
            if (table == NULL || corrections == NULL) {
                free(table);
                free(corrections);
                break;
            }
            init_search_state_with(&grown[tables].search_state, table, corrections);
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
        memcpy(state->history_moves, search_state->history_moves, sizeof(state->history_moves));
        memcpy(state->capture_history, search_state->capture_history, sizeof(state->capture_history));
        memcpy(*state->cont_history, *search_state->cont_history, sizeof(cont_history_t));
        memcpy(*state->corr_history, *search_state->corr_history, sizeof(corr_history_t));
        atomic_init(&state->shared_nodes, 0);
        helper->limits = limits;
        if (pthread_create(&helper->thread, NULL, helper_search, helper) != 0) break;
    }
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

move_t find_best_move(board_t* board, search_state_t* search_state, limits_t* limits) {
    int score = 0;

    int depth = limits->depth;
    move_t move = 0;
    move_t previous_move = 0;
    int stability = 0;

    search_state->thread_id = 0;
    /* Before the helpers start: they read the root restriction it sets. */
    syzygy_rank_root(board);
    search_helper_t* helpers;
    const int helper_count = start_helpers(board, search_state, limits, &helpers);

    for (int i = 1; i <= depth; i++) {
        const uint64_t nodes_before = search_state->nodes;
        score = aspiration_search(board, search_state, limits, i, score);

        /* The search may have been stopped, the PV table is then partly overwritten so keep
         * the move from the last completed depth. */
        if (limits->stop_search_flag) break;

        move = search_state->pv_table[0][0];
        search_state->score = score;

        if (search_state->pv_length[0] && !limits->quiet) {
            print_info(score, i, total_nodes(search_state, helpers, helper_count), search_state, limits);
        }

        stability = move == previous_move ? stability + 1 : 0;
        previous_move = move;

        /* Soft limit: no new depth after it. With a clock it's scaled by how settled the search is,
         * from TM_SCALE_MIN_DEPTH on, and never later than the hard limit. */
        if (limits->time_set) {
            uint64_t soft_stop = limits->soft_stop_time;
            if (limits->adaptive && i >= TM_SCALE_MIN_DEPTH) {
                const double scale = tm_scale(stability, search_state->best_move_nodes,
                                              search_state->nodes - nodes_before);
                soft_stop = limits->start_time +
                            (uint64_t) ((double) (limits->soft_stop_time - limits->start_time) * scale);
                if (soft_stop > limits->stop_time) soft_stop = limits->stop_time;
            }
            if (clock_time_ms() > soft_stop) break;
        }
    }

    stop_helpers(limits, helpers, helper_count);
    if (!move) move = first_root_move(board);
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
