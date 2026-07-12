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

void test_selectFanCoolerDelay_above_zero_returns_20000(void) {
    TEST_ASSERT_EQUAL_UINT32(20000UL, selectFanCoolerDelay(5.0f));
}

void test_selectFanCoolerDelay_zero_returns_17500(void) {
    TEST_ASSERT_EQUAL_UINT32(17500UL, selectFanCoolerDelay(0.0f));
}

void test_selectFanCoolerDelay_minus_five_returns_17500(void) {
    TEST_ASSERT_EQUAL_UINT32(17500UL, selectFanCoolerDelay(-5.0f));
}

void test_selectFanCoolerDelay_minus_ten_returns_15000(void) {
    TEST_ASSERT_EQUAL_UINT32(15000UL, selectFanCoolerDelay(-10.0f));
}

void test_selectFanCoolerDelay_minus_fifteen_returns_12500(void) {
    TEST_ASSERT_EQUAL_UINT32(12500UL, selectFanCoolerDelay(-15.0f));
}

void test_selectFanCoolerDelay_below_minus_fifteen_returns_10000(void) {
    TEST_ASSERT_EQUAL_UINT32(10000UL, selectFanCoolerDelay(-20.0f));
}

void test_resetLoadOnLimit_formula(void) {
    TEST_ASSERT_EQUAL_UINT32(10000UL, resetLoadOnLimit(20000UL));
}

void test_adaptDutyLimits_hot_decreases_on_limit(void) {
    DutyLimits result = adaptDutyLimits(20.0f, 20.0f, 10000UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(9000UL, result.loadOnLimit);
    TEST_ASSERT_EQUAL_UINT32(13333UL, result.loadOffLimit);
}

void test_adaptDutyLimits_hot_clamps_at_floor(void) {
    DutyLimits result = adaptDutyLimits(20.0f, 20.0f, 5200UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(5000UL, result.loadOnLimit); // floor = baseDelay/4 = 5000
}

void test_adaptDutyLimits_cold_increases_on_limit(void) {
    DutyLimits result = adaptDutyLimits(15.0f, 20.0f, 10000UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(11000UL, result.loadOnLimit);
    TEST_ASSERT_EQUAL_UINT32(8000UL, result.loadOffLimit);
}

void test_adaptDutyLimits_neutral_zone_keeps_on_limit(void) {
    DutyLimits result = adaptDutyLimits(17.0f, 20.0f, 10000UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(10000UL, result.loadOnLimit);
    TEST_ASSERT_EQUAL_UINT32(10000UL, result.loadOffLimit);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_shouldStartHeating_below_lower_hysteresis_returns_true);
    RUN_TEST(test_shouldStartHeating_at_lower_hysteresis_returns_false);
    RUN_TEST(test_shouldStopHeating_above_upper_hysteresis_returns_true);
    RUN_TEST(test_shouldStopHeating_at_upper_hysteresis_returns_false);
    RUN_TEST(test_selectFanCoolerDelay_above_zero_returns_20000);
    RUN_TEST(test_selectFanCoolerDelay_zero_returns_17500);
    RUN_TEST(test_selectFanCoolerDelay_minus_five_returns_17500);
    RUN_TEST(test_selectFanCoolerDelay_minus_ten_returns_15000);
    RUN_TEST(test_selectFanCoolerDelay_minus_fifteen_returns_12500);
    RUN_TEST(test_selectFanCoolerDelay_below_minus_fifteen_returns_10000);
    RUN_TEST(test_resetLoadOnLimit_formula);
    RUN_TEST(test_adaptDutyLimits_hot_decreases_on_limit);
    RUN_TEST(test_adaptDutyLimits_hot_clamps_at_floor);
    RUN_TEST(test_adaptDutyLimits_cold_increases_on_limit);
    RUN_TEST(test_adaptDutyLimits_neutral_zone_keeps_on_limit);
    return UNITY_END();
}
