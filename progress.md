# Garage Heat v2 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the C++/PlatformIO ESP8266 firmware for the garage motorcycle-storage heater exactly as specified in `CLAUDE.md`, driven by the architecture in `docs/superpowers/specs/2026-07-09-garage-heat-firmware-architecture-design.md`.

**Architecture:** Pure decision functions (hysteresis, duty-cycle adaptation, radiator escalation, watchdog, JSON status) live in `lib/control_logic/` and are unit-tested on PlatformIO's `native` platform with zero Arduino dependencies. `src/main.cpp` is thin wiring: it reads sensors, calls the pure functions, and drives GPIO/MQTT. All power-output writes go exclusively through `setFan()`/`setElement()`/`setAuxHeater()`/`setRadiatorFan()` so the fan/element interlock (CLAUDE.md §3.4) can never be bypassed.

**Tech Stack:** PlatformIO, `board = d1_mini`, `framework = arduino`. Libraries: PubSubClient (MQTT), ArduinoJson v7 (status JSON), OneWire + DallasTemperature (DS18b20, async conversion). Native unit tests via PlatformIO's `native` platform + Unity.

## Global Constraints

- Single `main.cpp` holds all control-flow logic (per CLAUDE.md project layout) — pure functions are the only code allowed to live outside it.
- `loop()` must never call `delay()`. Every periodic process uses an independent `millis()`-based timer.
- `include/secrets.h` is never committed (`.gitignore`); `include/secrets.h.example` is the committed template.
- No numeric threshold from `CLAUDE.md` may be changed. Where `CLAUDE.md` leaves a formula qualitative (duty-cycle adaptation), the exact legacy Lua formula recovered from `docs/check_sensors.lua`/`docs/main.lua` is authoritative (±1000 ms step, `baseDelay/4` floor, `baseDelay/1.5`/`/2.5`/`/2` off-limit divisors, reset `20000 - floor(baseDelay/2)`).
- ROM addresses for the three DS18b20 on D3 are unknown until real hardware is available — `config.h` ships with clearly-flagged placeholder byte arrays that compile but must be replaced before flashing to real hardware (CLAUDE.md §2/§7).
- **Every commit message must be written in Russian** (explicit user instruction).
- Fan/element interlock (CLAUDE.md §3.4) is structural: `digitalWrite` on D5/D6 must never appear outside `setFan()`/`setElement()`.

---

## Task 1: PlatformIO scaffold + control_logic skeleton

**Files:**
- Create: `platformio.ini`
- Create: `.gitignore`
- Create: `include/secrets.h.example`
- Create: `lib/control_logic/control_logic.h`
- Create: `lib/control_logic/control_logic.cpp`
- Create: `src/main.cpp` (empty stub, just enough to compile)

**Interfaces:**
- Produces: `HeaterState`, `AuxHeaterState`, `RadiatorAlarmState` enums (used by every later task).

- [x] **Step 1: Create `platformio.ini`**

```ini
[platformio]
default_envs = esp8266

[env]
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0

[env:esp8266]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200
lib_deps =
    ${env.lib_deps}
    knolleary/PubSubClient @ ^2.8
    milesburton/DallasTemperature @ ^3.11.0
    paulstoffregen/OneWire @ ^2.3.7

[env:native]
platform = native
lib_deps =
    ${env.lib_deps}
```

- [x] **Step 2: Create `.gitignore`**

```
.pio/
include/secrets.h
```

- [x] **Step 3: Create `include/secrets.h.example`**

```cpp
#pragma once

#define WIFI_SSID     "your-wifi-ssid"
#define WIFI_PASS     "your-wifi-password"
#define MQTT_BROKER   "192.168.1.10"
#define MQTT_PORT     1883
#define MQTT_USER     "your-mqtt-user"
#define MQTT_PASS     "your-mqtt-password"
```

- [x] **Step 4: Create `lib/control_logic/control_logic.h` with just the state enums**

```cpp
#pragma once

// Автоматы состояний (CLAUDE.md §3.1, §3.2, §3.3)
enum class HeaterState { OFF, FAN_STARTING, ELEMENT_DUTY_ON, ELEMENT_DUTY_OFF, COOLDOWN };
enum class AuxHeaterState { OFF, ON };
enum class RadiatorAlarmState { NORMAL, FAN_FAULT, OVERTEMP };
```

- [x] **Step 5: Create empty `lib/control_logic/control_logic.cpp`**

```cpp
#include "control_logic.h"
```

- [x] **Step 6: Create minimal `src/main.cpp` stub**

```cpp
#include <Arduino.h>

void setup() {
}

void loop() {
}
```

- [x] **Step 7: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS` (empty sketch links cleanly against the declared lib_deps)
Actual: `SUCCESS` — RAM 34.2%, Flash 24.9%.

- [x] **Step 8: Create an empty `test/` directory (PlatformIO requires it to exist before any `pio test` invocation, even a no-op one)**

Run: `mkdir -p test`

Note: `pio test -e native --without-testing` on a `test/` directory with zero test suites fails with `Nothing to build` — this is a PlatformIO CLI requirement, not a project error. Standalone verification that `lib/control_logic` compiles on the `native` platform happens together with the first real test in Task 3 (`pio test -e native`), not here.

- [x] **Step 9: Commit**

```bash
git add platformio.ini .gitignore include/secrets.h.example lib/control_logic src/main.cpp
git commit -m "$(cat <<'EOF'
Добавить структуру проекта PlatformIO и заготовку control_logic

Каркас под envs esp8266/native, шаблон secrets.h.example, пустой
main.cpp и заголовок control_logic.h с автоматами состояний.
EOF
)"
```

**Статус: выполнено.** Коммит `9e03e53`. esp8266 собирается (`SUCCESS`); нативная сборка `control_logic` будет подтверждена вместе с первым тестом в Задаче 3 (см. примечание к Step 8). Каркас прошёл проверку code-simplifier — упрощать нечего.

---

## Task 2: `config.h` — все константы

**Files:**
- Create: `include/config.h`

**Interfaces:**
- Produces: all `#define` constants consumed by `control_logic.cpp` (radiator thresholds) and by every `main.cpp` task (pins, topics, periods, defaults).

- [x] **Step 1: Write `include/config.h`**

```cpp
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
```

- [x] **Step 2: Verify it compiles standalone (no Arduino symbols used)**

`pio test -e native --without-testing` fails with `Nothing to build` on an empty
`test/` directory (same PlatformIO CLI limitation documented in Task 1, Step 8) —
`config.h` isn't consumed by any test suite yet, so this isn't a meaningful check here.
Used a direct compiler check instead: `g++ -std=gnu++17 -fsyntax-only -x c++ include/config.h`.
Actual: no errors (only a benign `#pragma once in main file` warning) — confirms
`config.h` contains only preprocessor macros with no Arduino-core dependency.

