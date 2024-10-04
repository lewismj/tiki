#ifndef TIKI_SEARCH_H
#define TIKI_SEARCH_H

#include <stdio.h>

#include "types.h"
#include "board.h"
#include "move_generator.h"
#include "evaluation.h"
#include "search_constants.h"
#include "search.h"
#include "sort.h"


typedef struct align {
    move_t best_move;
    move_t killer_moves[2][64];
    int history_moves[12][64];
    long nodes_visited;
    int ply;
} search_state_t ;


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
        if (search_state->killer_moves[0][search_state->ply] == move) return 9000;
        else if (search_state->killer_moves[1][search_state->ply] == move) return 8000;
        else return search_state->history_moves[source_piece][target_sq];

    }
    return 0;
}

static inline_always void sort_move_buffer(move_buffer_t* buffer, board_t* board, search_state_t* search_state) {
    int scores[buffer->index];
    for (int i=0; i<buffer->index; i++) {
        scores[i] = score_move(buffer->moves[i], board, search_state);
    }
    sort_moves_by_scores(buffer->moves, scores, buffer->index);
}

static inline_always int quiescence(int alpha,
                                    int beta,
                                    board_t* board,
                                    search_state_t* search_state) {
    search_state->nodes_visited++;
    int eval = evaluation(board);

    if (eval >= beta) return beta;
    if (eval > alpha) alpha = eval;

    move_buffer_t move_buffer;
    move_buffer.index = 0;
    generate_moves(board, &move_buffer);
    sort_move_buffer(&move_buffer, board, search_state);

    for (int i=0; i < move_buffer.index; i++) {
        
    }

}


/* negamax0 no optimisations, no quiescence, no move ordering etc. */
static inline_always int negamax0(int alpha,
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
    for (int i=0; i<buffer.index; i++) {
        if (make_move(board,buffer.moves[i])) {
            search_state->ply++;
            int score = -negamax0(-beta, -alpha, depth-1, board, search_state);
            search_state->ply--;
            pop_move(board);
            if (score >= beta) {
                return beta;
            }
            if (score > alpha) {
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
    int score = negamax0(-50000, 50000, depth, board, &search);
    return search.best_move;
}



#endif
