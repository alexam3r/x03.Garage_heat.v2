#include "control_logic.h"
#include "config.h"
#include <ArduinoJson.h>

// Главный контур хранения (TARGET-датчик), гистерезис ±0.5°C — CLAUDE.md §3.1/§0 решение 3.
// Пуск и остановка — отдельные функции, а не одна с флагом направления, потому что вызываются
// из разных состояний автомата (heaterTick: OFF -> старт, ELEMENT_DUTY_* -> стоп).
bool shouldStartHeating(float targetSensorReading, float targetSetpoint, float hysteresis) {
    return targetSensorReading < (targetSetpoint - hysteresis);
}

bool shouldStopHeating(float targetSensorReading, float targetSetpoint, float hysteresis) {
    return targetSensorReading > (targetSetpoint + hysteresis);
}

// Таблица fanCoolerDelay по уличной температуре — CLAUDE.md §3.1. Это одновременно базовый
// период duty-цикла элемента и время дожига вентилятора после выключения элемента (cooldown).
unsigned long selectFanCoolerDelay(float outdoorTemp) {
    if (outdoorTemp > 0.0f) return 20000UL;
    if (outdoorTemp >= -5.0f) return 17500UL;
    if (outdoorTemp >= -10.0f) return 15000UL;
    if (outdoorTemp >= -15.0f) return 12500UL;
    return 10000UL;
}

// Стартовое значение loadOnLimit при входе в FAN_STARTING (до первой адаптации по BLOWN_AIR).
// Обратная зависимость от baseDelayMs: чем холоднее улица (меньше fanCoolerDelay по таблице
// выше), тем больше стартовый loadOnLimit — элемент греет дольше на старте в мороз.
unsigned long resetLoadOnLimit(unsigned long baseDelayMs) {
    return 20000UL - (baseDelayMs / 2UL);
}

// Адаптация длительностей ON/OFF элемента по датчику обдува (BLOWN_AIR) — CLAUDE.md §3.1.
// sensorMaxTemp = targetSensorTemp + sensorTempDiff (дефолт target+15=20°C, нижняя граница
// нейтральной зоны — sensorMaxTemp-5=15°C).
DutyLimits adaptDutyLimits(float blownAirTemp, float sensorMaxTemp, unsigned long currentLoadOnLimitMs, unsigned long baseDelayMs) {
    DutyLimits result;
    if (blownAirTemp >= sensorMaxTemp) {
        // Поток перегрет — сокращаем нагрев (не ниже baseDelay/4), удлиняем продувку.
        unsigned long decreased = currentLoadOnLimitMs - 1000UL;
        unsigned long floorLimit = baseDelayMs / 4UL;
        result.loadOnLimit = (decreased < floorLimit) ? floorLimit : decreased;
        result.loadOffLimit = (unsigned long)((float)baseDelayMs / 1.5f);
    } else if (blownAirTemp <= sensorMaxTemp - 5.0f) {
        // Поток холодный — удлиняем нагрев, сокращаем продувку.
        result.loadOnLimit = currentLoadOnLimitMs + 1000UL;
        result.loadOffLimit = (unsigned long)((float)baseDelayMs / 2.5f);
    } else {
        // Нейтральная зона — loadOnLimit не трогаем, loadOffLimit к среднему значению.
        result.loadOnLimit = currentLoadOnLimitMs;
        result.loadOffLimit = baseDelayMs / 2UL;
    }
    return result;
}

// Фиксированная калибровочная поправка датчика уличного воздуха — CLAUDE.md §3.2, дефолт -1.0°C.
float applyAirTempCalibration(float rawTemp, float offsetC) {
    return rawTemp + offsetC;
}

// Термоконтур вспомогательного нагревателя (D7) по уличному воздуху, гистерезис ±1.0°C — §3.2.
bool shouldAuxHeaterTurnOn(float airTemp, float targetAirTemp, float hysteresis) {
    return airTemp < (targetAirTemp - hysteresis);
}

