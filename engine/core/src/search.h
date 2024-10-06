#ifndef TIKI_SEARCH_H
#define TIKI_SEARCH_H

#include <stdio.h>
#include <assert.h>
#include <execinfo.h>
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
    uint32_t pv_table[MAX_PLY][MAX_PLY];
    int32_t history_moves[12][64];
    uint32_t killer_moves[2][MAX_PLY];
    uint8_t pv_length[MAX_PLY];
    uint64_t nodes_visited;
    uint32_t best_move;
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
     */
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

    search_state->nodes_visited++;

    bool isin_check = board->side == white ?
                        is_square_attacked_black(board, trailing_zero_count(board->pieces[K]))
                      : is_square_attacked_white(board, trailing_zero_count(board->pieces[k]));

    /* increment depth if position is in check. */
    if (isin_check) ++depth;

    int legal_moves = 0;

#ifdef FEATURE_NULL_MOVE_PRUNING
    if (depth > NULL_MOVE_DEPTH_BOUND && !isin_check && search_state->ply) {
        make_null_move(board);
        search_state->ply++;
        int score = -negamax(-beta, -beta + 1, depth - NULL_MOVE_DEPTH_BOUND - 1, board, search_state);
        search_state->ply--;
        pop_null_move(board);
        if (score >= beta) return beta;
    }
#endif

    move_buffer_t buffer;
    generate_moves(board, &buffer);
    sort_move_buffer(&buffer, board, search_state);     /* move ordering. */

    for (int i=0; i<buffer.index; i++) {
        move_t mv = buffer.moves[i];
        if (make_move(board,mv)) {
            ++legal_moves;

#ifdef FEATURE_LATE_MOVE_REDUCTION
            search_state->ply++;
            int score;

            if (legal_moves == 1) { /* evaluate first ordered move at full depth. */
                score = -negamax(-beta, -alpha, depth - 1, board, search_state);
            }
            else {
                if (depth > LMR_DEPTH_BOUND &&          /* we are at sufficient depth.          */
                    !isin_check &&                      /* we are not in check.                 */
                    !get_capture_flag(mv) &&            /* move is a quiet move, not capture.   */
                    !get_promoted_piece(mv)             /* move is not pawn promotion.          */
                ) {
                    score = -negamax(-alpha-1, -alpha, depth  - 2, board, search_state);
                } else {
                    score = alpha +1;
                }

                if (score > alpha) {
                    score = -negamax(-alpha-1, -alpha, depth-1, board, search_state);
                    if ((score >alpha) && (score <beta)) score = -negamax(-beta, -alpha, depth-1, board, search_state);
                }
            }
#else
            search_state->ply++;
            int score = -negamax(-beta, -alpha, depth - 1, board, search_state);
#endif
            search_state->ply--;
            pop_move(board);

            if (score > alpha) {
                if (!get_capture_flag(mv)) {
                    search_state->history_moves[get_piece_moved(mv)][get_target_square(mv)] += depth;
                }
                alpha = score;
                best_move_so_far = mv;
                if (search_state->ply == 0) search_state->best_move = best_move_so_far;
                if (score >= beta) {
                    if (!get_capture_flag(mv)) {
                        search_state->killer_moves[1][search_state->ply] = search_state->killer_moves[0][search_state->ply];
                        search_state->killer_moves[0][search_state->ply] = mv;
                    }
                    tt_save(board->hash, tt_beta, mv, depth, score);
                    return beta;
                }
                tt_save(board->hash, tt_alpha, mv, depth, score);
            }

        } else {
            /* Generate move resulted in an invalid position, in our case leaving player in check. */
            pop_move(board);
        }
    }

    if (!legal_moves) { /* No legal moves implies checkmate or stalemate. */
        return isin_check ? 0 : -MATE_VALUE - search_state->ply;
    }

    search_state->best_move = best_move_so_far;
    tt_save(board->hash, tt_alpha, best_move_so_far, depth, alpha);
    return alpha;
}


static move_t inline_always find_best_move(board_t* board,
                                           int depth,
                                           volatile int* cancel_flag) {
    search_state_t search;

    /* Initialize search parameters. */
    search.nodes_visited = 0;
    search.best_move = NULL_MOVE;
    search.ply = 0;
    memset(search.history_moves, 0, sizeof (search.history_moves));
    memset(search.killer_moves, 0, sizeof (search.killer_moves));


    int score = negamax(-50000, 50000, depth, board, &search);
    printf("score: %d\n", score);
    printf("nodes visited: %ld, depth: %d\n", search.nodes_visited, depth);
    return search.best_move;
}

#endif
