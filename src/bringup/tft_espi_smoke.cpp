#include <Arduino.h>
#include <TFT_eSPI.h>

namespace {
constexpr int kStatusLedPin = 2;
constexpr int kBacklightPin = 13;

TFT_eSPI tft;

std::uint32_t g_lastAliveMs = 0;
std::uint32_t g_lastPatternMs = 0;
std::uint8_t g_pattern = 0;
bool g_ledOn = false;

void drawPattern(std::uint8_t pattern) {
    Serial.printf("[tft_espi] draw pattern=%u\n", static_cast<unsigned>(pattern % 4U));
    tft.startWrite();
    switch (pattern % 4U) {
        case 0:
            tft.fillScreen(TFT_BLACK);
            tft.fillRect(0, 0, 80, 240, TFT_RED);
            tft.fillRect(80, 0, 80, 240, TFT_GREEN);
            tft.fillRect(160, 0, 80, 240, TFT_BLUE);
            break;
        case 1:
            tft.fillScreen(TFT_BLACK);
            tft.fillRect(0, 0, 240, 80, TFT_WHITE);
            tft.fillRect(0, 80, 240, 80, TFT_YELLOW);
            tft.fillRect(0, 160, 240, 80, TFT_BLACK);
            break;
        case 2:
            tft.fillScreen(TFT_BLACK);
            tft.fillRect(0, 0, 120, 120, TFT_CYAN);
            tft.fillRect(120, 0, 120, 120, TFT_MAGENTA);
            tft.fillRect(0, 120, 120, 120, TFT_YELLOW);
            tft.fillRect(120, 120, 120, 120, TFT_BLUE);
            break;
        default:
            tft.fillScreen(TFT_WHITE);
            tft.fillRect(16, 16, 208, 208, TFT_BLACK);
            tft.fillRect(32, 32, 176, 176, TFT_RED);
            tft.fillRect(48, 48, 144, 144, TFT_GREEN);
            tft.fillRect(64, 64, 112, 112, TFT_BLUE);
            break;
    }
    tft.endWrite();

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("TFT_eSPI ST7789", 120, 108, 2);
    tft.drawString("240x240 CS=-1", 120, 132, 2);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(kStatusLedPin, OUTPUT);
    digitalWrite(kStatusLedPin, LOW);

    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, HIGH);

    Serial.println("[tft_espi] setup start");
    Serial.println("[tft_espi] pins SCL=18 SDA/MOSI=23 RES=14 DC=19 BLK=13 CS=-1");
    Serial.println("[tft_espi] driver=ST7789 size=240x240 spi=10MHz");

    tft.init();
    tft.setRotation(0);
    digitalWrite(kBacklightPin, HIGH);
    drawPattern(g_pattern);
    Serial.println("[tft_espi] started");
}

void loop() {
    const std::uint32_t nowMs = millis();
    if (nowMs - g_lastPatternMs >= 5000UL) {
        g_lastPatternMs = nowMs;
        ++g_pattern;
        drawPattern(g_pattern);
    }
    if (nowMs - g_lastAliveMs >= 1000UL) {
        g_lastAliveMs = nowMs;
        g_ledOn = !g_ledOn;
        digitalWrite(kStatusLedPin, g_ledOn ? HIGH : LOW);
        Serial.printf("[tft_espi] alive ms=%lu pattern=%u\n",
                      static_cast<unsigned long>(nowMs),
                      static_cast<unsigned>(g_pattern % 4U));
    }
}
