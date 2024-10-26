#include <stdio.h>
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





void print_board(board_t* board, showable_t options) {
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
