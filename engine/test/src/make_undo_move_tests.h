
#ifndef TIKI_MAKE_UNDO_MOVE_TESTS_H
#define TIKI_MAKE_UNDO_MOVE_TESTS_H


void test_make_move_hash1();
void test_make_move_hash2();
void test_make_move_hash3();
void test_undo_restores_state1();
void test_incremental_hash_matches_full();
void test_pop_on_empty_stack_is_clamped();

#endif
