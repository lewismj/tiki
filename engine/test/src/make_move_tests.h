
#ifndef TIKI_MAKE_MOVE_TESTS_H
#define TIKI_MAKE_MOVE_TESTS_H


/**
 * Given two board configurations apply different moves to each, such
 * that they yield the same logical board position post moves.
 * Test that the hash (and consequently all position state) match.
 */
void test_correct_hash0();
void test_correct_hash1();
void test_correct_hash2();

#endif
