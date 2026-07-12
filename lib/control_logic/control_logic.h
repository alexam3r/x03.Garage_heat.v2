#pragma once

// Автоматы состояний (CLAUDE.md §3.1, §3.2, §3.3)
enum class HeaterState { OFF, FAN_STARTING, ELEMENT_DUTY_ON, ELEMENT_DUTY_OFF, COOLDOWN };
enum class AuxHeaterState { OFF, ON };
enum class RadiatorAlarmState { NORMAL, FAN_FAULT, OVERTEMP };

// Тепловая пушка: гистерезис TARGET (CLAUDE.md §3.1)
bool shouldStartHeating(float targetSensorReading, float targetSetpoint, float hysteresis);
bool shouldStopHeating(float targetSensorReading, float targetSetpoint, float hysteresis);
