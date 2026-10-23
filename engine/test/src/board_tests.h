#ifndef TIKI_BOARD_TESTS_H
#define TIKI_BOARD_TESTS_H


void test_parse_fen_fields();
void test_parse_move_promotions();
void test_move_to_uci();
void test_parse_move_castling();
void test_parse_move_rejects_invalid();
void test_slider_attacks_match_reference();
void test_generate_captures_matches_filtered_moves();
void test_generate_captures_promotions();
void test_see();
void test_board_to_fen_round_trip();

#endif
