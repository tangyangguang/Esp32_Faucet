#include <unity.h>

#include "app/AppConfig.h"

#include <cstring>

using namespace faucet;

void test_default_config_matches_product_defaults() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultMaxOutTimeSec, config.maxOutTimeSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultMaxOutVolumeMl, config.maxOutVolumeMl);
    TEST_ASSERT_EQUAL_UINT8(kDefaultOverflowPercent, config.overflowPercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultNoFlowTimeoutSec, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultHighFlowMlPerMin, config.highFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(kDefaultHighFlowDurationSec, config.highFlowDurationSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultPauseTimeoutSec, config.pauseTimeoutSec);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, kDefaultPulsePerMl, config.pulsePerMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultCalibrationTargetMl, config.calibrationTargetMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultValveFullPowerSec, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kDefaultValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultOledSleepSec, config.oledSleepSec);
    TEST_ASSERT_TRUE(config.beepEnabled);
}

void test_default_presets_use_two_enabled_volume_presets() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_TRUE(config.presets[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PresetType::Volume), static_cast<unsigned>(config.presets[0].type));
    TEST_ASSERT_EQUAL_UINT32(1500, config.presets[0].value);
    TEST_ASSERT_EQUAL_STRING("1.5L", config.presets[0].name);

    TEST_ASSERT_TRUE(config.presets[1].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PresetType::Volume), static_cast<unsigned>(config.presets[1].type));
    TEST_ASSERT_EQUAL_UINT32(7500, config.presets[1].value);
    TEST_ASSERT_EQUAL_STRING("7.5L", config.presets[1].name);

    for (std::size_t i = 2; i < kPresetCount; ++i) {
        TEST_ASSERT_FALSE(config.presets[i].enabled);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PresetType::Volume), static_cast<unsigned>(config.presets[i].type));
        TEST_ASSERT_EQUAL_UINT32(1000, config.presets[i].value);
    }
}

void test_default_filters_support_six_lightweight_records() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_TRUE(config.filters[0].enabled);
    TEST_ASSERT_EQUAL_STRING("Filter 1", config.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].usedMl);

    for (std::size_t i = 1; i < kFilterCount; ++i) {
        TEST_ASSERT_FALSE(config.filters[i].enabled);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].startTime);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].usedMl);
    }
}

void test_sanitize_config_clamps_scalar_ranges() {
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 0;
    config.maxOutTimeSec = 999999;
    config.maxOutVolumeMl = 1;
    config.overflowPercent = 99;
    config.noFlowTimeoutSec = 0;
    config.highFlowMlPerMin = 1;
    config.highFlowDurationSec = 99;
    config.pauseTimeoutSec = 999999;
    config.pulsePerMl = 999.0f;
    config.calibrationTargetMl = 1234;
    config.valveFullPowerSec = 0;
    config.valveHoldDutyPercent = 1;
    config.oledSleepSec = 999999;

    sanitizeConfig(config);

    TEST_ASSERT_EQUAL_UINT32(3, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(7200, config.maxOutTimeSec);
    TEST_ASSERT_EQUAL_UINT32(1000, config.maxOutVolumeMl);
    TEST_ASSERT_EQUAL_UINT8(50, config.overflowPercent);
    TEST_ASSERT_EQUAL_UINT32(1, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1000, config.highFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(30, config.highFlowDurationSec);
    TEST_ASSERT_EQUAL_UINT32(3600, config.pauseTimeoutSec);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, kMaxPulsePerMl, config.pulsePerMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultCalibrationTargetMl, config.calibrationTargetMl);
    TEST_ASSERT_EQUAL_UINT32(1, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kMinValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(300, config.oledSleepSec);
}

void test_sanitize_config_clamps_preset_values_by_type() {
    SystemConfig config = makeDefaultConfig();
    config.presets[0].type = PresetType::Volume;
    config.presets[0].value = 1;
    config.presets[1].type = PresetType::Volume;
    config.presets[1].value = 999999;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 1;
    config.presets[3].type = PresetType::Time;
    config.presets[3].value = 999999;
    std::memset(config.presets[4].name, 'x', sizeof(config.presets[4].name));

    sanitizeConfig(config);

    TEST_ASSERT_EQUAL_UINT32(kMinVolumePresetMl, config.presets[0].value);
    TEST_ASSERT_EQUAL_UINT32(kMaxVolumePresetMl, config.presets[1].value);
    TEST_ASSERT_EQUAL_UINT32(kMinTimePresetSec, config.presets[2].value);
    TEST_ASSERT_EQUAL_UINT32(kMaxTimePresetSec, config.presets[3].value);
    TEST_ASSERT_EQUAL_CHAR('\0', config.presets[4].name[kNameLength - 1]);
}

void test_calibration_target_and_page_size_helpers() {
    TEST_ASSERT_TRUE(isValidCalibrationTarget(500));
    TEST_ASSERT_TRUE(isValidCalibrationTarget(1000));
    TEST_ASSERT_TRUE(isValidCalibrationTarget(1500));
    TEST_ASSERT_TRUE(isValidCalibrationTarget(2000));
    TEST_ASSERT_FALSE(isValidCalibrationTarget(750));

    TEST_ASSERT_EQUAL_UINT16(kDefaultLogPageSize, sanitizeLogPageSize(0));
    TEST_ASSERT_EQUAL_UINT16(1, sanitizeLogPageSize(1));
    TEST_ASSERT_EQUAL_UINT16(kMaxLogPageSize, sanitizeLogPageSize(999));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_default_config_matches_product_defaults);
    RUN_TEST(test_default_presets_use_two_enabled_volume_presets);
    RUN_TEST(test_default_filters_support_six_lightweight_records);
    RUN_TEST(test_sanitize_config_clamps_scalar_ranges);
    RUN_TEST(test_sanitize_config_clamps_preset_values_by_type);
    RUN_TEST(test_calibration_target_and_page_size_helpers);
    return UNITY_END();
}
