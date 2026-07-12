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

void test_applyAirTempCalibration_subtracts_offset(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, applyAirTempCalibration(5.2f, -1.0f));
}

void test_shouldAuxHeaterTurnOn_below_lower_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldAuxHeaterTurnOn(8.9f, 10.0f, 1.0f));
}

void test_shouldAuxHeaterTurnOn_at_lower_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldAuxHeaterTurnOn(9.0f, 10.0f, 1.0f));
}

void test_shouldAuxHeaterTurnOff_above_upper_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldAuxHeaterTurnOff(11.1f, 10.0f, 1.0f));
}

void test_shouldAuxHeaterTurnOff_at_upper_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldAuxHeaterTurnOff(11.0f, 10.0f, 1.0f));
}

static RadiatorInput makeRadiatorInput(float temp, bool valid, unsigned long now,
                                        unsigned long fanOnSince, bool fanWasOn,
                                        RadiatorAlarmState prevAlarm) {
    RadiatorInput in;
    in.radiatorTemp = temp;
    in.sensorValid = valid;
    in.nowMillis = now;
    in.fanOnSince = fanOnSince;
    in.fanWasOn = fanWasOn;
    in.previousAlarm = prevAlarm;
    return in;
}

void test_evaluateRadiator_cold_normal_fan_off(void) {
    RadiatorInput in = makeRadiatorInput(20.0f, true, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_FALSE(out.fanOn);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::NORMAL, (int)out.alarmState);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
    TEST_ASSERT_EQUAL_UINT32(0UL, out.fanOnSince);
}

void test_evaluateRadiator_crosses_fan_on_threshold_records_fanOnSince(void) {
    RadiatorInput in = makeRadiatorInput(31.0f, true, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_TRUE(out.fanOn);
    TEST_ASSERT_EQUAL_UINT32(100000UL, out.fanOnSince);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
}

void test_evaluateRadiator_cools_below_threshold_resets_fanOnSince(void) {
    RadiatorInput in = makeRadiatorInput(29.0f, true, 110000UL, 100000UL, true, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_FALSE(out.fanOn);
    TEST_ASSERT_EQUAL_UINT32(0UL, out.fanOnSince);
}

void test_evaluateRadiator_fan_fault_after_min_runtime(void) {
    // fan on since 100000, now 165000 => 65s elapsed >= 60s minimum, temp still >= 33
    RadiatorInput in = makeRadiatorInput(33.0f, true, 165000UL, 100000UL, true, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::FAN_FAULT, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
}

void test_evaluateRadiator_no_fault_before_min_runtime(void) {
    // only 30s elapsed, below the 60s minimum runtime
    RadiatorInput in = makeRadiatorInput(33.0f, true, 130000UL, 100000UL, true, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::NORMAL, (int)out.alarmState);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
}

void test_evaluateRadiator_critical_overtemp_forces_shutdown(void) {
    RadiatorInput in = makeRadiatorInput(36.0f, true, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::OVERTEMP, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
}

void test_evaluateRadiator_recovers_below_15c(void) {
    RadiatorInput in = makeRadiatorInput(14.0f, true, 200000UL, 0UL, false, RadiatorAlarmState::OVERTEMP);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::NORMAL, (int)out.alarmState);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
    TEST_ASSERT_FALSE(out.fanOn);
}

void test_evaluateRadiator_alarm_persists_above_recovery_threshold(void) {
    RadiatorInput in = makeRadiatorInput(25.0f, true, 200000UL, 0UL, false, RadiatorAlarmState::FAN_FAULT);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::FAN_FAULT, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
}

void test_evaluateRadiator_sensor_failure_forces_shutdown(void) {
    RadiatorInput in = makeRadiatorInput(0.0f, false, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::OVERTEMP, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
    TEST_ASSERT_TRUE(out.fanOn);
}

void test_shouldRestartForWatchdog_below_timeout_returns_false(void) {
    TEST_ASSERT_FALSE(shouldRestartForWatchdog(0UL, 299999UL, 300000UL));
}

void test_shouldRestartForWatchdog_at_timeout_returns_true(void) {
    TEST_ASSERT_TRUE(shouldRestartForWatchdog(0UL, 300000UL, 300000UL));
}

void test_isValidSensorTempDiff_in_range_returns_true(void) {
    TEST_ASSERT_TRUE(isValidSensorTempDiff(15.0f));
}

void test_isValidSensorTempDiff_below_min_returns_false(void) {
    TEST_ASSERT_FALSE(isValidSensorTempDiff(4.9f));
}

void test_isValidSensorTempDiff_above_max_returns_false(void) {
    TEST_ASSERT_FALSE(isValidSensorTempDiff(50.1f));
}

void test_isValidSensorTempDiff_at_bounds_returns_true(void) {
    TEST_ASSERT_TRUE(isValidSensorTempDiff(5.0f));
    TEST_ASSERT_TRUE(isValidSensorTempDiff(50.0f));
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
    RUN_TEST(test_applyAirTempCalibration_subtracts_offset);
    RUN_TEST(test_shouldAuxHeaterTurnOn_below_lower_hysteresis_returns_true);
    RUN_TEST(test_shouldAuxHeaterTurnOn_at_lower_hysteresis_returns_false);
    RUN_TEST(test_shouldAuxHeaterTurnOff_above_upper_hysteresis_returns_true);
    RUN_TEST(test_shouldAuxHeaterTurnOff_at_upper_hysteresis_returns_false);
    RUN_TEST(test_evaluateRadiator_cold_normal_fan_off);
    RUN_TEST(test_evaluateRadiator_crosses_fan_on_threshold_records_fanOnSince);
    RUN_TEST(test_evaluateRadiator_cools_below_threshold_resets_fanOnSince);
    RUN_TEST(test_evaluateRadiator_fan_fault_after_min_runtime);
    RUN_TEST(test_evaluateRadiator_no_fault_before_min_runtime);
    RUN_TEST(test_evaluateRadiator_critical_overtemp_forces_shutdown);
    RUN_TEST(test_evaluateRadiator_recovers_below_15c);
    RUN_TEST(test_evaluateRadiator_alarm_persists_above_recovery_threshold);
    RUN_TEST(test_evaluateRadiator_sensor_failure_forces_shutdown);
    RUN_TEST(test_shouldRestartForWatchdog_below_timeout_returns_false);
    RUN_TEST(test_shouldRestartForWatchdog_at_timeout_returns_true);
    RUN_TEST(test_isValidSensorTempDiff_in_range_returns_true);
    RUN_TEST(test_isValidSensorTempDiff_below_min_returns_false);
    RUN_TEST(test_isValidSensorTempDiff_above_max_returns_false);
    RUN_TEST(test_isValidSensorTempDiff_at_bounds_returns_true);
    return UNITY_END();
}
