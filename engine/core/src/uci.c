#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "uci.h"
#include "attack_mask.h"
#include "zobrist_key.h"
#include "transposition.h"
#include "search_state.h"
#include "search.h"
#include "limits.h"
#include "uci_input.h"
#include "bench.h"
#include "version.h"


#define startpos "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"


/* Will be invoked on atexit, no need to invoke directly. */
void on_shutdown() {
    free_transposition_table();
}

/* Call once at program start (main), registers on_shutdown with atexit. */
void on_startup() {
    init_zobrist_key();
    init_attack_table();
    init_lmr_table();
    init_transposition_table(256);
    atexit(on_shutdown);
}


void reset_board(board_t* board) {
    memset(board->pieces, 0, 12 * sizeof(bitboard));
    memset(board->occupancy, 0, 3 * sizeof(bitboard));
    board->en_passant = none_sq;
    board->castle_flag = 0;
    board->side = white;
    board->fifty_move = 0;
    board->stack_ptr = 0;
    board->hash = 0ULL;
}

void parse_fen(const char* fen, board_t* board) {
    int rank = 0;
    int file = 0;
    const char* ptr = fen;

    reset_board(board);

    /* parse pieces.        */
    while (*ptr && *ptr != ' ') {
        if (isdigit(*ptr)) {
            file += *ptr - '0';
        } else if (*ptr == '/') {
            rank++;
            file=0;
        } else if (file <8) {
            int piece = char_to_piece[*ptr];
            set_bit(&board->pieces[piece], rank * 8 + file);
            board->occupancy[piece < p ? white : black] |= board->pieces[piece];
            file++;
        }
        ptr++;
    }
    /* side to move.        */
    ptr++;
    board->side = *ptr == 'w' ? white : black;
    ptr+=2; /* skip to castling flag. */

    /* castling rights.     */
    while (*ptr != ' ') {
        switch (*ptr) {
            case 'K': board->castle_flag |= white_king_side; break;
            case 'Q': board->castle_flag |= white_queen_side; break;
            case 'k': board->castle_flag |= black_king_side; break;
            case 'q': board->castle_flag |= black_queen_side; break;
            case '-': break;
        }
        ptr++;
    }

    /* en-passant square.   */
    ptr++;
    if (*ptr != '-') {
        int ep_file = ptr[0] - 'a';
        int ep_rank = 8 - (ptr[1] - '0');
        board->en_passant = ep_rank * 8 + ep_file;
        ptr++;
    }
    else {
        board->en_passant = none_sq;
    }
    ptr++;

    /* half-move & full-move counters. Don't use 'strtol' engine shouldn't validate, GUI clients do. */
    board->fifty_move = atoi(ptr); ptr++;

    /* Skip the full move counter. */
    /*  while (*ptr++ != ' ') ;
        board->fifty_move = atoi(ptr); */

    board->occupancy[both] |= board->occupancy[white];
    board->occupancy[both] |= board->occupancy[black];

    recalculate_hash(board);
}

move_t parse_move(const char* input, board_t* board) {
    /* Parse the move and validate the move. */
    move_buffer_t buffer;
    generate_moves(board, &buffer);

    square source_sq = (input[0] - 'a') + (8 - (input[1] - '0')) * 8;
    square target_sq = (input[2] - 'a') + (8 - (input[3] - '0')) * 8;

    for (int i=0; i < buffer.index; i++ ) {
        move_t mv = buffer.moves[i];

        if (source_sq == get_source_square(mv) && target_sq == get_target_square(mv)) {
            piece promoted = get_promoted_piece(mv);
            if (promoted) {
               if ((promoted==Q || promoted==q) && (input[4]=='q' || input[4] =='Q')) return mv;
               if ((promoted==R || promoted==r) && (input[4]=='r' || input[4] =='R')) return mv;
               if ((promoted==B || promoted==b) && (input[4]=='b' || input[4] =='B')) return mv;
               if ((promoted==N || promoted==n) && (input[4]=='n' || input[4] =='N')) return mv;
               continue; /* Check that move doesn't contain incorrect promotion, e.g. d2d4P */
            }
            return mv;
        }
    }

    return 0; /* No move found. */
}


void parse_position(const char* position, board_t* board, search_state_t* search_state) {
    reset_board(board);
    search_state->repetition_index = 0; /* Each 'position' command sends the whole game. */
    const char *ptr = position +9;
    if (strncmp(ptr, "startpos",8) == 0) {
        parse_fen(startpos, board);
    } else {
        ptr = strstr(ptr, "fen");
        if (ptr == NULL) {
            parse_fen(startpos, board);
        } else {
            ptr += 4;
            parse_fen(ptr, board);
        }
    }
    ptr = strstr(position, "moves");
    if (ptr != NULL) {
        ptr +=6;
        while (*ptr) {
            move_t move = parse_move(ptr, board);
            if (move == 0) break;
            search_state->repetition_check[search_state->repetition_index++] = board->hash;
            make_move(board, move);
            /* Positions before an irreversible move (pawn move or capture) can't repeat. */
            if (board->fifty_move == 0) search_state->repetition_index = 0;
            /* Keep room for the search path, only the last 100 plies matter for repetition. */
            if (search_state->repetition_index >= MAX_MOVES - MAX_PLY - 1) {
                memmove(search_state->repetition_check,
                        search_state->repetition_check + search_state->repetition_index - 100,
                        100 * sizeof(search_state->repetition_check[0]));
                search_state->repetition_index = 100;
            }
            while (*ptr && *ptr != ' ') ptr++;
            if (*ptr == ' ') ptr++; /* Don't step past the terminator after the last move. */
        }
    }


    /* Root position, the search stores each child at the next index. */
    search_state->repetition_check[search_state->repetition_index] = board->hash;

    board->stack_ptr = 0; /* Reset the stack pointer for undo state. */
}

