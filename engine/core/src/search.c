#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "search.h"
#include "evaluation.h"
#include "transposition.h"
#include "see.h"

/**
 * Move ordering score, highest first. Each category has its own band (search_constants.h): the TT
 * move, the PV move (while following the previous iteration's PV), winning and equal captures by
 * MVV-LVA, the two killers, quiet moves (and promotions that don't capture) by history, then losing
 * captures.
 */
int score_move(move_t move, move_t tt_move, board_t* board, search_state_t* search_state) {
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
            return CAPTURE_SCORE + search_const.mvv_lva[piece][board->side == white ? p : P];
        }
        const int* pieces = (board->side == white) ? black_pieces : white_pieces;
        for (int i = 0; i < 6; i++) {
            if (is_bit_set(&board->pieces[pieces[i]], target)) {
                /* Losing captures (SEE < 0) after the quiet moves. Only possible when the
                 * attacker is worth more than the victim, so SEE is skipped otherwise. */
                if (see_values[piece] > see_values[pieces[i]] && see(board, move) < 0) {
                    return BAD_CAPTURE_SCORE + search_const.mvv_lva[piece][pieces[i]];
                }
                return CAPTURE_SCORE + search_const.mvv_lva[piece][pieces[i]];
            }
        }
        return 0;
    } else {
        if (search_state->killer_moves[0][search_state->ply] == move) return KILLER_1_SCORE;
        else if (search_state->killer_moves[1][search_state->ply] == move) return KILLER_2_SCORE;
        return search_state->history_moves[get_piece_moved(move)][get_target_square(move)];
    }
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

/* Adds a signed bonus to the move's history. */
static inline_always void add_history_bonus(search_state_t* search_state, move_t mv, int bonus) {
    int32_t* h = &search_state->history_moves[get_piece_moved(mv)][get_target_square(mv)];
    *h = history_gravity(*h, bonus);
}

static inline_always int history_bonus(int depth) {
    return depth < 10 ? 16 * depth * depth : 1600;
}

/* Quiet move raised alpha, bonus min(16 * depth^2, 1600). */
void update_history(search_state_t* search_state, move_t mv, int depth) {
    add_history_bonus(search_state, mv, history_bonus(depth));
}

/**
 * History malus: a quiet move searched before the quiet move that caused a cutoff didn't cut, so
 * its history is lowered by min(16 * depth^2, 1600). Without it history only rises, and a move that's often tried
 * but rarely good keeps a high score.
 */
void penalise_history(search_state_t* search_state, move_t mv, int depth) {
    add_history_bonus(search_state, mv, -history_bonus(depth));
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
    for (int i = 0; i < buffer->index; i++) {
        scores[i] = score_move(buffer->moves[i], tt_move, board, search_state);
    }
    search_state->score_pv = false;
}

/**
 * Selection sort one step at a time: swap the best scored move from i onwards into i and return it.
 * Most nodes cut off after one or two moves, so sorting the whole list up front is wasted work.
 */
