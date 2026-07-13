#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

#include "config.h"
#include "control_logic.h"
#include "secrets.h"

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

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static unsigned long lastMqttReconnectAttempt = 0;

static unsigned long lastStatusPublish = 0;

static bool cannonFanState = false;
static bool cannonElementState = false;
static bool auxHeaterState = false;
static bool radiatorFanState = false;

void setFan(bool on) {
    cannonFanState = on;
    digitalWrite(PIN_CANNON_FAN, on ? HIGH : LOW);
}

void setElement(bool on) {
    if (on && !cannonFanState) {
        Serial.println("setElement: refused, cannon fan is not running");
        return;
    }
    cannonElementState = on;
    digitalWrite(PIN_CANNON_ELEMENT, on ? HIGH : LOW);
}

void setAuxHeater(bool on) {
    auxHeaterState = on;
    digitalWrite(PIN_AUX_HEATER, on ? HIGH : LOW);
}

void setRadiatorFan(bool on) {
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
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void fastSensorTick() {
    unsigned long now = millis();
    if (now - lastFastPoll < FAST_SENSOR_POLL_PERIOD_MS) return;
    lastFastPoll = now;

    if (fastConversionPending) {
        float blownAirRaw = sensorsRoles.getTempC(romBlownAir);
        blownAirValid = (blownAirRaw != DEVICE_DISCONNECTED_C);
        blownAirTemp = blownAirValid ? blownAirRaw : NAN;

        float targetRaw = sensorsRoles.getTempC(romTarget);
        targetValid = (targetRaw != DEVICE_DISCONNECTED_C);
        targetTemp = targetValid ? targetRaw : NAN;

        float infoRaw = sensorsRoles.getTempC(romInfo);
        infoValid = (infoRaw != DEVICE_DISCONNECTED_C);
        infoTemp = infoValid ? infoRaw : NAN;

        float outdoorRaw = sensorOutdoor.getTempCByIndex(0);
        outdoorValid = (outdoorRaw != DEVICE_DISCONNECTED_C);
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
    }

    sensorRadiator.requestTemperatures();
    radiatorConversionPending = true;
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char value[32];
    unsigned int copyLen = (length < sizeof(value) - 1) ? length : sizeof(value) - 1;
    memcpy(value, payload, copyLen);
    value[copyLen] = '\0';

    String topicStr(topic);

    if (topicStr == MQTT_TOPIC_CMD_FAN_HEATER) {
        fanHeaterEnabled = (strcmp(value, "ON") == 0);
    } else if (topicStr == MQTT_TOPIC_CMD_TARGET_TEMP) {
        targetSensorTemp = atof(value);
    } else if (topicStr == MQTT_TOPIC_CMD_SENSOR_DIFF) {
        float diff = atof(value);
        if (isValidSensorTempDiff(diff)) {
            sensorTempDiff = diff;
        }
    } else if (topicStr == MQTT_TOPIC_CMD_CALORIFER) {
        caloriferEnabled = (strcmp(value, "ON") == 0);
    } else if (topicStr == MQTT_TOPIC_CMD_TARGET_AIR) {
        targetAirTemp = atof(value);
    } else if (topicStr == MQTT_TOPIC_CMD_RESTART) {
        setElement(false);
        setFan(false);
        setAuxHeater(false);
        setRadiatorFan(false);
        ESP.restart();
    }
}

static void mqttReconnectTick() {
    if (mqttClient.connected()) return;

    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt < MQTT_RECONNECT_PERIOD_MS) return;
    lastMqttReconnectAttempt = now;

    if (WiFi.status() != WL_CONNECTED) return;

    bool connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                                         MQTT_TOPIC_STATE, 0, true, "OFF");
    if (connected) {
        mqttClient.publish(MQTT_TOPIC_STATE, "ON", true);
        mqttClient.subscribe(MQTT_TOPIC_CMD_FAN_HEATER);
        mqttClient.subscribe(MQTT_TOPIC_CMD_TARGET_TEMP);
        mqttClient.subscribe(MQTT_TOPIC_CMD_SENSOR_DIFF);
        mqttClient.subscribe(MQTT_TOPIC_CMD_CALORIFER);
        mqttClient.subscribe(MQTT_TOPIC_CMD_TARGET_AIR);
        mqttClient.subscribe(MQTT_TOPIC_CMD_RESTART);
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

    RadiatorDecision decision = evaluateRadiator(in);

    setRadiatorFan(decision.fanOn);
    radiatorAlarmState = decision.alarmState;
    radiatorFanOnSince = decision.fanOnSince;

    if (decision.forceLoadsOff) {
        setElement(false);
        setFan(false);
        heaterState = HeaterState::OFF;
        setAuxHeater(false);
        auxHeaterLogicalState = AuxHeaterState::OFF;
    }
}

