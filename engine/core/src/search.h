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



static inline_always void init_search(search_state_t* search_state) {
    search_state->nodes_visited = 0;
    search_state->ply = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    memset(search_state->pv_table, 0, sizeof(search_state->pv_table));
    memset(search_state->pv_length, 0, sizeof(search_state->pv_length));
    memset(search_state->history_moves, 0, sizeof (search_state->history_moves));
    memset(search_state->killer_moves, 0, sizeof (search_state->killer_moves));
}

static inline_always int score_move(move_t move, board_t* board, search_state_t* search_state) {

    /*
     * We require, in order of move type,
     *      0. PV
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
    if (search_state->score_pv && search_state->pv_table[0][search_state->ply] == move) {
        search_state->score_pv = false;
        return 40000;
    }

    if (get_capture_flag(move)) {
        square target = get_target_square(move);
        piece piece = get_piece_moved(move);
        int* pieces = (board->side == white) ? black_pieces : white_pieces;
        for (int i = 0; i < 6; i++) {
            if (is_bit_set(&board->pieces[pieces[i]], target)) {
                return 30000 + search_const.mvv_lva[piece][pieces[i]];
            }
        }
        return 0;
    } else {
        if (search_state->killer_moves[0][search_state->ply] == move) return 20000;
        else if (search_state->killer_moves[1][search_state->ply] == move) return 10000;
        return search_state->history_moves[get_piece_moved(move)][get_target_square(move)];
    }
}


static inline_always void sort_move_buffer(move_buffer_t* buffer, board_t* board, search_state_t* search_state) {
    int scores[buffer->index];
    /*
     * Insertion sort, n.b. probably should look into splitting up the move gen, so captures are at the
     * front of the move list, or separate lists.
     */
    for (int i = 0; i < buffer->index; i++) {
        scores[i] = score_move(buffer->moves[i], board, search_state);
    }

    for (int i = 1; i < buffer->index; i++) {
        int current_score = scores[i];
        move_t current_move = buffer->moves[i];
        int j = i - 1;

        while (j >= 0 && scores[j] < current_score) {
            scores[j + 1] = scores[j];
            buffer->moves[j + 1] = buffer->moves[j];
            j--;
        }

        scores[j + 1] = current_score;
        buffer->moves[j + 1] = current_move;
    }
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

static inline_hint int quiescence(int alpha, int beta, board_t* board, search_state_t* search_state) {
    search_state->nodes_visited++;

    int eval = evaluation(board);
    if (eval >= beta) return beta;
    if (eval > alpha) alpha = eval;

    move_buffer_t buffer;
    generate_moves(board, &buffer);
    sort_move_buffer(&buffer, board, search_state);

    for (int i=0; i< buffer.index; i++) {
        move_t mv = buffer.moves[i];
        /* Just look at captures in quiescence. */
        if (get_capture_flag(mv)) {
            search_state->ply++;
            /* Make move will return false if player is left in check. */
            if (make_move(board, mv)) {
                int score = -quiescence(-beta, -alpha, board, search_state);
                search_state->ply--;
                pop_move(board);
                if (score >= beta) return beta;
                if (score > alpha) alpha = score;
            } else {
                search_state->ply--;
                pop_move(board);
            }
        }
    }
    return alpha;
}


static inline int negamax(int alpha, int beta, int depth, board_t* board, search_state_t* search_state) {
    /* At depth 0, just return the quiescence search. */
    if (depth == 0) return quiescence(alpha, beta, board, search_state);

    /* Check the position in the transposition table. */
    int cached_eval = tt_probe(board->hash, depth, alpha, beta);
    if (cached_eval != TT_NOT_FOUND) return cached_eval;

    search_state->pv_length[search_state->ply] = search_state->ply;
    search_state->nodes_visited++;

    bool isin_check = board->side == white ?
                        is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K]))
                      : is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));

    if (isin_check) ++depth;

    /* Null move pruning. */
    if (depth > NULL_MOVE_DEPTH_BOUND && !isin_check && search_state->ply) {
        make_null_move(board);
        search_state->ply++;
        int score = -negamax(-beta, -beta + 1, depth - NULL_MOVE_DEPTH_BOUND - 1, board, search_state);
        search_state->ply--;
        pop_null_move(board);
        if (score >= beta) return beta;
    }

    /* Generate & sort moves. */
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    if (search_state->follow_pv) set_follow_pv_flags(&buffer, search_state);
    sort_move_buffer(&buffer, board, search_state);

    int best_score = -INF;
    int score;
    move_t best_move = NULL_MOVE;
    bool searched_first_move = false;
    int moves_searched = 0;

    for (int i = 0; i < buffer.index; i++) {
        move_t mv = buffer.moves[i];
        if (make_move(board, mv)) {
            ++search_state->ply;
            ++moves_searched;
            if (!searched_first_move) {
                score = -negamax(-beta, -alpha, depth - 1, board, search_state);
            } else {

                if ( moves_searched > LMR_DEPTH_BOUND && /* Late move reduction use same bound for moves/depth. */
                    depth > LMR_DEPTH_BOUND &&
                    !isin_check &&
                    !get_capture_flag(mv)&&
                    !get_promoted_piece(mv)) {

                    score = -negamax(-alpha - 1, -alpha, depth - 2, board, search_state);
                    if (score > alpha) {
                        score = -negamax(-beta, -alpha, depth - 1, board, search_state);
                    }
                }
                else { /* Principal variation search. */
                    score = -negamax(-alpha - 1, -alpha, depth - 1, board, search_state);
                    if (score > alpha && score < beta) {
                        score = -negamax(-beta, -alpha, depth - 1, board, search_state);
                    }
                }
            }

            /* decrement the ply & pop the move from the board. */
            --search_state->ply;
            pop_move(board);

            if (score > best_score) {
                best_score = score;
                best_move = mv;

                /* Update the pv table for 'display'. */
                search_state->pv_table[search_state->ply][0] = mv;
                memcpy(
                        &search_state->pv_table[search_state->ply][1],
                        &search_state->pv_table[search_state->ply + 1][0],
                        search_state->pv_length[search_state->ply + 1] * sizeof(move_t)
                );
                search_state->pv_length[search_state->ply] = search_state->pv_length[search_state->ply + 1] + 1;

                if (score >= beta) { /* beta cut-off. */
                    search_state->killer_moves[1][search_state->ply] =
                            search_state->killer_moves[0][search_state->ply];
                    search_state->killer_moves[0][search_state->ply] = best_move;

                    tt_save(board->hash, tt_beta, depth, best_score);
                    return best_score;
                }
                if (score > alpha) {
                    alpha = score;
                }
            }
            searched_first_move = true;
        } else {
            /* pop the invalid move from the board, e.g. moving into check, castling through check etc. */
            pop_move(board);
        }
    }

    if (!searched_first_move) { /* Couldn't search a single move, check for checkmate or stalemate. */
        if (isin_check) return -MATE_VALUE + search_state->ply;
        else return 0;
    }

    if (best_score > alpha) {
        tt_save(board->hash, tt_exact, depth, best_score);
    } else {
        tt_save(board->hash, tt_alpha, depth, alpha);
    }

    return best_score;
}

static move_t inline_always find_best_move(board_t* board, int depth, const volatile int* cancel_flag) {
    search_state_t search_state;
    init_search(&search_state);

    int alpha = -INF;
    int beta = INF;
    int score;

    for (int i=1; i<=depth; i++) {
        if (*cancel_flag) break;
        search_state.follow_pv = true;
        score = negamax(alpha, beta, i, board, &search_state);
        if (score <= alpha || score >= beta) {
            /* If we fall outside the search_state window, widen the search_state *and*
             * re-search_state the same depth, don't 'continue' to the next depth. */
            alpha = -INF;
            beta = INF;
            score = negamax(alpha, beta, i, board, &search_state);
        }
        alpha = score - ASPIRATION_WINDOW;
        beta = score + ASPIRATION_WINDOW;
    }

    return search_state.pv_table[0][0];
}

#endif
