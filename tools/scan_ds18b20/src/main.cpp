// Диагностический скетч: определение ROM-адресов DS18b20 на трёх шинах
// garage-heat-v2 (D2/D3/D4). Не часть боевой прошивки — см. README.md рядом.
//
// D3 — три датчика BLOWN_AIR/TARGET/INFO на одном проводе, ради него и
// написан этот скетч (см. CLAUDE.md §2/§7). D2/D4 — по одному датчику,
// печатаются только для проверки, что провода не перепутаны.

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define PIN_ONEWIRE_ROLES    D3
#define PIN_ONEWIRE_OUTDOOR  D2
#define PIN_ONEWIRE_RADIATOR D4

static OneWire oneWireRoles(PIN_ONEWIRE_ROLES);
static DallasTemperature sensorsRoles(&oneWireRoles);
static OneWire oneWireOutdoor(PIN_ONEWIRE_OUTDOOR);
static DallasTemperature sensorOutdoor(&oneWireOutdoor);
static OneWire oneWireRadiator(PIN_ONEWIRE_RADIATOR);
static DallasTemperature sensorRadiator(&oneWireRadiator);

// Печатает ROM в формате, готовом для вставки в include/config.h
static void printRomAsConfigLiteral(const DeviceAddress addr) {
    Serial.print("{ ");
    for (uint8_t i = 0; i < 8; i++) {
        Serial.print("0x");
        if (addr[i] < 0x10) Serial.print('0');
        Serial.print(addr[i], HEX);
        if (i < 7) Serial.print(", ");
    }
    Serial.print(" }");
}

static void reportBus(DallasTemperature& sensors, const char* label, uint8_t expectedCount) {
    uint8_t count = sensors.getDeviceCount();
    Serial.print(label);
    Serial.print(": найдено устройств = ");
    Serial.println(count);
    if (count != expectedCount) {
        Serial.print("  !!! ОЖИДАЛОСЬ ");
        Serial.print(expectedCount);
        Serial.println(" — проверь проводку и подтяжку 4.7к между DQ и VCC !!!");
    }
    for (uint8_t i = 0; i < count; i++) {
        DeviceAddress addr;
        sensors.getAddress(addr, i);
        Serial.print("  #");
        Serial.print(i);
        Serial.print(" ROM = ");
        printRomAsConfigLiteral(addr);
        Serial.println();
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println();
    Serial.println(F("=== Сканер DS18b20 (garage-heat-v2) ==="));

    sensorsRoles.begin();
    sensorOutdoor.begin();
    sensorRadiator.begin();

    reportBus(sensorsRoles, "D3 (BLOWN_AIR/TARGET/INFO)", 3);
    reportBus(sensorOutdoor, "D2 (уличный воздух)", 1);
    reportBus(sensorRadiator, "D4 (радиатор SSR)", 1);

    Serial.println();
    Serial.println(F("Дальше в Serial раз в секунду печатаются live-показания шины D3."));
    Serial.println(F("Грей/остужай по одному физическому датчику (палец, фен, лёд) и"));
    Serial.println(F("смотри, у какого # меняется температура — так сопоставишь"));
    Serial.println(F("физический датчик с его ROM и назначишь роль (см. README.md)."));
    Serial.println();
}

void loop() {
    sensorsRoles.requestTemperatures();
    uint8_t count = sensorsRoles.getDeviceCount();
    for (uint8_t i = 0; i < count; i++) {
        DeviceAddress addr;
        sensorsRoles.getAddress(addr, i);
        Serial.print("D3 #");
        Serial.print(i);
        Serial.print("  t = ");
        Serial.print(sensorsRoles.getTempC(addr));
        Serial.print(" C   ROM = ");
        printRomAsConfigLiteral(addr);
        Serial.println();
    }
    Serial.println("---");
    delay(1000);
}
