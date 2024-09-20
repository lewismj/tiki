#include "unity.h"
#include "make_undo_move_tests.h"

#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"

/**
 * Ensure pre-computed tables are initialised prior to running tests.
 */
void setUp() {
    init_attack_table();
    init_zobrist_key();
}

void tearDown() {
}


int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_make_move_hash1);
    RUN_TEST(test_make_move_hash2);
    RUN_TEST(test_make_move_hash3);
    RUN_TEST(test_undo_restores_state1);
    return UNITY_END();
}