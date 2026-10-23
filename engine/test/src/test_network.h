#ifndef TIKI_TEST_NETWORK_H
#define TIKI_TEST_NETWORK_H

/**
 * Builds a small network that counts material, so tests can search without a trained network
 * file. One hidden unit per piece kind (own and their pawn, knight, bishop, rook, queen), biased
 * to the middle of SCReLU's range where it's close to linear, with output weights giving about
 * 100 / 300 / 300 / 500 / 900 centipawns. The test runner loads it before every test (setUp).
 */
void load_material_network(void);

#endif
