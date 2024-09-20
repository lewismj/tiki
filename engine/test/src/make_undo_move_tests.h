
#ifndef TIKI_MAKE_UNDO_MOVE_TESTS_H
#define TIKI_MAKE_UNDO_MOVE_TESTS_H


/**
 * Given two board configurations apply different moves to each, such
 * that they yield the same logical board position post moves.
 * Test that the hash (and consequently all position state) match.
 */
void test_make_move_hash1();
void test_make_move_hash2();
void test_make_move_hash3();
void test_undo_restores_state();

#endif
