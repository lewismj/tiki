#ifndef TIKI_MASK_GENERATOR_H
#define TIKI_MASK_GENERATOR_H

#include "types.h"

/**
 *  __These functions are used for initialization only.__
 *
 *  We use magic bitboard not 'on the fly' calculation which would be far too slow.
 */


/**
 * Given a square 's' return those squares that may block the movement of the rook.
 * Note, this function is used during initialization only.
 *
 * @param s the square on which the rook is present.
 * @return  the bitboard showing the blockers.
 */
bitboard create_rook_blocker_mask(square s);

/**
 * Given a square 's' return those squares that may block the movement of the bishop.
 * Note, this function is used during initialization only.
 *
 * @param s the square on which the rook is present.
 * @return  the bitboard showing the blockers.
 */
bitboard create_bishop_blocker_mask(square s);


/**
 *  Given some definition of relevant blockers  we compute the squares attacked by a rook for a given square 's'.
 *  For each square we will only have a relevant number of different configuration of relevant blockers.
 *
 * @param s the square on which the rook is present.
 * @param blockers the relevant blockers for the rook.
 * @return a bitboard representing the squares attacked.
 */
bitboard create_rook_attack_mask(square s, bitboard blockers);

/**
 * Given some definition of relevant blockers we compute the squares attacked by a bishop for a given square 's'.
 * For each square we will only have a relevant number of different configuration of relevant blockers.
 *
 * @param s the square on which the rook is present.
 * @param blockers the relevant blockers for the bishop.
 * @return a bitboard representing the squares attacked.
 */
bitboard create_bishop_attack_mask(square s, bitboard blockers);

/**
 * Given a square 's', this returns the bitboard representing the king attacks (blockers are irrelevant
 * for the king).
 * @param s the square that the king is positioned.
 * @return the bitboard representing the squares attacked by the king.
 */
bitboard create_king_attack_mask(square s);

/**
 * Given a square 's', this returns the bitboard representing the knight attacks (blockers are irrelevant
 * for the knight).
 * @param s the square that the knight is occupying.
 * @return the bitboard representing the squares attacked by the knight.
 */
bitboard create_knight_attack_mask(square s);

/**
 * Given a pawn on square 's' and colour 'c' return the bitboard representing the standard attacks.
 * Note, en-passant is dealt with separately by the engine.
 *
 * @param s the square that the pawn is occupying.
 * @param c the colour of the pawn.
 * @return the bitboard representing the squares attacked by the pawn.
 */
bitboard create_pawn_attack_mask(square s, colour c);


/**
 *
 * @param index
 * @param num_relevant_bits
 * @param mask
 * @return
 */
bitboard set_occupancy_variation(unsigned int index, unsigned int num_relevant_bits, bitboard mask);



#endif
