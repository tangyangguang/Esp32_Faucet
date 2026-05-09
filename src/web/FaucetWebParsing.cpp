#include "web/FaucetWebParsing.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace faucet {
namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

std::uint8_t daysInMonth(std::uint16_t year, std::uint8_t month) {
    static constexpr std::uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return month >= 1 && month <= 12 ? days[month - 1] : 0;
}

std::uint32_t daysSince2000(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    static constexpr std::uint16_t kDaysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    std::uint32_t days = 0;
    for (std::uint16_t y = 2000; y < year; ++y) {
        days += isLeapYear(y) ? 366UL : 365UL;
    }
    std::uint32_t value = days + kDaysBeforeMonth[month - 1] + day - 1U;
    if (month > 2 && isLeapYear(year)) {
        ++value;
    }
    return value;
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
    std::uint32_t day = seconds / 86400UL;
    std::uint16_t year = 2000;
    while (true) {
        const std::uint16_t yearDays = isLeapYear(year) ? 366 : 365;
        if (day < yearDays) {
            break;
        }
        day -= yearDays;
        ++year;
    }
    std::uint8_t month = 1;
    while (month <= 12) {
        const std::uint8_t monthDays = daysInMonth(year, month);
        if (day < monthDays) {
            break;
        }
        day -= monthDays;
        ++month;
    }
    std::snprintf(out, len, "%04u-%02u-%02u", static_cast<unsigned>(year), static_cast<unsigned>(month),
                  static_cast<unsigned>(day + 1));
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

bool parseLitersToMl(const char* text, std::uint32_t& value) {
    float liters = 0.0f;
    if (!parseFloat(text, liters) || liters < 0.0f) {
        return false;
    }
    if (liters == 0.0f) {
        value = 0;
        return true;
    }
    const float ml = liters * 1000.0f;
    if (ml > static_cast<float>(UINT32_MAX)) {
        return false;
    }
    value = static_cast<std::uint32_t>(ml + 0.5f);
    return true;
}

}  // namespace faucet