- [x] **Step 3: Commit**

```bash
git add include/config.h
git commit -m "$(cat <<'EOF'
Добавить config.h со всеми константами прошивки

Пины, пороги радиатора, таблица fanCoolerDelay, периоды опроса,
MQTT-топики и ROM-заглушки трёх DS18b20 — согласно CLAUDE.md §5.
EOF
)"
```

**Статус: выполнено.** Коммит `43b7d54`.

---

## Task 3: Гистерезис управления тепловой пушкой (TARGET)

**Files:**
- Modify: `lib/control_logic/control_logic.h`
- Modify: `lib/control_logic/control_logic.cpp`
- Create: `test/test_control_logic/test_main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `bool shouldStartHeating(float reading, float target, float hysteresis)`, `bool shouldStopHeating(float reading, float target, float hysteresis)` — used by `main.cpp` Task 11.

- [x] **Step 1: Write the failing test — create `test/test_control_logic/test_main.cpp`**

```cpp
#include <unity.h>
#include "control_logic.h"

void setUp(void) {}
void tearDown(void) {}

void test_shouldStartHeating_below_lower_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldStartHeating(4.4f, 5.0f, 0.5f));
}

void test_shouldStartHeating_at_lower_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldStartHeating(4.5f, 5.0f, 0.5f));
}

void test_shouldStopHeating_above_upper_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldStopHeating(5.6f, 5.0f, 0.5f));
}

void test_shouldStopHeating_at_upper_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldStopHeating(5.5f, 5.0f, 0.5f));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_shouldStartHeating_below_lower_hysteresis_returns_true);
    RUN_TEST(test_shouldStartHeating_at_lower_hysteresis_returns_false);
    RUN_TEST(test_shouldStopHeating_above_upper_hysteresis_returns_true);
    RUN_TEST(test_shouldStopHeating_at_upper_hysteresis_returns_false);
    return UNITY_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `pio test -e native`
Expected: build FAILS — linker error `undefined reference to 'shouldStartHeating(float, float, float)'`

- [x] **Step 3: Declare the functions in `control_logic.h`** (append after the enums)

```cpp
// Тепловая пушка: гистерезис TARGET (CLAUDE.md §3.1)
bool shouldStartHeating(float targetSensorReading, float targetSetpoint, float hysteresis);
bool shouldStopHeating(float targetSensorReading, float targetSetpoint, float hysteresis);
```

- [x] **Step 4: Implement in `control_logic.cpp`** (append)

```cpp
bool shouldStartHeating(float targetSensorReading, float targetSetpoint, float hysteresis) {
    return targetSensorReading < (targetSetpoint - hysteresis);
}

bool shouldStopHeating(float targetSensorReading, float targetSetpoint, float hysteresis) {
    return targetSensorReading > (targetSetpoint + hysteresis);
}
```

- [x] **Step 5: Run test to verify it passes**

Run: `pio test -e native`
Expected: `4 Tests 0 Failures 0 Ignored` — `PASSED`

- [x] **Step 6: Commit**

```bash
git add lib/control_logic test/test_control_logic
git commit -m "$(cat <<'EOF'
Реализовать гистерезис контура тепловой пушки по TARGET

shouldStartHeating/shouldStopHeating: ±0.5°C вокруг targetSensorTemp,
CLAUDE.md §3.1. Первый нативный TDD-цикл на platform=native.
EOF
)"
```

**Статус: выполнено.** Коммит `f88b6d6`. 4/4 теста PASSED.

---

## Task 4: fanCoolerDelay + адаптация duty-цикла по BLOWN_AIR

**Files:**
- Modify: `lib/control_logic/control_logic.h`
- Modify: `lib/control_logic/control_logic.cpp`
- Modify: `test/test_control_logic/test_main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `unsigned long selectFanCoolerDelay(float outdoorTemp)`, `unsigned long resetLoadOnLimit(unsigned long baseDelayMs)`, `struct DutyLimits { unsigned long loadOnLimit; unsigned long loadOffLimit; }`, `DutyLimits adaptDutyLimits(float blownAirTemp, float sensorMaxTemp, unsigned long currentLoadOnLimitMs, unsigned long baseDelayMs)` — used by `main.cpp` Tasks 10/11.

- [x] **Step 1: Write the failing tests — append to `test/test_control_logic/test_main.cpp`**

```cpp
void test_selectFanCoolerDelay_above_zero_returns_20000(void) {
    TEST_ASSERT_EQUAL_UINT32(20000UL, selectFanCoolerDelay(5.0f));
}

void test_selectFanCoolerDelay_zero_returns_17500(void) {
    TEST_ASSERT_EQUAL_UINT32(17500UL, selectFanCoolerDelay(0.0f));
}

void test_selectFanCoolerDelay_minus_five_returns_17500(void) {
    TEST_ASSERT_EQUAL_UINT32(17500UL, selectFanCoolerDelay(-5.0f));
}

void test_selectFanCoolerDelay_minus_ten_returns_15000(void) {
    TEST_ASSERT_EQUAL_UINT32(15000UL, selectFanCoolerDelay(-10.0f));
}

void test_selectFanCoolerDelay_minus_fifteen_returns_12500(void) {
    TEST_ASSERT_EQUAL_UINT32(12500UL, selectFanCoolerDelay(-15.0f));
}

void test_selectFanCoolerDelay_below_minus_fifteen_returns_10000(void) {
    TEST_ASSERT_EQUAL_UINT32(10000UL, selectFanCoolerDelay(-20.0f));
}

void test_resetLoadOnLimit_formula(void) {
    TEST_ASSERT_EQUAL_UINT32(10000UL, resetLoadOnLimit(20000UL));
}

