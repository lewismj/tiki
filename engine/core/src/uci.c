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


void on_shutdown() {
    printf("Free transposition table.\n");
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

void uci_main() {

    board_t board;
    search_state_t search_state;
    const volatile int cancel_flag;


}