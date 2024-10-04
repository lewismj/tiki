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


typedef struct align {
    uint64_t repetition_table[1024];
    unsigned long nodes_visited;
    move_t best_move;
    int history_moves[12][64];
    move_t killer_moves[2][64];
    int repetition_index;
    int ply;
} search_state_t;



static inline_always int score_move(move_t move, board_t* board, search_state_t* search_state) {
    square target_sq = get_target_square(move);
    piece source_piece = get_move_piece(move);
    if (get_capture_flag(move)) {
        piece target_piece = none;
        int* pieces = board->side == white ? black_pieces : white_pieces;
        for (int i=0; i<6; i++) {
            if (is_bit_set(&board->pieces[pieces[i]], target_sq)) {
                target_piece = pieces[i];
                break;
            }
        }
        return search_const.mvv_lva[source_piece][target_piece] + 10000;
    } else {
        return 0;
//        if (search_state->killer_moves[0][search_state->ply] == move) return 9000;
//        else if (search_state->killer_moves[1][search_state->ply] == move) return 8000;
//        else
//            return search_state->history_moves[get_move_piece(move)][get_target_square(move)];
    }
}

static inline_always void sort_move_buffer(move_buffer_t* buffer, board_t* board, search_state_t* search_state) {
    /*
     * Could implement some heuristic, we assume for relatively small number of average moves.
     * This bubble sort is probably going to be the fastest method, but we could choose
     * different sort if the number of moves > some threshold.
     */
    int scores[buffer->index];
    for (int i=0; i<buffer->index; i++) scores[i] = score_move(buffer->moves[i], board, search_state);

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

static inline_always int quiescence(int alpha,
                                    int beta,
                                    board_t* board,
                                    search_state_t* search_state) {
    search_state->nodes_visited++;

    int eval = evaluation(board);
    if (eval >= beta) return beta;
    if (eval > alpha) alpha = eval;

    move_buffer_t buffer;
    buffer.index = 0;
    generate_moves(board, &buffer);
    sort_move_buffer(&buffer, board, search_state);

    for (int i=0; i< buffer.index; i++) {
        if (get_capture_flag(buffer.moves[i])) {
            search_state->ply++;
            if (make_move(board, buffer.moves[i])) {
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


static inline_always int negamax(int alpha,
                                 int beta,
                                 int depth,
                                 board_t* board,
                                 search_state_t* search_state) {
    if (depth == 0) return evaluation(board);
    search_state->nodes_visited++;
    int current_alpha = alpha;
    move_t best_move_so_far;

    align move_buffer_t buffer;
    buffer.index = 0;
    generate_moves(board, &buffer);
    sort_move_buffer(&buffer, board, search_state);

    for (int i=0; i<buffer.index; i++) {
        if (make_move(board,buffer.moves[i])) {
            search_state->ply++;
            int score = -negamax(-beta, -alpha, depth - 1, board, search_state);
            search_state->ply--;
            pop_move(board);
            if (score >= beta) {
                search_state->killer_moves[1][search_state->ply] = search_state->killer_moves[0][search_state->ply];
                search_state->killer_moves[0][search_state->ply] = buffer.moves[i];
                return beta;
            }
            if (score > alpha) {
                piece move_piece = get_move_piece(buffer.moves[i]);
                square target_sq = get_target_square(buffer.moves[i]);
                search_state->history_moves[move_piece][target_sq] += depth;
                alpha = score;
                if (search_state->ply == 0) best_move_so_far = buffer.moves[i];
            }
        } else {
            pop_move(board);
        }
    }
    if (current_alpha != alpha) search_state->best_move = best_move_so_far;

    return alpha;
}


static move_t inline_always find_best_move( board_t* board,
                                            int depth,
                                            volatile int* cancel_flag) {
    search_state_t search;
    search.nodes_visited = 0;
    search.ply = 0;
    search.best_move = 0;
    int score = negamax(-50000, 50000, depth, board, &search);
    printf("nodes visited: %ld, depth: %d\n", search.nodes_visited, depth);
    return search.best_move;
}



#endif
