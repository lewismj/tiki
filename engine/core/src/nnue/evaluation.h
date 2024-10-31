#ifndef TIKI_NNUE_EVALUATION_H
#define TIKI_NNUE_EVALUATION_H

#include "../types.h"
#include "../board.h"
#include "nnue.h"


static int nnue_pieces[12] = { 6, 5, 4, 3, 2, 1, 12, 11, 10, 9, 8, 7 };

static int nnue_squares[64] = {
        a1, b1, c1, d1, e1, f1, g1, h1,
        a2, b2, c2, d2, e2, f2, g2, h2,
        a3, b3, c3, d3, e3, f3, g3, h3,
        a4, b4, c4, d4, e4, f4, g4, h4,
        a5, b5, c5, d5, e5, f5, g5, h5,
        a6, b6, c6, d6, e6, f6, g6, h6,
        a7, b7, c7, d7, e7, f7, g7, h7,
        a8, b8, c8, d8, e8, f8, g8, h8
};


static inline_always int eval_nnue(const board_t* const board) {
    int square;
    int pieces[33];
    int squares[33];
    int index = 2;


    for (int i = P; i <= k; i++) {
        bitboard tmp = board->pieces[i];
        while (tmp) {
            square = get_lsb_and_pop_bit(&tmp);
            if (i == K) {
                pieces[0] = nnue_pieces[i];
                squares[0] = nnue_squares[square];
            } else if (i == k) {
                pieces[1] = nnue_pieces[i];
                squares[1] = nnue_squares[square];
            } else {
                pieces[index] = nnue_pieces[i];
                squares[index] = nnue_squares[square];
                index++;
            }
        }
    }

    pieces[index] = 0;
    squares[index] = 0;
    return (nnue_evaluate(board->side , pieces, squares)/100) * (100 - board->fifty_move/ 100);
}


#endif