void test_adaptDutyLimits_hot_decreases_on_limit(void) {
    DutyLimits result = adaptDutyLimits(20.0f, 20.0f, 10000UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(9000UL, result.loadOnLimit);
    TEST_ASSERT_EQUAL_UINT32(13333UL, result.loadOffLimit);
}

void test_adaptDutyLimits_hot_clamps_at_floor(void) {
    DutyLimits result = adaptDutyLimits(20.0f, 20.0f, 5200UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(5000UL, result.loadOnLimit); // floor = baseDelay/4 = 5000
}

void test_adaptDutyLimits_cold_increases_on_limit(void) {
    DutyLimits result = adaptDutyLimits(15.0f, 20.0f, 10000UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(11000UL, result.loadOnLimit);
    TEST_ASSERT_EQUAL_UINT32(8000UL, result.loadOffLimit);
}

void test_adaptDutyLimits_neutral_zone_keeps_on_limit(void) {
    DutyLimits result = adaptDutyLimits(17.0f, 20.0f, 10000UL, 20000UL);
    TEST_ASSERT_EQUAL_UINT32(10000UL, result.loadOnLimit);
    TEST_ASSERT_EQUAL_UINT32(10000UL, result.loadOffLimit);
}
```

Add the matching `RUN_TEST(...)` lines to `main()` in the same file, before `return UNITY_END();`.

- [x] **Step 2: Run test to verify it fails**

Run: `pio test -e native`
Expected: build FAILS — `undefined reference to 'selectFanCoolerDelay(float)'`

- [x] **Step 3: Declare in `control_logic.h`** (append)

```cpp
// fanCoolerDelay по уличной температуре (CLAUDE.md §3.1, таблица)
unsigned long selectFanCoolerDelay(float outdoorTemp);

// Duty-цикл нагревательного элемента, адаптация по BLOWN_AIR (CLAUDE.md §3.1)
struct DutyLimits {
    unsigned long loadOnLimit;
    unsigned long loadOffLimit;
};

unsigned long resetLoadOnLimit(unsigned long baseDelayMs);
DutyLimits adaptDutyLimits(float blownAirTemp, float sensorMaxTemp, unsigned long currentLoadOnLimitMs, unsigned long baseDelayMs);
```

- [x] **Step 4: Implement in `control_logic.cpp`** (append)

```cpp
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
```

- [x] **Step 5: Run test to verify it passes**

Run: `pio test -e native`
Expected: `15 Tests 0 Failures 0 Ignored` — `PASSED`

- [x] **Step 6: Commit**

```bash
git add lib/control_logic test/test_control_logic
git commit -m "$(cat <<'EOF'
Реализовать выбор fanCoolerDelay и адаптацию duty-цикла по BLOWN_AIR

Формула и каденция взяты как есть из старой Lua-прошивки
(docs/check_sensors.lua, docs/main.lua): шаг ±1000мс, floor baseDelay/4,
делители 1.5/2.5/2 для loadOffLimit.
EOF
)"
```

**Статус: выполнено.** Коммит `c272ed9`. 15/15 тестов PASSED.

---

## Task 5: Вспомогательный нагреватель — калибровка и гистерезис

**Files:**
- Modify: `lib/control_logic/control_logic.h`
- Modify: `lib/control_logic/control_logic.cpp`
- Modify: `test/test_control_logic/test_main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `float applyAirTempCalibration(float rawTemp, float offsetC)`, `bool shouldAuxHeaterTurnOn(float airTemp, float targetAirTemp, float hysteresis)`, `bool shouldAuxHeaterTurnOff(float airTemp, float targetAirTemp, float hysteresis)` — used by `main.cpp` Tasks 10/12.

- [x] **Step 1: Write the failing tests — append to `test/test_control_logic/test_main.cpp`**

```cpp
void test_applyAirTempCalibration_subtracts_offset(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, applyAirTempCalibration(5.2f, -1.0f));
}

void test_shouldAuxHeaterTurnOn_below_lower_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldAuxHeaterTurnOn(8.9f, 10.0f, 1.0f));
}

void test_shouldAuxHeaterTurnOn_at_lower_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldAuxHeaterTurnOn(9.0f, 10.0f, 1.0f));
}

void test_shouldAuxHeaterTurnOff_above_upper_hysteresis_returns_true(void) {
    TEST_ASSERT_TRUE(shouldAuxHeaterTurnOff(11.1f, 10.0f, 1.0f));
}

void test_shouldAuxHeaterTurnOff_at_upper_hysteresis_returns_false(void) {
    TEST_ASSERT_FALSE(shouldAuxHeaterTurnOff(11.0f, 10.0f, 1.0f));
}
```

Add matching `RUN_TEST(...)` lines to `main()`.

- [x] **Step 2: Run test to verify it fails**

Run: `pio test -e native`
Expected: build FAILS — `undefined reference to 'applyAirTempCalibration(float, float)'`

- [x] **Step 3: Declare in `control_logic.h`** (append)

```cpp
// Вспомогательный нагреватель: калибровка + гистерезис (CLAUDE.md §3.2)
float applyAirTempCalibration(float rawTemp, float offsetC);
bool shouldAuxHeaterTurnOn(float airTemp, float targetAirTemp, float hysteresis);
bool shouldAuxHeaterTurnOff(float airTemp, float targetAirTemp, float hysteresis);
```

- [x] **Step 4: Implement in `control_logic.cpp`** (append)

```cpp
float applyAirTempCalibration(float rawTemp, float offsetC) {
    return rawTemp + offsetC;
}

bool shouldAuxHeaterTurnOn(float airTemp, float targetAirTemp, float hysteresis) {
    return airTemp < (targetAirTemp - hysteresis);
}

bool shouldAuxHeaterTurnOff(float airTemp, float targetAirTemp, float hysteresis) {
    return airTemp > (targetAirTemp + hysteresis);
}
```

- [x] **Step 5: Run test to verify it passes**

Run: `pio test -e native`
Expected: `20 Tests 0 Failures 0 Ignored` — `PASSED`

- [x] **Step 6: Commit**

```bash
git add lib/control_logic test/test_control_logic
git commit -m "$(cat <<'EOF'
Реализовать калибровку и гистерезис вспомогательного нагревателя

applyAirTempCalibration (-1°C), shouldAuxHeaterTurnOn/Off (±1°C
вокруг targetAirTemp) — CLAUDE.md §3.2.
EOF
)"
```

**Статус: выполнено.** Коммит `6b8596a`. 20/20 тестов PASSED.

---

## Task 6: Эскалация радиатора (перегрев + отказ вентилятора)

**Files:**
- Modify: `lib/control_logic/control_logic.h`
- Modify: `lib/control_logic/control_logic.cpp`
- Modify: `test/test_control_logic/test_main.cpp`

**Interfaces:**
- Consumes: `RadiatorAlarmState` enum (Task 1), `config.h` constants `RADIATOR_FAN_ON_TEMP`, `RADIATOR_FAN_FAULT_TEMP`, `RADIATOR_CRITICAL_TEMP`, `RADIATOR_RECOVERY_TEMP`, `RADIATOR_FAN_MIN_RUNTIME_MS` (Task 2).
- Produces: `struct RadiatorInput`, `struct RadiatorDecision`, `RadiatorDecision evaluateRadiator(const RadiatorInput& input)` — used by `main.cpp` Task 13.

This is the most safety-critical function in the firmware — it decides when to force-shutdown D5/D6/D7. Test every branch from CLAUDE.md §3.3 individually.

- [x] **Step 1: Write the failing tests — append to `test/test_control_logic/test_main.cpp`**

```cpp
static RadiatorInput makeRadiatorInput(float temp, bool valid, unsigned long now,
                                        unsigned long fanOnSince, bool fanWasOn,
                                        RadiatorAlarmState prevAlarm) {
    RadiatorInput in;
    in.radiatorTemp = temp;
    in.sensorValid = valid;
    in.nowMillis = now;
    in.fanOnSince = fanOnSince;
    in.fanWasOn = fanWasOn;
    in.previousAlarm = prevAlarm;
    return in;
}

void test_evaluateRadiator_cold_normal_fan_off(void) {
    RadiatorInput in = makeRadiatorInput(20.0f, true, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_FALSE(out.fanOn);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::NORMAL, (int)out.alarmState);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
    TEST_ASSERT_EQUAL_UINT32(0UL, out.fanOnSince);
}

void test_evaluateRadiator_crosses_fan_on_threshold_records_fanOnSince(void) {
    RadiatorInput in = makeRadiatorInput(31.0f, true, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_TRUE(out.fanOn);
    TEST_ASSERT_EQUAL_UINT32(100000UL, out.fanOnSince);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
}

void test_evaluateRadiator_cools_below_threshold_resets_fanOnSince(void) {
    RadiatorInput in = makeRadiatorInput(29.0f, true, 110000UL, 100000UL, true, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_FALSE(out.fanOn);
    TEST_ASSERT_EQUAL_UINT32(0UL, out.fanOnSince);
}

void test_evaluateRadiator_fan_fault_after_min_runtime(void) {
    // fan on since 100000, now 165000 => 65s elapsed >= 60s minimum, temp still >= 33
    RadiatorInput in = makeRadiatorInput(33.0f, true, 165000UL, 100000UL, true, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::FAN_FAULT, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
}

void test_evaluateRadiator_no_fault_before_min_runtime(void) {
    // only 30s elapsed, below the 60s minimum runtime
    RadiatorInput in = makeRadiatorInput(33.0f, true, 130000UL, 100000UL, true, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::NORMAL, (int)out.alarmState);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
}

void test_evaluateRadiator_critical_overtemp_forces_shutdown(void) {
    RadiatorInput in = makeRadiatorInput(36.0f, true, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::OVERTEMP, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
}

void test_evaluateRadiator_recovers_below_15c(void) {
    RadiatorInput in = makeRadiatorInput(14.0f, true, 200000UL, 0UL, false, RadiatorAlarmState::OVERTEMP);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::NORMAL, (int)out.alarmState);
    TEST_ASSERT_FALSE(out.forceLoadsOff);
    TEST_ASSERT_FALSE(out.fanOn);
}

void test_evaluateRadiator_alarm_persists_above_recovery_threshold(void) {
    RadiatorInput in = makeRadiatorInput(25.0f, true, 200000UL, 0UL, false, RadiatorAlarmState::FAN_FAULT);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::FAN_FAULT, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
}

void test_evaluateRadiator_sensor_failure_forces_shutdown(void) {
    RadiatorInput in = makeRadiatorInput(0.0f, false, 100000UL, 0UL, false, RadiatorAlarmState::NORMAL);
    RadiatorDecision out = evaluateRadiator(in);
    TEST_ASSERT_EQUAL_INT((int)RadiatorAlarmState::OVERTEMP, (int)out.alarmState);
    TEST_ASSERT_TRUE(out.forceLoadsOff);
    TEST_ASSERT_TRUE(out.fanOn);
}
```

Add matching `RUN_TEST(...)` lines to `main()`.

- [x] **Step 2: Run test to verify it fails**

Run: `pio test -e native`
Expected: build FAILS — `undefined reference to 'evaluateRadiator(RadiatorInput const&)'`

- [x] **Step 3: Declare in `control_logic.h`** (append)

```cpp
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
```

- [x] **Step 4: Implement in `control_logic.cpp`** (append; needs `#include "config.h"` added to the top of the file for the radiator threshold constants)

```cpp
#include "config.h"

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
```

- [x] **Step 5: Run test to verify it passes**

Run: `pio test -e native`
Expected: `29 Tests 0 Failures 0 Ignored` — `PASSED`

- [x] **Step 6: Commit**

```bash
git add lib/control_logic test/test_control_logic
git commit -m "$(cat <<'EOF'
Реализовать эскалацию защиты радиатора SSR

evaluateRadiator: порог вентилятора 30°C, промежуточный отказ 33°C
с минимальным временем работы 60с, критический 35°C, автосброс 15°C —
CLAUDE.md §3.3. Девять сценариев покрыты нативными тестами.
EOF
)"
```

**Статус: выполнено.** Коммит `d644a07`. 29/29 тестов PASSED. Заодно добавлен
`-Iinclude` в общие `build_flags` (platformio.ini) — без него строгий LDF
PlatformIO не резолвил `include/config.h` из `lib/control_logic` на
`platform=native`.

---

## Task 7: Watchdog связи и валидация SensorTempDiff

**Files:**
- Modify: `lib/control_logic/control_logic.h`
- Modify: `lib/control_logic/control_logic.cpp`
- Modify: `test/test_control_logic/test_main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `bool shouldRestartForWatchdog(unsigned long lastFullyConnectedMillis, unsigned long nowMillis, unsigned long watchdogTimeoutMs)`, `bool isValidSensorTempDiff(float diff)` — used by `main.cpp` Tasks 14/16.

- [x] **Step 1: Write the failing tests — append to `test/test_control_logic/test_main.cpp`**

```cpp
void test_shouldRestartForWatchdog_below_timeout_returns_false(void) {
    TEST_ASSERT_FALSE(shouldRestartForWatchdog(0UL, 299999UL, 300000UL));
}

void test_shouldRestartForWatchdog_at_timeout_returns_true(void) {
    TEST_ASSERT_TRUE(shouldRestartForWatchdog(0UL, 300000UL, 300000UL));
}

void test_isValidSensorTempDiff_in_range_returns_true(void) {
    TEST_ASSERT_TRUE(isValidSensorTempDiff(15.0f));
}

void test_isValidSensorTempDiff_below_min_returns_false(void) {
    TEST_ASSERT_FALSE(isValidSensorTempDiff(4.9f));
}

void test_isValidSensorTempDiff_above_max_returns_false(void) {
    TEST_ASSERT_FALSE(isValidSensorTempDiff(50.1f));
}

void test_isValidSensorTempDiff_at_bounds_returns_true(void) {
    TEST_ASSERT_TRUE(isValidSensorTempDiff(5.0f));
    TEST_ASSERT_TRUE(isValidSensorTempDiff(50.0f));
}
```

Add matching `RUN_TEST(...)` lines to `main()`.

- [x] **Step 2: Run test to verify it fails**

Run: `pio test -e native`
Expected: build FAILS — `undefined reference to 'shouldRestartForWatchdog(unsigned long, unsigned long, unsigned long)'`

- [x] **Step 3: Declare in `control_logic.h`** (append)

```cpp
// Watchdog связи WiFi+MQTT (CLAUDE.md §3.5)
bool shouldRestartForWatchdog(unsigned long lastFullyConnectedMillis, unsigned long nowMillis, unsigned long watchdogTimeoutMs);

// Валидация входящей MQTT-команды SensorTempDiff (CLAUDE.md §4)
bool isValidSensorTempDiff(float diff);
```

- [x] **Step 4: Implement in `control_logic.cpp`** (append)

```cpp
bool shouldRestartForWatchdog(unsigned long lastFullyConnectedMillis, unsigned long nowMillis, unsigned long watchdogTimeoutMs) {
    return (nowMillis - lastFullyConnectedMillis) >= watchdogTimeoutMs;
}

bool isValidSensorTempDiff(float diff) {
    return diff >= SENSOR_TEMP_DIFF_MIN && diff <= SENSOR_TEMP_DIFF_MAX;
}
```

- [x] **Step 5: Run test to verify it passes**

Run: `pio test -e native`
Expected: `35 Tests 0 Failures 0 Ignored` — `PASSED`

- [x] **Step 6: Commit**

```bash
git add lib/control_logic test/test_control_logic
git commit -m "$(cat <<'EOF'
Реализовать watchdog связи и валидацию SensorTempDiff

shouldRestartForWatchdog (5 минут без WiFi+MQTT), isValidSensorTempDiff
(диапазон 5..50) — CLAUDE.md §3.5 и §4.
EOF
)"
```

**Статус: выполнено.** Коммит `7c5ebae`. 35/35 тестов PASSED. Все чистые
функции CLAUDE.md §3 (гистерезис пушки, duty-цикл, вспомогательный
нагреватель, эскалация радиатора, watchdog) реализованы и покрыты тестами.

---

## Task 8: Сборка JSON-статуса

**Files:**
- Modify: `lib/control_logic/control_logic.h`
- Modify: `lib/control_logic/control_logic.cpp`
- Modify: `test/test_control_logic/test_main.cpp`

**Interfaces:**
- Consumes: `RadiatorAlarmState` enum (Task 1).
- Produces: `struct DeviceStatus`, `void buildStatusJson(const DeviceStatus& status, char* outBuffer, size_t bufferSize)` — used by `main.cpp` Task 15.

- [x] **Step 1: Write the failing test — append to `test/test_control_logic/test_main.cpp`** (add `#include <ArduinoJson.h>` to the top of the file)

```cpp
void test_buildStatusJson_roundtrips_all_fields(void) {
    DeviceStatus status;
    status.blownAirTemp = 18.5f; status.blownAirValid = true;
    status.targetTemp = 4.8f;    status.targetValid = true;
    status.infoTemp = 0.0f;      status.infoValid = false;
    status.outdoorTemp = -3.2f;  status.outdoorValid = true;
    status.radiatorTemp = 22.0f; status.radiatorValid = true;
    status.fanOn = true;
    status.elementOn = false;
    status.auxOn = true;
    status.radiatorFanOn = false;
    status.targetSensorTemp = 5.0f;
    status.sensorTempDiff = 15.0f;
    status.targetAirTemp = 10.0f;
    status.targetHysteresis = 0.5f;
    status.auxHysteresis = 1.0f;
    status.radiatorAlarm = RadiatorAlarmState::NORMAL;
    status.uptimeSeconds = 12345UL;
    status.freeHeapBytes = 30000UL;
    status.rssiDbm = -60;
    status.wifiConnected = true;
    status.mqttConnected = true;

    char buffer[512];
    buildStatusJson(status, buffer, sizeof(buffer));

    JsonDocument parsed;
    DeserializationError err = deserializeJson(parsed, buffer);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err.code());

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.5f, parsed["blownAirTemp"].as<float>());
    TEST_ASSERT_TRUE(parsed["infoTemp"].isNull());
    TEST_ASSERT_TRUE(parsed["fanOn"].as<bool>());
    TEST_ASSERT_FALSE(parsed["elementOn"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("NORMAL", parsed["radiatorAlarm"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(12345UL, parsed["uptimeSeconds"].as<unsigned long>());
    TEST_ASSERT_TRUE(parsed["mqttConnected"].as<bool>());
}

void test_buildStatusJson_reports_fan_fault_alarm(void) {
    DeviceStatus status = {};
    status.radiatorAlarm = RadiatorAlarmState::FAN_FAULT;
    char buffer[512];
    buildStatusJson(status, buffer, sizeof(buffer));
    JsonDocument parsed;
    deserializeJson(parsed, buffer);
    TEST_ASSERT_EQUAL_STRING("FAN_FAULT", parsed["radiatorAlarm"].as<const char*>());
}
```

Add matching `RUN_TEST(...)` lines to `main()`.

- [x] **Step 2: Run test to verify it fails**

Run: `pio test -e native`
Expected: build FAILS — `undefined reference to 'buildStatusJson(DeviceStatus const&, char*, unsigned long)'`

- [x] **Step 3: Declare in `control_logic.h`** (add `#include <stddef.h>` near the top, then append)

```cpp
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
```

- [x] **Step 4: Implement in `control_logic.cpp`** (add `#include <ArduinoJson.h>` to the top of the file, then append)

```cpp
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
```

- [x] **Step 5: Run test to verify it passes**

Run: `pio test -e native`
Expected: `37 Tests 0 Failures 0 Ignored` — `PASSED`

- [x] **Step 6: Commit**

```bash
git add lib/control_logic test/test_control_logic
git commit -m "$(cat <<'EOF'
Реализовать сборку JSON-статуса устройства

buildStatusJson: температуры всех датчиков, состояния выходов,
таргеты/гистерезисы, флаг аварии радиатора, uptime/heap/RSSI,
WiFi/MQTT — CLAUDE.md §4. Все чистые функции control_logic готовы,
переходим к сборке main.cpp.
EOF
)"
```

**Статус: выполнено.** Коммит `363e176`. 37/37 тестов PASSED. API ArduinoJson v7
(`JsonDocument`, `serializeJson`, `deserializeJson`) сверен через context7 —
совпадает с планом без изменений. Все чистые функции `control_logic`
(Tasks 3-8) готовы; следующий шаг — обвязка `main.cpp` (Task 9+).

---

## Task 9: `main.cpp` — безопасная инициализация выходов и скелет setup()/loop()

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `config.h` (Task 2), `secrets.h` (not yet created — this task only compiles against `secrets.h.example` renamed locally by the developer; see Task 17 for the real file).
- Produces: `setFan(bool)`, `setElement(bool)`, `setAuxHeater(bool)`, `setRadiatorFan(bool)` — the only functions ever allowed to touch D5–D8. Also produces global output-state booleans `cannonFanState`, `cannonElementState`, `auxHeaterState`, `radiatorFanState` consumed by every later `main.cpp` task.

- [ ] **Step 1: Replace `src/main.cpp` with the safe-init skeleton**

```cpp
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
```

- [ ] **Step 2: Create a local (untracked) `include/secrets.h` from the example so the build can link**

Run: `cp include/secrets.h.example include/secrets.h`
(This file is gitignored — never staged.)

- [ ] **Step 3: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 4: Commit** (secrets.h itself must NOT be staged — verify with `git status` that only `src/main.cpp` is listed)

```bash
git status
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Добавить безопасную инициализацию выходов и скелет setup()/loop()

D5-D8 переводятся в LOW первой строкой setup(), до WiFi/сенсоров/MQTT.
setFan/setElement/setAuxHeater/setRadiatorFan — единственные точки
записи в силовые пины; setElement структурно отказывает без
работающего вентилятора (CLAUDE.md §3.4).
EOF
)"
```

---

## Task 10: `main.cpp` — подсистема датчиков (3 независимые шины 1-Wire)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `selectFanCoolerDelay`, `applyAirTempCalibration` (Tasks 4/5), `config.h` pins/ROM addresses/periods (Task 2).
- Produces: cached globals `blownAirTemp/blownAirValid`, `targetTemp/targetValid`, `infoTemp/infoValid`, `outdoorTempRaw/outdoorValid`, `outdoorTemp` (calibrated), `radiatorTemp/radiatorValid`, `fanCoolerDelay` — consumed by `main.cpp` Tasks 11–13, 15.

- [ ] **Step 1: Add sensor bus objects and cached readings** (insert after the `#include` block in `src/main.cpp`)

```cpp
#include <OneWire.h>
#include <DallasTemperature.h>

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
```

- [ ] **Step 2: Add the fast-poll (5s) and radiator-poll (60s) tick functions** (insert before `setup()`)

```cpp
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
```

- [ ] **Step 3: Wire async mode and initial requests into `setup()`, and the ticks into `loop()`**

```cpp
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
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        // Реконнект WiFi обрабатывается ESP8266WiFi автоматически (WIFI_STA);
        // явная неблокирующая проверка добавится вместе с watchdog (Task 16).
    }

    fastSensorTick();
    radiatorSensorTick();
}
```

- [ ] **Step 4: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Подключить подсистему датчиков: три независимые шины 1-Wire

D3 (BLOWN_AIR/TARGET/INFO по ROM) и D2 (улица) опрашиваются каждые 5с,
D4 (радиатор) — раз в 60с, обе шины в неблокирующем async-режиме
DallasTemperature. Отказ датчика помечается invalid-флагом.
EOF
)"
```

---

## Task 11: `main.cpp` — автомат тепловой пушки (HeaterState)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `shouldStartHeating`, `shouldStopHeating`, `resetLoadOnLimit`, `adaptDutyLimits`, `DutyLimits` (Tasks 3/4); cached sensor globals `targetTemp/targetValid`, `blownAirTemp/blownAirValid`, `fanCoolerDelay` (Task 10); `setFan`/`setElement` (Task 9).
- Produces: `heaterState` global, MQTT-mutable `fanHeaterEnabled`/`targetSensorTemp`/`sensorTempDiff` globals (defaults from config.h, wired to MQTT in Task 14) — consumed by `main.cpp` Task 13 (radiator force-shutdown) and Task 15 (status JSON).

- [ ] **Step 1: Add heater state globals** (insert alongside the sensor globals from Task 10)

```cpp
static HeaterState heaterState = HeaterState::OFF;
static unsigned long dutyPhaseStartedAt = 0;
static unsigned long cooldownStartedAt = 0;
static unsigned long loadOnLimit = 0;
static unsigned long loadOffLimit = 0;
static unsigned long lastDutyAdaptCheck = 0;

