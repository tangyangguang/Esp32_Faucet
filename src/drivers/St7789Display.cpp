#include "drivers/St7789Display.h"

#include "drivers/St7789Font.h"

#include <Arduino.h>
#include <SPI.h>

#include <cmath>
#include <cstring>

#ifndef FAUCET_ST7789_USE_TFT_ESPI
#define FAUCET_ST7789_USE_TFT_ESPI 0
#endif

#ifndef FAUCET_ST7789_BOOT_TEST
#define FAUCET_ST7789_BOOT_TEST 0
#endif

#if FAUCET_ST7789_USE_TFT_ESPI
#include <TFT_eSPI.h>
#endif

namespace faucet {
namespace {

constexpr std::uint16_t kWidth = 240;
constexpr std::uint16_t kHeight = 240;
constexpr std::uint16_t rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r & 0xF8U) << 8U) | ((g & 0xFCU) << 3U) | (b >> 3U));
}
constexpr std::uint16_t kBlack = 0x0000;
constexpr std::uint16_t kBg = kBlack;
constexpr std::uint16_t kPanel = rgb565(0x04, 0x0B, 0x0F);
constexpr std::uint16_t kPanel2 = rgb565(0x05, 0x10, 0x16);
constexpr std::uint16_t kLine = rgb565(0x21, 0x45, 0x52);
constexpr std::uint16_t kInk = rgb565(0xEC, 0xFB, 0xFF);
constexpr std::uint16_t kMuted = rgb565(0x91, 0xAE, 0xB7);
constexpr std::uint16_t kDim = rgb565(0x4C, 0x69, 0x72);
constexpr std::uint16_t kCyan = rgb565(0x57, 0xE6, 0xFF);
constexpr std::uint16_t kBlue = rgb565(0x3A, 0x8F, 0xFF);
constexpr std::uint16_t kGreen = rgb565(0x5C, 0xF6, 0x96);
constexpr std::uint16_t kAmber = rgb565(0xFF, 0xD1, 0x5A);
constexpr std::uint16_t kRed = rgb565(0xFF, 0x5F, 0x7A);
constexpr std::int16_t kTextLineHeight = 16;
constexpr std::int16_t kTagHeight = 22;

constexpr std::uint32_t kSt7789SpiHz = 27000000UL;
constexpr std::uint8_t kSt7789SpiMode = SPI_MODE3;

#if FAUCET_ST7789_USE_TFT_ESPI
TFT_eSPI g_tft;
TFT_eSprite g_frameSprite(&g_tft);
bool g_frameSpriteReady = false;
bool g_drawToSprite = false;
#endif

constexpr std::uint8_t kCmdNop = 0x00;
constexpr std::uint8_t kCmdSleepOut = 0x11;
constexpr std::uint8_t kCmdNormalOn = 0x13;
constexpr std::uint8_t kCmdCaseT = 0x2A;
constexpr std::uint8_t kCmdRaseT = 0x2B;
constexpr std::uint8_t kCmdRamWr = 0x2C;
constexpr std::uint8_t kCmdColMod = 0x3A;
constexpr std::uint8_t kCmdMadCtl = 0x36;
constexpr std::uint8_t kCmdInvOn = 0x21;
constexpr std::uint8_t kCmdDispOn = 0x29;
constexpr std::uint8_t kCmdRamCtrl = 0xB0;
constexpr std::uint8_t kCmdPorchCtrl = 0xB2;
constexpr std::uint8_t kCmdDisplayFunctionCtrl = 0xB6;
constexpr std::uint8_t kCmdGateCtrl = 0xB7;
constexpr std::uint8_t kCmdVcomSet = 0xBB;
constexpr std::uint8_t kCmdLcmCtrl = 0xC0;
constexpr std::uint8_t kCmdVdvVrhEnable = 0xC2;
constexpr std::uint8_t kCmdVrhSet = 0xC3;
constexpr std::uint8_t kCmdVdvSet = 0xC4;
constexpr std::uint8_t kCmdFrameRateCtrl = 0xC6;
constexpr std::uint8_t kCmdPowerCtrl1 = 0xD0;
constexpr std::uint8_t kCmdPositiveGamma = 0xE0;
constexpr std::uint8_t kCmdNegativeGamma = 0xE1;

bool sameFrame(const ColorDisplayFrame& a, const ColorDisplayFrame& b) {
    return std::memcmp(&a, &b, sizeof(ColorDisplayFrame)) == 0;
}

std::uint16_t accentForPage(ColorDisplayPage page) {
    switch (page) {
        case ColorDisplayPage::StandbyTime:
        case ColorDisplayPage::ConfirmVolume:
        case ColorDisplayPage::ConfirmTime:
        case ColorDisplayPage::RunningTime:
        case ColorDisplayPage::PausedVolume:
        case ColorDisplayPage::PausedTime:
        case ColorDisplayPage::StandbyOffline:
            return kAmber;
        case ColorDisplayPage::Alert:
            return kRed;
        case ColorDisplayPage::CalibrationReady:
        case ColorDisplayPage::CalibrationEntry:
            return kCyan;
        case ColorDisplayPage::ResultCompleted:
            return kGreen;
        case ColorDisplayPage::ResultStopped:
            return kAmber;
        case ColorDisplayPage::StandbyVolume:
        case ColorDisplayPage::RunningVolume:
        default:
            return kCyan;
    }
}

std::uint32_t decodeUtf8(const char*& text) {
    const std::uint8_t first = static_cast<std::uint8_t>(*text++);
    if (first < 0x80) {
        return first;
    }
    if ((first & 0xE0U) == 0xC0U) {
        const std::uint8_t b1 = static_cast<std::uint8_t>(*text++);
        return static_cast<std::uint32_t>(((first & 0x1FU) << 6U) | (b1 & 0x3FU));
    }
    if ((first & 0xF0U) == 0xE0U) {
        const std::uint8_t b1 = static_cast<std::uint8_t>(*text++);
        const std::uint8_t b2 = static_cast<std::uint8_t>(*text++);
        return static_cast<std::uint32_t>(((first & 0x0FU) << 12U) | ((b1 & 0x3FU) << 6U) | (b2 & 0x3FU));
    }
    if ((first & 0xF8U) == 0xF0U) {
        const std::uint8_t b1 = static_cast<std::uint8_t>(*text++);
        const std::uint8_t b2 = static_cast<std::uint8_t>(*text++);
        const std::uint8_t b3 = static_cast<std::uint8_t>(*text++);
        return static_cast<std::uint32_t>(((first & 0x07U) << 18U) | ((b1 & 0x3FU) << 12U) |
                                          ((b2 & 0x3FU) << 6U) | (b3 & 0x3FU));
    }
    return '?';
}

