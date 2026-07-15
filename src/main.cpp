#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

#include "config.h"
#include "control_logic.h"
#include "secrets.h"

#if DEBUG_LOG
    #define DBG_PRINTLN(msg) Serial.println(msg)
    #define DBG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
    #define DBG_PRINTLN(msg)
    #define DBG_PRINTF(...)
#endif

static const char* heaterStateName(HeaterState s) {
    switch (s) {
        case HeaterState::OFF:              return "OFF";
        case HeaterState::FAN_STARTING:      return "FAN_STARTING";
        case HeaterState::ELEMENT_DUTY_ON:   return "ELEMENT_DUTY_ON";
        case HeaterState::ELEMENT_DUTY_OFF:  return "ELEMENT_DUTY_OFF";
        case HeaterState::COOLDOWN:          return "COOLDOWN";
    }
    return "?";
}

static const char* radiatorAlarmName(RadiatorAlarmState s) {
    switch (s) {
        case RadiatorAlarmState::NORMAL:    return "NORMAL";
        case RadiatorAlarmState::FAN_FAULT: return "FAN_FAULT";
        case RadiatorAlarmState::OVERTEMP:  return "OVERTEMP";
    }
    return "?";
}

static OneWire oneWireRoles(PIN_ONEWIRE_ROLES);
static DallasTemperature sensorsRoles(&oneWireRoles);
static OneWire oneWireOutdoor(PIN_ONEWIRE_OUTDOOR);
static DallasTemperature sensorOutdoor(&oneWireOutdoor);
static OneWire oneWireRadiator(PIN_ONEWIRE_RADIATOR);
static DallasTemperature sensorRadiator(&oneWireRadiator);

static DeviceAddress romBlownAir = ROM_BLOWN_AIR;
static DeviceAddress romTarget = ROM_TARGET;
static DeviceAddress romInfo = ROM_INFO;

static float blownAirTemp = NAN; static bool blownAirValid = false;
static float targetTemp = NAN;   static bool targetValid = false;
static float infoTemp = NAN;     static bool infoValid = false;
static float outdoorTempRaw = NAN; static bool outdoorValid = false;
static float outdoorTemp = NAN;    // откалиброванная (CLAUDE.md §3.2)
static float radiatorTemp = NAN; static bool radiatorValid = false;

static unsigned long fanCoolerDelay = FAN_COOLER_DELAY_DEFAULT_MS;

static bool fastConversionPending = false;
static bool radiatorConversionPending = false;

static bool prevBlownAirValid = false;
static bool prevTargetValid = false;
static bool prevInfoValid = false;
static bool prevOutdoorValid = false;
static bool prevRadiatorValid = false;

static unsigned long lastFastPoll = 0;
static unsigned long lastRadiatorPoll = 0;

static HeaterState heaterState = HeaterState::OFF;
static unsigned long dutyPhaseStartedAt = 0;
static unsigned long cooldownStartedAt = 0;
static unsigned long loadOnLimit = 0;
static unsigned long loadOffLimit = 0;
static unsigned long lastDutyAdaptCheck = 0;

static bool fanHeaterEnabled = false;
static float targetSensorTemp = DEFAULT_TARGET_STORAGE_TEMP;
static float sensorTempDiff = DEFAULT_SENSOR_TEMP_DIFF;

static AuxHeaterState auxHeaterLogicalState = AuxHeaterState::OFF;
static RadiatorAlarmState radiatorAlarmState = RadiatorAlarmState::NORMAL;

static bool caloriferEnabled = false;
static float targetAirTemp = DEFAULT_TARGET_AUX_AIR_TEMP;

static unsigned long radiatorFanOnSince = 0;
static unsigned int radiatorConsecutiveInvalidReads = 0;

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static unsigned long lastMqttReconnectAttempt = 0;
static unsigned long lastWifiReconnectAttempt = 0;

static unsigned long lastStatePublish = 0;
static unsigned long lastFullyConnectedMillis = 0;

static bool cannonFanState = false;
static bool cannonElementState = false;
static bool auxHeaterState = false;
static bool radiatorFanState = false;

void setFan(bool on) {
    if (on != cannonFanState) DBG_PRINTF("output CANNON_FAN: %s\n", on ? "ON" : "OFF");
    cannonFanState = on;
    digitalWrite(PIN_CANNON_FAN, on ? HIGH : LOW);
}