static bool fanHeaterEnabled = false;
static float targetSensorTemp = DEFAULT_TARGET_STORAGE_TEMP;
static float sensorTempDiff = DEFAULT_SENSOR_TEMP_DIFF;
```

- [ ] **Step 2: Add the heater state machine tick and duty-adaptation tick** (insert before `setup()`)

```cpp
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
```

- [ ] **Step 3: Wire both ticks into `loop()`**

```cpp
void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        // Реконнект WiFi обрабатывается ESP8266WiFi автоматически (WIFI_STA);
        // явная неблокирующая проверка добавится вместе с watchdog (Task 16).
    }

    fastSensorTick();
    radiatorSensorTick();
    heaterTick();
    dutyAdaptTick();
}
```

- [ ] **Step 4: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Подключить автомат тепловой пушки HeaterState

OFF -> FAN_STARTING -> ELEMENT_DUTY_ON/OFF -> COOLDOWN -> OFF,
CLAUDE.md §3.1. Отказ TARGET или BLOWN_AIR уводит контур в штатный
COOLDOWN (элемент сразу, вентилятор ещё fanCoolerDelay). Адаптация
duty-цикла по BLOWN_AIR — раз в 10с, независимо от 5с опроса датчиков.
EOF
)"
```

---

## Task 12: `main.cpp` — вспомогательный нагреватель (D7)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `shouldAuxHeaterTurnOn`, `shouldAuxHeaterTurnOff` (Task 5); `outdoorTemp/outdoorValid` (Task 10); `setAuxHeater` (Task 9); `radiatorAlarmState` (declared here, populated by Task 13).
- Produces: `auxHeaterLogicalState` global, MQTT-mutable `caloriferEnabled`/`targetAirTemp` globals — consumed by `main.cpp` Task 15.

