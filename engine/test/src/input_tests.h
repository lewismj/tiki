#ifndef TIKI_INPUT_TESTS_H
#define TIKI_INPUT_TESTS_H


void test_input_queues_commands_in_order();
void test_input_stop_during_search();
void test_input_quit_during_search();
void test_input_commands_during_search_are_kept();
void test_input_stop_queued_before_search();
void test_input_stop_for_later_search();
void test_input_wait_for_stop();
void test_uci_loop_keeps_commands_during_search();
void test_uci_loop_go_infinite_stop();
void test_soft_time_limit();
void test_stopped_search_returns_legal_move();
void test_input_stop_after_queued_go();

#endif
