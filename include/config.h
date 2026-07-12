#pragma once

// === Пины (Wemos D1 mini, макросы D0..D8 из ядра Arduino для esp8266) ===
#define PIN_CANNON_FAN       D5   // вентилятор тепловой пушки (триак MOC3066/BTA16)
#define PIN_CANNON_ELEMENT   D6   // нагревательный элемент тепловой пушки (SSR)
#define PIN_AUX_HEATER       D7   // вспомогательный нагреватель (SSR)
#define PIN_RADIATOR_FAN     D8   // вентилятор охлаждения радиатора SSR
#define PIN_ONEWIRE_OUTDOOR  D2   // DS18b20 уличного воздуха
#define PIN_ONEWIRE_ROLES    D3   // 3x DS18b20: BLOWN_AIR/TARGET/INFO
#define PIN_ONEWIRE_RADIATOR D4   // DS18b20 радиатора SSR

// === ROM-адреса трёх DS18b20 на шине D3 (CLAUDE.md §2/§7) ===
// ЗАГЛУШКИ. Снять реальные ROM-коды сканирующим скетчем перед прошивкой
// реального железа — без этого роли BLOWN_AIR/TARGET/INFO не определены.
#define ROM_BLOWN_AIR { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define ROM_TARGET    { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }
#define ROM_INFO      { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 }

// === Тепловая пушка: целевая температура хранения (TARGET) ===
#define DEFAULT_TARGET_STORAGE_TEMP   5.0f
#define TARGET_STORAGE_HYSTERESIS     0.5f

// === Duty-цикл нагревательного элемента, адаптация по BLOWN_AIR ===
#define DEFAULT_SENSOR_TEMP_DIFF      15.0f   // sensorMaxTemp = target + diff
#define SENSOR_TEMP_DIFF_MIN          5.0f
#define SENSOR_TEMP_DIFF_MAX          50.0f

// === Вспомогательный нагреватель (D7), термоконтур по уличному воздуху (D2) ===
#define DEFAULT_TARGET_AUX_AIR_TEMP   10.0f
#define TARGET_AUX_AIR_HYSTERESIS     1.0f
#define AIR_TEMP_CALIBRATION_OFFSET   (-1.0f)

// === fanCoolerDelay по уличной температуре (мс), таблица CLAUDE.md §3.1 ===
#define FAN_COOLER_DELAY_DEFAULT_MS   20000UL

// === Радиатор SSR (D4) + вентилятор охлаждения (D8), эскалация CLAUDE.md §3.3 ===
#define RADIATOR_FAN_ON_TEMP           30.0f
#define RADIATOR_FAN_FAULT_TEMP        33.0f
#define RADIATOR_CRITICAL_TEMP         35.0f
#define RADIATOR_RECOVERY_TEMP         15.0f
#define RADIATOR_FAN_MIN_RUNTIME_MS    60000UL
#define RADIATOR_SENSOR_POLL_PERIOD_MS 60000UL

// === Периоды опроса/публикации ===
#define FAST_SENSOR_POLL_PERIOD_MS    5000UL
#define DUTY_ADAPT_CHECK_PERIOD_MS    10000UL
#define MQTT_STATUS_PERIOD_MS         60000UL
#define MQTT_RECONNECT_PERIOD_MS      5000UL

// === Watchdog связи (WiFi+MQTT), CLAUDE.md §3.5 ===
#define WATCHDOG_TIMEOUT_MS           300000UL   // 5 минут

// === MQTT (без ID устройства, корневой топик используется напрямую) ===
#define MQTT_CLIENT_ID               "garage-heat-v2"
#define MQTT_TOPIC_STATUS            "garage/heat/status"
#define MQTT_TOPIC_STATE             "garage/heat/state"
#define MQTT_TOPIC_DEBUG             "garage/heat/debug"
#define MQTT_TOPIC_CMD_FAN_HEATER    "garage/heat/fanHeater"
#define MQTT_TOPIC_CMD_TARGET_TEMP   "garage/heat/targetSensorTemp"
#define MQTT_TOPIC_CMD_SENSOR_DIFF   "garage/heat/SensorTempDiff"
#define MQTT_TOPIC_CMD_CALORIFER     "garage/heat/calorifer"
#define MQTT_TOPIC_CMD_TARGET_AIR    "garage/heat/targetAirTemp"
#define MQTT_TOPIC_CMD_RESTART       "garage/heat/restart"