void setElement(bool on) {
    if (on && !cannonFanState) {
        Serial.println("setElement: refused, cannon fan is not running");
        return;
    }
    if (on != cannonElementState) DBG_PRINTF("output CANNON_ELEMENT: %s\n", on ? "ON" : "OFF");
    cannonElementState = on;
    digitalWrite(PIN_CANNON_ELEMENT, on ? HIGH : LOW);
}

void setAuxHeater(bool on) {
    if (on != auxHeaterState) DBG_PRINTF("output AUX_HEATER: %s\n", on ? "ON" : "OFF");
    auxHeaterState = on;
    digitalWrite(PIN_AUX_HEATER, on ? HIGH : LOW);
}

void setRadiatorFan(bool on) {
    if (on != radiatorFanState) DBG_PRINTF("output RADIATOR_FAN: %s\n", on ? "ON" : "OFF");
    radiatorFanState = on;
    digitalWrite(PIN_RADIATOR_FAN, on ? HIGH : LOW);
}

static void safeInitOutputs() {
    pinMode(PIN_CANNON_FAN, OUTPUT);
    pinMode(PIN_CANNON_ELEMENT, OUTPUT);
    pinMode(PIN_AUX_HEATER, OUTPUT);
    pinMode(PIN_RADIATOR_FAN, OUTPUT);
    digitalWrite(PIN_CANNON_FAN, LOW);
    digitalWrite(PIN_CANNON_ELEMENT, LOW);
    digitalWrite(PIN_AUX_HEATER, LOW);
    digitalWrite(PIN_RADIATOR_FAN, LOW);
}

