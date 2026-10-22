#ifndef TIKI_REPETITION_TESTS_H
#define TIKI_REPETITION_TESTS_H


void test_position_resets_history();
void test_position_records_root();
void test_position_irreversible_move_clears_history();
void test_position_detects_game_repetition();
void test_position_long_history();
void test_go_preserves_history();
void test_fifty_move_draw();
void test_position_history_overflow();

#endif
