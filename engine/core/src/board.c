#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "types.h"
#include "board.h"


static char *piece_to_unicode[12] = {"♙",
                                     "♘",
                                     "♗",
                                     "♖",
                                     "♕",
                                     "♔",
                                     "♟",
                                     "♞",
                                     "♝",
                                     "♜",
                                     "♛",
                                     "♚"};

static char *piece_to_ascii[12] = {"P",
                                     "N",
                                     "B",
                                     "R",
                                     "Q",
                                     "K",
                                     "p",
                                     "n",
                                     "b",
                                     "r",
                                     "q",
                                     "k"};


board_t* new_board() {
    board_t* board = (board_t*) malloc(sizeof(board_t));
    board->en_passant = none_sq;
    board->castle_flag = 0;
    board->side = white;
    board->half_move = 0;
    board->full_move = 0;
    board->stack_ptr = 0;
    return board;
}

void reset_board(board_t* board) {
    memset(board->pieces, 0, 12 * sizeof(bitboard));
    memset(board->occupancy, 0, 3 * sizeof(bitboard));
    board->en_passant = none_sq;
    board->castle_flag = 0;
    board->side = white;
    board->half_move = 0;
    board->full_move = 0;
    board->stack_ptr = 0;
}

void unsafe_parse_fen(const char* fen, board_t* board) {
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

    /*
     * half-move & full-move counters.
     * Don't use 'strtol' engine shouldn't validate, clients do.
     */
    board->half_move = atoi(ptr); ptr++;
    while (*ptr++ != ' ') ;
    board->full_move = atoi(ptr);

    board->occupancy[both] |= board->occupancy[white];
    board->occupancy[both] |= board->occupancy[black];

    recalculate_hash(board);
}

void free_board(board_t* board) {
    if (board != NULL) {
        free(board);
    }
}

void print_board(board_t* board, showable options) {
    if (options & (show|min) ) {
        for (int rank = 0; rank < 8; rank++) {
            for (int file = 0; file < 8; file++) {
                square sq = rank * 8 + file;
                if (file == 0) printf("  %d ", 8 - rank);
                if (!is_bit_set(&board->occupancy[both], sq)) printf(" . ");
                else {
                    for (int piece = P; piece <= k; piece++) {
                        if (is_bit_set(&board->pieces[piece], sq)) printf(" %s ", piece_to_ascii[piece]);
                    }
                }
            }
            printf("\n");
        }
        printf("\n     A  B  C  D  E  F  G  H\n\n");
        if (options & show && ! (options & min)) {
            printf("side\t\t%s\n", board->side == white ? "white" : "black");
            printf("en passant:\t%s\n", square_to_str[board->en_passant]);
            printf("castle rights:\t");
            if (board->castle_flag == 0) {
                printf("none");
            } else {
                if (board->castle_flag & white_king_side) printf("K");
                if (board->castle_flag & white_queen_side) printf("Q");
                if (board->castle_flag & black_king_side) printf("k");
                if (board->castle_flag & black_queen_side) printf("q");
            }
            printf("\n");
            printf("Half move:\t%d\n", board->half_move);
            printf("Full move:\t%d\n", board->full_move);
            printf("\n");
        }
    }

    if (options & hex) {
        printf("hex:\n");
        printf("white:\t\t0x%" PRIx64 "\n", board->occupancy[white]);
        printf("black:\t\t0x%" PRIx64 "\n", board->occupancy[black]);
        printf("both:\t\t0x%" PRIx64 "\n", board->occupancy[both]);
        printf("hash:\t\t0x%" PRIx64 "\n\n", board->hash);
    }

}
