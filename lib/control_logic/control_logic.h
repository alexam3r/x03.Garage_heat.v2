#pragma once

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