static bool is_token(const char* command, const char* token) {
    size_t n = strlen(token);
    return strncmp(command, token, n) == 0 && (command[n] == '\0' || isspace((unsigned char) command[n]));
}

uci_command_t parse_uci_command(const char* command) {
    /* Whole token match, so that 'ucinewgame' isn't treated as 'uci'. */
    if (is_token(command, "uci")) return uci_cmd_uci;
    if (is_token(command, "ucinewgame")) return uci_cmd_ucinewgame;
    if (is_token(command, "isready")) return uci_cmd_isready;
    if (is_token(command, "position")) return uci_cmd_position;
    if (is_token(command, "go")) return uci_cmd_go;
    if (is_token(command, "stop")) return uci_cmd_stop;
    if (is_token(command, "quit")) return uci_cmd_quit;
    if (is_token(command, "bench")) return uci_cmd_bench;
    return uci_cmd_unknown;
}

void parse_go_limits(const char* command, const board_t* board, limits_t* limits) {
    reset_time_control(limits);

    char* arg;

    if ((arg = strstr(command, "winc")) && board->side == white)
        limits->increment = atoi(arg+5);

    if ((arg = strstr(command, "binc")) && board->side == black)
        limits->increment = atoi(arg+5);

    if ((arg = strstr(command, "wtime")) && board->side == white)
        limits->time = atoi(arg+6);

    if ((arg = strstr(command, "btime")) && board->side == black)
        limits->time = atoi(arg+6);

    if ((arg= strstr(command, "movetime")))
        limits->move_time = atoi(arg+9);

    if ((arg = strstr(command, "movestogo")))
        limits->moves_to_go = atoi(arg+10);

    limits->depth = MAX_SEARCH_DEPTH;
    if ((arg = strstr(command, "depth"))) {
        limits->time_set = false;
        limits->time = -1;
        limits->depth = atoi(arg + 6);
    }

    if (limits->move_time != -1) {
        limits->time = limits->move_time;
        limits->moves_to_go = 1;
    }

    /* Search until 'stop', ignore any clock values. */
    if (strstr(command, "infinite")) {
        limits->infinite = true;
        limits->time = -1;
        limits->depth = MAX_SEARCH_DEPTH;
    }

    limits->start_time = clock_time_ms();
    if (limits->time != -1) {
        limits->time_set= true;
        limits->time /= limits->moves_to_go;
        limits->time -= 55;
        if (limits->time <0) {
            limits->time =0;
            limits->increment -= 55;
            if (limits->increment < 0) limits->increment = 55;
        }
        limits->stop_time = limits->start_time + limits->time + limits->increment;

        /* Don't start a new depth after half the allocation, 'movetime' can use all of it. */
        limits->soft_stop_time = limits->move_time != -1 ?
                                 limits->stop_time :
                                 limits->start_time + (limits->stop_time - limits->start_time) / 2;
    }
}

void parse_go(const char* command, board_t* board, search_state_t* search_state, limits_t* limits) {
    clear_pv_table(search_state); /* Preserves the game history from 'position' for repetition checks. */
    tt_new_search(); /* The TT is kept between moves, it's only cleared on 'ucinewgame'. */
    parse_go_limits(command, board, limits);
    input_search_started(limits); /* After the limits are reset, a queued 'stop' may set the flag. */

    move_t mv = find_best_move(board, search_state, limits);

    /* UCI: with 'go infinite' bestmove must not be sent before 'stop', even if the search completed. */
    if (limits->infinite) input_wait_for_stop(limits);

    input_search_finished();
    print_bestmove(mv);
}

void new_game(board_t* board, search_state_t* search_state) {
    tt_clear();
    init_search_state(search_state);
    parse_position("position startpos", board, search_state);
}

bool handle_uci_command(const char* command, board_t* board, search_state_t* search_state, limits_t* limits) {
    switch (parse_uci_command(command)) {
        case uci_cmd_uci:
            printf("id name %s %s\n", ENGINE_NAME, ENGINE_VERSION);
            printf("id author %s\n", ENGINE_AUTHOR);
            printf("uciok\n");
            break;
        case uci_cmd_isready:
            printf("readyok\n");
            break;
        case uci_cmd_ucinewgame:
            new_game(board, search_state);
            break;
        case uci_cmd_position:
            parse_position(command, board, search_state);
            break;
        case uci_cmd_go:
            parse_go(command, board, search_state, limits);
            break;
        case uci_cmd_stop:
            /* No search running, nothing to stop. */
            break;
        case uci_cmd_quit:
            limits->stop_engine_flag = true;
            break;
        case uci_cmd_bench: {
            bench(atoi(command + 5)); /* 0 (no depth given) is the default depth. */
            new_game(board, search_state); /* Bench leaves its last position on the board. */
            break;
        }
        default:
            printf("info string Unknown command: %s\n", command);
    }
    /* 'quit' may also have been received while searching. */
    return !limits->stop_engine_flag;
}


void uci_loop(FILE* in, board_t* board, search_state_t* search_state, limits_t* limits) {
    input_start(in);

    char* command;
    while ((command = input_pop()) != NULL) {
        bool running = handle_uci_command(command, board, search_state, limits);
        free(command);
        if (!running) break;
    }

    input_join();
}

void uci_main() {
    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);
    limits_t limits;
    reset_time_control(&limits);
    parse_position("position startpos", &board, &search_state);

    uci_loop(stdin, &board, &search_state, &limits);
}