bool shouldAuxHeaterTurnOff(float airTemp, float targetAirTemp, float hysteresis) {
    return airTemp > (targetAirTemp + hysteresis);
}

// Многоуровневая эскалация защиты радиатора SSR (D4) + вентилятор охлаждения (D8) — CLAUDE.md §3.3.
// Чистая функция состояния: не трогает GPIO напрямую, только возвращает решение — вызывающий
// код (main.cpp) применяет его к выходам.
RadiatorDecision evaluateRadiator(const RadiatorInput& in) {
    RadiatorDecision out;

    // Отказ самого датчика радиатора не может остаться незамеченным — трактуем как критический
    // перегрев без права на сомнение (не FAN_FAULT, сразу OVERTEMP).
    if (!in.sensorValid) {
        out.fanOn = true;
        out.alarmState = RadiatorAlarmState::OVERTEMP;
        out.forceLoadsOff = true;
        out.fanOnSince = 0UL;
        return out;
    }

    // Сброс аварии — автоматический, по остыванию ниже RADIATOR_RECOVERY_TEMP (15°C),
    // без ручного MQTT-подтверждения (CLAUDE.md §0 решение 7).
    RadiatorAlarmState alarm = in.previousAlarm;
    if (alarm != RadiatorAlarmState::NORMAL && in.radiatorTemp < RADIATOR_RECOVERY_TEMP) {
        alarm = RadiatorAlarmState::NORMAL;
    }

    // fanOnSince фиксирует момент включения вентилятора охлаждения — нужен ниже для отсчёта
    // минимального времени работы перед проверкой отказа (вариант B, без тахометра).
    bool fanShouldBeOn = in.radiatorTemp >= RADIATOR_FAN_ON_TEMP;
    unsigned long fanOnSince = in.fanOnSince;
    if (fanShouldBeOn && !in.fanWasOn) {
        fanOnSince = in.nowMillis;
    } else if (!fanShouldBeOn) {
        fanOnSince = 0UL;
    }

    if (in.radiatorTemp >= RADIATOR_CRITICAL_TEMP) {
        // Критический порог (35°C) — безусловно, независимо от вентилятора и его истории.
        alarm = RadiatorAlarmState::OVERTEMP;
    } else if (in.radiatorTemp >= RADIATOR_FAN_FAULT_TEMP && fanOnSince != 0UL &&
               (in.nowMillis - fanOnSince) >= RADIATOR_FAN_MIN_RUNTIME_MS) {
        // Промежуточный порог (33°C) не спадает спустя минимальное время работы вентилятора
        // (60с) — вентилятор считается неисправным, эскалируем не дожидаясь 35°C.
        alarm = RadiatorAlarmState::FAN_FAULT;
    }

    out.fanOn = fanShouldBeOn;
    out.alarmState = alarm;
    out.forceLoadsOff = (alarm != RadiatorAlarmState::NORMAL); // любая авария — обе нагрузки на радиаторе гасятся
    out.fanOnSince = fanOnSince;
    return out;
}

// Watchdog связи (WiFi+MQTT) — CLAUDE.md §3.5. Чистая проверка таймаута; main.cpp сам решает,
// что считать "полностью подключено" и когда сбрасывать lastFullyConnectedMillis.
bool shouldRestartForWatchdog(unsigned long lastFullyConnectedMillis, unsigned long nowMillis, unsigned long watchdogTimeoutMs) {
    return (nowMillis - lastFullyConnectedMillis) >= watchdogTimeoutMs;
}

// Валидация MQTT-команды SensorTempDiff (топик garage/heat/SensorTempDiff) — диапазон 5..50.
bool isValidSensorTempDiff(float diff) {
    return diff >= SENSOR_TEMP_DIFF_MIN && diff <= SENSOR_TEMP_DIFF_MAX;
}

// Строковое представление RadiatorAlarmState для поля radiatorAlarm в JSON-статусе.
static const char* radiatorAlarmToString(RadiatorAlarmState state) {
    switch (state) {
        case RadiatorAlarmState::FAN_FAULT: return "FAN_FAULT";
        case RadiatorAlarmState::OVERTEMP:  return "OVERTEMP";
        default:                            return "NORMAL";
    }
}

