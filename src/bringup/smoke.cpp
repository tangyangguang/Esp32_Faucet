#include <Arduino.h>

#include "drivers/BoardPins.h"

namespace {
constexpr int kStatusLedPin = 2;
constexpr std::uint16_t kTftWidth = 240;
constexpr std::uint16_t kTftHeight = 240;
constexpr std::uint16_t kRed = 0xF800;
constexpr std::uint16_t kGreen = 0x07E0;
constexpr std::uint16_t kBlue = 0x001F;
constexpr std::uint16_t kWhite = 0xFFFF;
constexpr std::uint16_t kBlack = 0x0000;
constexpr std::uint16_t kYellow = 0xFFE0;
constexpr std::uint16_t kCyan = 0x07FF;
constexpr std::uint16_t kMagenta = 0xF81F;

std::uint32_t g_lastAliveMs = 0;
std::uint32_t g_lastPatternMs = 0;
std::uint8_t g_pattern = 0;
bool g_ledOn = false;

void writeByte(std::uint8_t value) {
    for (std::uint8_t mask = 0x80; mask != 0; mask >>= 1U) {
        digitalWrite(faucet::kPinSt7789Sclk, LOW);
        digitalWrite(faucet::kPinSt7789Mosi, (value & mask) ? HIGH : LOW);
        delayMicroseconds(2);
        digitalWrite(faucet::kPinSt7789Sclk, HIGH);
        delayMicroseconds(2);
    }
    digitalWrite(faucet::kPinSt7789Sclk, LOW);
}

void command(std::uint8_t value) {
    digitalWrite(faucet::kPinSt7789Dc, LOW);
    writeByte(value);
}

void data(std::uint8_t value) {
    digitalWrite(faucet::kPinSt7789Dc, HIGH);
    writeByte(value);
}

void data(const std::uint8_t* values, std::size_t len) {
    digitalWrite(faucet::kPinSt7789Dc, HIGH);
    for (std::size_t i = 0; i < len; ++i) {
        writeByte(values[i]);
    }
}

void setWindow(std::uint16_t x, std::uint16_t y, std::uint16_t w, std::uint16_t h) {
    const std::uint16_t x1 = static_cast<std::uint16_t>(x + w - 1U);
    const std::uint16_t y1 = static_cast<std::uint16_t>(y + h - 1U);
    command(0x2A);
    data(static_cast<std::uint8_t>(x >> 8U));
    data(static_cast<std::uint8_t>(x & 0xFFU));
    data(static_cast<std::uint8_t>(x1 >> 8U));
    data(static_cast<std::uint8_t>(x1 & 0xFFU));
    command(0x2B);
    data(static_cast<std::uint8_t>(y >> 8U));
    data(static_cast<std::uint8_t>(y & 0xFFU));
    data(static_cast<std::uint8_t>(y1 >> 8U));
    data(static_cast<std::uint8_t>(y1 & 0xFFU));
    command(0x2C);
}

void fillRect(std::uint16_t x, std::uint16_t y, std::uint16_t w, std::uint16_t h, std::uint16_t color) {
    setWindow(x, y, w, h);
    digitalWrite(faucet::kPinSt7789Dc, HIGH);
    const std::uint8_t hi = static_cast<std::uint8_t>(color >> 8U);
    const std::uint8_t lo = static_cast<std::uint8_t>(color & 0xFFU);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(w) * static_cast<std::uint32_t>(h); ++i) {
        writeByte(hi);
        writeByte(lo);
    }
}

