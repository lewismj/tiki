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
#include "version.h"


#define startpos "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"


/* Will be invoked on atexit, no need to invoke directly. */
void on_shutdown() {
    free_transposition_table();
}

void on_startup() {
    init_zobrist_key();
    init_attack_table();
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
            while (*ptr && *ptr != ' ') ptr++;
            ptr++;
        }
    }


    board->stack_ptr = 0; /* Reset the stack pointer for undo state. */
}

void parse_go(const char* position, board_t* board, search_state_t* search_state, limits_t* limits) {
    init_search_state(search_state);
    reset_time_control(limits);
    tt_clear();

    char* arg;

    if ((arg = strstr(position, "winc")) && board->side == white)
        limits->increment = atoi(arg+5);

    if ((arg = strstr(position, "binc")) && board->side == black)
        limits->increment = atoi(arg+5);

    if ((arg = strstr(position, "wtime")) && board->side == white)
        limits->time = atoi(arg+6);

    if ((arg = strstr(position, "btime")) && board->side == black)
        limits->time = atoi(arg+6);

    if ((arg= strstr(position, "movetime")))
        limits->move_time = atoi(arg+9);

    if ((arg = strstr(position, "movestogo")))
        limits->moves_to_go = atoi(arg+10);

    limits->depth=32;
    if ((arg = strstr(position, "depth"))) {
        limits->time_set = false;
        limits->time = -1;
        limits->depth = atoi(arg + 6);
    }

    if (limits->move_time != -1) {
        limits->time = limits->move_time;
        limits->moves_to_go = 1;
    }

    limits->start_time = clock_time_ms();
    if (limits->time != -1) {
        limits->time_set= true;
        limits->time /= limits->moves_to_go;
        if (limits->time <0) {
            limits->time =0;
            if (limits->increment < 0) limits->increment = 1;
        }
        limits->stop_time = limits->start_time + limits->time + limits->increment;
    }

    move_t mv = find_best_move(board, search_state, limits);
    print(mv);
}


void uci_main() {
    on_startup();

    board_t board;
    search_state_t search_state;
    init_search_state(&search_state);
    limits_t limits;
    reset_time_control(&limits);

    char command[100000];

    while (fgets(command, sizeof(command), stdin) && !limits.stop_engine_flag) {
        command[strcspn(command, "\n")] = 0;
        if (strncmp(command, "uci", 3) == 0) {
            printf("id name %s %s\n", ENGINE_NAME, ENGINE_VERSION);
            printf("id author %s\n", ENGINE_AUTHOR);
            printf("uciok\n");
        } else if (strncmp(command, "isready", 7) == 0) {
            printf("readyok\n");
            continue;
        } else if (strncmp(command, "position", 8) == 0) {
            parse_position(command, &board, &search_state);
        } else if (strncmp(command, "go", 2) == 0) {
            parse_go(command, &board, &search_state, &limits);
        } else if (strncmp(command, "ucinewgame", 10) == 0) {
            parse_position("position startpos", &board, &search_state);
        } else if (strncmp(command, "quit", 4) == 0 || strncmp(command, "stop", 4) == 0)  {
            break;
        } else {
            printf("info string Unknown command: %s\n", command);
        }
    }
}