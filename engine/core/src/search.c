#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "search.h"
#include "evaluation.h"
#include "transposition.h"
#include "see.h"

int score_move(move_t move, move_t tt_move, board_t* board, search_state_t* search_state) {

    /*
     * We require, in order of move type,
     *      0. TT best move, then PV
     *      1. captures,
     *      2. killer moves,
     *      3. history heuristic moves.
     *
     * Since these scores are passed into a sort method, we just apply a constant
     * offset to ensure the ordering of the three categories.
     *
     * However, if we are using principal variation search and following principal variation line,
     * the mark that move as the most important (i.e. give it some integer so its before the 3
     * categories above).
     *
     */
    if (tt_move && move == tt_move) return 30000;

    if (search_state->score_pv && search_state->pv_table[0][search_state->ply] == move) {
        search_state->score_pv = false;
        return 20000;
    }

    if (get_capture_flag(move)) {
        square target = get_target_square(move);
        piece piece = get_piece_moved(move);
        /* En passant, the target square is empty, the victim is the pawn beside it. */
        if (get_enpassant_flag(move)) {
            return 10000 + search_const.mvv_lva[piece][board->side == white ? p : P];
        }
        const int* pieces = (board->side == white) ? black_pieces : white_pieces;
        for (int i = 0; i < 6; i++) {
            if (is_bit_set(&board->pieces[pieces[i]], target)) {
                /* Losing captures (SEE < 0) after the quiet moves. Only possible when the
                 * attacker is worth more than the victim, so SEE is skipped otherwise. */
                if (see_values[piece] > see_values[pieces[i]] && see(board, move) < 0) {
                    return BAD_CAPTURE_SCORE + search_const.mvv_lva[piece][pieces[i]];
                }
                return 10000 + search_const.mvv_lva[piece][pieces[i]];
            }
        }
        return 0;
    } else {
        if (search_state->killer_moves[0][search_state->ply] == move) return 9000;
        else if (search_state->killer_moves[1][search_state->ply] == move) return 8000;
        return search_state->history_moves[get_piece_moved(move)][get_target_square(move)];
    }
}


/**
 * Quiet move raised alpha, bonus depth^2 with 'gravity': h += bonus - h * bonus / HISTORY_MAX.
 * The increase shrinks as h approaches HISTORY_MAX and h never exceeds it, so history stays
 * below the killer and capture scores however deep or long the search runs.
 */
void update_history(search_state_t* search_state, move_t mv, int depth) {
    int32_t* h = &search_state->history_moves[get_piece_moved(mv)][get_target_square(mv)];
    int bonus = depth * depth;
    if (bonus > HISTORY_MAX) bonus = HISTORY_MAX;
    *h += bonus - *h * bonus / HISTORY_MAX;
    if (*h > HISTORY_MAX) *h = HISTORY_MAX;
}

static inline_always void score_moves(const move_buffer_t* buffer, int* scores, move_t tt_move, board_t* board, search_state_t* search_state) {
    for (int i = 0; i < buffer->index; i++) {
        scores[i] = score_move(buffer->moves[i], tt_move, board, search_state);
    }
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


static inline_always void set_follow_pv_flags(move_buffer_t* buffer, search_state_t* search_state) {
    search_state->follow_pv = false;
    for (int i=0; i< buffer->index; i++) {
        if (search_state->pv_table[0][search_state->ply] == buffer->moves[i]) {
            search_state->score_pv = true;
            search_state->follow_pv = true;
            break;
        }
    }
}

static inline_always void check_time(limits_t* limits) {
    /* Set flag if out of time. 'stop' and 'quit' set the flag from the input thread. */
    ++limits->time_checks;
    if (limits->time_set && clock_time_ms() > limits->stop_time) {
        limits->stop_search_flag = true;
    }
}

/**
 * Every 2048 nodes. The main thread checks the clock (its stop flag stops the helpers too), the
 * helpers publish their node count for the main thread's 'info' output.
 */
static inline_always void node_batch(search_state_t* search_state, limits_t* limits) {
    if (search_state->thread_id == 0) {
        check_time(limits);
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

/* Whether the side to move is in check. */
static inline_always bool side_in_check(board_t* board) {
    return board->side == white ?
           is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K])) :
           is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));
}