void drawPattern(std::uint8_t pattern) {
    Serial.printf("[bitbang] draw pattern=%u\n", static_cast<unsigned>(pattern % 4U));
    switch (pattern % 4U) {
        case 0:
            fillRect(0, 0, 80, kTftHeight, kRed);
            fillRect(80, 0, 80, kTftHeight, kGreen);
            fillRect(160, 0, 80, kTftHeight, kBlue);
            break;
        case 1:
            fillRect(0, 0, kTftWidth, 80, kWhite);
            fillRect(0, 80, kTftWidth, 80, kYellow);
            fillRect(0, 160, kTftWidth, 80, kBlack);
            break;
        case 2:
            fillRect(0, 0, 120, 120, kCyan);
            fillRect(120, 0, 120, 120, kMagenta);
            fillRect(0, 120, 120, 120, kYellow);
            fillRect(120, 120, 120, 120, kBlue);
            break;
        default:
            fillRect(0, 0, kTftWidth, kTftHeight, kWhite);
            fillRect(16, 16, 208, 208, kBlack);
            fillRect(32, 32, 176, 176, kRed);
            fillRect(48, 48, 144, 144, kGreen);
            fillRect(64, 64, 112, 112, kBlue);
            break;
    }
}

void beginTft() {
    pinMode(faucet::kPinSt7789Sclk, OUTPUT);
    pinMode(faucet::kPinSt7789Mosi, OUTPUT);
    pinMode(faucet::kPinSt7789Dc, OUTPUT);
    pinMode(faucet::kPinSt7789Rst, OUTPUT);
    pinMode(faucet::kPinSt7789Backlight, OUTPUT);
    digitalWrite(faucet::kPinSt7789Sclk, LOW);
    digitalWrite(faucet::kPinSt7789Mosi, LOW);
    digitalWrite(faucet::kPinSt7789Dc, HIGH);
    digitalWrite(faucet::kPinSt7789Backlight, HIGH);

    digitalWrite(faucet::kPinSt7789Rst, HIGH);
    delay(20);
    digitalWrite(faucet::kPinSt7789Rst, LOW);
    delay(50);
    digitalWrite(faucet::kPinSt7789Rst, HIGH);
    delay(120);

    command(0x36);
    data(0x00);
    command(0x3A);
    data(0x05);
    const std::uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    command(0xB2);
    data(porch, sizeof(porch));
    command(0xB7);
    data(0x35);
    command(0xBB);
    data(0x19);
    command(0xC0);
    data(0x2C);
    command(0xC2);
    data(0x01);
    command(0xC3);
    data(0x12);
    command(0xC4);
    data(0x20);
    command(0xC6);
    data(0x0F);
    const std::uint8_t power[] = {0xA4, 0xA1};
    command(0xD0);
    data(power, sizeof(power));
    const std::uint8_t gammaPos[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                                     0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    command(0xE0);
    data(gammaPos, sizeof(gammaPos));
    const std::uint8_t gammaNeg[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                                     0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
    command(0xE1);
    data(gammaNeg, sizeof(gammaNeg));
    command(0x21);
    command(0x11);
    delay(120);
    command(0x29);
    delay(20);
    command(0x36);
    data(0x00);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(kStatusLedPin, OUTPUT);
    digitalWrite(kStatusLedPin, LOW);
    Serial.println("[bitbang] setup start");
    Serial.printf("[bitbang] pins SCL=%u SDA=%u RES=%u DC=%u BLK=%u\n",
                  static_cast<unsigned>(faucet::kPinSt7789Sclk),
                  static_cast<unsigned>(faucet::kPinSt7789Mosi),
                  static_cast<unsigned>(faucet::kPinSt7789Rst),
                  static_cast<unsigned>(faucet::kPinSt7789Dc),
                  static_cast<unsigned>(faucet::kPinSt7789Backlight));
    beginTft();
    drawPattern(g_pattern);
    Serial.println("[bitbang] started");
}

void loop() {
    const std::uint32_t nowMs = millis();
    if (nowMs - g_lastPatternMs >= 10000UL) {
        g_lastPatternMs = nowMs;
        ++g_pattern;
        drawPattern(g_pattern);
    }
    if (nowMs - g_lastAliveMs >= 1000UL) {
        g_lastAliveMs = nowMs;
        g_ledOn = !g_ledOn;
        digitalWrite(kStatusLedPin, g_ledOn ? HIGH : LOW);
        Serial.printf("[bitbang] alive ms=%lu pattern=%u\n",
                      static_cast<unsigned long>(nowMs),
                      static_cast<unsigned>(g_pattern % 4U));
    }
}