static void connectWiFi() {
    DBG_PRINTF("WiFi: connecting to \"%s\"\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// Ядро ESP8266 Arduino НЕ переподключает WiFi само по себе после разрыва — без этого тика
// станция просто оставалась бы отключена до срабатывания 5-минутного watchdog (CLAUDE.md §3.5),
// который лишь перезагружает устройство целиком. Здесь — активная попытка восстановить связь тем
// же способом, что и на старте (connectWiFi()), периодически, пока соединения нет.
static void wifiReconnectTick() {
    if (WiFi.status() == WL_CONNECTED) return;

    unsigned long now = millis();
    if (now - lastWifiReconnectAttempt < WIFI_RECONNECT_PERIOD_MS) return;
    lastWifiReconnectAttempt = now;

    DBG_PRINTLN("WiFi: not connected, retrying");
    connectWiFi();
}

static void fastSensorTick() {
    unsigned long now = millis();
    if (now - lastFastPoll < FAST_SENSOR_POLL_PERIOD_MS) return;
    lastFastPoll = now;

    if (fastConversionPending) {
        float blownAirRaw = sensorsRoles.getTempC(romBlownAir);
        blownAirValid = (blownAirRaw != DEVICE_DISCONNECTED_C);
        blownAirTemp = blownAirValid ? blownAirRaw : NAN;
        if (blownAirValid != prevBlownAirValid) {
            DBG_PRINTF("sensor BLOWN_AIR: %s\n", blownAirValid ? "OK" : "DISCONNECTED");
            prevBlownAirValid = blownAirValid;
        }

        float targetRaw = sensorsRoles.getTempC(romTarget);
        targetValid = (targetRaw != DEVICE_DISCONNECTED_C);
        targetTemp = targetValid ? targetRaw : NAN;
        if (targetValid != prevTargetValid) {
            DBG_PRINTF("sensor TARGET: %s\n", targetValid ? "OK" : "DISCONNECTED");
            prevTargetValid = targetValid;
        }

        float infoRaw = sensorsRoles.getTempC(romInfo);
        infoValid = (infoRaw != DEVICE_DISCONNECTED_C);
        infoTemp = infoValid ? infoRaw : NAN;
        if (infoValid != prevInfoValid) {
            DBG_PRINTF("sensor INFO: %s\n", infoValid ? "OK" : "DISCONNECTED");
            prevInfoValid = infoValid;
        }

        float outdoorRaw = sensorOutdoor.getTempCByIndex(0);
        outdoorValid = (outdoorRaw != DEVICE_DISCONNECTED_C);
        if (outdoorValid != prevOutdoorValid) {
            DBG_PRINTF("sensor OUTDOOR: %s\n", outdoorValid ? "OK" : "DISCONNECTED");
            prevOutdoorValid = outdoorValid;
        }
        if (outdoorValid) {
            outdoorTempRaw = outdoorRaw;
            outdoorTemp = applyAirTempCalibration(outdoorTempRaw, AIR_TEMP_CALIBRATION_OFFSET);
            fanCoolerDelay = selectFanCoolerDelay(outdoorTemp);
        } else {
            outdoorTempRaw = NAN;
            outdoorTemp = NAN;
            fanCoolerDelay = FAN_COOLER_DELAY_DEFAULT_MS;
        }
    }

    sensorsRoles.requestTemperatures();
    sensorOutdoor.requestTemperatures();
    fastConversionPending = true;
}

static void radiatorSensorTick() {
    unsigned long now = millis();
    if (now - lastRadiatorPoll < RADIATOR_SENSOR_POLL_PERIOD_MS) return;
    lastRadiatorPoll = now;

    if (radiatorConversionPending) {
        float raw = sensorRadiator.getTempCByIndex(0);
        radiatorValid = (raw != DEVICE_DISCONNECTED_C);
        radiatorTemp = radiatorValid ? raw : NAN;
        if (radiatorValid != prevRadiatorValid) {
            DBG_PRINTF("sensor RADIATOR: %s\n", radiatorValid ? "OK" : "DISCONNECTED");
            prevRadiatorValid = radiatorValid;
        }
    }

    sensorRadiator.requestTemperatures();
    radiatorConversionPending = true;
}

// Публикация одного скалярного значения в свой топик .../state, retain=true (CLAUDE.md §4).
// Формат — простой текст, не JSON: топики теперь по одному на величину, обёртка не нужна.

static void publishFloatState(const char* topic, bool valid, float value) {
    char buf[16];
    if (valid) {
        snprintf(buf, sizeof(buf), "%.1f", value);
    } else {
        // HA MQTT sensor: payload "None" переводит сущность в состояние unknown (проверено
        // по документации интеграции mqtt.sensor) — так сигналим отвалившийся датчик.
        strcpy(buf, "None");
    }
    mqttClient.publish(topic, buf, true);
}

static void publishBoolState(const char* topic, bool value) {
    mqttClient.publish(topic, value ? "ON" : "OFF", true);
}

static void publishULongState(const char* topic, unsigned long value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", value);
    mqttClient.publish(topic, buf, true);
}

static void publishIntState(const char* topic, int value) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    mqttClient.publish(topic, buf, true);
}

static void publishStringState(const char* topic, const char* value) {
    mqttClient.publish(topic, value, true);
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char value[32];
    unsigned int copyLen = (length < sizeof(value) - 1) ? length : sizeof(value) - 1;
    memcpy(value, payload, copyLen);
    value[copyLen] = '\0';

    String topicStr(topic);
    DBG_PRINTF("MQTT cmd: %s = %s\n", topic, value);

    if (topicStr == MQTT_TOPIC_FAN_HEATER_SET) {
        fanHeaterEnabled = (strcmp(value, "ON") == 0);
        publishBoolState(MQTT_TOPIC_FAN_HEATER_STATE, fanHeaterEnabled);
    } else if (topicStr == MQTT_TOPIC_TARGET_TEMP_SET) {
        targetSensorTemp = atof(value);
        publishFloatState(MQTT_TOPIC_TARGET_TEMP_STATE, true, targetSensorTemp);
    } else if (topicStr == MQTT_TOPIC_SENSOR_DIFF_SET) {
        float diff = atof(value);
        if (isValidSensorTempDiff(diff)) {
            sensorTempDiff = diff;
            publishFloatState(MQTT_TOPIC_SENSOR_DIFF_STATE, true, sensorTempDiff);
        } else {
            DBG_PRINTF("MQTT cmd: sensorTempDiff=%.1f rejected, out of range\n", diff);
        }
    } else if (topicStr == MQTT_TOPIC_CALORIFER_SET) {
        caloriferEnabled = (strcmp(value, "ON") == 0);
        publishBoolState(MQTT_TOPIC_CALORIFER_STATE, caloriferEnabled);
    } else if (topicStr == MQTT_TOPIC_TARGET_AIR_SET) {
        targetAirTemp = atof(value);
        publishFloatState(MQTT_TOPIC_TARGET_AIR_STATE, true, targetAirTemp);
    } else if (topicStr == MQTT_TOPIC_RESTART_SET) {
        DBG_PRINTLN("MQTT cmd: restart requested, shutting down outputs");
        setElement(false);
        setFan(false);
        setAuxHeater(false);
        setRadiatorFan(false);
        ESP.restart();
    }
}

// Полная перепубликация всех .../state — раз в MQTT_STATE_PUBLISH_PERIOD_MS (см. statePublishTick)
// и сразу после (пере)подключения к брокеру (см. mqttReconnectTick), чтобы подписчики (Home
// Assistant и т.п.) не ждали до минуты после реконнекта, чтобы увидеть актуальные значения.
static void publishAllState() {
    if (!mqttClient.connected()) return;

    publishFloatState(MQTT_TOPIC_BLOWN_AIR_STATE, blownAirValid, blownAirTemp);
    publishFloatState(MQTT_TOPIC_TARGET_STATE, targetValid, targetTemp);
    publishFloatState(MQTT_TOPIC_INFO_STATE, infoValid, infoTemp);
    publishFloatState(MQTT_TOPIC_OUTDOOR_STATE, outdoorValid, outdoorTemp);
    publishFloatState(MQTT_TOPIC_RADIATOR_TEMP_STATE, radiatorValid, radiatorTemp);
    publishStringState(MQTT_TOPIC_RADIATOR_ALARM_STATE, radiatorAlarmToString(radiatorAlarmState));

    publishBoolState(MQTT_TOPIC_FAN_ON_STATE, cannonFanState);
    publishBoolState(MQTT_TOPIC_ELEMENT_ON_STATE, cannonElementState);
    publishBoolState(MQTT_TOPIC_AUX_ON_STATE, auxHeaterState);
    publishBoolState(MQTT_TOPIC_RADIATOR_FAN_ON_STATE, radiatorFanState);

    publishBoolState(MQTT_TOPIC_FAN_HEATER_STATE, fanHeaterEnabled);
    publishBoolState(MQTT_TOPIC_CALORIFER_STATE, caloriferEnabled);

    publishFloatState(MQTT_TOPIC_TARGET_TEMP_STATE, true, targetSensorTemp);
    publishFloatState(MQTT_TOPIC_SENSOR_DIFF_STATE, true, sensorTempDiff);
    publishFloatState(MQTT_TOPIC_TARGET_AIR_STATE, true, targetAirTemp);
    publishFloatState(MQTT_TOPIC_TARGET_HYSTERESIS_STATE, true, TARGET_STORAGE_HYSTERESIS);
    publishFloatState(MQTT_TOPIC_AUX_HYSTERESIS_STATE, true, TARGET_AUX_AIR_HYSTERESIS);

    publishULongState(MQTT_TOPIC_UPTIME_STATE, millis() / 1000UL);
    publishULongState(MQTT_TOPIC_FREE_HEAP_STATE, ESP.getFreeHeap());
    publishIntState(MQTT_TOPIC_RSSI_STATE, WiFi.RSSI());
    publishBoolState(MQTT_TOPIC_WIFI_CONNECTED_STATE, WiFi.status() == WL_CONNECTED);
    publishBoolState(MQTT_TOPIC_MQTT_CONNECTED_STATE, true); // публикуем -> значит подключены

    DBG_PRINTLN("state: published all topics");
}

// Критичные топики (CLAUDE.md §4) публикуются немедленно при изменении значения, а не только раз
// в минуту как остальные (heartbeat, см. statePublishTick/publishAllState) — минутная задержка
// неприемлема для аварийных сигналов и состояния силовых выходов. Сравнение со "снимком"
// последнего опубликованного значения на каждой итерации loop(). mqttConnected/state сюда
// намеренно не включён: эта функция и так публикует только когда клиент подключён (см. guard
// ниже), а сам факт (пере)подключения уже покрыт немедленной publishAllState() в
// mqttReconnectTick() и LWT (garage/heat/state).
static void publishCriticalStateIfChanged() {
    if (!mqttClient.connected()) return;

    static RadiatorAlarmState lastRadiatorAlarm = RadiatorAlarmState::NORMAL;
    static bool lastRadiatorValid = false;
    static float lastRadiatorTemp = NAN;
    static bool lastFanOn = false;
    static bool lastElementOn = false;
    static bool lastAuxOn = false;
    static bool lastRadiatorFanOn = false;
    static bool lastTargetValid = false;
    static float lastTargetTemp = NAN;
    static bool lastBlownAirValid = false;
    static float lastBlownAirTemp = NAN;
    static bool lastOutdoorValid = false;
    static float lastOutdoorTemp = NAN;
    static bool lastWifiConnected = false;

    if (radiatorAlarmState != lastRadiatorAlarm) {
        publishStringState(MQTT_TOPIC_RADIATOR_ALARM_STATE, radiatorAlarmToString(radiatorAlarmState));
        lastRadiatorAlarm = radiatorAlarmState;
    }
    if (radiatorValid != lastRadiatorValid || (radiatorValid && radiatorTemp != lastRadiatorTemp)) {
        publishFloatState(MQTT_TOPIC_RADIATOR_TEMP_STATE, radiatorValid, radiatorTemp);
        lastRadiatorValid = radiatorValid;
        lastRadiatorTemp = radiatorTemp;
    }
    if (cannonFanState != lastFanOn) {
        publishBoolState(MQTT_TOPIC_FAN_ON_STATE, cannonFanState);
        lastFanOn = cannonFanState;
    }
    if (cannonElementState != lastElementOn) {
        publishBoolState(MQTT_TOPIC_ELEMENT_ON_STATE, cannonElementState);
        lastElementOn = cannonElementState;
    }
    if (auxHeaterState != lastAuxOn) {
        publishBoolState(MQTT_TOPIC_AUX_ON_STATE, auxHeaterState);
        lastAuxOn = auxHeaterState;
    }
    if (radiatorFanState != lastRadiatorFanOn) {
        publishBoolState(MQTT_TOPIC_RADIATOR_FAN_ON_STATE, radiatorFanState);
        lastRadiatorFanOn = radiatorFanState;
    }
    if (targetValid != lastTargetValid || (targetValid && targetTemp != lastTargetTemp)) {
        publishFloatState(MQTT_TOPIC_TARGET_STATE, targetValid, targetTemp);
        lastTargetValid = targetValid;
        lastTargetTemp = targetTemp;
    }
    if (blownAirValid != lastBlownAirValid || (blownAirValid && blownAirTemp != lastBlownAirTemp)) {
        publishFloatState(MQTT_TOPIC_BLOWN_AIR_STATE, blownAirValid, blownAirTemp);
        lastBlownAirValid = blownAirValid;
        lastBlownAirTemp = blownAirTemp;
    }
    if (outdoorValid != lastOutdoorValid || (outdoorValid && outdoorTemp != lastOutdoorTemp)) {
        publishFloatState(MQTT_TOPIC_OUTDOOR_STATE, outdoorValid, outdoorTemp);
        lastOutdoorValid = outdoorValid;
        lastOutdoorTemp = outdoorTemp;
    }
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected != lastWifiConnected) {
        publishBoolState(MQTT_TOPIC_WIFI_CONNECTED_STATE, wifiConnected);
        lastWifiConnected = wifiConnected;
    }
}

