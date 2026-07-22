#pragma once

#include "app/ColorDisplayPresenter.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

struct St7789Glyph;

class St7789Display {
public:
    explicit St7789Display(std::uint8_t backlightPin);

    bool begin();
    bool present() const;
    void apply(const ColorDisplayFrame& frame);

private:
    void setBacklight(bool on);
    void fillScreen(std::uint16_t color);
    void fillRect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, std::uint16_t color);
    bool beginBufferedFrame(bool fullRedraw);
    void endBufferedFrame(bool buffered);
    bool beginBufferedRegion(std::int16_t x,
                             std::int16_t y,
                             std::int16_t w,
                             std::int16_t h,
                             std::uint16_t background);
    void endBufferedRegion(bool buffered,
                           std::int16_t x,
                           std::int16_t y,
                           std::int16_t w,
                           std::int16_t h);
    void fillRoundRect(std::int16_t x,
                       std::int16_t y,
                       std::int16_t w,
                       std::int16_t h,
                       std::int16_t radius,
                       std::uint16_t color,
                       std::uint16_t background);
    void drawRoundRect(std::int16_t x,
                       std::int16_t y,
                       std::int16_t w,
                       std::int16_t h,
                       std::int16_t radius,
                       std::uint16_t color,
                       std::uint16_t background);
    void drawPixel(std::int16_t x, std::int16_t y, std::uint16_t color);
    void drawLine(std::int16_t x0, std::int16_t y0, std::int16_t x1, std::int16_t y1, std::uint16_t color);
    void fillCircle(std::int16_t x0, std::int16_t y0, std::int16_t r, std::uint16_t color);
    void drawRing(std::int16_t cx, std::int16_t cy, std::int16_t radius, std::uint16_t progressPermille, std::uint16_t color);
    void drawGlyphBlock(std::int16_t x,
                        std::int16_t y,
                        std::uint32_t codepoint,
                        const St7789Glyph& glyph,
                        std::uint16_t color,
                        std::uint16_t background,
                        std::uint8_t scale);
    void drawText(std::int16_t x,
                  std::int16_t y,
                  const char* text,
                  std::uint16_t color,
                  std::uint16_t background,
                  std::uint8_t scale);
    void drawAsciiText(std::int16_t x,
                       std::int16_t y,
                       const char* text,
                       std::uint16_t color,
                       std::uint16_t background,
                       std::uint8_t font);
    void drawAsciiTrackedText(std::int16_t x,
                              std::int16_t y,
                              const char* text,
                              std::uint16_t color,
                              std::uint16_t background,
                              std::uint8_t font,
                              std::int16_t tracking);
    void drawTextFit(std::int16_t x,
                     std::int16_t y,
                     std::int16_t maxWidth,
                     const char* text,
                     std::uint16_t color,
                     std::uint16_t background,
                     std::uint8_t scale);
    void drawMetricLabel(std::int16_t x,
                         std::int16_t y,
                         std::int16_t maxWidth,
                         const char* text,
                         std::uint16_t color,
                         std::uint16_t background);
    void drawCenteredText(std::int16_t y,
                          const char* text,
                          std::uint16_t color,
                          std::uint16_t background,
                          std::uint8_t scale);
    void drawCenteredTextFit(std::int16_t y,
                             std::int16_t maxWidth,
                             const char* text,
                             std::uint16_t color,
                             std::uint16_t background,
                             std::uint8_t scale);
    void drawBoxCenteredText(std::int16_t x,
                             std::int16_t y,
                             std::int16_t w,
                             const char* text,
                             std::uint16_t color,
                             std::uint16_t background,
                             std::uint8_t scale);
    void drawMainValue(std::int16_t centerX,
                       std::int16_t y,
                       const char* value,
                       const char* unit,
                       std::uint8_t valueFont,
                       std::uint8_t unitFont,
                       std::uint16_t valueColor,
                       std::uint16_t unitColor,
                       std::uint16_t background);
    void drawTagPill(std::int16_t rightX,
                     std::int16_t y,
                     const char* text,
                     std::uint16_t color,
                     std::uint16_t background);
    void drawStatusPill(std::int16_t x,
                        std::int16_t y,
                        std::int16_t w,
                        std::int16_t h,
                        const char* text,
                        std::uint16_t textColor,
                        std::uint16_t fillColor,
                        std::uint16_t borderColor,
                        std::uint16_t background);
    void drawMetricCard(std::int16_t x,
                        std::int16_t y,
                        std::int16_t w,
                        const ColorDisplayMetric& metric,
                        std::int16_t h);
    void drawSensorCard(std::int16_t x, std::int16_t y, std::int16_t w, const ColorDisplaySensor& sensor);
    void drawTopBar(const ColorDisplayFrame& frame, std::uint16_t accent);
    void drawHintSlot(std::int16_t x, std::int16_t y, std::int16_t w, const char* text);
    void drawHints(const ColorDisplayFrame& frame);
    bool renderPartialFrame(const ColorDisplayFrame& frame, const ColorDisplayFrame& previous);
    void renderFrame(const ColorDisplayFrame& frame, bool fullRedraw);

    std::uint8_t backlightPin_;
    bool present_;
    bool backlight_;
    bool lastFrameValid_;
    ColorDisplayFrame lastFrame_;
};

}  // namespace faucet