move_t pick_next_move(move_buffer_t* buffer, int* scores, int i) {
    int best = i;
    for (int j = i + 1; j < buffer->index; j++) {
        if (scores[j] > scores[best]) best = j;
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


/* A beta cutoff, for the move ordering statistics. Nothing unless built with TIKI_SEARCH_STATS. */
static inline_always void count_cutoff(search_state_t* search_state, int moves_searched, bool is_quiet,
                                       int quiets_searched) {
#ifdef TIKI_SEARCH_STATS
    search_stats_t* stats = &search_state->stats;
    stats->cutoffs++;
    if (moves_searched == 1) stats->first_move_cutoffs++;
    if (is_quiet) {
        stats->quiet_cutoffs++;
        stats->quiets_before_cutoff += (uint64_t) quiets_searched;
    }
#else
    (void) search_state; (void) moves_searched; (void) is_quiet; (void) quiets_searched;
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

/**
 * Whether to try null move pruning at this node. Not at the root or in PV nodes, not in check,
 * not straight after another null move (the position after a null move is at null_move_index),
 * not with only king and pawns, and only when the static eval is already at or above beta.
 */
bool can_null_move(const board_t* board,
                   const search_state_t* search_state,
                   int depth,
                   int beta,
                   int static_eval,
                   bool in_check,
                   bool is_pv_node) {
    if (depth < NULL_MOVE_DEPTH_BOUND || in_check || is_pv_node || search_state->ply == 0) return false;
    if (search_state->null_move_index != 0 && search_state->null_move_index == search_state->repetition_index) return false;
    if (!has_non_pawn_material(board)) return false;
    return static_eval >= beta;
}

static inline_always bool is_mate_score(int score) {
    return score >= MATE_SCORE || score <= -MATE_SCORE;
}

/**
 * Reverse futility (static null move) pruning: at low depth, a static eval this far above beta
 * is very unlikely to drop below it, so the node fails high without a search. Not in check, PV
 * nodes, at the root or near mate scores.
 */
bool can_reverse_futility_prune(int depth, int static_eval, int beta, bool in_check, bool is_pv_node, int ply) {
    return ply > 0 && !in_check && !is_pv_node && depth <= RFP_MAX_DEPTH && !is_mate_score(beta) &&
           static_eval - RFP_MARGIN * depth >= beta;
}

/**
 * Futility pruning: at low depth, a static eval this far below alpha means a quiet move is very
 * unlikely to raise it, so quiet moves (after the first) are skipped. Captures, promotions and
 * checks are still searched.
 */
bool is_futile(int depth, int static_eval, int alpha, bool in_check, bool is_pv_node) {
    return !in_check && !is_pv_node && depth <= FUTILITY_MAX_DEPTH && !is_mate_score(alpha) &&
           static_eval + FUTILITY_MARGIN * depth <= alpha;
}

/**
 * Late move pruning: at low depth, once base + depth^2 moves have been searched, the remaining
 * quiet moves (sorted last, after the killers and the best history) are very unlikely to raise
 * alpha, so they're skipped. Captures and promotions are still searched.
 */
bool can_late_move_prune(int depth, int moves_searched, int alpha, bool in_check, bool is_pv_node) {
    return !in_check && !is_pv_node && depth <= LMP_MAX_DEPTH && !is_mate_score(alpha) &&
           moves_searched >= LMP_BASE + depth * depth;
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
 * Whether a move is reduced: quiet moves late in the list at enough depth. Checks, moves made in
 * check and the killers are searched at full depth.
 */
bool can_reduce(int depth, int moves_searched, bool is_quiet, bool gives_check, bool in_check, bool is_killer) {
    return moves_searched >= LMR_MIN_MOVES && depth >= LMR_MIN_DEPTH && is_quiet && !gives_check && !in_check &&
           !is_killer;
}

/**
 * Late move reduction for a quiet move, from the log table, one ply less in PV nodes. Clamped so
 * the reduced search still has at least one ply (depth - 1 - r >= 1).
 */
int lmr_reduction(int depth, int moves_searched, bool is_pv_node) {
    int r = lmr_table[depth < LMR_TABLE_SIZE ? depth : LMR_TABLE_SIZE - 1]
                     [moves_searched < LMR_TABLE_SIZE ? moves_searched : LMR_TABLE_SIZE - 1];
    if (is_pv_node) r--;
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
           contains_repetition(search_state, board->hash, board->fifty_move);
}

/**
 * make_move / pop_move for the search, keeping the network's accumulators in step. An illegal move
 * is made without updating the accumulators (most moves are illegal in check), so it's undone with
 * pop_move alone.
 */
static inline_always bool search_make_move(board_t* board, search_state_t* search_state, move_t mv) {
    if (!make_move(board, mv)) return false;
    nnue_push(&search_state->nnue, board, mv);
    return true;
}

static inline_always void search_pop_move(board_t* board, search_state_t* search_state) {
    pop_move(board);
    nnue_pop(&search_state->nnue);
}

/* The qsearch node itself, already counted: by quiescence, or by negamax at depth 0. */
static int quiescence_node(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    if (search_state->ply == 0) nnue_reset(&search_state->nnue);
    if (search_state->ply > MAX_PLY-1) return evaluation(board, &search_state->nnue);

    /* In check there's no standing pat (the position may be lost), every evasion is searched. */
    const bool in_check = side_in_check(board);
    const int eval = in_check ? -INF : evaluation(board, &search_state->nnue);
    if (eval >= beta) return beta;
    if (eval > alpha) alpha = eval;

    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    if (in_check) generate_moves(board, &buffer);
    else generate_captures(board, &buffer);
    score_moves(&buffer, scores, 0, board, search_state);

    /* Out of check, captures and queen promotions only. */
    bool has_legal_moves = false;
    for (int i=0; i< buffer.index; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);

        if (!in_check) {
            /* Sorted best first, so from here on every capture loses material (SEE < 0). Not a quiet
             * promotion: it's scored by history, which the malus can make negative, and the losing
             * captures (BAD_CAPTURE_SCORE) still sort after it. */
            if (scores[i] < 0 && get_capture_flag(mv)) break;

            /* Delta pruning, even winning the piece can't bring the score up to alpha. */
            if (!get_promoted_piece(mv) && eval + captured_value(board, mv) + DELTA_MARGIN <= alpha) continue;
        }

        ++search_state->ply;
        if (search_make_move(board, search_state, mv)) {
            has_legal_moves = true;
            int score = -quiescence(-beta, -alpha, board, search_state, limits);
            --search_state->ply;
            search_pop_move(board, search_state);

            if (limits->stop_search_flag) return STOPPED_SEARCH;

            if (score > alpha) {
                alpha = score;
                if (score >= beta) return beta;
            }
        } else {
            --search_state->ply;
            pop_move(board);
        }
    }
    if (in_check && !has_legal_moves) return -MATE_VALUE + search_state->ply;
    return alpha;
}

int quiescence(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    if ((++search_state->nodes & 2047) == 0) node_batch(search_state, limits);
    return quiescence_node(alpha, beta, board, search_state, limits);
}


int negamax(int alpha,
            int beta,
            int depth,
            board_t* board,
            search_state_t* search_state,
            limits_t* limits) {

    /* Every node, including those that return early (TT, draw, pruning). Checked straight after
     * counting, so the check runs once per 2048 counted nodes. */
    if ((++search_state->nodes & 2047) == 0) node_batch(search_state, limits);

    search_state->pv_length[search_state->ply] = search_state->ply;

    /* The root, the accumulators start again from this position. */
    if (search_state->ply == 0) nnue_reset(&search_state->nnue);

    /* Whether this node is on the previous iteration's PV, set by the parent for this node only. */
    const bool follow_pv = search_state->follow_pv;

    /* Not at the root, so a move is always returned. */
    if (search_state->ply > 0 && is_draw(board, search_state))
        return 0;

    /* The deepest ply the tables hold. No moves are made from here, so no node is ever at MAX_PLY
     * (pv_length and pv_table are read at ply + 1). */
    if (search_state->ply >= MAX_PLY - 1) return evaluation(board, &search_state->nnue);

    /* Extend checks before the depth 0 test, so a check at the horizon is searched rather than
     * standing pat in qsearch (which would miss mates). */
    bool in_check = side_in_check(board);

    if (in_check) ++depth;

    /* A singular extension search of this node without the TT move (see below). Its result isn't
     * the node's: it doesn't cut off from the TT, save to it, or prune the whole node. */
    const move_t excluded_move = search_state->excluded_moves[search_state->ply];

    /* Probed with the extended depth, the same depth the node is saved with. */
    bool is_pv_node = beta - alpha > 1;
    const int alpha_orig = alpha;
    move_t tt_move;
    int cached_eval = tt_probe(board->hash, depth, search_state->ply, alpha, beta, &tt_move);
    if (search_state->ply && !is_pv_node && !excluded_move && cached_eval != TT_NOT_FOUND) return cached_eval;

    if (depth == 0) {
        return quiescence_node(alpha, beta, board, search_state, limits);
    }

    depth -= iir_reduction(depth, tt_move);

    /* Not meaningful in check, every move is searched there anyway. */
    const int static_eval = in_check ? -INF : evaluation(board, &search_state->nnue);

    if (!excluded_move && can_reverse_futility_prune(depth, static_eval, beta, in_check, is_pv_node, search_state->ply))
        return beta;

    /* Null move pruning. */
    if (!excluded_move && can_null_move(board, search_state, depth, beta, static_eval, in_check, is_pv_node)) {
        make_null_move(board);
        search_state->repetition_check[++search_state->repetition_index] = board->hash;
        /* Repetitions can't span the null move, it isn't a real move. */
        const uint16_t null_move_index = search_state->null_move_index;
        search_state->null_move_index = search_state->repetition_index;
        search_state->follow_pv = false;
        ++search_state->ply;
        int score = -negamax(-beta, -beta + 1, depth - 3, board, search_state, limits);
        --search_state->ply;
        search_state->null_move_index = null_move_index;
        --search_state->repetition_index;
        pop_null_move(board);
        /* A stopped null search returns -STOPPED_SEARCH, which would look like a cutoff. */
        if (limits->stop_search_flag) return STOPPED_SEARCH;
        if (score >= beta) return beta;
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
            const int score = negamax(s_beta - 1, s_beta, (depth - 1) / 2, board, search_state, limits);
            search_state->excluded_moves[search_state->ply] = 0;
            /* The search without the TT move used this ply's PV row, the node starts its own. */
            search_state->pv_length[search_state->ply] = search_state->ply;
            if (limits->stop_search_flag) return STOPPED_SEARCH;

            const singular_result_t result = singular_result(score, s_beta, tt_score, beta, is_pv_node);
            count_singular(search_state, search_state->nodes - nodes_before, result);
            if (result == singular_multi_cut) return beta;
            if (result == singular_extend) extension = 1;
            else if (result == singular_reduce) extension = -1;
        }
    }

    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    generate_moves(board, &buffer);
    /* On the PV, its move is ordered first (after the TT move) and its child is on the PV too. */
    move_t pv_move = 0;
    if (follow_pv) {
        pv_move = find_pv_move(&buffer, search_state);
        if (pv_move) search_state->pv_nodes++;
    }
    search_state->score_pv = pv_move != 0;
    score_moves(&buffer, scores, tt_move, board, search_state);

    const bool futile = is_futile(depth, static_eval, alpha, in_check, is_pv_node);

    bool has_legal_moves = false;
    int num_moves_searched = 0;
    int score;
    move_t best_move = 0;
    /* Quiet moves searched without a cutoff, for the history malus when a later quiet move cuts. */
    move_t quiets_searched[MAX_QUIETS_PENALISED];
    int num_quiets_searched = 0;
    for (int i=0; i < buffer.index; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);
        if (mv == excluded_move) continue;
        bool is_capture = get_capture_flag(mv);
        const bool is_quiet = !is_capture && !get_promoted_piece(mv);

        /* Late move pruning, before making the move: a legal move has been searched already, so
         * whether this one is legal doesn't matter. */
        if (is_quiet && can_late_move_prune(depth, num_moves_searched, alpha, in_check, is_pv_node)) continue;

        /* SEE pruning, also before making the move. Promotions are always searched. */
        if (!get_promoted_piece(mv) && can_see_prune(depth, num_moves_searched, alpha, in_check, is_pv_node) &&
            see(board, mv) < see_prune_threshold(depth, is_quiet)) continue;

        if (!search_make_move(board, search_state, mv)) {
            pop_move(board);
            continue;
        }
        has_legal_moves = true;

        /* Only needed when futility or LMR might apply, it costs an attack test. */
        const bool may_prune = is_quiet && num_moves_searched > 0 &&
                               (futile || (num_moves_searched >= LMR_MIN_MOVES && depth >= LMR_MIN_DEPTH && !in_check));
        const bool gives_check = may_prune && side_in_check(board);

        /* Futility pruning, quiet moves that don't give check. The first move is always searched. */
        if (futile && num_moves_searched > 0 && is_quiet && !gives_check) {
            search_pop_move(board, search_state);
            continue;
        }

        /* Late move reduction. Killers are for this ply, checked before ++ply. */
        const bool is_killer = mv == search_state->killer_moves[0][search_state->ply] ||
                               mv == search_state->killer_moves[1][search_state->ply];
        const int reduction = can_reduce(depth, num_moves_searched, is_quiet, gives_check, in_check, is_killer) ?
                              lmr_reduction(depth, num_moves_searched, is_pv_node) : 0;
        /* The TT move a ply deeper if it's singular, shallower if it isn't but beat beta (see above). */
        const int new_depth = depth - 1 + (mv == tt_move ? extension : 0);

        /* Set before every search of the child, each one changes it further down the tree. */
        const bool child_on_pv = pv_move != 0 && mv == pv_move;
        ++search_state->ply;
        search_state->repetition_check[++search_state->repetition_index] = board->hash;

        search_state->follow_pv = child_on_pv;
        if (num_moves_searched == 0 ) {
            score = -negamax(-beta, -alpha, new_depth, board, search_state, limits);
        } else {
            /* Principal variation search, null window (reduced for late moves). A reduced move that
             * beats alpha is searched again at full depth with the null window, then with the full
             * window if it's still inside it. */
            score = -negamax(-alpha - 1, -alpha, new_depth - reduction, board, search_state, limits);
            if (score > alpha && reduction > 0) {
                search_state->follow_pv = child_on_pv;
                score = -negamax(-alpha - 1, -alpha, new_depth, board, search_state, limits);
            }
            if (score > alpha && score < beta) {
                search_state->follow_pv = child_on_pv;
                score = -negamax(-beta, -alpha, new_depth, board, search_state, limits);
            }
        }

        ++num_moves_searched;
        --search_state->ply;
        --search_state->repetition_index;
        search_pop_move(board, search_state);

        if (limits->stop_search_flag) return STOPPED_SEARCH;

        if (score > alpha) {
            best_move = mv;

            search_state->pv_table[search_state->ply][search_state->ply] = mv;
            for (int j = search_state->ply + 1; j < search_state->pv_length[search_state->ply + 1]; j++) {
                search_state->pv_table[search_state->ply][j] = search_state->pv_table[search_state->ply + 1][j];
            }
            search_state->pv_length[search_state->ply] = search_state->pv_length[search_state->ply + 1];

            if (!is_capture) update_history(search_state, mv, depth);

            alpha = score;

            if (score >= beta) {
                count_cutoff(search_state, num_moves_searched, is_quiet, num_quiets_searched);
                if (!excluded_move) tt_save(board->hash, tt_beta, depth, search_state->ply, score, mv);
                if (!is_capture) store_killer(search_state, mv);
                if (is_quiet) {
                    for (int j = 0; j < num_quiets_searched; j++) penalise_history(search_state, quiets_searched[j], depth);
                }
                return beta;
            }
        }

        if (is_quiet && num_quiets_searched < MAX_QUIETS_PENALISED) quiets_searched[num_quiets_searched++] = mv;
    }

    /* Without the TT move there may be nothing else to play: no other move, so it's singular. */
    if (!has_legal_moves) {
        if (excluded_move) return alpha;
        return in_check ? -MATE_VALUE + search_state->ply : 0;
    }
    if (excluded_move) return alpha;

    /* Fail-hard, alpha only moves if a move scored inside the window. */
    if (alpha > alpha_orig) {
        tt_save(board->hash, tt_exact, depth, search_state->ply, alpha, best_move);
    } else {
        tt_save(board->hash, tt_alpha, depth, search_state->ply, alpha, 0);
    }

    return alpha;
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

static void free_helpers(search_helper_t* helpers) {
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
 * game history for repetitions (up to the root) and the scalars are copied from the main thread's.
 */
static int start_helpers(board_t* board, search_state_t* search_state, limits_t* limits, search_helper_t** helpers) {
    int count = limits->threads - 1;
    *helpers = helpers_kept;
    if (count <= 0) return 0;

    /* Nothing in a helper carries over between searches, so a larger set starts afresh. */
    if (count > helpers_allocated) {
        search_helper_t* grown = allocate_helpers(count);
        if (grown == NULL) {
            count = helpers_allocated;
        } else {
            for (int i = 0; i < count; i++) init_search_state(&grown[i].search_state);
            free_helpers(helpers_kept);
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
        state->repetition_index = search_state->repetition_index;
        state->null_move_index = search_state->null_move_index;
        state->nodes = 0;
        state->pv_nodes = 0;
        state->ply = 0;
        state->score = 0;
        state->score_pv = false;
        state->follow_pv = true;
        state->thread_id = started + 1;
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

move_t find_best_move(board_t* board, search_state_t* search_state, limits_t* limits) {
    int score = 0;

    int depth = limits->depth;
    move_t move = 0;

    search_state->thread_id = 0;
    search_helper_t* helpers;
    const int helper_count = start_helpers(board, search_state, limits, &helpers);

    for (int i = 1; i <= depth; i++) {
        score = aspiration_search(board, search_state, limits, i, score);

        /* The search may have been stopped, the PV table is then partly overwritten so keep
         * the move from the last completed depth. */
        if (limits->stop_search_flag) break;

        move = search_state->pv_table[0][0];
        search_state->score = score;

        if (search_state->pv_length[0] && !limits->quiet) {
            print_info(score, i, total_nodes(search_state, helpers, helper_count), search_state, limits);
        }

        /* Soft limit, the next depth is unlikely to complete in the time left. */
        if (limits->time_set && clock_time_ms() > limits->soft_stop_time) break;
    }

    stop_helpers(limits, helpers, helper_count);
    return move ? move : first_legal_move(board);
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