static void heaterTick() {
    unsigned long now = millis();

    switch (heaterState) {
        case HeaterState::OFF:
            if (fanHeaterEnabled && targetValid && shouldStartHeating(targetTemp, targetSensorTemp, TARGET_STORAGE_HYSTERESIS)) {
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
}

static void dutyAdaptTick() {
    unsigned long now = millis();
    if (now - lastDutyAdaptCheck < DUTY_ADAPT_CHECK_PERIOD_MS) return;
    lastDutyAdaptCheck = now;

    if (heaterState != HeaterState::ELEMENT_DUTY_ON && heaterState != HeaterState::ELEMENT_DUTY_OFF) return;
    if (!blownAirValid) return;

    float sensorMaxTemp = targetSensorTemp + sensorTempDiff;
    DutyLimits limits = adaptDutyLimits(blownAirTemp, sensorMaxTemp, loadOnLimit, fanCoolerDelay);
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

static void statusPublishTick() {
    unsigned long now = millis();
    if (now - lastStatusPublish < MQTT_STATUS_PERIOD_MS) return;
    lastStatusPublish = now;

    if (!mqttClient.connected()) return;

    DeviceStatus status;
    status.blownAirTemp = blownAirTemp; status.blownAirValid = blownAirValid;
    status.targetTemp = targetTemp;     status.targetValid = targetValid;
    status.infoTemp = infoTemp;         status.infoValid = infoValid;
    status.outdoorTemp = outdoorTemp;   status.outdoorValid = outdoorValid;
    status.radiatorTemp = radiatorTemp; status.radiatorValid = radiatorValid;

    status.fanOn = cannonFanState;
    status.elementOn = cannonElementState;
    status.auxOn = auxHeaterState;
    status.radiatorFanOn = radiatorFanState;

    status.targetSensorTemp = targetSensorTemp;
    status.sensorTempDiff = sensorTempDiff;
    status.targetAirTemp = targetAirTemp;
    status.targetHysteresis = TARGET_STORAGE_HYSTERESIS;
    status.auxHysteresis = TARGET_AUX_AIR_HYSTERESIS;

    status.radiatorAlarm = radiatorAlarmState;

    status.uptimeSeconds = millis() / 1000UL;
    status.freeHeapBytes = ESP.getFreeHeap();
    status.rssiDbm = WiFi.RSSI();
    status.wifiConnected = (WiFi.status() == WL_CONNECTED);
    status.mqttConnected = mqttClient.connected();

    char buffer[MQTT_STATUS_BUFFER_SIZE];
    buildStatusJson(status, buffer, sizeof(buffer));
    mqttClient.publish(MQTT_TOPIC_STATUS, buffer);
}

void setup() {
    safeInitOutputs();
    Serial.begin(115200);
    connectWiFi();

    sensorsRoles.begin();
    sensorsRoles.setWaitForConversion(false);
    sensorOutdoor.begin();
    sensorOutdoor.setWaitForConversion(false);
    sensorRadiator.begin();
    sensorRadiator.setWaitForConversion(false);

    mqttClient.setBufferSize(MQTT_STATUS_BUFFER_SIZE);
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        // Реконнект WiFi обрабатывается ESP8266WiFi автоматически (WIFI_STA);
        // явная неблокирующая проверка добавится вместе с watchdog (Task 16).
    }

    mqttReconnectTick();
    mqttClient.loop();

    fastSensorTick();
    radiatorSensorTick();
    radiatorEscalationTick();
    heaterTick();
    dutyAdaptTick();
    auxHeaterTick();
    statusPublishTick();
}