#if FAUCET_ST7789_USE_TFT_ESPI
std::uint8_t asciiFontForScale(std::uint8_t scale) {
    return scale >= 2 ? 4 : 2;
}

bool isAsciiText(const char* text) {
    const char* cursor = text ? text : "";
    while (*cursor) {
        if (static_cast<std::uint8_t>(*cursor) >= 0x80U) {
            return false;
        }
        ++cursor;
    }
    return true;
}

std::int16_t asciiTextWidth(const char* text, std::uint8_t font) {
    return static_cast<std::int16_t>(g_tft.textWidth(text ? text : "", font));
}

std::int16_t asciiTrackedTextWidth(const char* text, std::uint8_t font, std::int16_t tracking) {
    const char* cursor = text ? text : "";
    std::int16_t width = 0;
    bool first = true;
    char one[2]{};
    while (*cursor) {
        if (!first) {
            width = static_cast<std::int16_t>(width + tracking);
        }
        one[0] = *cursor++;
        width = static_cast<std::int16_t>(width + asciiTextWidth(one, font));
        first = false;
    }
    return width;
}
#endif

std::int16_t textWidth(const char* text, std::uint8_t scale) {
    std::uint16_t width = 0;
    const char* cursor = text ? text : "";
#if FAUCET_ST7789_USE_TFT_ESPI
    const std::uint8_t asciiFont = asciiFontForScale(scale);
    char asciiRun[32]{};
    std::uint8_t asciiLen = 0;
    auto flushAsciiRun = [&]() {
        if (asciiLen == 0) {
            return;
        }
        asciiRun[asciiLen] = '\0';
        width = static_cast<std::uint16_t>(width + asciiTextWidth(asciiRun, asciiFont));
        asciiLen = 0;
    };
    while (*cursor) {
        if (static_cast<std::uint8_t>(*cursor) < 0x80U) {
            asciiRun[asciiLen++] = *cursor++;
            if (asciiLen == sizeof(asciiRun) - 1U) {
                flushAsciiRun();
            }
            continue;
        }
        flushAsciiRun();
        decodeUtf8(cursor);
        width = static_cast<std::uint16_t>(width + kSt7789GlyphWidth * scale);
    }
    flushAsciiRun();
    return static_cast<std::int16_t>(width);
#else
    while (*cursor) {
        const std::uint32_t cp = decodeUtf8(cursor);
        width = static_cast<std::uint16_t>(width + (cp < 0x80UL ? 8U : kSt7789GlyphWidth));
    }
    return static_cast<std::int16_t>(width * scale);
#endif
}

const char* compactMetricLabel(const char* label) {
    if (!label) {
        return "";
    }
    if (std::strcmp(label, "今日累计") == 0) {
        return "今日";
    }
    if (std::strcmp(label, "剩余水量") == 0 || std::strcmp(label, "剩余时间") == 0) {
        return "剩余";
    }
    if (std::strcmp(label, "实时流速") == 0 || std::strcmp(label, "均流速") == 0) {
        return "流速";
    }
    if (std::strcmp(label, "预计完成") == 0 || std::strcmp(label, "预计总量") == 0) {
        return "预计";
    }
    if (std::strcmp(label, "记录出水") == 0) {
        return "记录";
    }
    if (std::strcmp(label, "有效样本") == 0) {
        return "样本";
    }
    return label;
}

const char* compactHintLabel(const char* label) {
    if (!label) {
        return "";
    }
    if (std::strcmp(label, "确认 开始") == 0 || std::strcmp(label, "确认 返回") == 0 ||
        std::strcmp(label, "确认 继续") == 0) {
        return "确认";
    }
    if (std::strcmp(label, "取消 返回") == 0 || std::strcmp(label, "取消 待机") == 0 ||
        std::strcmp(label, "取消 结束") == 0 || std::strcmp(label, "取消 退出") == 0 ||
        std::strcmp(label, "取消 放弃") == 0) {
        return "取消";
    }
    if (std::strcmp(label, "加/减 调整") == 0) {
        return "加减";
    }
    if (std::strcmp(label, "确认 保存") == 0) {
        return "保存";
    }
    if (std::strcmp(label, "长按校准") == 0) {
        return "校准";
    }
    if (std::strcmp(label, "30s 后返回") == 0) {
        return "30s";
    }
    if (std::strcmp(label, "先排查水路") == 0) {
        return "排查";
    }
    return label;
}

}  // namespace

St7789Display::St7789Display(std::uint8_t sclkPin,
                             std::uint8_t mosiPin,
                             std::uint8_t dcPin,
                             std::uint8_t rstPin,
                             std::uint8_t backlightPin)
    : sclkPin_(sclkPin),
      mosiPin_(mosiPin),
      dcPin_(dcPin),
      rstPin_(rstPin),
      backlightPin_(backlightPin),
      present_(false),
      backlight_(false),
      lastFrameValid_(false),
      lastFrame_{}
#if FAUCET_ST7789_BOOT_TEST
      ,
      bootTestUntilMs_(0)
#endif
{}

bool St7789Display::begin() {
#if FAUCET_ST7789_USE_TFT_ESPI
    pinMode(backlightPin_, OUTPUT);
    setBacklight(false);
    g_tft.init();
    g_tft.setRotation(0);
    g_tft.setTextWrap(false, false);
    g_tft.setTextPadding(0);
#else
    pinMode(dcPin_, OUTPUT);
    pinMode(rstPin_, OUTPUT);
    pinMode(backlightPin_, OUTPUT);
    setBacklight(false);
    SPI.begin(sclkPin_, -1, mosiPin_, -1);
    command(kCmdNop);
    reset();

    tftInit();
#endif
    present_ = true;
    lastFrameValid_ = false;
#if FAUCET_ST7789_BOOT_TEST
    setBacklight(true);
    drawBootTestPattern();
    bootTestUntilMs_ = millis() + 5000UL;
#else
    fillScreen(kBlack);
#endif
    return true;
}

bool St7789Display::present() const {
    return present_;
}

void St7789Display::apply(const ColorDisplayFrame& frame) {
    if (!present_) {
        return;
    }
#if FAUCET_ST7789_BOOT_TEST
    if (bootTestUntilMs_ != 0 && static_cast<std::int32_t>(millis() - bootTestUntilMs_) < 0) {
        return;
    }
    bootTestUntilMs_ = 0;
#endif
    if (lastFrameValid_ && sameFrame(frame, lastFrame_)) {
        return;
    }
    if (!frame.on) {
        fillScreen(kBlack);
        setBacklight(false);
        lastFrame_ = frame;
        lastFrameValid_ = true;
        return;
    }
    setBacklight(true);
    renderFrame(frame, true);
    lastFrame_ = frame;
    lastFrameValid_ = true;
}

