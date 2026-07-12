#pragma once

#include <stddef.h>

// Автоматы состояний (CLAUDE.md §3.1, §3.2, §3.3)
enum class HeaterState { OFF, FAN_STARTING, ELEMENT_DUTY_ON, ELEMENT_DUTY_OFF, COOLDOWN };
enum class AuxHeaterState { OFF, ON };
enum class RadiatorAlarmState { NORMAL, FAN_FAULT, OVERTEMP };

// Тепловая пушка: гистерезис TARGET (CLAUDE.md §3.1)
bool shouldStartHeating(float targetSensorReading, float targetSetpoint, float hysteresis);
bool shouldStopHeating(float targetSensorReading, float targetSetpoint, float hysteresis);

// fanCoolerDelay по уличной температуре (CLAUDE.md §3.1, таблица)
unsigned long selectFanCoolerDelay(float outdoorTemp);

// Duty-цикл нагревательного элемента, адаптация по BLOWN_AIR (CLAUDE.md §3.1)
struct DutyLimits {
    unsigned long loadOnLimit;
    unsigned long loadOffLimit;
};

unsigned long resetLoadOnLimit(unsigned long baseDelayMs);
DutyLimits adaptDutyLimits(float blownAirTemp, float sensorMaxTemp, unsigned long currentLoadOnLimitMs, unsigned long baseDelayMs);

// Вспомогательный нагреватель: калибровка + гистерезис (CLAUDE.md §3.2)
float applyAirTempCalibration(float rawTemp, float offsetC);
bool shouldAuxHeaterTurnOn(float airTemp, float targetAirTemp, float hysteresis);
bool shouldAuxHeaterTurnOff(float airTemp, float targetAirTemp, float hysteresis);

// Радиатор SSR: эскалация перегрева/отказа вентилятора (CLAUDE.md §3.3)
struct RadiatorInput {
    float radiatorTemp;
    bool sensorValid;
    unsigned long nowMillis;
    unsigned long fanOnSince;      // 0 = вентилятор сейчас не включён
    bool fanWasOn;                 // состояние D8 до этого вызова
    RadiatorAlarmState previousAlarm;
};

struct RadiatorDecision {
    bool fanOn;
    RadiatorAlarmState alarmState;
    bool forceLoadsOff;            // true -> main.cpp обязан выключить D5/D6/D7
    unsigned long fanOnSince;      // обновлённое значение, сохранить для следующего вызова
};

RadiatorDecision evaluateRadiator(const RadiatorInput& input);

// Watchdog связи WiFi+MQTT (CLAUDE.md §3.5)
bool shouldRestartForWatchdog(unsigned long lastFullyConnectedMillis, unsigned long nowMillis, unsigned long watchdogTimeoutMs);

// Валидация входящей MQTT-команды SensorTempDiff (CLAUDE.md §4)
bool isValidSensorTempDiff(float diff);

// Сборка JSON-статуса (CLAUDE.md §4)
struct DeviceStatus {
    float blownAirTemp;      bool blownAirValid;
    float targetTemp;        bool targetValid;
    float infoTemp;          bool infoValid;
    float outdoorTemp;       bool outdoorValid;
    float radiatorTemp;      bool radiatorValid;

    bool fanOn;
    bool elementOn;
    bool auxOn;
    bool radiatorFanOn;

    float targetSensorTemp;
    float sensorTempDiff;
    float targetAirTemp;
    float targetHysteresis;
    float auxHysteresis;

    RadiatorAlarmState radiatorAlarm;

    unsigned long uptimeSeconds;
    unsigned long freeHeapBytes;
    int rssiDbm;
    bool wifiConnected;
    bool mqttConnected;
};

void buildStatusJson(const DeviceStatus& status, char* outBuffer, size_t bufferSize);
