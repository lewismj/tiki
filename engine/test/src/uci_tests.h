#ifndef TIKI_UCI_TESTS_H
#define TIKI_UCI_TESTS_H


void test_uci_command_tokens();
void test_uci_stop_does_not_quit();
void test_uci_quit_exits();
void test_uci_newgame_resets_position();
void test_go_infinite_limits();
void test_go_clock_limits();
void test_search_input_stop();
void test_search_input_quit();
void test_search_input_isready();
void test_setoption_hash();
void test_setoption_threads();

#endif
