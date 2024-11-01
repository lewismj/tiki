#ifndef TIKI_SEARCH_H
#define TIKI_SEARCH_H

#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>

#include "types.h"
#include "board.h"
#include "move_generator.h"
#include "evaluation.h"
#include "search_constants.h"
#include "search_state.h"
#include "transposition.h"
#include "limits.h"


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
        return 20000;
    }

    if (get_capture_flag(move)) {
        square target = get_target_square(move);
        piece piece = get_piece_moved(move);
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


static inline_always void sort_move_buffer(move_buffer_t* buffer,  board_t* board, search_state_t* search_state) {
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

static inline_always bool poll_stdin() {
    struct pollfd fds;
    fds.fd = fileno(stdin);
    fds.events = POLLIN;
    int ret = poll(&fds, 1, 0);
    return (ret > 0 && (fds.revents & POLLIN)) ? 1 : 0;
}

static inline_always void has_stdin_input(limits_t* limits) {
    char input[256] = {0};
    if (poll_stdin()) {
        limits->stop_search_flag = true;
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = '\0';
            if (strcmp(input, "quit") == 0) {
                limits->stop_engine_flag = true;
            }
        }
    }
}

static inline_always void check_time(limits_t* limits) {
    /* Set flag if out of time & then check stdin. */
    if (limits->time_set && clock_time_ms() > limits->stop_time) {
        limits->stop_search_flag = true;
        has_stdin_input(limits);
    }
}

static inline_hint int quiescence(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits) {
    if ((search_state->nodes & 2048) == 0) check_time(limits);

    search_state->nodes++;

    if (search_state->ply > MAX_PLY-1) return evaluation(board);

    int eval = evaluation(board);
    if (eval >= beta) return beta;
    if (eval > alpha) alpha = eval;

    move_buffer_t buffer;
    generate_moves(board, &buffer);
    sort_move_buffer(&buffer, board, search_state);

    for (int i=0; i< buffer.index; i++) {
        move_t mv = buffer.moves[i];
        if (get_capture_flag(mv)) {
            ++search_state->ply;
            if (make_move(board, mv)) {
                int score = -quiescence(-beta, -alpha, board, search_state, limits);
                --search_state->ply;
                pop_move(board);

                if (limits->stop_search_flag) return 0;

                if (score > alpha) {
                    alpha = score;
                    if (score >= beta) return beta;
                }
            } else {
                --search_state->ply;
                pop_move(board);
            }
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

    if ((search_state->ply>0 && contains_repetition(search_state, board->hash)) || board->fifty_move > 100)
        return 0;

    bool is_pv_node = beta - alpha > 1;
    int cached_eval = tt_probe(board->hash, depth, search_state->ply, alpha, beta);
    if (search_state->ply && !is_pv_node && cached_eval != TT_NOT_FOUND) return cached_eval;

    if ((search_state->nodes & 2048) == 0) check_time(limits);

    if (depth == 0) {
        return quiescence(alpha, beta, board, search_state, limits);
    }

    if (search_state->ply>MAX_PLY-1) return evaluation(board);

    bool in_check = board->side == white ?
                    is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K])) :
                    is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));

    if (in_check) ++depth;

    /* Null move pruning. */
    if (depth >= NULL_MOVE_DEPTH_BOUND && !in_check) {
        make_null_move(board);
        search_state->repetition_check[++search_state->repetition_index] = board->hash;
        ++search_state->ply;
        int score = -negamax(-beta, -beta + 1, depth - 3, board, search_state, limits);
        --search_state->ply;
        --search_state->repetition_index;
        pop_null_move(board);
        if (score >= beta) return beta;
    }

    ++search_state->nodes;

    move_buffer_t buffer;
    generate_moves(board, &buffer);
    if (search_state->follow_pv) set_follow_pv_flags(&buffer, search_state);
    sort_move_buffer(&buffer, board, search_state);

    bool has_legal_moves = false;
    int num_moves_searched = 0;
    int score;
    int best_score = -INF;
    for (int i=0; i < buffer.index; i++) {
        move_t mv = buffer.moves[i];
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

        if (limits->stop_search_flag) return 0;

        if (score > alpha) {
            best_score = score;

            search_state->pv_table[search_state->ply][search_state->ply] = mv;
            for (int j = search_state->ply + 1; j < search_state->pv_length[search_state->ply + 1]; j++) {
                search_state->pv_table[search_state->ply][j] = search_state->pv_table[search_state->ply + 1][j];
            }
            search_state->pv_length[search_state->ply] = search_state->pv_length[search_state->ply + 1];

            if (!is_capture) {
                search_state->history_moves[get_piece_moved(mv)][get_target_square(mv)] += depth;
            }

            alpha = score;

            if (score >= beta) {
                tt_save(board->hash, tt_beta, depth, search_state->ply, score);
                if (!is_capture) {
                    search_state->killer_moves[1][search_state->ply] = search_state->killer_moves[0][search_state->ply];
                    search_state->killer_moves[0][search_state->ply] = mv;
                }
                return beta;
            }
        }
    }

    if (best_score > alpha) {
        tt_save(board->hash, tt_exact, depth, search_state->ply, best_score);
    } else {
        tt_save(board->hash, tt_alpha, depth, search_state->ply, alpha);
    }

    return has_legal_moves ? alpha : in_check ? -MATE_VALUE + search_state->ply : 0;
}


static move_t inline_always find_best_move(board_t* board, search_state_t* search_state, limits_t* limits) {
    int alpha = -INF;
    int beta = INF;
    int score;

    int depth = limits->depth;
    for (int i=1; i<=depth; i++) {
        if (limits->stop_search_flag) break;
        search_state->follow_pv = true;
        score = negamax(alpha, beta, i, board, search_state, limits);
        if (score <= alpha || score >= beta) {
            /* If we fall outside the search_state window, widen the search_state *and*
             * re-search_state the same depth, don't 'continue' to the next depth. */
            alpha = -INF;
            beta = INF;
            score = negamax(alpha, beta, i, board, search_state, limits);
        }
        alpha = score - ASPIRATION_WINDOW;
        beta = score + ASPIRATION_WINDOW;
        if (search_state->pv_length[0])     {
            if (score > -MATE_VALUE && score < -MATE_SCORE) {
                printf("info score mate %d depth %d nodes %lu time %lu pv ",
                       -(score + MATE_VALUE) / 2 - 1,
                       i,
                       search_state->nodes,
                       clock_time_ms() - limits->start_time
                );
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
   }

    move_t mv =search_state->pv_table[0][0];
    return mv;
}


static void inline_always print(move_t mv) {

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
