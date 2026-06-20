#include <unity.h>

#include "app/DateTimeUtils.h"

using namespace faucet;

void test_leap_year_and_month_days_follow_gregorian_rules() {
    TEST_ASSERT_TRUE(isLeapYear(2024));
    TEST_ASSERT_FALSE(isLeapYear(2100));
    TEST_ASSERT_TRUE(isLeapYear(2000));
    TEST_ASSERT_EQUAL_UINT8(29, daysInMonth(2024, 2));
    TEST_ASSERT_EQUAL_UINT8(28, daysInMonth(2023, 2));
    TEST_ASSERT_EQUAL_UINT8(0, daysInMonth(2024, 0));
    TEST_ASSERT_EQUAL_UINT8(0, daysInMonth(2024, 13));
}

void test_days_and_seconds_since_2000_round_trip() {
    const std::uint32_t seconds = secondsSince2000(2024, 2, 29, 13, 45, 5);
    TEST_ASSERT_EQUAL_UINT32(762529505UL, seconds);

    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    dateFromDayIndex(seconds / 86400UL, year, month, day);

    TEST_ASSERT_EQUAL_UINT16(2024, year);
    TEST_ASSERT_EQUAL_UINT8(2, month);
    TEST_ASSERT_EQUAL_UINT8(29, day);
}

void test_month_start_day_returns_first_day_of_same_month() {
    const std::uint32_t march15 = daysSince2000(2026, 3, 15);
    const std::uint32_t march1 = daysSince2000(2026, 3, 1);

    TEST_ASSERT_EQUAL_UINT32(march1, monthStartDay(march15));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_leap_year_and_month_days_follow_gregorian_rules);
    RUN_TEST(test_days_and_seconds_since_2000_round_trip);
    RUN_TEST(test_month_start_day_returns_first_day_of_same_month);
    return UNITY_END();
}
