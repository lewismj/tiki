#include "unity.h"
#include "make_move_tests.h"

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
    RUN_TEST(test_correct_hash0);
    RUN_TEST(test_correct_hash1);
    return UNITY_END();
}