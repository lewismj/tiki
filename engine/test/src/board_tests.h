#ifndef TIKI_BOARD_TESTS_H
#define TIKI_BOARD_TESTS_H


void test_parse_fen_fields();
void test_parse_move_promotions();
void test_parse_move_castling();
void test_parse_move_rejects_invalid();
void test_slider_attacks_match_reference();

#endif
