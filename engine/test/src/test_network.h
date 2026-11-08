#ifndef TIKI_TEST_NETWORK_H
#define TIKI_TEST_NETWORK_H

/**
 * Builds a small network that counts material, so tests can search without a trained network
 * file: 100 / 300 / 300 / 500 / 900 centipawns for the side to move, exactly, through the real layer
 * stack (two accumulator units per piece kind whose pairwise products differ linearly, one hidden
 * unit in the middle of CReLU's range, the same in every bucket; test_network.c). The test runner
 * loads it before every test (setUp).
 */
void load_material_network(void);

#endif