static void mqttReconnectTick() {
    if (mqttClient.connected()) return;

    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt < MQTT_RECONNECT_PERIOD_MS) return;
    lastMqttReconnectAttempt = now;

    if (WiFi.status() != WL_CONNECTED) return;

    DBG_PRINTLN("MQTT: connecting to broker");
    bool connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                                         MQTT_TOPIC_STATE, 0, true, "OFF");
    if (connected) {
        DBG_PRINTLN("MQTT: connected, subscribing to command topics");
        mqttClient.publish(MQTT_TOPIC_STATE, "ON", true);
        mqttClient.subscribe(MQTT_TOPIC_FAN_HEATER_SET);
        mqttClient.subscribe(MQTT_TOPIC_TARGET_TEMP_SET);
        mqttClient.subscribe(MQTT_TOPIC_SENSOR_DIFF_SET);
        mqttClient.subscribe(MQTT_TOPIC_CALORIFER_SET);
        mqttClient.subscribe(MQTT_TOPIC_TARGET_AIR_SET);
        mqttClient.subscribe(MQTT_TOPIC_RESTART_SET);
        publishAllState();
    } else {
        DBG_PRINTF("MQTT: connect failed, state=%d\n", mqttClient.state());
    }
}

static void radiatorEscalationTick() {
    RadiatorInput in;
    in.radiatorTemp = radiatorTemp;
    in.sensorValid = radiatorValid;
    in.nowMillis = millis();
    in.fanOnSince = radiatorFanOnSince;
    in.fanWasOn = radiatorFanState;
    in.previousAlarm = radiatorAlarmState;
    in.consecutiveInvalidReads = radiatorConsecutiveInvalidReads;

    RadiatorDecision decision = evaluateRadiator(in);

    setRadiatorFan(decision.fanOn);
    if (decision.alarmState != radiatorAlarmState) {
        DBG_PRINTF("radiator alarm: %s -> %s\n",
                   radiatorAlarmName(radiatorAlarmState), radiatorAlarmName(decision.alarmState));
    }
    radiatorAlarmState = decision.alarmState;
    radiatorFanOnSince = decision.fanOnSince;
    radiatorConsecutiveInvalidReads = decision.consecutiveInvalidReads;

    static bool wasForcedOff = false;
    if (decision.forceLoadsOff) {
        if (!wasForcedOff) DBG_PRINTLN("radiator: forcing all loads off");
        wasForcedOff = true;
        setElement(false);
        setFan(false);
        heaterState = HeaterState::OFF;
        setAuxHeater(false);
        auxHeaterLogicalState = AuxHeaterState::OFF;
    } else {
        wasForcedOff = false;
    }
}

