#include <unity.h>
#include "control_logic.h"

void setUp(void) {}
void tearDown(void) {}

void test_shouldStartHeating_below_lower_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldStartHeating(4.4f, 5.0f, 0.5f));
}

void test_shouldStartHeating_at_lower_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldStartHeating(4.5f, 5.0f, 0.5f));
}

void test_shouldStopHeating_above_upper_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldStopHeating(5.6f, 5.0f, 0.5f));
}

void test_shouldStopHeating_at_upper_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldStopHeating(5.5f, 5.0f, 0.5f));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_shouldStartHeating_below_lower_hysteresis_returns_true);
    RUN_TEST(test_shouldStartHeating_at_lower_hysteresis_returns_false);
    RUN_TEST(test_shouldStopHeating_above_upper_hysteresis_returns_true);
    RUN_TEST(test_shouldStopHeating_at_upper_hysteresis_returns_false);
    return UNITY_END();
}
