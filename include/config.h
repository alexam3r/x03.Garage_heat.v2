#pragma once

// === Отладочный вывод в Serial ===
// 1 — печатать события (переходы состояний, сенсоры, MQTT, watchdog) в Serial
// для отладки; 0 — полностью отключить (макросы DBG_* схлопываются в no-op).
#define DEBUG_LOG 1

// === Пины (Wemos D1 mini, макросы D0..D8 из ядра Arduino для esp8266) ===
#define PIN_CANNON_FAN       D5   // вентилятор тепловой пушки (триак MOC3066/BTA16)
#define PIN_CANNON_ELEMENT   D6   // нагревательный элемент тепловой пушки (SSR)
#define PIN_AUX_HEATER       D7   // вспомогательный нагреватель (SSR)
#define PIN_RADIATOR_FAN     D8   // вентилятор охлаждения радиатора SSR
#define PIN_ONEWIRE_OUTDOOR  D2   // DS18b20 уличного воздуха
#define PIN_ONEWIRE_ROLES    D3   // 3x DS18b20: BLOWN_AIR/TARGET/INFO
#define PIN_ONEWIRE_RADIATOR D4   // DS18b20 радиатора SSR

// === ROM-адреса трёх DS18b20 на шине D3 (CLAUDE.md §2/§7) ===
// Реальные адреса, сняты tools/scan_ds18b20 (2026-07-13).
#define ROM_BLOWN_AIR { 0x28, 0x59, 0x79, 0x45, 0x92, 0x0F, 0x02, 0x9A }
#define ROM_TARGET    { 0x28, 0xAA, 0x92, 0x24, 0x18, 0x13, 0x02, 0xAD }
#define ROM_INFO      { 0x28, 0xAA, 0xB8, 0x85, 0x18, 0x13, 0x02, 0xCE }

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
// Порог сброса аварии — тот же, что и порог включения вентилятора (RADIATOR_FAN_ON_TEMP):
// остыл ниже точки, где вообще требуется охлаждение, — авария снята. Отдельного более низкого
// порога нет специально, чтобы сброс не зависел от сезона/уличной температуры.
#define RADIATOR_FAN_MIN_RUNTIME_MS    60000UL
#define RADIATOR_SENSOR_POLL_PERIOD_MS 10000UL
// Debounce отказа датчика D4: одиночный сбойный/отключённый опрос не эскалирует мгновенно до
// OVERTEMP (частая причина ложных срабатываний из-за наводок на 1-Wire) — нужно столько подряд
// неудачных опросов (при периоде опроса 10с это ~30 секунд).
#define RADIATOR_SENSOR_FAULT_DEBOUNCE_COUNT 3U

// === Периоды опроса/публикации ===
#define FAST_SENSOR_POLL_PERIOD_MS    5000UL
#define DUTY_ADAPT_CHECK_PERIOD_MS    10000UL
#define MQTT_STATE_PUBLISH_PERIOD_MS  60000UL   // период полной перепубликации всех .../state (CLAUDE.md §4)
#define MQTT_RECONNECT_PERIOD_MS      5000UL
// Дольше, чем MQTT_RECONNECT_PERIOD_MS: WPA2-handshake+DHCP обычно занимают несколько секунд —
// частый повторный WiFi.begin() посреди уже идущей попытки подключения мешает ей завершиться,
// а не ускоряет реконнект.
#define WIFI_RECONNECT_PERIOD_MS      15000UL

// === Watchdog связи (WiFi+MQTT), CLAUDE.md §3.5 ===
#define WATCHDOG_TIMEOUT_MS           300000UL   // 5 минут