- [ ] **Step 1: Add aux heater globals** (insert alongside heater globals from Task 11)

```cpp
static AuxHeaterState auxHeaterLogicalState = AuxHeaterState::OFF;
static RadiatorAlarmState radiatorAlarmState = RadiatorAlarmState::NORMAL;

static bool caloriferEnabled = false;
static float targetAirTemp = DEFAULT_TARGET_AUX_AIR_TEMP;
```

- [ ] **Step 2: Add the aux heater tick** (insert before `setup()`)

```cpp
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
```

- [ ] **Step 3: Wire the tick into `loop()`**

```cpp
    fastSensorTick();
    radiatorSensorTick();
    heaterTick();
    dutyAdaptTick();
    auxHeaterTick();
```

- [ ] **Step 4: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Подключить контур вспомогательного нагревателя (D7)

Гейтится MQTT-флагом calorifer и аварией радиатора (форс-OFF при
любом RadiatorAlarmState != NORMAL); термоконтур по уличному воздуху
с гистерезисом ±1°C — CLAUDE.md §3.2.
EOF
)"
```

---

## Task 13: `main.cpp` — эскалация радиатора (D4 + D8)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `evaluateRadiator`, `RadiatorInput`, `RadiatorDecision` (Task 6); `radiatorTemp/radiatorValid` (Task 10); `setRadiatorFan`, `setElement`, `setFan`, `setAuxHeater` (Task 9); `heaterState`, `auxHeaterLogicalState` (Tasks 11/12).
- Produces: updates to `radiatorAlarmState` (declared in Task 12) and new global `radiatorFanOnSince` — consumed by `main.cpp` Task 15.

- [ ] **Step 1: Add the radiator fan-on-since tracker** (insert alongside the globals from Task 12)

```cpp
static unsigned long radiatorFanOnSince = 0;
```

- [ ] **Step 2: Add the radiator escalation tick** (insert before `setup()`, right after `radiatorSensorTick()` since it must run on the same 60s cadence)

```cpp
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
```

Note: `radiatorFanState` is the private state variable inside `setRadiatorFan()`/`setFan()`/`setElement()` in Task 9 — change those four functions' local statics (`cannonFanState`, `cannonElementState`, `auxHeaterState`, `radiatorFanState`) from `static bool` at file scope (already file-scope static in Task 9) so they're visible here; no further change needed since Task 9 already declared them at file scope, not function-local.

- [ ] **Step 3: Call the escalation tick right after the radiator sensor tick in `loop()`**

```cpp
    fastSensorTick();
    radiatorSensorTick();
    radiatorEscalationTick();
    heaterTick();
    dutyAdaptTick();
    auxHeaterTick();