void St7789Display::reset() {
    digitalWrite(rstPin_, HIGH);
    delay(5);
    digitalWrite(rstPin_, LOW);
    delay(20);
    digitalWrite(rstPin_, HIGH);
    delay(120);
}

void St7789Display::tftInit() {
    command(kCmdSleepOut);
    delay(120);

    command(kCmdNormalOn);
    command(kCmdMadCtl);
    data(0x00);

    command(kCmdDisplayFunctionCtrl);
    const std::uint8_t displayFunction[] = {0x0A, 0x82};
    data(displayFunction, sizeof(displayFunction));

    command(kCmdRamCtrl);
    const std::uint8_t ramCtrl[] = {0x00, 0xE0};
    data(ramCtrl, sizeof(ramCtrl));

    command(kCmdColMod);
    data(0x55);
    delay(10);

    command(kCmdPorchCtrl);
    const std::uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    data(porch, sizeof(porch));

    command(kCmdGateCtrl);
    data(0x35);

    command(kCmdVcomSet);
    data(0x28);

    command(kCmdLcmCtrl);
    data(0x0C);

    command(kCmdVdvVrhEnable);
    const std::uint8_t vdvVrhEnable[] = {0x01, 0xFF};
    data(vdvVrhEnable, sizeof(vdvVrhEnable));

    command(kCmdVrhSet);
    data(0x10);

    command(kCmdVdvSet);
    data(0x20);

    command(kCmdFrameRateCtrl);
    data(0x0F);

    command(kCmdPowerCtrl1);
    const std::uint8_t powerCtrl[] = {0xA4, 0xA1};
    data(powerCtrl, sizeof(powerCtrl));

    command(kCmdPositiveGamma);
    const std::uint8_t positiveGamma[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
                                          0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
    data(positiveGamma, sizeof(positiveGamma));

    command(kCmdNegativeGamma);
    const std::uint8_t negativeGamma[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
                                          0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};
    data(negativeGamma, sizeof(negativeGamma));

    command(kCmdInvOn);

    command(kCmdCaseT);
    const std::uint8_t columnRange[] = {0x00, 0x00, 0x00, 0xEF};
    data(columnRange, sizeof(columnRange));

    command(kCmdRaseT);
    const std::uint8_t rowRange[] = {0x00, 0x00, 0x01, 0x3F};
    data(rowRange, sizeof(rowRange));

    delay(120);
    command(kCmdDispOn);
    delay(120);
}

#if FAUCET_ST7789_BOOT_TEST
void St7789Display::drawBootTestPattern() {
    fillRect(0, 0, 80, kHeight, kRed);
    fillRect(80, 0, 80, kHeight, kGreen);
    fillRect(160, 0, 80, kHeight, kBlue);
    fillRect(16, 92, 208, 56, kBlack);
    drawCenteredText(108, "ST7789 BOOT", kInk, kBlack, 1);
    drawCenteredText(130, "LIGHT DRIVER", kCyan, kBlack, 1);
}
#endif

void St7789Display::command(std::uint8_t value) {
    SPI.beginTransaction(SPISettings(kSt7789SpiHz, MSBFIRST, kSt7789SpiMode));
    digitalWrite(dcPin_, LOW);
    SPI.transfer(value);
    SPI.endTransaction();
}

void St7789Display::data(std::uint8_t value) {
    SPI.beginTransaction(SPISettings(kSt7789SpiHz, MSBFIRST, kSt7789SpiMode));
    digitalWrite(dcPin_, HIGH);
    SPI.transfer(value);
    SPI.endTransaction();
}

void St7789Display::data(const std::uint8_t* values, std::size_t len) {
    SPI.beginTransaction(SPISettings(kSt7789SpiHz, MSBFIRST, kSt7789SpiMode));
    digitalWrite(dcPin_, HIGH);
    SPI.writeBytes(values, len);
    SPI.endTransaction();
}

void St7789Display::setBacklight(bool on) {
    backlight_ = on;
    digitalWrite(backlightPin_, on ? HIGH : LOW);
}

void St7789Display::setAddressWindow(std::uint16_t x, std::uint16_t y, std::uint16_t w, std::uint16_t h) {
    const std::uint16_t x1 = static_cast<std::uint16_t>(x + w - 1U);
    const std::uint16_t y1 = static_cast<std::uint16_t>(y + h - 1U);
    std::uint8_t buffer[4] = {
        static_cast<std::uint8_t>(x >> 8U),
        static_cast<std::uint8_t>(x & 0xFFU),
        static_cast<std::uint8_t>(x1 >> 8U),
        static_cast<std::uint8_t>(x1 & 0xFFU),
    };
    command(kCmdCaseT);
    data(buffer, sizeof(buffer));
    buffer[0] = static_cast<std::uint8_t>(y >> 8U);
    buffer[1] = static_cast<std::uint8_t>(y & 0xFFU);
    buffer[2] = static_cast<std::uint8_t>(y1 >> 8U);
    buffer[3] = static_cast<std::uint8_t>(y1 & 0xFFU);
    command(kCmdRaseT);
    data(buffer, sizeof(buffer));
    command(kCmdRamWr);
}

void St7789Display::fillScreen(std::uint16_t color) {
    fillRect(0, 0, kWidth, kHeight, color);
}

void St7789Display::fillRect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, std::uint16_t color) {
    if (w <= 0 || h <= 0 || x >= static_cast<std::int16_t>(kWidth) || y >= static_cast<std::int16_t>(kHeight)) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > static_cast<std::int16_t>(kWidth)) {
        w = static_cast<std::int16_t>(kWidth - x);
    }
    if (y + h > static_cast<std::int16_t>(kHeight)) {
        h = static_cast<std::int16_t>(kHeight - y);
    }
    if (w <= 0 || h <= 0) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (g_drawToSprite) {
        g_frameSprite.fillRect(x, y, w, h, color);
    } else {
        g_tft.fillRect(x, y, w, h, color);
    }
#else
    setAddressWindow(static_cast<std::uint16_t>(x),
                     static_cast<std::uint16_t>(y),
                     static_cast<std::uint16_t>(w),
                     static_cast<std::uint16_t>(h));
    const std::uint8_t hi = static_cast<std::uint8_t>(color >> 8U);
    const std::uint8_t lo = static_cast<std::uint8_t>(color & 0xFFU);
    const std::uint8_t pixel[2] = {hi, lo};
    SPI.beginTransaction(SPISettings(kSt7789SpiHz, MSBFIRST, kSt7789SpiMode));
    digitalWrite(dcPin_, HIGH);
    SPI.writePattern(pixel, sizeof(pixel), static_cast<std::uint32_t>(w) * static_cast<std::uint32_t>(h));
    SPI.endTransaction();
