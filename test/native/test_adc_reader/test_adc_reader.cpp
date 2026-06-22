#include <unity.h>

#include "app/AdcReader.h"
#include "drivers/Esp32AnalogAdcReader.h"

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

void test_esp32_adc_reader_maps_water_sensor_channels_to_adc1_pins() {
    TEST_ASSERT_EQUAL_UINT8(35, Esp32AnalogAdcReader::defaultPinForChannel(AdcChannel::A1));
    TEST_ASSERT_EQUAL_UINT8(34, Esp32AnalogAdcReader::defaultPinForChannel(AdcChannel::A2));
    TEST_ASSERT_EQUAL_UINT8(255, Esp32AnalogAdcReader::defaultPinForChannel(AdcChannel::A0));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_adc_range_full_scale_values);
    RUN_TEST(test_adc_range_steps_are_bounded);
    RUN_TEST(test_adc_raw_to_millivolts_rejects_negative_single_ended_values);
    RUN_TEST(test_adc_raw_to_millivolts_marks_full_scale_overflow);
    RUN_TEST(test_esp32_adc_reader_maps_water_sensor_channels_to_adc1_pins);
    return UNITY_END();
}