// === MQTT (без ID устройства, корневой топик используется напрямую) ===
// Схема топиков (CLAUDE.md §4): у каждой управляемой величины — .../set (команда, подписка
// устройства) и .../state (подтверждённое устройством значение, публикация с retain). У чисто
// телеметрийных величин (сенсоры, физическое состояние выходов, диагностика) — только .../state.
// Единого JSON-статуса больше нет — каждая величина публикуется в свой собственный топик.
#define MQTT_CLIENT_ID                "garage-heat-v2"
#define MQTT_TOPIC_STATE              "garage/heat/state"   // LWT/доступность устройства целиком, retain
#define MQTT_TOPIC_DEBUG              "garage/heat/debug"

// --- Управляемые величины: команда (.../set, подписка) + подтверждение (.../state, публикация) ---
#define MQTT_TOPIC_FAN_HEATER_SET     "garage/heat/fanHeater/set"
#define MQTT_TOPIC_FAN_HEATER_STATE   "garage/heat/fanHeater/state"
#define MQTT_TOPIC_TARGET_TEMP_SET    "garage/heat/targetSensorTemp/set"
#define MQTT_TOPIC_TARGET_TEMP_STATE  "garage/heat/targetSensorTemp/state"
#define MQTT_TOPIC_SENSOR_DIFF_SET    "garage/heat/sensorTempDiff/set"
#define MQTT_TOPIC_SENSOR_DIFF_STATE  "garage/heat/sensorTempDiff/state"
#define MQTT_TOPIC_CALORIFER_SET      "garage/heat/calorifer/set"
#define MQTT_TOPIC_CALORIFER_STATE    "garage/heat/calorifer/state"
#define MQTT_TOPIC_TARGET_AIR_SET     "garage/heat/targetAirTemp/set"
#define MQTT_TOPIC_TARGET_AIR_STATE   "garage/heat/targetAirTemp/state"
#define MQTT_TOPIC_RESTART_SET        "garage/heat/restart/set"   // действие, состояния нет

// --- Телеметрия: датчики температуры (§2) ---
#define MQTT_TOPIC_BLOWN_AIR_STATE     "garage/heat/blownAirTemp/state"
#define MQTT_TOPIC_TARGET_STATE        "garage/heat/targetTemp/state"
#define MQTT_TOPIC_INFO_STATE          "garage/heat/infoTemp/state"
#define MQTT_TOPIC_OUTDOOR_STATE       "garage/heat/outdoorTemp/state"
#define MQTT_TOPIC_RADIATOR_TEMP_STATE "garage/heat/radiatorTemp/state"
#define MQTT_TOPIC_RADIATOR_ALARM_STATE "garage/heat/radiatorAlarm/state"   // "NORMAL"/"FAN_FAULT"/"OVERTEMP"

// --- Телеметрия: физическое состояние силовых выходов (§1) ---
#define MQTT_TOPIC_FAN_ON_STATE          "garage/heat/fanOn/state"
#define MQTT_TOPIC_ELEMENT_ON_STATE      "garage/heat/elementOn/state"
#define MQTT_TOPIC_AUX_ON_STATE          "garage/heat/auxOn/state"
#define MQTT_TOPIC_RADIATOR_FAN_ON_STATE "garage/heat/radiatorFanOn/state"

// --- Телеметрия: константы гистерезиса (для наблюдаемости, по MQTT не изменяются) ---
#define MQTT_TOPIC_TARGET_HYSTERESIS_STATE "garage/heat/targetHysteresis/state"
#define MQTT_TOPIC_AUX_HYSTERESIS_STATE    "garage/heat/auxHysteresis/state"

// --- Телеметрия: диагностика устройства ---
#define MQTT_TOPIC_UPTIME_STATE          "garage/heat/uptimeSeconds/state"
#define MQTT_TOPIC_FREE_HEAP_STATE       "garage/heat/freeHeapBytes/state"
#define MQTT_TOPIC_RSSI_STATE            "garage/heat/rssiDbm/state"
#define MQTT_TOPIC_WIFI_CONNECTED_STATE  "garage/heat/wifiConnected/state"
#define MQTT_TOPIC_MQTT_CONNECTED_STATE  "garage/heat/mqttConnected/state"