#endif
}

bool St7789Display::beginBufferedFrame(bool fullRedraw) {
    if (!fullRedraw) {
        return false;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (!g_frameSpriteReady) {
        g_frameSprite.setColorDepth(16);
        g_frameSpriteReady = g_frameSprite.createSprite(kWidth, kHeight) != nullptr;
    }
    if (!g_frameSpriteReady) {
        return false;
    }
    g_drawToSprite = true;
    g_frameSprite.fillSprite(kBg);
    return true;
#else
    return false;
#endif
}

void St7789Display::endBufferedFrame(bool buffered) {
    if (!buffered) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    g_drawToSprite = false;
    g_frameSprite.pushSprite(0, 0);
#endif
}

void St7789Display::fillRoundRect(std::int16_t x,
                                  std::int16_t y,
                                  std::int16_t w,
                                  std::int16_t h,
                                  std::int16_t radius,
                                  std::uint16_t color,
                                  std::uint16_t background) {
    if (w <= 0 || h <= 0) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (g_drawToSprite) {
        g_frameSprite.fillSmoothRoundRect(x, y, w, h, radius, color, background);
    } else {
        g_tft.fillSmoothRoundRect(x, y, w, h, radius, color, background);
    }
#else
    fillRect(static_cast<std::int16_t>(x + radius), y, static_cast<std::int16_t>(w - radius * 2), h, color);
    fillRect(x, static_cast<std::int16_t>(y + radius), w, static_cast<std::int16_t>(h - radius * 2), color);
    fillCircle(static_cast<std::int16_t>(x + radius), static_cast<std::int16_t>(y + radius), radius, color);
    fillCircle(static_cast<std::int16_t>(x + w - radius - 1),
               static_cast<std::int16_t>(y + radius),
               radius,
               color);
    fillCircle(static_cast<std::int16_t>(x + radius),
               static_cast<std::int16_t>(y + h - radius - 1),
               radius,
               color);
    fillCircle(static_cast<std::int16_t>(x + w - radius - 1),
               static_cast<std::int16_t>(y + h - radius - 1),
               radius,
               color);
    (void)background;
#endif
}

void St7789Display::drawRoundRect(std::int16_t x,
                                  std::int16_t y,
                                  std::int16_t w,
                                  std::int16_t h,
                                  std::int16_t radius,
                                  std::uint16_t color,
                                  std::uint16_t background) {
    if (w <= 0 || h <= 0) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (g_drawToSprite) {
        g_frameSprite.drawSmoothRoundRect(x, y, radius, static_cast<std::int16_t>(radius - 1), w, h, color, background);
    } else {
        g_tft.drawSmoothRoundRect(x, y, radius, static_cast<std::int16_t>(radius - 1), w, h, color, background);
    }
#else
    drawLine(static_cast<std::int16_t>(x + radius), y, static_cast<std::int16_t>(x + w - radius - 1), y, color);
    drawLine(static_cast<std::int16_t>(x + radius),
             static_cast<std::int16_t>(y + h - 1),
             static_cast<std::int16_t>(x + w - radius - 1),
             static_cast<std::int16_t>(y + h - 1),
             color);
    drawLine(x, static_cast<std::int16_t>(y + radius), x, static_cast<std::int16_t>(y + h - radius - 1), color);
    drawLine(static_cast<std::int16_t>(x + w - 1),
             static_cast<std::int16_t>(y + radius),
             static_cast<std::int16_t>(x + w - 1),
             static_cast<std::int16_t>(y + h - radius - 1),
             color);
    (void)background;
#endif
}

void St7789Display::drawPixel(std::int16_t x, std::int16_t y, std::uint16_t color) {
    if (x < 0 || y < 0 || x >= static_cast<std::int16_t>(kWidth) || y >= static_cast<std::int16_t>(kHeight)) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (g_drawToSprite) {
        g_frameSprite.drawPixel(x, y, color);
    } else {
        g_tft.drawPixel(x, y, color);
    }
#else
    setAddressWindow(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), 1, 1);
    const std::uint8_t pixel[2] = {static_cast<std::uint8_t>(color >> 8U), static_cast<std::uint8_t>(color & 0xFFU)};
    data(pixel, sizeof(pixel));
#endif
}

void St7789Display::drawLine(std::int16_t x0,
                             std::int16_t y0,
                             std::int16_t x1,
                             std::int16_t y1,
                             std::uint16_t color) {
    const std::int16_t dx = std::abs(x1 - x0);
    const std::int16_t sx = x0 < x1 ? 1 : -1;
    const std::int16_t dy = -std::abs(y1 - y0);
    const std::int16_t sy = y0 < y1 ? 1 : -1;
    std::int16_t error = dx + dy;
    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const std::int16_t e2 = static_cast<std::int16_t>(2 * error);
        if (e2 >= dy) {
            error = static_cast<std::int16_t>(error + dy);
            x0 = static_cast<std::int16_t>(x0 + sx);
        }
        if (e2 <= dx) {
            error = static_cast<std::int16_t>(error + dx);
            y0 = static_cast<std::int16_t>(y0 + sy);
        }
    }
}

void St7789Display::fillCircle(std::int16_t x0, std::int16_t y0, std::int16_t r, std::uint16_t color) {
    for (std::int16_t y = -r; y <= r; ++y) {
        const std::int16_t span = static_cast<std::int16_t>(std::sqrt(static_cast<float>(r * r - y * y)));
        fillRect(static_cast<std::int16_t>(x0 - span), static_cast<std::int16_t>(y0 + y), static_cast<std::int16_t>(span * 2 + 1), 1, color);
    }
}

void St7789Display::drawRing(std::int16_t cx,
                             std::int16_t cy,
                             std::int16_t radius,
                             std::uint16_t progressPermille,
                             std::uint16_t color) {
    fillCircle(cx, cy, radius, kPanel2);
    fillCircle(cx, cy, static_cast<std::int16_t>(radius - 8), kBg);
    const std::uint16_t degrees = static_cast<std::uint16_t>(progressPermille * 360U / 1000U);
    for (std::uint16_t deg = 0; deg < degrees; deg += 8) {
        const float rad = (static_cast<float>(deg) - 90.0f) * 0.0174532925f;
        const std::int16_t x = static_cast<std::int16_t>(cx + std::cos(rad) * (radius - 4));
        const std::int16_t y = static_cast<std::int16_t>(cy + std::sin(rad) * (radius - 4));
        fillRect(static_cast<std::int16_t>(x - 2), static_cast<std::int16_t>(y - 2), 5, 5, color);
    }
}

