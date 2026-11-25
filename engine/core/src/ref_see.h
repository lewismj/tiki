#ifndef TIKI_REF_SEE_H
#define TIKI_REF_SEE_H

/**
 * The reference engine's static exchange test (Position.cpp:784-908), for the decisions unit A ports
 * from it (SEE pruning in the move loop, the losing-capture class in the reductions): values pawn
 * 100, knight 300, bishop 300, rook 500, queen 900 (see.h's knight and bishop are 320 and 330); a
 * piece pinned to its king doesn't recapture while its pinner is on the board; a promotion adds
 * nothing; an en passant capture leaves the captured pawn's square occupied (as the reference).
 * see_ge_ref(board, move, threshold): whether the exchange on the target square gains at least
 * 'threshold'. tiki's other SEE uses (qsearch, capture ordering) keep see.h.
 */

#include "types.h"
#include "board.h"
#include "attack_mask.h"
#include "move_encoding.h"
#include "bitboard_ops.h"

static const int ref_see_values[6] = { 100, 300, 300, 500, 900, 0 };

static inline_always bitboard ref_see_bit(int sq) { return 1ULL << sq; }

static inline_always bool see_ge_ref(const board_t* board, move_t move, int threshold) {
    const int to = get_target_square(move);
    const int from = get_source_square(move);

    int balance = -threshold;
    if (get_capture_flag(move)) {
        int captured = 0;
        if (!get_enpassant_flag(move)) {
            const int t = board->side == white ? 6 : 0;
            for (int i = 0; i < 5; i++) {
                if (board->pieces[t + i] & ref_see_bit(to)) {
                    captured = i;
                    break;
                }
            }
        }
        balance += ref_see_values[captured];
    }
    if (balance < 0) return false;

    balance = ref_see_values[get_piece_moved(move) % 6] - balance;
    if (balance <= 0) return true;

    const bitboard white_occupied = board->occupancy[white];
    const bitboard black_occupied = board->occupancy[black];
    bitboard occupied = white_occupied | black_occupied;

    /* "do" the move */
    occupied &= ~ref_see_bit(from);
    occupied |= ref_see_bit(to);

    const bitboard queens = board->pieces[Q] | board->pieces[q];
    const bitboard bishops_queens = bishop_attack((square) to, 0) & (board->pieces[B] | board->pieces[b] | queens);
    const bitboard rooks_queens = rook_attack((square) to, 0) & (board->pieces[R] | board->pieces[r] | queens);
    const bitboard knights = board->pieces[N] | board->pieces[n];

    bitboard all_attackers = (king_attack((square) to) & (board->pieces[K] | board->pieces[k])) |
                             (pawn_attack((square) to, black) & board->pieces[P]) |
                             (pawn_attack((square) to, white) & board->pieces[p]);
    if (knights) all_attackers |= knight_attack((square) to) & knights;
    if (bishops_queens) all_attackers |= bishop_attack((square) to, occupied) & bishops_queens;
    if (rooks_queens) all_attackers |= rook_attack((square) to, occupied) & rooks_queens;

    /* Pieces pinned to their own king (a capture along the pin line isn't affected: the occupied target
     * square is a second blocker, and a slider on it is captured first). */
    bitboard pinned[2] = { 0, 0 };
    bitboard pinners[2] = { 0, 0 };
    for (int colour = 0; colour < 2; colour++) {
        const int o = colour == white ? 0 : 6, t = colour == white ? 6 : 0;
        const bitboard our_occupied = colour == white ? white_occupied : black_occupied;
        const int king_sq = trailing_zero_count(board->pieces[o + 5]);
        const bitboard sliders = occupied & ~ref_see_bit(to) &
            ((rook_attack((square) king_sq, 0) & (board->pieces[t + 3] | board->pieces[t + 4])) |
             (bishop_attack((square) king_sq, 0) & (board->pieces[t + 2] | board->pieces[t + 4])));
        for (bitboard s = sliders; s; s &= s - 1) {
            const int slider = trailing_zero_count(s);
            const bitboard ray = (rook_attack((square) king_sq, ref_see_bit(slider)) & rook_attack((square) slider, ref_see_bit(king_sq))) |
                                 (bishop_attack((square) king_sq, ref_see_bit(slider)) & bishop_attack((square) slider, ref_see_bit(king_sq)));
            const bitboard blockers = ray & occupied;
            if ((blockers & our_occupied) && (blockers & (blockers - 1)) == 0) {
                pinned[colour] |= blockers;
                pinners[colour] |= ref_see_bit(slider);
            }
        }
    }

    int side = board->side;
    int result = 1;
    for (;;) {
        side ^= 1;
        all_attackers &= occupied;
        const int o = side == white ? 0 : 6;
        bitboard our_attackers = all_attackers & (side == white ? white_occupied : black_occupied);
        const bitboard their_attackers = all_attackers & (side == white ? black_occupied : white_occupied);

        if (pinners[side] & occupied) our_attackers &= ~pinned[side];
        if (our_attackers == 0) break;
        result ^= 1;

        int piece = 0;
        for (; piece < 5; piece++)
            if (board->pieces[o + piece] & our_attackers) break;
        if (piece == 5) {
            if (their_attackers) result ^= 1;
            break;
        }

        balance = ref_see_values[piece] - balance;
        if (balance < result) break;

        const bitboard candidates = board->pieces[o + piece] & our_attackers;
        occupied ^= candidates & -candidates;

        if ((piece == 0 || piece == 2 || piece == 4) && (bishops_queens & occupied & ~all_attackers))
            all_attackers |= bishop_attack((square) to, occupied) & bishops_queens;
        if ((piece == 3 || piece == 4) && (rooks_queens & occupied & ~all_attackers))
            all_attackers |= rook_attack((square) to, occupied) & rooks_queens;
    }
    return result != 0;
}

#endif
