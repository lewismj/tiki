#include "unity.h"

void setUp() {
}

void tearDown() {
}

void test_Addition() {
    TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Addition);
    return UNITY_END();
}