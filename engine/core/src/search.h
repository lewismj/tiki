#ifndef TIKI_SEARCH_H
#define TIKI_SEARCH_H

#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>

#include "types.h"
#include "board.h"
#include "move_generator.h"
#include "evaluation.h"
#include "search_constants.h"
#include "search.h"
#include "transposition.h"


typedef struct align {
    /*
     * pv_table, use to store the principal variation, we use 64.64
     * array rather than a vector (triangular matrix), the indexing
     * is simplified using a matrix.
     */
    uint32_t pv_table[MAX_PLY][MAX_PLY];
    int32_t history_moves[12][64];
    uint32_t killer_moves[2][MAX_PLY];
    uint8_t pv_length[MAX_PLY];
    uint64_t nodes_visited;
    uint8_t ply;
    bool follow_pv;
    bool score_pv;
} search_state_t;


static inline_always int score_move(move_t move, board_t* board, search_state_t* search_state) {
    /*
     * We require, in order of move type,
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
        else return search_state->history_moves[get_piece_moved(move)][get_target_square(move)];
    }
}


static inline_always void sort_move_buffer(move_buffer_t* buffer, board_t* board, search_state_t* search_state) {
    /* Given general size of the move list, bubble sort should be preferred over quick sort ? */
    int scores[buffer->index];
    for (int i=0; i<buffer->index; i++) {
        scores[i] = score_move(buffer->moves[i], board, search_state);
    }

    for (int i = 0; i < buffer->index; i++) {
        for (int j = i + 1; j < buffer->index; j++) {
            if (scores[i] < scores[j]) {
                int temp_score = scores[i];
                scores[i] = scores[j];
                scores[j] = temp_score;
                move_t temp_move = buffer->moves[i];
                buffer->moves[i] = buffer->moves[j];
                buffer->moves[j] = temp_move;
            }
        }
    }
}

static inline_hint int quiescence(int alpha,
                                    int beta,
                                    board_t* board,
                                    search_state_t* search_state) {
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

static inline_hint int negamax(int alpha,
                                 int beta,
                                 int depth,
                                 board_t* board,
                                 search_state_t* search_state) {
    move_t best_move_so_far;

    /* transposition table: if the evaluation is in the tt, return it. */
    int cached_eval = tt_probe(board->hash, depth, alpha, beta, &best_move_so_far);
    if (cached_eval != TT_NOT_FOUND) return cached_eval;

    /* quiescence. */
    if (depth == 0) return quiescence(alpha, beta, board, search_state);


    search_state->pv_length[search_state->ply] = search_state->ply;
    search_state->nodes_visited++;

    bool isin_check = board->side == white ?
                      is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K]))
                      : is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));

    /* increment depth if position is in check. */
    if (isin_check) ++depth;

    int legal_moves = 0;

    /* Null move pruning. */
    if (depth > NULL_MOVE_DEPTH_BOUND && !isin_check && search_state->ply) {
        make_null_move(board);
        search_state->ply++;
        int score = -negamax(-beta, -beta + 1, depth - NULL_MOVE_DEPTH_BOUND - 1, board, search_state);
        search_state->ply--;
        pop_null_move(board);
        if (score >= beta) return beta;
    }

    move_buffer_t buffer;
    generate_moves(board, &buffer);
    if (search_state->follow_pv) set_follow_pv_flags(&buffer, search_state);

    sort_move_buffer(&buffer, board, search_state);     /* move ordering. */

    for (int i=0; i<buffer.index; i++) {
        move_t mv = buffer.moves[i];
        if (make_move(board,mv)) {
            ++legal_moves;
            search_state->ply++;

            int score;
            if (legal_moves == 1) { /* evaluate first ordered move at full depth. */
                score = -negamax(-beta, -alpha, depth - 1, board, search_state);
            }
            else {
                /* Late move reduction & PVS. */
                if (depth > LMR_DEPTH_BOUND &&      /* we are at sufficient depth.          */
                    !isin_check &&                  /* we are not in check.                 */
                    !get_capture_flag(mv) &&    /* move is a quiet move, not capture.   */
                    !get_promoted_piece(mv)     /* move is not pawn promotion.          */
                ) {
                    score = -negamax(-alpha-1, -alpha, depth  - 2, board, search_state);
                } else {
                    score = alpha +1;
                }

                if (score > alpha) {
                    score = -negamax(-alpha-1, -alpha, depth-1, board, search_state);
                    if ((score >alpha) && (score <beta)) {
                        score = -negamax(-beta, -alpha, depth - 1, board, search_state);
                    }
                }
            }

            search_state->ply--;
            pop_move(board);

            if (score > alpha) {
                if (!get_capture_flag(mv)) {
                    search_state->history_moves[get_piece_moved(mv)][get_target_square(mv)] += depth;
                }
                alpha = score;
                best_move_so_far = mv;

                int ply = search_state->ply;
                search_state->pv_table[ply][ply] = mv;
                for (int j=ply+1; j<search_state->pv_length[ply+1];j++)
                    search_state->pv_table[ply][j] = search_state->pv_table[ply+1][j];
                search_state->pv_length[ply] = search_state->pv_length[ply+1];

                if (score >= beta) {
                    if (!get_capture_flag(mv)) {
                        search_state->killer_moves[1][search_state->ply] = search_state->killer_moves[0][search_state->ply];
                        search_state->killer_moves[0][search_state->ply] = mv;
                    }
                    tt_save(board->hash, tt_beta, mv, depth, score);
                    return beta;
                }
                else {
                    tt_save(board->hash, tt_exact, mv, depth, score);
                }
            }

        } else {
            /* Generate move resulted in an invalid position, in our case leaving player in check. */
            pop_move(board);
        }
    }

    if (!legal_moves) { /* No legal moves implies checkmate or stalemate. */
        return isin_check ?  -MATE_VALUE - search_state->ply : 0;
    }

    tt_save(board->hash, tt_alpha, best_move_so_far, depth, alpha);
    return alpha;
}


static move_t inline_always find_best_move(board_t* board,
                                           int depth,
                                           bool show_pv,
                                           const volatile int* cancel_flag) {
    search_state_t search;

    /* Initialize search parameters. */
    search.nodes_visited = 0;
    search.ply = 0;
    search.score_pv = false;
    search.follow_pv = false;
    memset(search.pv_table, 0, sizeof(search.pv_table));
    memset(search.pv_length, 0, sizeof(search.pv_length));
    memset(search.history_moves, 0, sizeof (search.history_moves));
    memset(search.killer_moves, 0, sizeof (search.killer_moves));

    /* Iterative deepening. */
    int alpha = -INF;
    int beta = INF;

    int score;
    for (int i=1; i<=depth; i++) {
        if (*cancel_flag) break;
        search.follow_pv = true;
        score = negamax(alpha, beta, i, board, &search);
        if (score <= alpha || score >= beta) {
            /* If we fall outside the search window, widen the search *and*
             * re-search the same depth, don't 'continue' to the next depth. */
            alpha = -INF;
            beta = INF;
            printf("retry depth: %d\n",i);
            score = negamax(alpha, beta, i, board, &search);
        }
        alpha = score - ASPIRATION_WINDOW;
        beta = score + ASPIRATION_WINDOW;
    }

    printf("nodes evaluated:%ld\n",search.nodes_visited);
    printf("score=%d\n",score);
    printf("pv length: %d\n",search.pv_length[0]);
    return search.pv_table[0][0];
}

#endif