void St7789Display::drawGlyphBlock(std::int16_t x,
                                   std::int16_t y,
                                   std::uint32_t codepoint,
                                   const St7789Glyph& glyph,
                                   std::uint16_t color,
                                   std::uint16_t background,
                                   std::uint8_t scale) {
    if (scale == 0 || scale > 3) {
        return;
    }
    const bool narrow = codepoint < 0x80UL;
    const std::uint8_t glyphWidth = narrow ? 8 : kSt7789GlyphWidth;
    const std::int16_t w = static_cast<std::int16_t>(glyphWidth * scale);
    const std::int16_t h = static_cast<std::int16_t>(kSt7789GlyphHeight * scale);
    if (x < 0 || y < 0 || x + w > static_cast<std::int16_t>(kWidth) || y + h > static_cast<std::int16_t>(kHeight)) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (g_drawToSprite) {
        g_frameSprite.setWindow(x,
                                y,
                                static_cast<std::int16_t>(x + w - 1),
                                static_cast<std::int16_t>(y + h - 1));
        for (std::uint8_t gy = 0; gy < kSt7789GlyphHeight; ++gy) {
            const std::uint8_t b0 = glyph.bitmap[gy * 2U];
            const std::uint8_t b1 = glyph.bitmap[gy * 2U + 1U];
            for (std::uint8_t sy = 0; sy < scale; ++sy) {
                for (std::uint8_t gx = 0; gx < glyphWidth; ++gx) {
                    const std::uint8_t sourceX = narrow ? static_cast<std::uint8_t>(gx * 2U) : gx;
                    const bool lit0 = sourceX < 8 ? (b0 & (1U << (7U - sourceX))) : (b1 & (1U << (15U - sourceX)));
                    const std::uint8_t sourceX1 = static_cast<std::uint8_t>(sourceX + 1U);
                    const bool lit1 = narrow && sourceX1 < kSt7789GlyphWidth
                                          ? (sourceX1 < 8 ? (b0 & (1U << (7U - sourceX1)))
                                                         : (b1 & (1U << (15U - sourceX1))))
                                          : false;
                    const std::uint16_t pixel = (lit0 || lit1) ? color : background;
                    for (std::uint8_t sx = 0; sx < scale; ++sx) {
                        g_frameSprite.pushColor(pixel);
                    }
                }
            }
        }
        return;
    }
    g_tft.startWrite();
    g_tft.setAddrWindow(x, y, w, h);
    std::uint16_t row[static_cast<std::size_t>(kSt7789GlyphWidth) * 3U]{};
    for (std::uint8_t gy = 0; gy < kSt7789GlyphHeight; ++gy) {
        const std::uint8_t b0 = glyph.bitmap[gy * 2U];
        const std::uint8_t b1 = glyph.bitmap[gy * 2U + 1U];
        std::size_t offset = 0;
        for (std::uint8_t gx = 0; gx < glyphWidth; ++gx) {
            const std::uint8_t sourceX = narrow ? static_cast<std::uint8_t>(gx * 2U) : gx;
            const bool lit0 = sourceX < 8 ? (b0 & (1U << (7U - sourceX))) : (b1 & (1U << (15U - sourceX)));
            const std::uint8_t sourceX1 = static_cast<std::uint8_t>(sourceX + 1U);
            const bool lit1 = narrow && sourceX1 < kSt7789GlyphWidth
                                  ? (sourceX1 < 8 ? (b0 & (1U << (7U - sourceX1)))
                                                 : (b1 & (1U << (15U - sourceX1))))
                                  : false;
            const std::uint16_t pixel = (lit0 || lit1) ? color : background;
            for (std::uint8_t sx = 0; sx < scale; ++sx) {
                row[offset++] = pixel;
            }
        }
        for (std::uint8_t sy = 0; sy < scale; ++sy) {
            g_tft.pushColors(row, static_cast<std::uint32_t>(offset), true);
        }
    }
    g_tft.endWrite();
#else
    setAddressWindow(static_cast<std::uint16_t>(x),
                     static_cast<std::uint16_t>(y),
                     static_cast<std::uint16_t>(w),
                     static_cast<std::uint16_t>(h));
    std::uint8_t row[static_cast<std::size_t>(kSt7789GlyphWidth) * 3U * 2U]{};
    SPI.beginTransaction(SPISettings(kSt7789SpiHz, MSBFIRST, kSt7789SpiMode));
    digitalWrite(dcPin_, HIGH);
    for (std::uint8_t gy = 0; gy < kSt7789GlyphHeight; ++gy) {
        const std::uint8_t b0 = glyph.bitmap[gy * 2U];
        const std::uint8_t b1 = glyph.bitmap[gy * 2U + 1U];
        std::size_t offset = 0;
        for (std::uint8_t gx = 0; gx < glyphWidth; ++gx) {
            const std::uint8_t sourceX = narrow ? static_cast<std::uint8_t>(gx * 2U) : gx;
            const bool lit0 = sourceX < 8 ? (b0 & (1U << (7U - sourceX))) : (b1 & (1U << (15U - sourceX)));
            const std::uint8_t sourceX1 = static_cast<std::uint8_t>(sourceX + 1U);
            const bool lit1 = narrow && sourceX1 < kSt7789GlyphWidth
                                  ? (sourceX1 < 8 ? (b0 & (1U << (7U - sourceX1)))
                                                 : (b1 & (1U << (15U - sourceX1))))
                                  : false;
            const std::uint16_t pixel = (lit0 || lit1) ? color : background;
            const std::uint8_t hi = static_cast<std::uint8_t>(pixel >> 8U);
            const std::uint8_t lo = static_cast<std::uint8_t>(pixel & 0xFFU);
            for (std::uint8_t sx = 0; sx < scale; ++sx) {
                row[offset++] = hi;
                row[offset++] = lo;
            }
        }
        for (std::uint8_t sy = 0; sy < scale; ++sy) {
            SPI.writeBytes(row, offset);
        }
    }
    SPI.endTransaction();
#endif
}

