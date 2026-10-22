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
void test_repetition_same_side_to_move();
void test_repetition_limited_by_fifty_move();
void test_repetition_limited_by_null_move();
void test_insufficient_material();
void test_search_scores_insufficient_material_as_draw();

#endif
