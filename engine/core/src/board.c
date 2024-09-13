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
    board->pieces = (bitboard*) malloc(12 * sizeof(bitboard));
    board->occupancy = (bitboard*) malloc(3 * sizeof(bitboard));
    board->en_passant = none_sq;
    board->castle_flag = white_king | white_queen | black_king | black_queen;
    board->side = white;
    board->half_move = 0;
    board->full_move = 0;
    return board;
}

void reset_board(board_t* board) {
    memset(board->pieces, 0, 12 * sizeof(bitboard));
    memset(board->occupancy, 0, 3 * sizeof(bitboard));
    board->en_passant = none_sq;
    board->castle_flag = white_king | white_queen | black_king | black_queen;
    board->side = white;
    board->half_move = 0;
    board->full_move = 0;
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
            file++;
        }
        ptr++;
    }
    /* side to move.        */
    while (*ptr == ' ') ptr++;
    board->side = *ptr == 'w' ? white : black;
    *ptr++;

    /* castling rights.     */
    while (*ptr != ' ') {
        switch (*ptr) {
            case 'K': board->castle_flag |= white_king;
            case 'Q': board->castle_flag |= white_queen;
            case 'k': board->castle_flag |= black_king;
            case 'q': board->castle_flag |= black_queen;
            case '-': break;
        }
    }

    /* en-passant square.   */
    while (*ptr == ' ') ptr++;
    if (*ptr != '-') {
        int ep_file = ptr[0] - 'a';
        int ep_rank = 8 - (fen[1] - '0');
        board->en_passant = ep_rank * 8 + ep_file;
    }
    else {
        board->en_passant = none_sq;
    }

    /* half-move counter.   */
    while (*ptr == ' ') ptr++;
    /* Don't use 'strtol' engine shouldn't validate, clients do. */
    board->half_move = atoi(ptr);
    while (isdigit(*ptr)) ptr++;

    /* full-move counter.   */
    while (*ptr == ' ') ptr++;
    board->full_move = atoi(ptr);

    for (int piece = P; piece <= K; piece++) board->occupancy[white] |= board->pieces[piece];
    for (int piece = p; piece <= k; piece++) board->occupancy[black] |= board->pieces[piece];
    board->occupancy[both] |= board->occupancy[white];
    board->occupancy[both] |= board->occupancy[black];

    recalculate_hash(board);
}

void free_board(board_t* board) {
    if (board != NULL) {
        free(board->pieces);
        free(board->occupancy);
        free(board);
    }
}

void print_board(board_t* board, showable options) {

    if (options & show) {
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
    }

    if (options & hex) {

    }

}