void St7789Display::drawText(std::int16_t x,
                             std::int16_t y,
                             const char* text,
                             std::uint16_t color,
                             std::uint16_t background,
                             std::uint8_t scale) {
    const char* cursor = text ? text : "";
#if FAUCET_ST7789_USE_TFT_ESPI
    const std::uint8_t asciiFont = asciiFontForScale(scale);
    char asciiRun[32]{};
    std::uint8_t asciiLen = 0;
    auto flushAsciiRun = [&]() {
        if (asciiLen == 0) {
            return;
        }
        asciiRun[asciiLen] = '\0';
        drawAsciiText(x, y, asciiRun, color, background, asciiFont);
        x = static_cast<std::int16_t>(x + asciiTextWidth(asciiRun, asciiFont));
        asciiLen = 0;
    };
    while (*cursor) {
        if (static_cast<std::uint8_t>(*cursor) < 0x80U) {
            asciiRun[asciiLen++] = *cursor++;
            if (asciiLen == sizeof(asciiRun) - 1U) {
                flushAsciiRun();
            }
            continue;
        }
        flushAsciiRun();
        const std::uint32_t cp = decodeUtf8(cursor);
        const St7789Glyph* glyph = findSt7789Glyph(cp);
        if (glyph) {
            drawGlyphBlock(x, y, cp, *glyph, color, background, scale);
        }
        x = static_cast<std::int16_t>(x + kSt7789GlyphWidth * scale);
    }
    flushAsciiRun();
#else
    while (*cursor) {
        const std::uint32_t cp = decodeUtf8(cursor);
        const St7789Glyph* glyph = findSt7789Glyph(cp);
        if (glyph) {
            drawGlyphBlock(x, y, cp, *glyph, color, background, scale);
        }
        x = static_cast<std::int16_t>(x + (cp < 0x80UL ? 8 : kSt7789GlyphWidth) * scale);
    }
#endif
}

void St7789Display::drawAsciiText(std::int16_t x,
                                  std::int16_t y,
                                  const char* text,
                                  std::uint16_t color,
                                  std::uint16_t background,
                                  std::uint8_t font) {
    if (!text || !*text) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (g_drawToSprite) {
        g_frameSprite.setTextDatum(TL_DATUM);
        g_frameSprite.setTextSize(1);
        g_frameSprite.setTextColor(color, background);
        g_frameSprite.drawString(text, x, y, font);
    } else {
        g_tft.setTextDatum(TL_DATUM);
        g_tft.setTextSize(1);
        g_tft.setTextColor(color, background);
        g_tft.drawString(text, x, y, font);
    }
#else
    const std::uint8_t scale = font >= 4 ? 2 : 1;
    drawText(x, y, text, color, background, scale);
#endif
}

void St7789Display::drawAsciiTrackedText(std::int16_t x,
                                         std::int16_t y,
                                         const char* text,
                                         std::uint16_t color,
                                         std::uint16_t background,
                                         std::uint8_t font,
                                         std::int16_t tracking) {
    if (!text || !*text) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    char one[2]{};
    const char* cursor = text;
    while (*cursor) {
        one[0] = *cursor++;
        drawAsciiText(x, y, one, color, background, font);
        x = static_cast<std::int16_t>(x + asciiTextWidth(one, font) + tracking);
    }
#else
    (void)tracking;
    drawAsciiText(x, y, text, color, background, font);
#endif
}

void St7789Display::drawTextFit(std::int16_t x,
                                std::int16_t y,
                                std::int16_t maxWidth,
                                const char* text,
                                std::uint16_t color,
                                std::uint16_t background,
                                std::uint8_t scale) {
    if (!text || !*text || maxWidth <= 0) {
        return;
    }
    if (textWidth(text, scale) <= maxWidth) {
        drawText(x, y, text, color, background, scale);
        return;
    }
    const char* compact = compactMetricLabel(text);
    if (compact != text && textWidth(compact, scale) <= maxWidth) {
        drawText(x, y, compact, color, background, scale);
        return;
    }
    char fitted[32]{};
    const char* cursor = compact;
    std::size_t len = 0;
    while (*cursor && len < sizeof(fitted) - 1U) {
        const char* glyphStart = cursor;
        decodeUtf8(cursor);
        const std::size_t glyphBytes = static_cast<std::size_t>(cursor - glyphStart);
        if (len + glyphBytes >= sizeof(fitted)) {
            break;
        }
        std::memcpy(fitted + len, glyphStart, glyphBytes);
        fitted[len + glyphBytes] = '\0';
        if (textWidth(fitted, scale) > maxWidth) {
            fitted[len] = '\0';
            break;
        }
        len += glyphBytes;
    }
    if (fitted[0]) {
        drawText(x, y, fitted, color, background, scale);
    }
}

void St7789Display::drawMetricLabel(std::int16_t x,
                                    std::int16_t y,
                                    std::int16_t maxWidth,
                                    const char* text,
                                    std::uint16_t color,
                                    std::uint16_t background) {
    if (!text || !*text) {
        return;
    }
#if FAUCET_ST7789_USE_TFT_ESPI
    if (isAsciiText(text)) {
        constexpr std::int16_t kTracking = 2;
        if (asciiTrackedTextWidth(text, 2, kTracking) <= maxWidth) {
            drawAsciiTrackedText(x, y, text, color, background, 2, kTracking);
            return;
        }
    }
#endif
    drawTextFit(x, y, maxWidth, text, color, background, 1);
}

void St7789Display::drawCenteredText(std::int16_t y,
                                     const char* text,
                                     std::uint16_t color,
                                     std::uint16_t background,
                                     std::uint8_t scale) {
    const std::int16_t x = static_cast<std::int16_t>((static_cast<std::int16_t>(kWidth) - textWidth(text, scale)) / 2);
    drawText(x, y, text, color, background, scale);
}

void St7789Display::drawCenteredTextFit(std::int16_t y,
                                        std::int16_t maxWidth,
                                        const char* text,
                                        std::uint16_t color,
                                        std::uint16_t background,
                                        std::uint8_t scale) {
    const std::int16_t textW = std::min(textWidth(text, scale), maxWidth);
    const std::int16_t x = static_cast<std::int16_t>((static_cast<std::int16_t>(kWidth) - textW) / 2);
    drawTextFit(x, y, maxWidth, text, color, background, scale);
}

void St7789Display::drawBoxCenteredText(std::int16_t x,
                                        std::int16_t y,
                                        std::int16_t w,
                                        const char* text,
                                        std::uint16_t color,
                                        std::uint16_t background,
                                        std::uint8_t scale) {
    const std::int16_t textW = std::min(textWidth(text, scale), w);
    drawTextFit(static_cast<std::int16_t>(x + (w - textW) / 2), y, w, text, color, background, scale);
}