```

- [ ] **Step 4: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Подключить эскалацию защиты радиатора (D4+D8)

radiatorEscalationTick вызывается на каждом 60с цикле опроса D4,
независимо от HeaterState/AuxHeaterState: при forceLoadsOff гасит
D5/D6/D7 безусловно — CLAUDE.md §3.3, §7 дизайн-документа.
EOF
)"
```

---

## Task 14: `main.cpp` — MQTT-клиент (подключение, LWT, команды, реконнект)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `isValidSensorTempDiff` (Task 7); all MQTT-mutable globals: `fanHeaterEnabled`, `targetSensorTemp`, `sensorTempDiff` (Task 11), `caloriferEnabled`, `targetAirTemp` (Task 12); `config.h` MQTT topic/period constants (Task 2); `secrets.h` broker credentials.
- Produces: `mqttClient` (PubSubClient instance) — consumed by `main.cpp` Tasks 15/16.

- [ ] **Step 1: Add MQTT includes, client instance, and reconnect timer** (insert alongside other globals)

```cpp
#include <PubSubClient.h>

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static unsigned long lastMqttReconnectAttempt = 0;
```

- [ ] **Step 2: Add the command callback and non-blocking reconnect function** (insert before `setup()`)

```cpp
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
```

- [ ] **Step 3: Wire MQTT server/callback setup into `setup()`, and `loop()`/reconnect into `loop()`**

