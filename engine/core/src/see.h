#ifndef TIKI_SEE_H
#define TIKI_SEE_H

#include "types.h"
#include "board.h"
#include "attack_mask.h"
#include "move_encoding.h"

/**
 * Static exchange evaluation: the material balance of the capture sequence on a move's target
 * square, each side recapturing with its least valuable piece and stopping when it would lose
 * by continuing. Used to order losing captures last and to skip them in quiescence.
 */

/* Piece values for SEE only (P, N, B, R, Q, K, then black). */
static const int see_values[12] = { 100, 320, 330, 500, 900, 20000, 100, 320, 330, 500, 900, 20000 };

/* All pieces of both colours attacking 'sq' with the given occupancy (so x-rays appear as pieces are removed). */
static inline_always bitboard attackers_to(const board_t* board, square sq, bitboard occupancy) {
    return (pawn_attack(sq, black) & board->pieces[P]) |
           (pawn_attack(sq, white) & board->pieces[p]) |
           (knight_attack(sq) & (board->pieces[N] | board->pieces[n])) |
           (king_attack(sq) & (board->pieces[K] | board->pieces[k])) |
           (bishop_attack(sq, occupancy) & (board->pieces[B] | board->pieces[b] | board->pieces[Q] | board->pieces[q])) |
           (rook_attack(sq, occupancy) & (board->pieces[R] | board->pieces[r] | board->pieces[Q] | board->pieces[q]));
}

/* Piece of colour 'c' on 'sq', or none. */
static inline_always piece piece_on(const board_t* board, square sq, colour c) {
    const int* pieces = c == white ? white_pieces : black_pieces;
    for (int i = 0; i < 6; i++) {
        if (is_bit_set(&board->pieces[pieces[i]], sq)) return pieces[i];
    }
    return none;
}

/* Value of the piece a capture takes (a pawn for en passant), 0 if not a capture. */
static inline_always int captured_value(const board_t* board, move_t move) {
    if (!get_capture_flag(move)) return 0;
    if (get_enpassant_flag(move)) return see_values[P];
    piece victim = piece_on(board, get_target_square(move), board->side ^ 1);
    return victim == none ? 0 : see_values[victim];
}

static inline_always int see(const board_t* board, move_t move) {
    const square target = get_target_square(move);
    bitboard occupancy = board->occupancy[both];
    int gain[32];
    int d = 0;

    gain[0] = captured_value(board, move);
    if (get_enpassant_flag(move)) {
        occupancy ^= 1ULL << (board->side == white ? target + 8 : target - 8);
    }

    piece attacker = get_piece_moved(move);
    bitboard from = 1ULL << get_source_square(move);

    do {
        d++;
        /* Speculative, the score if the other side recaptures the piece now on 'target'. No early
         * exit: the usual max(-gain[d-1], gain[d]) < 0 cut keeps only the sign exact. */
        gain[d] = see_values[attacker] - gain[d - 1];

        occupancy ^= from;
        bitboard attackers = attackers_to(board, target, occupancy) & occupancy;

        /* Least valuable attacker of the side to recapture (odd d: the opponent of the mover). */
        colour side = (d & 1) ? board->side ^ 1 : board->side;
        bitboard own = attackers & board->occupancy[side];
        const int* pieces = side == white ? white_pieces : black_pieces;
        from = 0ULL;
        for (int i = 0; i < 6 && own; i++) {
            bitboard candidates = own & board->pieces[pieces[i]];
            if (!candidates) continue;
            /* The king can't recapture into a square the other side still attacks. */
            if ((pieces[i] == K || pieces[i] == k) && (attackers & board->occupancy[side ^ 1])) break;
            from = candidates & -candidates;
            attacker = pieces[i];
            break;
        }
    } while (from);

    while (--d) gain[d - 1] = -(-gain[d - 1] > gain[d] ? -gain[d - 1] : gain[d]);
    return gain[0];
}

#endif
