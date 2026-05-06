#include <Arduino.h>

namespace {
constexpr int kStatusLedPin = 2;
std::uint32_t g_lastPrintMs = 0;
bool g_ledOn = false;

void printResetReason() {
    Serial.printf("[smoke] reset reason=%d\n", static_cast<int>(esp_reset_reason()));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(kStatusLedPin, OUTPUT);
    digitalWrite(kStatusLedPin, LOW);
    Serial.println("[smoke] setup start");
    printResetReason();
    Serial.printf("[smoke] chip revision=%u cpu=%uMHz flash=%u bytes\n",
                  static_cast<unsigned>(ESP.getChipRevision()),
                  static_cast<unsigned>(ESP.getCpuFreqMHz()),
                  static_cast<unsigned>(ESP.getFlashChipSize()));
}

void loop() {
    const std::uint32_t nowMs = millis();
    if (nowMs - g_lastPrintMs >= 1000UL) {
        g_lastPrintMs = nowMs;
        g_ledOn = !g_ledOn;
        digitalWrite(kStatusLedPin, g_ledOn ? HIGH : LOW);
        Serial.printf("[smoke] alive ms=%lu heap=%lu\n",
                      static_cast<unsigned long>(nowMs),
                      static_cast<unsigned long>(ESP.getFreeHeap()));
    }
}
