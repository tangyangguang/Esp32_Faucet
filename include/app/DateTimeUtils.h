#pragma once

#include <cstdint>

namespace faucet {

inline bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

inline std::uint8_t daysInMonth(std::uint16_t year, std::uint8_t month) {
    static constexpr std::uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

inline std::uint16_t dayOfYear(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    static constexpr std::uint16_t kDaysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const std::uint8_t safeMonth = month >= 1 && month <= 12 ? month : 1;
    std::uint16_t value = kDaysBeforeMonth[safeMonth - 1] + day - 1U;
    if (safeMonth > 2 && isLeapYear(year)) {
        ++value;
    }
    return value;
}

inline std::uint32_t daysSince2000(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    std::uint32_t days = 0;
    for (std::uint16_t y = 2000; y < year; ++y) {
        days += isLeapYear(y) ? 366UL : 365UL;
    }
    return days + dayOfYear(year, month, day);
}

inline std::uint32_t secondsSince2000(std::uint16_t year,
                                      std::uint8_t month,
                                      std::uint8_t day,
                                      std::uint8_t hour,
                                      std::uint8_t minute,
                                      std::uint8_t second) {
    return daysSince2000(year, month, day) * 86400UL + static_cast<std::uint32_t>(hour) * 3600UL +
           static_cast<std::uint32_t>(minute) * 60UL + second;
}

inline void dateFromDayIndex(std::uint32_t day,
                             std::uint16_t& year,
                             std::uint8_t& month,
                             std::uint8_t& monthDay) {
    year = 2000;
    while (true) {
        const std::uint16_t yearDays = isLeapYear(year) ? 366 : 365;
        if (day < yearDays) {
            break;
        }
        day -= yearDays;
        ++year;
    }
    month = 1;
    while (month <= 12) {
        const std::uint8_t monthDays = daysInMonth(year, month);
        if (day < monthDays) {
            break;
        }
        day -= monthDays;
        ++month;
    }
    monthDay = static_cast<std::uint8_t>(day + 1U);
}

inline std::uint32_t monthStartDay(std::uint32_t day) {
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t monthDay = 0;
    dateFromDayIndex(day, year, month, monthDay);
    return day - static_cast<std::uint32_t>(monthDay - 1U);
}

}  // namespace faucet