static void heaterTick() {
    unsigned long now = millis();
    HeaterState prevState = heaterState;

    switch (heaterState) {
        case HeaterState::OFF:
            // targetValid/blownAirValid — см. mustStop ниже (тот же критерий на старте, не
            // только во время работы). radiatorAlarmState обязателен здесь же: без этой
            // проверки radiatorEscalationTick() каждую итерацию сбрасывает heaterState в OFF
            // и глушит вентилятор, а эта ветка тут же снова его включает — дребезг D5 на
            // каждом loop() пока авария активна. auxHeaterTick() уже требует NORMAL первым
            // условием — здесь та же защита для симметрии.
            if (fanHeaterEnabled && targetValid && blownAirValid &&
                radiatorAlarmState == RadiatorAlarmState::NORMAL &&
                shouldStartHeating(targetTemp, targetSensorTemp, TARGET_STORAGE_HYSTERESIS)) {
                setFan(true);
                heaterState = HeaterState::FAN_STARTING;
                dutyPhaseStartedAt = now;
            }
            break;

        case HeaterState::FAN_STARTING:
            loadOnLimit = resetLoadOnLimit(fanCoolerDelay);
            loadOffLimit = fanCoolerDelay / 2UL;
            setElement(true);
            heaterState = HeaterState::ELEMENT_DUTY_ON;
            dutyPhaseStartedAt = now;
            break;

        case HeaterState::ELEMENT_DUTY_ON:
        case HeaterState::ELEMENT_DUTY_OFF: {
            bool mustStop = !fanHeaterEnabled || !targetValid || !blownAirValid ||
                             (targetValid && shouldStopHeating(targetTemp, targetSensorTemp, TARGET_STORAGE_HYSTERESIS));
            if (mustStop) {
                DBG_PRINTF("heater: stop requested (fanHeaterEnabled=%d targetValid=%d blownAirValid=%d)\n",
                           fanHeaterEnabled, targetValid, blownAirValid);
                setElement(false);
                heaterState = HeaterState::COOLDOWN;
                cooldownStartedAt = now;
                break;
            }

            if (heaterState == HeaterState::ELEMENT_DUTY_ON && (now - dutyPhaseStartedAt) >= loadOnLimit) {
                setElement(false);
                heaterState = HeaterState::ELEMENT_DUTY_OFF;
                dutyPhaseStartedAt = now;
            } else if (heaterState == HeaterState::ELEMENT_DUTY_OFF && (now - dutyPhaseStartedAt) >= loadOffLimit) {
                setElement(true);
                heaterState = HeaterState::ELEMENT_DUTY_ON;
                dutyPhaseStartedAt = now;
            }
            break;
        }

        case HeaterState::COOLDOWN:
            if (now - cooldownStartedAt >= fanCoolerDelay) {
                setFan(false);
                heaterState = HeaterState::OFF;
            }
            break;
    }

    if (heaterState != prevState) {
        DBG_PRINTF("heater state: %s -> %s\n", heaterStateName(prevState), heaterStateName(heaterState));
    }
}