```cpp
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
}
```

- [ ] **Step 4: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Подключить MQTT-клиент: LWT, подписки на команды, реконнект

Неблокирующий реконнект раз в 5с, LWT garage/heat/state=OFF retain,
обработка всех командных топиков CLAUDE.md §4 синхронно в колбэке,
restart -> безопасное выключение всех выходов + ESP.restart().
Retained-эхо команд после реконнекта — ожидаемое поведение, не
обрабатывается отдельно.
EOF
)"
```

---

## Task 15: `main.cpp` — публикация JSON-статуса (60с)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `buildStatusJson`, `DeviceStatus` (Task 8); every cached sensor/state global from Tasks 10–14.
- Produces: nothing consumed by later tasks (terminal wiring for the status pipeline).

- [ ] **Step 1: Add the status-publish timer global** (insert alongside other globals)

```cpp
static unsigned long lastStatusPublish = 0;
```

- [ ] **Step 2: Add the status-publish tick** (insert before `setup()`)

```cpp
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

    char buffer[640];
    buildStatusJson(status, buffer, sizeof(buffer));
    mqttClient.publish(MQTT_TOPIC_STATUS, buffer);
}
```

- [ ] **Step 3: Wire the tick into `loop()`**

```cpp
    fastSensorTick();
    radiatorSensorTick();
    radiatorEscalationTick();
    heaterTick();
    dutyAdaptTick();
    auxHeaterTick();
    statusPublishTick();
```

- [ ] **Step 4: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Подключить публикацию JSON-статуса раз в 60 секунд

garage/heat/status: все температуры, состояния выходов, таргеты,
гистерезисы, флаг аварии радиатора, uptime/heap/RSSI, WiFi/MQTT.
Публикация пропускается, если MQTT не подключён (без очереди) —
CLAUDE.md §4.
EOF
)"
```

---

## Task 16: `main.cpp` — watchdog связи (WiFi+MQTT)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `shouldRestartForWatchdog` (Task 7).
- Produces: nothing consumed by later tasks (final safety wiring).

- [ ] **Step 1: Add the watchdog timestamp global** (insert alongside other globals)

```cpp
static unsigned long lastFullyConnectedMillis = 0;
```

- [ ] **Step 2: Add the watchdog tick** (insert before `setup()`)

```cpp
static void connectivityWatchdogTick() {
    unsigned long now = millis();
    bool fullyConnected = (WiFi.status() == WL_CONNECTED) && mqttClient.connected();

    if (fullyConnected) {
        lastFullyConnectedMillis = now;
        return;
    }

    if (shouldRestartForWatchdog(lastFullyConnectedMillis, now, WATCHDOG_TIMEOUT_MS)) {
        setElement(false);
        setFan(false);
        setAuxHeater(false);
        setRadiatorFan(false);
        ESP.restart();
    }
}
```

