#include "control_logic.h"

bool shouldStartHeating(float targetSensorReading, float targetSetpoint, float hysteresis) {
    return targetSensorReading < (targetSetpoint - hysteresis);
}

bool shouldStopHeating(float targetSensorReading, float targetSetpoint, float hysteresis) {
    return targetSensorReading > (targetSetpoint + hysteresis);
}

unsigned long selectFanCoolerDelay(float outdoorTemp) {
    if (outdoorTemp > 0.0f) return 20000UL;
    if (outdoorTemp >= -5.0f) return 17500UL;
    if (outdoorTemp >= -10.0f) return 15000UL;
    if (outdoorTemp >= -15.0f) return 12500UL;
    return 10000UL;
}

unsigned long resetLoadOnLimit(unsigned long baseDelayMs) {
    return 20000UL - (baseDelayMs / 2UL);
}

DutyLimits adaptDutyLimits(float blownAirTemp, float sensorMaxTemp, unsigned long currentLoadOnLimitMs, unsigned long baseDelayMs) {
    DutyLimits result;
    if (blownAirTemp >= sensorMaxTemp) {
        unsigned long decreased = currentLoadOnLimitMs - 1000UL;
        unsigned long floorLimit = baseDelayMs / 4UL;
        result.loadOnLimit = (decreased < floorLimit) ? floorLimit : decreased;
        result.loadOffLimit = (unsigned long)((float)baseDelayMs / 1.5f);
    } else if (blownAirTemp <= sensorMaxTemp - 5.0f) {
        result.loadOnLimit = currentLoadOnLimitMs + 1000UL;
        result.loadOffLimit = (unsigned long)((float)baseDelayMs / 2.5f);
    } else {
        result.loadOnLimit = currentLoadOnLimitMs;
        result.loadOffLimit = baseDelayMs / 2UL;
    }
    return result;
}

float applyAirTempCalibration(float rawTemp, float offsetC) {
    return rawTemp + offsetC;
}

bool shouldAuxHeaterTurnOn(float airTemp, float targetAirTemp, float hysteresis) {
    return airTemp < (targetAirTemp - hysteresis);
}

bool shouldAuxHeaterTurnOff(float airTemp, float targetAirTemp, float hysteresis) {
    return airTemp > (targetAirTemp + hysteresis);
}