void St7789Display::drawMainValue(std::int16_t centerX,
                                  std::int16_t y,
                                  const char* value,
                                  const char* unit,
                                  std::uint8_t valueFont,
                                  std::uint8_t unitFont,
                                  std::uint16_t valueColor,
                                  std::uint16_t unitColor,
                                  std::uint16_t background) {
    const char* safeValue = value ? value : "";
    const char* safeUnit = unit ? unit : "";
#if FAUCET_ST7789_USE_TFT_ESPI
    const std::int16_t valueW = asciiTextWidth(safeValue, valueFont);
    const bool hasUnit = safeUnit[0] != '\0';
    const bool asciiUnit = isAsciiText(safeUnit);
    const std::int16_t unitW =
        hasUnit ? (asciiUnit ? asciiTextWidth(safeUnit, unitFont) : textWidth(safeUnit, 1)) : 0;
    const std::int16_t gap = hasUnit ? 4 : 0;
    const std::int16_t x = static_cast<std::int16_t>(centerX - (valueW + gap + unitW) / 2);
    drawAsciiText(x, y, safeValue, valueColor, background, valueFont);
    if (!hasUnit) {
        return;
    }
    const std::int16_t unitX = static_cast<std::int16_t>(x + valueW + gap);
    const std::int16_t unitY = static_cast<std::int16_t>(y + (valueFont >= 7 ? 30 : 12));
    if (asciiUnit) {
        drawAsciiText(unitX, unitY, safeUnit, unitColor, background, unitFont);
    } else {
        drawText(unitX, unitY, safeUnit, unitColor, background, 1);
    }
#else
    (void)valueFont;
    (void)unitFont;
    drawCenteredText(y, safeValue, valueColor, background, 3);
    if (safeUnit[0]) {
        drawText(static_cast<std::int16_t>(centerX + 54), static_cast<std::int16_t>(y + 19), safeUnit, unitColor, background, 1);
    }
#endif
}

void St7789Display::drawTagPill(std::int16_t rightX,
                                std::int16_t y,
                                const char* text,
                                std::uint16_t color,
                                std::uint16_t background) {
    constexpr std::int16_t kPadX = 7;
    const std::int16_t textW = textWidth(text, 1);
    const std::int16_t w = static_cast<std::int16_t>(textW + kPadX * 2);
    const std::int16_t x = static_cast<std::int16_t>(rightX - w);
    fillRoundRect(x, y, w, kTagHeight, 7, kPanel2, background);
    drawRoundRect(x, y, w, kTagHeight, 7, kLine, background);
    drawTextFit(static_cast<std::int16_t>(x + kPadX),
                static_cast<std::int16_t>(y + (kTagHeight - kTextLineHeight) / 2),
                static_cast<std::int16_t>(w - kPadX * 2),
                text,
                color,
                kPanel2,
                1);
}

void St7789Display::drawStatusPill(std::int16_t x,
                                   std::int16_t y,
                                   std::int16_t w,
                                   std::int16_t h,
                                   const char* text,
                                   std::uint16_t textColor,
                                   std::uint16_t fillColor,
                                   std::uint16_t borderColor,
                                   std::uint16_t background) {
    constexpr std::int16_t kPadX = 6;
    const std::int16_t maxTextW = static_cast<std::int16_t>(w - kPadX * 2);
    const std::int16_t textW = std::min(textWidth(text, 1), maxTextW);
    fillRoundRect(x, y, w, h, 7, fillColor, background);
    drawRoundRect(x, y, w, h, 7, borderColor, background);
    drawTextFit(static_cast<std::int16_t>(x + (w - textW) / 2),
                static_cast<std::int16_t>(y + (h - kTextLineHeight) / 2),
                maxTextW,
                text,
                textColor,
                fillColor,
                1);
}

void St7789Display::drawMetricCard(std::int16_t x,
                                   std::int16_t y,
                                   std::int16_t w,
                                   const ColorDisplayMetric& metric,
                                   std::int16_t h) {
    constexpr std::int16_t kPad = 6;
    const std::int16_t contentW = static_cast<std::int16_t>(w - kPad * 2);
    fillRoundRect(x, y, w, h, 6, kPanel, kBg);
    drawRoundRect(x, y, w, h, 6, kLine, kBg);
    drawMetricLabel(static_cast<std::int16_t>(x + kPad),
                    static_cast<std::int16_t>(y + (h >= 40 ? 5 : 3)),
                    contentW,
                    metric.label,
                    kMuted,
                    kPanel);
    char value[40]{};
    std::snprintf(value, sizeof(value), "%s%s", metric.value, metric.unit);
    drawTextFit(static_cast<std::int16_t>(x + kPad),
                static_cast<std::int16_t>(y + (h >= 40 ? 21 : 19)),
                contentW,
                value,
                kInk,
                kPanel,
                1);
}

void St7789Display::drawSensorCard(std::int16_t x, std::int16_t y, std::int16_t w, const ColorDisplaySensor& sensor) {
    const std::int16_t contentW = static_cast<std::int16_t>(w - 12);
    fillRoundRect(x, y, w, 54, 6, kPanel, kBg);
    drawRoundRect(x, y, w, 54, 6, kLine, kBg);
    drawTextFit(static_cast<std::int16_t>(x + 6),
                static_cast<std::int16_t>(y + 4),
                contentW,
                sensor.label,
                kMuted,
                kPanel,
                1);
    char value[40]{};
    std::snprintf(value, sizeof(value), "%s%s", sensor.value, sensor.unit);
    drawTextFit(static_cast<std::int16_t>(x + 6),
                static_cast<std::int16_t>(y + 20),
                contentW,
                value,
                sensor.unit[0] == 'p' ? kCyan : kGreen,
                kPanel,
                1);
    if (sensor.sampleCount < 2) {
        return;
    }
    std::uint16_t minValue = sensor.samples[0];
    std::uint16_t maxValue = sensor.samples[0];
    for (std::uint8_t i = 1; i < sensor.sampleCount; ++i) {
        minValue = std::min(minValue, sensor.samples[i]);
        maxValue = std::max(maxValue, sensor.samples[i]);
    }
    if (minValue == maxValue) {
        maxValue = static_cast<std::uint16_t>(minValue + 1);
    }
    std::int16_t lastX = x + 5;
    std::int16_t lastY = static_cast<std::int16_t>(y + 46 - (sensor.samples[0] - minValue) * 12 / (maxValue - minValue));
    for (std::uint8_t i = 1; i < sensor.sampleCount; ++i) {
        const std::int16_t px = static_cast<std::int16_t>(x + 5 + i * (w - 10) / (sensor.sampleCount - 1));
        const std::int16_t py =
            static_cast<std::int16_t>(y + 46 - (sensor.samples[i] - minValue) * 12 / (maxValue - minValue));
        drawLine(lastX, lastY, px, py, sensor.unit[0] == 'p' ? kCyan : kGreen);
        lastX = px;
        lastY = py;
    }
}

