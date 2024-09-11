#ifndef TIKI_RANDOM_H
#define TIKI_RANDOM_H

#include <stdint.h>

/**
 * Get the next uint32_t pseudo random number in the sequence.
 *
 * @param b the current state of the generator.
 * @return the next pseudo random number.
 */
uint32_t next_random_32(uint32_t* state);

/**
 * Get the next uint64_t pseudo random number in the sequence.
 *
 * @param b the current state of the generator (uint32_t based).
 * @return the next pseudo random number.
 */
uint64_t next_random_64(uint32_t* state);

/**
 * Start state for RNG.
 */
static const uint32_t random_seed = 1804289383;


#endif
