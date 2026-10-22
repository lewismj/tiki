#ifndef TIKI_SEARCH_H
#define TIKI_SEARCH_H

#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "types.h"
#include "board.h"
#include "move_generator.h"
#include "evaluation.h"
#include "search_constants.h"
#include "search_state.h"
#include "transposition.h"
#include "limits.h"


static inline_always int score_move(move_t move, move_t tt_move, board_t* board, search_state_t* search_state) {

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
        int* pieces = (board->side == white) ? black_pieces : white_pieces;
        for (int i = 0; i < 6; i++) {
            if (is_bit_set(&board->pieces[pieces[i]], target)) {
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
static inline_always void update_history(search_state_t* search_state, move_t mv, int depth) {
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
static inline_always move_t pick_next_move(move_buffer_t* buffer, int* scores, int i) {
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

/* KvK, KNvK and KBvK (either side), neither side can mate. */
static inline_always bool is_insufficient_material(const board_t* board) {
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
static inline_always bool can_null_move(const board_t* board,
                                        const search_state_t* search_state,
                                        int depth,
                                        int beta,
                                        bool in_check,
                                        bool is_pv_node) {
    if (depth < NULL_MOVE_DEPTH_BOUND || in_check || is_pv_node || search_state->ply == 0) return false;
    if (search_state->null_move_index != 0 && search_state->null_move_index == search_state->repetition_index) return false;
    if (!has_non_pawn_material(board)) return false;
    return evaluation(board) >= beta; /* Last, it's the most expensive. */
}

static inline_always bool is_draw(const board_t* board, search_state_t* search_state) {
    return board->fifty_move >= 100 ||
           is_insufficient_material(board) ||
           contains_repetition(search_state, board->hash, board->fifty_move);
}

static inline_hint int quiescence(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    if ((++search_state->nodes & 2047) == 0) check_time(limits);

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


static inline int negamax(int alpha,
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
    bool in_check = board->side == white ?
                    is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K])) :
                    is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));

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

    /* Null move pruning. */
    if (can_null_move(board, search_state, depth, beta, in_check, is_pv_node)) {
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
    if ((++search_state->nodes & 2047) == 0) check_time(limits);

    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    generate_moves(board, &buffer);
    if (search_state->follow_pv) set_follow_pv_flags(&buffer, search_state);
    score_moves(&buffer, scores, tt_move, board, search_state);

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
        ++search_state->ply;
        search_state->repetition_check[++search_state->repetition_index] = board->hash;

        if (num_moves_searched == 0 ) {
            score = -negamax(-beta, -alpha, depth-1, board, search_state, limits);
        } else {
            /* Late move reduction. */
            bool is_promoted = get_promoted_piece(mv);
            if ( num_moves_searched >= LMR_DEPTH_BOUND && depth >= LMR_DEPTH_BOUND && !in_check && !is_capture && !is_promoted) {
                score = -negamax(-alpha - 1, -alpha, depth - 2, board, search_state, limits);
                if (score > alpha) {
                    score = -negamax(-beta, -alpha, depth - 1, board, search_state, limits);
                }
            }
            else { /* Principal variation search. */
                score = -negamax(-alpha - 1, -alpha, depth - 1, board, search_state, limits);
                if (score > alpha && score < beta) {
                    score = -negamax(-beta, -alpha, depth - 1, board, search_state, limits);
                }
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
static move_t inline_always first_legal_move(board_t* board) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        bool legal = make_move(board, buffer.moves[i]);
        pop_move(board);
        if (legal) return buffer.moves[i];
    }
    return 0;
}

static move_t inline_always find_best_move(board_t* board, search_state_t* search_state, limits_t* limits) {
    int alpha = -INF;
    int beta = INF;
    int score;

    int depth = limits->depth;
    move_t move = 0;

    for (int i = 1; i <= depth; i++) {
        search_state->follow_pv = true;
        score = negamax(alpha, beta, i, board, search_state, limits);

        /* Outside the aspiration window, re-search with a full window. */
        if (!limits->stop_search_flag && (score <= alpha || score >= beta)) {
            alpha = -INF;
            beta = INF;
            score = negamax(alpha, beta, i, board, search_state, limits);
        }

        /* Either search may have been stopped, the PV table is then partly overwritten so keep
         * the move from the last completed depth. */
        if (limits->stop_search_flag || score == STOPPED_SEARCH) break;

        alpha = score - ASPIRATION_WINDOW;
        beta = score + ASPIRATION_WINDOW;

        move = search_state->pv_table[0][0];

        if (search_state->pv_length[0]) {
            if (score > -MATE_VALUE && score < -MATE_SCORE) {
                printf("info score mate %d depth %d nodes %lu time %lu pv ",
                       -(score + MATE_VALUE) / 2 - 1,
                       i,
                       search_state->nodes,
                       clock_time_ms() - limits->start_time);
            }
            else if (score > MATE_SCORE && score < MATE_VALUE) {
                printf("info score mate %d depth %d nodes %lu time %lu pv ", (MATE_VALUE - score) / 2 + 1,
                       i,
                       search_state->nodes,
                       clock_time_ms() - limits->start_time);
            }
            else {
                printf("info score cp %d depth %d nodes %lu time %lu pv ", score,
                       i,
                       search_state->nodes,
                       clock_time_ms() - limits->start_time);
            }

            for (int count = 0; count < search_state->pv_length[0]; count++) {
                print_move(search_state->pv_table[0][count], min);
                printf(" ");
            }
            printf("\n");
        }

        /* Soft limit, the next depth is unlikely to complete in the time left. */
        if (limits->time_set && clock_time_ms() > limits->soft_stop_time) break;
    }

    return move ? move : first_legal_move(board);
}

static void inline_always print(move_t mv) {

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

#endif