bool is_draw(const board_t* board, search_state_t* search_state) {
    return board->fifty_move >= 100 ||
           is_insufficient_material(board) ||
           contains_repetition(search_state, board->hash, board->fifty_move);
}

int quiescence(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    if ((++search_state->nodes & 2047) == 0) node_batch(search_state, limits);

    if (search_state->ply > MAX_PLY-1) return evaluation(board);

    int eval = evaluation(board);
    if (eval >= beta) return beta;
    if (eval > alpha) alpha = eval;

    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    generate_captures(board, &buffer);
    score_moves(&buffer, scores, 0, board, search_state);

    /* Captures and queen promotions only. */
    for (int i=0; i< buffer.index; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);

        /* Sorted best first, so from here on every capture loses material (SEE < 0). */
        if (scores[i] < 0) break;

        /* Delta pruning, even winning the piece can't bring the score up to alpha. */
        if (!get_promoted_piece(mv) && eval + captured_value(board, mv) + DELTA_MARGIN <= alpha) continue;

        ++search_state->ply;
        if (make_move(board, mv)) {
            int score = -quiescence(-beta, -alpha, board, search_state, limits);
            --search_state->ply;
            pop_move(board);

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
    return alpha;
}


int negamax(int alpha,
            int beta,
            int depth,
            board_t* board,
            search_state_t* search_state,
            limits_t* limits) {

    search_state->pv_length[search_state->ply] = search_state->ply;

    /* Not at the root, so a move is always returned. */
    if (search_state->ply > 0 && is_draw(board, search_state))
        return 0;

    if (search_state->ply>MAX_PLY-1) return evaluation(board);

    /* Extend checks before the depth 0 test, so a check at the horizon is searched rather than
     * standing pat in qsearch (which would miss mates). */
    bool in_check = side_in_check(board);

    if (in_check) ++depth;

    /* Probed with the extended depth, the same depth the node is saved with. */
    bool is_pv_node = beta - alpha > 1;
    const int alpha_orig = alpha;
    move_t tt_move;
    int cached_eval = tt_probe(board->hash, depth, search_state->ply, alpha, beta, &tt_move);
    if (search_state->ply && !is_pv_node && cached_eval != TT_NOT_FOUND) return cached_eval;

    if (depth == 0) {
        return quiescence(alpha, beta, board, search_state, limits);
    }

    depth -= iir_reduction(depth, tt_move);

    /* Not meaningful in check, every move is searched there anyway. */
    const int static_eval = in_check ? -INF : evaluation(board);

    if (can_reverse_futility_prune(depth, static_eval, beta, in_check, is_pv_node, search_state->ply)) return beta;

    /* Null move pruning. */
    if (can_null_move(board, search_state, depth, beta, static_eval, in_check, is_pv_node)) {
        make_null_move(board);
        search_state->repetition_check[++search_state->repetition_index] = board->hash;
        /* Repetitions can't span the null move, it isn't a real move. */
        const uint16_t null_move_index = search_state->null_move_index;
        search_state->null_move_index = search_state->repetition_index;
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

    /* Check straight after counting, so the check runs once per 2048 counted nodes. */
    if ((++search_state->nodes & 2047) == 0) node_batch(search_state, limits);

    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    generate_moves(board, &buffer);
    if (search_state->follow_pv) set_follow_pv_flags(&buffer, search_state);
    score_moves(&buffer, scores, tt_move, board, search_state);

    const bool futile = is_futile(depth, static_eval, alpha, in_check, is_pv_node);

    bool has_legal_moves = false;
    int num_moves_searched = 0;
    int score;
    move_t best_move = 0;
    for (int i=0; i < buffer.index; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);
        bool is_capture = get_capture_flag(mv);
        if (!make_move(board,mv)) {
            pop_move(board);
            continue;
        }
        has_legal_moves = true;

        const bool is_quiet = !is_capture && !get_promoted_piece(mv);
        /* Only needed when futility or LMR might apply, it costs an attack test. */
        const bool may_prune = is_quiet && num_moves_searched > 0 &&
                               (futile || (num_moves_searched >= LMR_MIN_MOVES && depth >= LMR_MIN_DEPTH && !in_check));
        const bool gives_check = may_prune && side_in_check(board);

        /* Futility pruning, quiet moves that don't give check. The first move is always searched. */
        if (futile && num_moves_searched > 0 && is_quiet && !gives_check) {
            pop_move(board);
            continue;
        }

        /* Late move reduction, quiet moves late in the list. Killers, checks and moves made in
         * check are searched at full depth. Killers are for this ply, checked before ++ply. */
        int reduction = 0;
        if (num_moves_searched >= LMR_MIN_MOVES && depth >= LMR_MIN_DEPTH && is_quiet && !gives_check && !in_check &&
            mv != search_state->killer_moves[0][search_state->ply] &&
            mv != search_state->killer_moves[1][search_state->ply]) {
            reduction = lmr_reduction(depth, num_moves_searched, is_pv_node);
        }

        ++search_state->ply;
        search_state->repetition_check[++search_state->repetition_index] = board->hash;

        if (num_moves_searched == 0 ) {
            score = -negamax(-beta, -alpha, depth-1, board, search_state, limits);
        } else {
            /* Principal variation search, null window (reduced for late moves). A reduced move that
             * beats alpha is searched again at full depth with the null window, then with the full
             * window if it's still inside it. */
            score = -negamax(-alpha - 1, -alpha, depth - 1 - reduction, board, search_state, limits);
            if (score > alpha && reduction > 0) {
                score = -negamax(-alpha - 1, -alpha, depth - 1, board, search_state, limits);
            }
            if (score > alpha && score < beta) {
                score = -negamax(-beta, -alpha, depth - 1, board, search_state, limits);
            }
        }

        ++num_moves_searched;
        --search_state->ply;
        --search_state->repetition_index;
        pop_move(board);

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
                tt_save(board->hash, tt_beta, depth, search_state->ply, score, mv);
                if (!is_capture) {
                    search_state->killer_moves[1][search_state->ply] = search_state->killer_moves[0][search_state->ply];
                    search_state->killer_moves[0][search_state->ply] = mv;
                }
                return beta;
            }
        }
    }

    if (!has_legal_moves) return in_check ? -MATE_VALUE + search_state->ply : 0;

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