void buildStatusJson(const DeviceStatus& status, char* outBuffer, size_t bufferSize) {
    JsonDocument doc;

    // Датчик обдува пушки (в потоке горячего воздуха от элемента) — вход адаптации duty-цикла (CLAUDE.md §2/§3.1)
    if (status.blownAirValid) doc["blownAirTemp"] = status.blownAirTemp; else doc["blownAirTemp"] = nullptr;
    // Целевой датчик зоны хранения — по нему держится targetSensorTemp с гистерезисом targetHysteresis (§3.1)
    if (status.targetValid)   doc["targetTemp"]   = status.targetTemp;   else doc["targetTemp"]   = nullptr;
    // Информационный датчик — ни в каких расчётах не участвует, только для наблюдения (§2)
    if (status.infoValid)     doc["infoTemp"]     = status.infoTemp;     else doc["infoTemp"]     = nullptr;
    // Уличный воздух (с калибровочной поправкой) — вход термоконтура вспом. нагревателя и таблицы fanCoolerDelay (§3.1/§3.2)
    if (status.outdoorValid)  doc["outdoorTemp"]  = status.outdoorTemp;  else doc["outdoorTemp"]  = nullptr;
    // Радиатор SSR (общий для D6/D7) — вход эскалации перегрева radiatorAlarm (§3.3)
    if (status.radiatorValid) doc["radiatorTemp"] = status.radiatorTemp; else doc["radiatorTemp"] = nullptr;

    doc["fanOn"] = status.fanOn;                 // выход D5: вентилятор тепловой пушки (триак)
    doc["elementOn"] = status.elementOn;         // выход D6: нагревательный элемент пушки (не включается без fanOn, §3.4)
    doc["auxOn"] = status.auxOn;                 // выход D7: вспомогательный нагреватель
    doc["radiatorFanOn"] = status.radiatorFanOn; // выход D8: вентилятор охлаждения радиатора SSR

    doc["fanHeaterEnabled"] = status.fanHeaterEnabled; // текущее значение MQTT-флага garage/heat/fanHeater (не физический выход D5/D6)
    doc["caloriferEnabled"] = status.caloriferEnabled; // текущее значение MQTT-флага garage/heat/calorifer (не физический выход D7)

    doc["targetSensorTemp"] = status.targetSensorTemp; // текущий таргет хранения (MQTT: targetSensorTemp), дефолт 5.0°C
    doc["sensorTempDiff"] = status.sensorTempDiff;     // текущий порог адаптации duty-цикла (MQTT: SensorTempDiff), дефолт 15
    doc["targetAirTemp"] = status.targetAirTemp;       // текущий таргет вспом. нагревателя (MQTT: targetAirTemp), дефолт 10.0°C
    doc["targetHysteresis"] = status.targetHysteresis; // гистерезис таргета хранения, константа ±0.5°C
    doc["auxHysteresis"] = status.auxHysteresis;       // гистерезис таргета вспом. нагревателя, константа ±1.0°C

    doc["radiatorAlarm"] = radiatorAlarmToString(status.radiatorAlarm); // "NORMAL"/"FAN_FAULT"/"OVERTEMP" — эскалация §3.3

    doc["uptimeSeconds"] = status.uptimeSeconds; // время с последней загрузки, секунды
    doc["freeHeapBytes"] = status.freeHeapBytes; // свободная куча ESP8266, байты — индикатор утечек памяти
    doc["rssiDbm"] = status.rssiDbm;             // уровень сигнала WiFi, дБм (отрицательное число, ближе к 0 — сильнее)
    doc["wifiConnected"] = status.wifiConnected; // подключение к WiFi (есть IP)
    doc["mqttConnected"] = status.mqttConnected; // подключение к MQTT-брокеру

    serializeJson(doc, outBuffer, bufferSize);
}
