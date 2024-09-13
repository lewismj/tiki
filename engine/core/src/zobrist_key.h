#ifndef TIKI_ZOBRIST_KEY_H
#define TIKI_ZOBRIST_KEY_H

#include <inttypes.h>
#include "types.h"


/**
 *
 */
void init_zobrist_key();

/**
 *
 * @param s
 * @param p
 * @return
 */
uint64_t get_piece_key(square s, piece p);

/**
 *
 * @param s
 * @return
 */
uint64_t get_enpassant_key(square s);

/**
 *
 * @param castle_flag
 * @return
 */
uint64_t get_castle_key(int castle_flag);

/**
 *
 * @return
 */
uint64_t get_side_key();

#endif
