#include "control_logic.h"
#include "config.h"
#include <ArduinoJson.h>

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

RadiatorDecision evaluateRadiator(const RadiatorInput& in) {
    RadiatorDecision out;

    if (!in.sensorValid) {
        out.fanOn = true;
        out.alarmState = RadiatorAlarmState::OVERTEMP;
        out.forceLoadsOff = true;
        out.fanOnSince = 0UL;
        return out;
    }

    RadiatorAlarmState alarm = in.previousAlarm;
    if (alarm != RadiatorAlarmState::NORMAL && in.radiatorTemp < RADIATOR_RECOVERY_TEMP) {
        alarm = RadiatorAlarmState::NORMAL;
    }

    bool fanShouldBeOn = in.radiatorTemp >= RADIATOR_FAN_ON_TEMP;
    unsigned long fanOnSince = in.fanOnSince;
    if (fanShouldBeOn && !in.fanWasOn) {
        fanOnSince = in.nowMillis;
    } else if (!fanShouldBeOn) {
        fanOnSince = 0UL;
    }

    if (in.radiatorTemp >= RADIATOR_CRITICAL_TEMP) {
        alarm = RadiatorAlarmState::OVERTEMP;
    } else if (in.radiatorTemp >= RADIATOR_FAN_FAULT_TEMP && fanOnSince != 0UL &&
               (in.nowMillis - fanOnSince) >= RADIATOR_FAN_MIN_RUNTIME_MS) {
        alarm = RadiatorAlarmState::FAN_FAULT;
    }

    out.fanOn = fanShouldBeOn;
    out.alarmState = alarm;
    out.forceLoadsOff = (alarm != RadiatorAlarmState::NORMAL);
    out.fanOnSince = fanOnSince;
    return out;
}

bool shouldRestartForWatchdog(unsigned long lastFullyConnectedMillis, unsigned long nowMillis, unsigned long watchdogTimeoutMs) {
    return (nowMillis - lastFullyConnectedMillis) >= watchdogTimeoutMs;
}

bool isValidSensorTempDiff(float diff) {
    return diff >= SENSOR_TEMP_DIFF_MIN && diff <= SENSOR_TEMP_DIFF_MAX;
}

static const char* radiatorAlarmToString(RadiatorAlarmState state) {
    switch (state) {
        case RadiatorAlarmState::FAN_FAULT: return "FAN_FAULT";
        case RadiatorAlarmState::OVERTEMP:  return "OVERTEMP";
        default:                            return "NORMAL";
    }
}

void buildStatusJson(const DeviceStatus& status, char* outBuffer, size_t bufferSize) {
    JsonDocument doc;

    if (status.blownAirValid) doc["blownAirTemp"] = status.blownAirTemp; else doc["blownAirTemp"] = nullptr;
    if (status.targetValid)   doc["targetTemp"]   = status.targetTemp;   else doc["targetTemp"]   = nullptr;
    if (status.infoValid)     doc["infoTemp"]     = status.infoTemp;     else doc["infoTemp"]     = nullptr;
    if (status.outdoorValid)  doc["outdoorTemp"]  = status.outdoorTemp;  else doc["outdoorTemp"]  = nullptr;
    if (status.radiatorValid) doc["radiatorTemp"] = status.radiatorTemp; else doc["radiatorTemp"] = nullptr;

    doc["fanOn"] = status.fanOn;
    doc["elementOn"] = status.elementOn;
    doc["auxOn"] = status.auxOn;
    doc["radiatorFanOn"] = status.radiatorFanOn;

    doc["targetSensorTemp"] = status.targetSensorTemp;
    doc["sensorTempDiff"] = status.sensorTempDiff;
    doc["targetAirTemp"] = status.targetAirTemp;
    doc["targetHysteresis"] = status.targetHysteresis;
    doc["auxHysteresis"] = status.auxHysteresis;

    doc["radiatorAlarm"] = radiatorAlarmToString(status.radiatorAlarm);

    doc["uptimeSeconds"] = status.uptimeSeconds;
    doc["freeHeapBytes"] = status.freeHeapBytes;
    doc["rssiDbm"] = status.rssiDbm;
    doc["wifiConnected"] = status.wifiConnected;
    doc["mqttConnected"] = status.mqttConnected;

    serializeJson(doc, outBuffer, bufferSize);
}