static void* helper_search(void* arg) {
    search_helper_t* helper = arg;
    limits_t* limits = helper->limits;
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
        printf("info score mate %d depth %d nodes %lu time %lu nps %lu pv ",
               -(score + MATE_VALUE) / 2 - 1, depth, nodes, time, nodes * 1000 / (time ? time : 1));
    } else if (score > MATE_SCORE && score < MATE_VALUE) {
        printf("info score mate %d depth %d nodes %lu time %lu nps %lu pv ",
               (MATE_VALUE - score) / 2 + 1, depth, nodes, time, nodes * 1000 / (time ? time : 1));
    } else {
        printf("info score cp %d depth %d nodes %lu time %lu nps %lu pv ",
               score, depth, nodes, time, nodes * 1000 / (time ? time : 1));
    }

    for (int count = 0; count < search_state->pv_length[0]; count++) {
        print_move(search_state->pv_table[0][count], min);
        printf(" ");
    }
    printf("\n");
}

/**
 * Starts limits->threads - 1 helpers on copies of the root position and search state, returns
 * the number started (fewer if memory or thread creation fails).
 */
static int start_helpers(board_t* board, search_state_t* search_state, limits_t* limits, search_helper_t** helpers) {
    const int count = limits->threads - 1;
    *helpers = NULL;
    if (count <= 0) return 0;

    *helpers = malloc((size_t) count * sizeof(search_helper_t));
    if (*helpers == NULL) return 0;

    int started = 0;
    for (; started < count; started++) {
        search_helper_t* helper = &(*helpers)[started];
        memcpy(&helper->board, board, sizeof(board_t));
        memcpy(&helper->search_state, search_state, sizeof(search_state_t));
        helper->search_state.thread_id = started + 1;
        atomic_init(&helper->search_state.shared_nodes, 0);
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
    free(helpers);
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
        if (limits->stop_search_flag || score == STOPPED_SEARCH) break;

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

    piece maybe_promoted = get_promoted_piece(mv);
    if (maybe_promoted == 0) {
        printf("bestmove %s%s\n",
               square_to_str[get_source_square(mv)],
               square_to_str[get_target_square(mv)]);
    } else {
        printf("bestmove %s%s%c\n",
               square_to_str[get_source_square(mv)],
               square_to_str[get_target_square(mv)],
               piece_to_char[maybe_promoted]);
    }
}
