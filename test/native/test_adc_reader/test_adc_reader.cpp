#include <unity.h>

#include "app/AdcReader.h"
#include "drivers/Ads1115AdcReader.h"

using namespace faucet;

void test_adc_range_full_scale_values() {
    TEST_ASSERT_EQUAL_UINT16(256, adcRangeFullScaleMv(AdcRange::P256));
    TEST_ASSERT_EQUAL_UINT16(512, adcRangeFullScaleMv(AdcRange::P512));
    TEST_ASSERT_EQUAL_UINT16(2048, adcRangeFullScaleMv(AdcRange::P2048));
    TEST_ASSERT_EQUAL_UINT16(4096, adcRangeFullScaleMv(AdcRange::P4096));
}

void test_adc_range_steps_are_bounded() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P512),
                            static_cast<std::uint8_t>(nextLargerRange(AdcRange::P256)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P2048),
                            static_cast<std::uint8_t>(nextLargerRange(AdcRange::P512)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P4096),
                            static_cast<std::uint8_t>(nextLargerRange(AdcRange::P2048)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P4096),
                            static_cast<std::uint8_t>(nextLargerRange(AdcRange::P4096)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P2048),
                            static_cast<std::uint8_t>(nextSmallerRange(AdcRange::P4096)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P512),
                            static_cast<std::uint8_t>(nextSmallerRange(AdcRange::P2048)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P256),
                            static_cast<std::uint8_t>(nextSmallerRange(AdcRange::P512)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P256),
                            static_cast<std::uint8_t>(nextSmallerRange(AdcRange::P256)));
}

void test_adc_raw_to_millivolts_rejects_negative_single_ended_values() {
    AdcReadResult negative = adcRawToMillivolts(-1, AdcRange::P2048);
    TEST_ASSERT_FALSE(negative.ok);
    TEST_ASSERT_FALSE(negative.overflow);

    AdcReadResult positive = adcRawToMillivolts(16000, AdcRange::P2048);
    TEST_ASSERT_TRUE(positive.ok);
    TEST_ASSERT_FALSE(positive.overflow);
    TEST_ASSERT_EQUAL_INT16(1000, positive.millivolts);
}

void test_adc_raw_to_millivolts_marks_full_scale_overflow() {
    AdcReadResult result = adcRawToMillivolts(32767, AdcRange::P256);

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(result.overflow);
    TEST_ASSERT_EQUAL_INT16(255, result.millivolts);
}

void test_ads1115_channel_and_range_register_mapping() {
    TEST_ASSERT_EQUAL_HEX8(0x04, Ads1115AdcReader::muxBits(AdcChannel::A0));
    TEST_ASSERT_EQUAL_HEX8(0x05, Ads1115AdcReader::muxBits(AdcChannel::A1));
    TEST_ASSERT_EQUAL_HEX8(0x06, Ads1115AdcReader::muxBits(AdcChannel::A2));
    TEST_ASSERT_EQUAL_HEX8(0x07, Ads1115AdcReader::muxBits(AdcChannel::A3));
    TEST_ASSERT_EQUAL_HEX8(0x01, Ads1115AdcReader::pgaBits(AdcRange::P4096));
    TEST_ASSERT_EQUAL_HEX8(0x02, Ads1115AdcReader::pgaBits(AdcRange::P2048));
    TEST_ASSERT_EQUAL_HEX8(0x04, Ads1115AdcReader::pgaBits(AdcRange::P512));
    TEST_ASSERT_EQUAL_HEX8(0x05, Ads1115AdcReader::pgaBits(AdcRange::P256));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_adc_range_full_scale_values);
    RUN_TEST(test_adc_range_steps_are_bounded);
    RUN_TEST(test_adc_raw_to_millivolts_rejects_negative_single_ended_values);
    RUN_TEST(test_adc_raw_to_millivolts_marks_full_scale_overflow);
    RUN_TEST(test_ads1115_channel_and_range_register_mapping);
    return UNITY_END();
}
