#include "web/FaucetWebParsing.h"

#include "app/DateTimeUtils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace faucet {
namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool parseFixedU32(const char* text, std::size_t offset, std::size_t count, unsigned& value) {
    value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[offset + i];
        if (!isDigit(c)) {
            return false;
        }
        value = value * 10U + static_cast<unsigned>(c - '0');
    }
    return true;
}

}  // namespace

bool parseU32(const char* text, std::uint32_t& value) {
    if (!text || !*text) {
        return false;
    }
    for (const char* p = text; *p; ++p) {
        if (!isDigit(*p)) {
            return false;
        }
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (!end || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parseDate(const char* text, std::uint32_t& seconds) {
    if (!text || !*text) {
        seconds = 0;
        return true;
    }
    if (!text[0] || !text[1] || !text[2] || !text[3] || text[4] != '-' || !text[5] || !text[6] || text[7] != '-' ||
        !text[8] || !text[9] || text[10] != '\0') {
        return false;
    }

    unsigned year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (!parseFixedU32(text, 0, 4, year) || !parseFixedU32(text, 5, 2, month) ||
        !parseFixedU32(text, 8, 2, day) || year < 2020 || year > 2099 || month < 1 || month > 12) {
        return false;
    }
    const std::uint8_t maxDay = daysInMonth(static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month));
    if (day < 1 || day > maxDay) {
        return false;
    }
    seconds = daysSince2000(static_cast<std::uint16_t>(year),
                            static_cast<std::uint8_t>(month),
                            static_cast<std::uint8_t>(day)) *
              86400UL;
    return true;
}

void formatDate(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (seconds == 0) {
        return;
    }
    const std::uint32_t day = seconds / 86400UL;
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t monthDay = 0;
    dateFromDayIndex(day, year, month, monthDay);
    std::snprintf(out, len, "%04u-%02u-%02u", static_cast<unsigned>(year), static_cast<unsigned>(month),
                  static_cast<unsigned>(monthDay));
}

bool parseFloat(const char* text, float& value) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(text, &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

}  // namespace faucet