static void dutyAdaptTick() {
    unsigned long now = millis();
    if (now - lastDutyAdaptCheck < DUTY_ADAPT_CHECK_PERIOD_MS) return;
    lastDutyAdaptCheck = now;

    if (heaterState != HeaterState::ELEMENT_DUTY_ON && heaterState != HeaterState::ELEMENT_DUTY_OFF) return;
    if (!blownAirValid) return;

    float sensorMaxTemp = targetSensorTemp + sensorTempDiff;
    DutyLimits limits = adaptDutyLimits(blownAirTemp, sensorMaxTemp, loadOnLimit, fanCoolerDelay);
    if (limits.loadOnLimit != loadOnLimit || limits.loadOffLimit != loadOffLimit) {
        DBG_PRINTF("duty adapt: loadOnLimit=%lu loadOffLimit=%lu (blownAir=%.1f)\n",
                   limits.loadOnLimit, limits.loadOffLimit, blownAirTemp);
    }
    loadOnLimit = limits.loadOnLimit;
    loadOffLimit = limits.loadOffLimit;
}

static void auxHeaterTick() {
    if (radiatorAlarmState != RadiatorAlarmState::NORMAL || !caloriferEnabled || !outdoorValid) {
        if (auxHeaterLogicalState != AuxHeaterState::OFF) {
            setAuxHeater(false);
            auxHeaterLogicalState = AuxHeaterState::OFF;
        }
        return;
    }

    if (auxHeaterLogicalState == AuxHeaterState::ON && shouldAuxHeaterTurnOff(outdoorTemp, targetAirTemp, TARGET_AUX_AIR_HYSTERESIS)) {
        setAuxHeater(false);
        auxHeaterLogicalState = AuxHeaterState::OFF;
    } else if (auxHeaterLogicalState == AuxHeaterState::OFF && shouldAuxHeaterTurnOn(outdoorTemp, targetAirTemp, TARGET_AUX_AIR_HYSTERESIS)) {
        setAuxHeater(true);
        auxHeaterLogicalState = AuxHeaterState::ON;
    }
}