- [ ] **Step 3: Initialize the timestamp in `setup()` and call the tick from `loop()`**

```cpp
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

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    lastFullyConnectedMillis = millis();
}

void loop() {
    mqttReconnectTick();
    mqttClient.loop();

    fastSensorTick();
    radiatorSensorTick();
    radiatorEscalationTick();
    heaterTick();
    dutyAdaptTick();
    auxHeaterTick();
    statusPublishTick();
    connectivityWatchdogTick();
}
```

(This step also removes the placeholder `if (WiFi.status() != WL_CONNECTED) { ... }` comment block left over from Task 9 — the watchdog tick now owns that responsibility.)

- [ ] **Step 4: Verify the esp8266 environment compiles**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
Подключить watchdog связи WiFi+MQTT

5 минут без полного подключения (WiFi IP + MQTT broker) -> безопасное
выключение всех силовых выходов + ESP.restart() — CLAUDE.md §3.5.
Таймер сбрасывается при каждом полном восстановлении связи.
EOF
)"
```

---

## Task 17: Локальный `secrets.h`, финальная проверка сборки

**Files:**
- Create (local, untracked): `include/secrets.h`
- No tracked files change in this task — it is a verification-only checkpoint before handing the firmware off for real-hardware bring-up.

**Interfaces:**
- Consumes: everything built in Tasks 1–16.
- Produces: nothing (terminal task of this plan).

- [ ] **Step 1: Confirm `include/secrets.h` exists locally with real (or placeholder) broker credentials**

Run: `test -f include/secrets.h && echo "exists"`
Expected: `exists` (created back in Task 9, Step 2; if missing, re-run `cp include/secrets.h.example include/secrets.h` and fill in real WiFi/MQTT credentials before flashing real hardware)

- [ ] **Step 2: Run the full native test suite one more time**

Run: `pio test -e native`
Expected: `37 Tests 0 Failures 0 Ignored` — `PASSED`

- [ ] **Step 3: Run the full esp8266 build one more time**

Run: `pio run -e esp8266`
Expected: `SUCCESS`

- [ ] **Step 4: Confirm `include/secrets.h` was never staged**

Run: `git status --porcelain include/secrets.h`
Expected: no output (file is gitignored, working tree clean with respect to it)

- [ ] **Step 5: Confirm nothing untracked remains outside `.pio/` and `include/secrets.h`**

Run: `git status --porcelain`
Expected: empty output (everything from Tasks 1–16 already committed)

No commit in this task — it is verification-only. If Step 5 shows uncommitted changes, they belong to an earlier task that was not committed; go back and commit them under that task's message before proceeding to hardware bring-up.

**Remaining before real-hardware bring-up (outside this plan's scope):** read the three DS18b20 ROM addresses on bus D3 with a scanning sketch and replace the placeholders in `include/config.h` (CLAUDE.md §2/§7) — the only hardware-dependent step this plan could not close in advance.

---

## Self-Review

**1. Spec coverage against `CLAUDE.md`:**
- §0 decisions 1–13: pins/roles (Task 2/10), hysteresis ±0.5 (Task 3/11), 2 SSR roles (Task 9/11/12), MQTT no device-ID (Task 2/14), fan interlock (Task 9), auto-reset 15°C (Task 6/13), status every 60s (Task 15), ROM addresses (Task 2/17), no TLS (Task 14, plain `mqttClient.connect`), no OTA (not implemented anywhere — correctly out of scope), fan feedback (not implemented — correctly out of scope), fan-fault variant B 33°C/60s (Task 6/13). ✓ all covered.
- §1 hardware table: every pin used exactly as specified (Task 2, Task 9–13). ✓
- §2 three DS18b20 roles by ROM: Task 2 (placeholders) + Task 10 (role-based reads). ✓
- §3.1 cannon logic: Task 3 (hysteresis), Task 4 (duty adaptation + fanCoolerDelay table), Task 11 (state machine, start/stop, cooldown sequencing). ✓
- §3.2 aux heater: Task 5 (calibration + hysteresis), Task 12 (wiring, radiator-alarm gating). ✓
- §3.3 radiator escalation incl. fan-fault detection: Task 6 (pure function, 9 scenarios), Task 13 (wiring, unconditional force-off). ✓
- §3.4 interlock: Task 9 (`setElement` refuses without fan). ✓
- §3.5 watchdog: Task 7 (pure function), Task 16 (wiring). ✓
- §4 MQTT (topics, LWT, no queue, retained-echo acceptance): Task 14 (commands/LWT/reconnect), Task 15 (status, drop-if-disconnected). Debug topic intentionally omitted from the plan — it's marked optional in CLAUDE.md §4 ("опциональный debug-топик"); can be added as a follow-up task if desired.
- §5 config.h parameters: Task 2, all present with exact values.
- §6 explicitly out of scope: OTA, TLS, humidity, hardware fan feedback — none implemented anywhere in this plan. ✓
- Sensor-failure policy (design doc §5, agreed this session): TARGET failure → `mustStop` in `heaterTick` (Task 11); BLOWN_AIR failure → same graceful `mustStop` path, not a hard alarm (Task 11); INFO failure → status-only via `infoValid` flag (Task 10/15, no control reference to `infoTemp`/`infoValid` anywhere outside the JSON builder — confirmed); D2 failure → forces `fanCoolerDelay` back to default and (via `outdoorValid` gate) forces aux heater off (Task 10/12); D4 failure → treated as critical overheat via `evaluateRadiator`'s `!sensorValid` branch (Task 6/13). ✓ all five cases match the agreed table.

**2. Placeholder scan:** No "TBD"/"add error handling"/"similar to Task N" patterns found. The only intentional placeholder is the ROM address byte arrays in `config.h`, which is a legitimate hardware-dependent stub explicitly flagged in the spec itself (CLAUDE.md §7) and called out again in Task 17's closing note — not a plan-writing shortcut.

**3. Type/signature consistency:** Verified across all tasks — `DutyLimits`, `RadiatorInput`, `RadiatorDecision`, `DeviceStatus` field names and types match between their Task 4/6/8 declarations and every `main.cpp` consumer in Tasks 11/13/15. `HeaterState`/`AuxHeaterState`/`RadiatorAlarmState` enum values used consistently from Task 1 onward. Output-state statics (`cannonFanState`, `cannonElementState`, `auxHeaterState`, `radiatorFanState`) declared once in Task 9 and referenced by name identically in Tasks 13/15.

---

**Plan complete and saved to `progress.md`.** Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
