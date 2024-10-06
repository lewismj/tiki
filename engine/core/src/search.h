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
    uint64_t nodes_visited;
    uint32_t best_move;
    uint8_t ply;
} search_state_t;


static inline_always int score_move(move_t move, board_t* board, search_state_t* search_state) {
    if (get_capture_flag(move)) {
        square target = get_target_square(move);
        piece piece = get_piece_moved(move);
        int* pieces = (board->side == white) ? black_pieces : white_pieces;
        for (int i = 0; i < 6; i++) {
            if (is_bit_set(&board->pieces[pieces[i]], target)) {
                return search_const.mvv_lva[piece][pieces[i]];
            }
        }
    } else {
        /*
         * Quiet moves, how do we score a quiet move? It must be in some scale
         * that corresponds to the MVV-LVA values ?
         */
    }

    return 0; // Non-capture moves
}


static inline_always void sort_move_buffer(move_buffer_t* buffer, board_t* board, search_state_t* search_state) {
    /* Given general size of the move list, bubble sort should be preferred over quick sort ? */
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
    generate_moves(board, &buffer);
    sort_move_buffer(&buffer, board, search_state);

    for (int i=0; i< buffer.index; i++) {
        /* Just look at captures in quiescence? */
        if (get_capture_flag(buffer.moves[i])) {
            search_state->ply++;
            /* make move will return false if player is left in check. */
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
    move_t best_move_so_far;

    /* transposition table: if the evaluation is in the tt, return it. */
    int cached_score = tt_probe(board->hash, depth, alpha, beta,&best_move_so_far);
    if (cached_score != TT_NOT_FOUND) return cached_score;

    /* quiescence. */
    if (depth == 0) return quiescence(alpha, beta, board, search_state);

    search_state->nodes_visited++;

    bool isin_check = board->side == white ?
                        is_square_attacked_black(board, trailing_zero_count(board->pieces[K]))
                      : is_square_attacked_white(board, trailing_zero_count(board->pieces[k]));

    bool legal_moves = false;

    /* null move pruning. */
    if (depth >= NULL_MOVE_DEPTH_BOUND && !isin_check && search_state->ply) {
        /*
         * Could have a special NULL_MOVE and have make_move/pop_move handle it?
         * We end up with extra if statements for every make_move, pop_move.
         */
    }

    move_buffer_t buffer;
    generate_moves(board, &buffer);

    /* move ordering. */
    sort_move_buffer(&buffer, board, search_state);

    for (int i=0; i<buffer.index; i++) {
        if (make_move(board,buffer.moves[i])) { /* legal move. */
            legal_moves = true;
            search_state->ply++;
            int score = -negamax(-beta, -alpha, depth - 1, board, search_state);
            search_state->ply--;
            pop_move(board);

            if (score > alpha) {
                alpha = score;
                best_move_so_far = buffer.moves[i];
                if (search_state->ply == 0) search_state->best_move = best_move_so_far;
                if (score >= beta) {
                    tt_save(board->hash, tt_beta, buffer.moves[i], depth, score);
                    return beta;
                }
                tt_save(board->hash, tt_alpha, buffer.moves[i], depth, score);
            }

        } else {
            /* Generate move resulted in an invalid position, in our case leaving player in check. */
            pop_move(board);
        }
    }

    if (!legal_moves) { /* no legal moves implies checkmate or stalemate. */
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
    search.nodes_visited = 0;
    search.ply = 0;
    search.best_move = 0;
    int score = negamax(-90000, 90000, depth, board, &search);
    printf("score: %d\n", score);
    printf("nodes visited: %ld, depth: %d\n", search.nodes_visited, depth);
    return search.best_move;
}



#endif