static void statePublishTick() {
    unsigned long now = millis();
    if (now - lastStatePublish < MQTT_STATE_PUBLISH_PERIOD_MS) return;
    lastStatePublish = now;
    publishAllState();
}

static void connectivityWatchdogTick() {
    unsigned long now = millis();
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool mqttConnected = mqttClient.connected();
    bool fullyConnected = wifiConnected && mqttConnected;

    static bool prevFullyConnected = true;
    if (fullyConnected != prevFullyConnected) {
        DBG_PRINTF("watchdog: connectivity %s (wifi=%d mqtt=%d)\n",
                   fullyConnected ? "restored" : "lost", wifiConnected, mqttConnected);
        prevFullyConnected = fullyConnected;
    }

    if (fullyConnected) {
        lastFullyConnectedMillis = now;
        return;
    }

    if (shouldRestartForWatchdog(lastFullyConnectedMillis, now, WATCHDOG_TIMEOUT_MS)) {
        DBG_PRINTLN("watchdog: timeout exceeded, shutting down outputs and restarting");
        setElement(false);
        setFan(false);
        setAuxHeater(false);
        setRadiatorFan(false);
        ESP.restart();
    }
}

void setup() {
    safeInitOutputs();
    Serial.begin(115200);
    DBG_PRINTLN("garage-heat-v2: booting, outputs safe-initialized");
    connectWiFi();

    sensorsRoles.begin();
    sensorsRoles.setWaitForConversion(false);
    sensorOutdoor.begin();
    sensorOutdoor.setWaitForConversion(false);
    sensorRadiator.begin();
    sensorRadiator.setWaitForConversion(false);

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    lastFullyConnectedMillis = millis();
    DBG_PRINTLN("garage-heat-v2: setup complete");
}

void loop() {
    wifiReconnectTick();
    mqttReconnectTick();
    mqttClient.loop();

    fastSensorTick();
    radiatorSensorTick();
    radiatorEscalationTick();
    heaterTick();
    dutyAdaptTick();
    auxHeaterTick();
    publishCriticalStateIfChanged();
    statePublishTick();
    connectivityWatchdogTick();
}
