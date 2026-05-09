#include <unity.h>

#include "web/FaucetWebParsing.h"

#include <cstring>

using namespace faucet;

void test_parse_u32_accepts_digits_only_and_preserves_value_on_failure() {
    std::uint32_t value = 42;

    TEST_ASSERT_TRUE(parseU32("0", value));
    TEST_ASSERT_EQUAL_UINT32(0, value);
    TEST_ASSERT_TRUE(parseU32("4294967295", value));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);

    TEST_ASSERT_FALSE(parseU32("", value));
    TEST_ASSERT_FALSE(parseU32(" 1", value));
    TEST_ASSERT_FALSE(parseU32("+1", value));
    TEST_ASSERT_FALSE(parseU32("-1", value));
    TEST_ASSERT_FALSE(parseU32("12x", value));
    TEST_ASSERT_FALSE(parseU32("4294967296", value));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
}

void test_parse_date_is_strict_and_handles_leap_years() {
    std::uint32_t seconds = 123;
    char formatted[16]{};

    TEST_ASSERT_TRUE(parseDate("", seconds));
    TEST_ASSERT_EQUAL_UINT32(0, seconds);
    TEST_ASSERT_TRUE(parseDate("2024-02-29", seconds));
    formatDate(seconds, formatted, sizeof(formatted));
    TEST_ASSERT_EQUAL_STRING("2024-02-29", formatted);

    TEST_ASSERT_FALSE(parseDate("2019-12-31", seconds));
    TEST_ASSERT_FALSE(parseDate("2100-01-01", seconds));
    TEST_ASSERT_FALSE(parseDate("2023-02-29", seconds));
    TEST_ASSERT_FALSE(parseDate("2024-13-01", seconds));
    TEST_ASSERT_FALSE(parseDate("2024-00-01", seconds));
    TEST_ASSERT_FALSE(parseDate("2024-01-00", seconds));
    TEST_ASSERT_FALSE(parseDate("2024-1-01", seconds));
    TEST_ASSERT_FALSE(parseDate("2024-01-01x", seconds));
}

void test_parse_float_and_liters_reject_non_finite_or_malformed_values() {
    float f = 0.0f;
    std::uint32_t ml = 77;

    TEST_ASSERT_TRUE(parseFloat("1.25", f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, f);
    TEST_ASSERT_FALSE(parseFloat("", f));
    TEST_ASSERT_FALSE(parseFloat("nan", f));
    TEST_ASSERT_FALSE(parseFloat("inf", f));
    TEST_ASSERT_FALSE(parseFloat("1e9999", f));
    TEST_ASSERT_FALSE(parseFloat("1.2ml", f));

    TEST_ASSERT_TRUE(parseLitersToMl("1.25", ml));
    TEST_ASSERT_EQUAL_UINT32(1250, ml);
    TEST_ASSERT_TRUE(parseLitersToMl("0", ml));
    TEST_ASSERT_EQUAL_UINT32(0, ml);
    TEST_ASSERT_FALSE(parseLitersToMl("-1", ml));
    TEST_ASSERT_FALSE(parseLitersToMl("nan", ml));
    TEST_ASSERT_FALSE(parseLitersToMl("4294968", ml));
    TEST_ASSERT_EQUAL_UINT32(0, ml);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_parse_u32_accepts_digits_only_and_preserves_value_on_failure);
    RUN_TEST(test_parse_date_is_strict_and_handles_leap_years);
    RUN_TEST(test_parse_float_and_liters_reject_non_finite_or_malformed_values);
    return UNITY_END();
}