void St7789Display::drawTopBar(const ColorDisplayFrame& frame, std::uint16_t accent) {
    fillCircle(16, 21, 4, accent);
    drawText(26, 14, frame.state, kInk, kBg, 1);
    drawTagPill(226, 10, frame.tag, accent, kBg);
}

void St7789Display::drawHintSlot(std::int16_t x, std::int16_t y, std::int16_t w, const char* text) {
    const char* label = compactHintLabel(text);
    const std::int16_t maxTextW = static_cast<std::int16_t>(w - 8);
    const std::int16_t labelW = std::min(textWidth(label, 1), maxTextW);
    fillRect(x, static_cast<std::int16_t>(y - 3), static_cast<std::int16_t>(w - 5), 1, kLine);
    drawTextFit(static_cast<std::int16_t>(x + (w - labelW) / 2),
                y,
                maxTextW,
                label,
                kMuted,
                kBg,
                1);
}

void St7789Display::drawHints(const ColorDisplayFrame& frame) {
    if (frame.hintCount == 0) {
        return;
    }
    const std::int16_t w = static_cast<std::int16_t>(212 / frame.hintCount);
    for (std::uint8_t i = 0; i < frame.hintCount; ++i) {
        const std::int16_t x = static_cast<std::int16_t>(14 + i * w);
        drawHintSlot(x, 221, w, frame.hints[i]);
    }
}

void St7789Display::renderFrame(const ColorDisplayFrame& frame, bool fullRedraw) {
    const std::uint16_t accent = accentForPage(frame.page);
    const bool buffered = beginBufferedFrame(fullRedraw);
    if (fullRedraw && !buffered) {
        fillScreen(kBg);
    }
    if (fullRedraw) {
        fillRect(0, 0, kWidth, 40, kBg);
    }
    drawTopBar(frame, accent);

    switch (frame.page) {
        case ColorDisplayPage::StandbyVolume:
        case ColorDisplayPage::StandbyTime:
        case ColorDisplayPage::StandbyOffline:
            drawCenteredText(49, frame.title, kMuted, kBg, 1);
            drawMainValue(120, 70, frame.mainValue, frame.mainUnit, 7, 4, kInk, accent, kBg);
            drawCenteredText(136, frame.subtitle, kCyan, kBg, 1);
            if (frame.metricCount > 0) {
                drawMetricCard(14, 184, 78, frame.metrics[0], 40);
            }
            if (frame.metricCount > 1) {
                drawMetricCard(99, 184, 66, frame.metrics[1], 40);
            }
            if (frame.metricCount > 2) {
                drawMetricCard(173, 184, 53, frame.metrics[2], 40);
            }
            break;

        case ColorDisplayPage::ConfirmVolume:
        case ColorDisplayPage::ConfirmTime:
            fillRoundRect(18, 58, 204, 112, 7, kPanel2, kBg);
            drawRoundRect(18, 58, 204, 112, 7, accent, kBg);
            drawCenteredTextFit(72, 184, frame.title, kMuted, kPanel2, 1);
            drawMainValue(120,
                          94,
                          frame.mainValue,
                          frame.mainUnit,
                          frame.page == ColorDisplayPage::ConfirmTime ? 4 : 7,
                          4,
                          kInk,
                          accent,
                          kPanel2);
            drawCenteredTextFit(151, 184, "确认后开始出水", kGreen, kPanel2, 1);
            drawCenteredTextFit(180, 212, frame.subtitle, kMuted, kBg, 1);
            drawHints(frame);
            break;

        case ColorDisplayPage::RunningVolume:
        case ColorDisplayPage::RunningTime:
            drawRing(69, 92, 50, frame.progressPermille, accent);
            drawBoxCenteredText(18, 62, 102, frame.title, kMuted, kBg, 1);
            drawMainValue(69, 82, frame.mainValue, frame.mainUnit, 4, 2, kInk, accent, kBg);
            for (std::uint8_t i = 0; i < frame.metricCount && i < 3; ++i) {
                drawMetricCard(132, static_cast<std::int16_t>(48 + i * 38), 94, frame.metrics[i], 36);
            }
            if (frame.sensorCount > 0) {
                drawSensorCard(14, 170, 102, frame.sensors[0]);
            }
            if (frame.sensorCount > 1) {
                drawSensorCard(124, 170, 102, frame.sensors[1]);
            }
            break;

        case ColorDisplayPage::PausedVolume:
        case ColorDisplayPage::PausedTime:
            drawCenteredText(49, frame.title, kInk, kBg, 2);
            drawStatusPill(48, 90, 144, 30, frame.status, kAmber, kPanel2, kLine, kBg);
            drawCenteredText(124, frame.subtitle, kMuted, kBg, 1);
            for (std::uint8_t i = 0; i < frame.metricCount && i < 4; ++i) {
                drawMetricCard(static_cast<std::int16_t>(16 + (i % 2) * 106),
                               static_cast<std::int16_t>(142 + (i / 2) * 39),
                               98,
                               frame.metrics[i],
                               36);
            }
            drawHints(frame);
            break;

        case ColorDisplayPage::ResultCompleted:
        case ColorDisplayPage::ResultStopped:
        case ColorDisplayPage::CalibrationReady:
        case ColorDisplayPage::CalibrationEntry:
            drawCenteredText(52, frame.title, kMuted, kBg, 1);
            drawMainValue(120, 73, frame.mainValue, frame.mainUnit, 7, 4, kInk, accent, kBg);
            for (std::uint8_t i = 0; i < frame.metricCount && i < 4; ++i) {
                drawMetricCard(static_cast<std::int16_t>(16 + (i % 2) * 106),
                               static_cast<std::int16_t>(140 + (i / 2) * 39),
                               98,
                               frame.metrics[i],
                               36);
            }
            drawHints(frame);
            break;

        case ColorDisplayPage::Alert:
            drawCenteredText(54, frame.title, kInk, kBg, 2);
            drawStatusPill(48, 100, 144, 30, frame.status, kRed, rgb565(0x32, 0x05, 0x12), kRed, kBg);
            for (std::uint8_t i = 0; i < frame.metricCount && i < 2; ++i) {
                drawMetricCard(static_cast<std::int16_t>(28 + i * 96), 153, 88, frame.metrics[i], 36);
            }
            drawHints(frame);
            break;

        case ColorDisplayPage::Sleep:
            fillScreen(kBlack);
            drawCenteredText(207, frame.subtitle, kDim, kBlack, 1);
            break;
    }
    endBufferedFrame(buffered);
}

}  // namespace faucet
