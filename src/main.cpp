#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "control_logic.h"
#include "secrets.h"

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

void setup() {
    safeInitOutputs();
    Serial.begin(115200);
    connectWiFi();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        // Реконнект WiFi обрабатывается ESP8266WiFi автоматически (WIFI_STA);
        // явная неблокирующая проверка добавится вместе с watchdog (Task 16).
    }
}
