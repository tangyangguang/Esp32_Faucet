#include <unity.h>

#include "app/AppConfig.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace faucet;

void test_default_config_matches_product_defaults() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultMaxOutTimeSec, config.maxOutTimeSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultMaxOutVolumeMl, config.maxOutVolumeMl);
    TEST_ASSERT_EQUAL_UINT8(kDefaultOverflowPercent, config.overflowPercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultNoFlowTimeoutSec, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(10, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultHighFlowMlPerMin, config.highFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(kDefaultHighFlowDurationSec, config.highFlowDurationSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultPauseTimeoutSec, config.pauseTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultVolumeAdjustStepMl, config.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultTimeAdjustStepSec, config.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultStartupCompensationMl, config.startupCompensationMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultPulseTraceMemoryKb, config.pulseTraceMemoryKb);
    TEST_ASSERT_EQUAL_UINT32(0, config.overallPulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(0, config.startupDurationSec);
    TEST_ASSERT_EQUAL_UINT32(0, config.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, config.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(0, config.startupPulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(0, config.stablePulsePerLiter);
    TEST_ASSERT_FALSE(config.segmentedMeteringCalibrated);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, kDefaultPulsePerMl, config.pulsePerMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultValveFullPowerSec, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kDefaultValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultDisplaySleepSec, config.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultResultDisplaySec, config.resultDisplaySec);
    TEST_ASSERT_EQUAL_UINT8(kDefaultLcdI2cAddress, config.lcdI2cAddress);
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
        TEST_ASSERT_EQUAL_STRING("预设", config.presets[i].name);
    }
}

void test_default_filters_support_six_lightweight_records() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_TRUE(config.filters[0].enabled);
    TEST_ASSERT_EQUAL_STRING("第1级滤芯", config.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(180, config.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(180, config.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].lifeMl);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].usedMl);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].startBootId);

    for (std::size_t i = 1; i < kFilterCount; ++i) {
        char name[kFilterNameLength]{};
        std::snprintf(name, sizeof(name), "第%u级滤芯", static_cast<unsigned>(i + 1));
        TEST_ASSERT_FALSE(config.filters[i].enabled);
        TEST_ASSERT_EQUAL_STRING(name, config.filters[i].name);
        TEST_ASSERT_EQUAL_UINT32(180, config.filters[i].recommendDays);
        TEST_ASSERT_EQUAL_UINT32(180, config.filters[i].maxDays);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].lifeMl);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].startTime);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].usedMl);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].startBootId);
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
    config.volumeAdjustStepMl = 0;
    config.timeAdjustStepSec = 0;
    config.startupCompensationMl = 999999;
    config.pulseTraceMemoryKb = 999999;
    config.overallPulsePerLiter = 999999;
    config.startupDurationSec = 999999;
    config.startupPulseCount = 999999;
    config.startupVolumeMl = 999999;
    config.startupPulsePerLiter = 999999;
    config.stablePulsePerLiter = 999999;
    config.segmentedMeteringCalibrated = true;
    config.pulsePerMl = 999.0f;
    config.valveFullPowerSec = 0;
    config.valveHoldDutyPercent = 1;
    config.displaySleepSec = 999999;
    config.resultDisplaySec = 999999;
    config.lcdI2cAddress = 0;

    sanitizeConfig(config);

    TEST_ASSERT_EQUAL_UINT32(3, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(7200, config.maxOutTimeSec);
    TEST_ASSERT_EQUAL_UINT32(1000, config.maxOutVolumeMl);
    TEST_ASSERT_EQUAL_UINT8(50, config.overflowPercent);
    TEST_ASSERT_EQUAL_UINT32(1, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1000, config.highFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(30, config.highFlowDurationSec);
    TEST_ASSERT_EQUAL_UINT32(3600, config.pauseTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(10, config.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(1, config.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kMaxStartupCompensationMl, config.startupCompensationMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxPulseTraceMemoryKb, config.pulseTraceMemoryKb);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedPulsePerLiter, config.overallPulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedStartupDurationSec, config.startupDurationSec);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedStartupPulseCount, config.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedStartupVolumeMl, config.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedPulsePerLiter, config.startupPulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedPulsePerLiter, config.stablePulsePerLiter);
    TEST_ASSERT_TRUE(config.segmentedMeteringCalibrated);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, kMaxPulsePerMl, config.pulsePerMl);
    TEST_ASSERT_EQUAL_UINT32(1, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kMinValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(300, config.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(60, config.resultDisplaySec);
    TEST_ASSERT_EQUAL_UINT8(0x03, config.lcdI2cAddress);
}

void test_sanitize_config_replaces_non_finite_pulse_factor() {
    SystemConfig config = makeDefaultConfig();
    config.pulsePerMl = NAN;
    sanitizeConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, kDefaultPulsePerMl, config.pulsePerMl);

    config.pulsePerMl = INFINITY;
    sanitizeConfig(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, kDefaultPulsePerMl, config.pulsePerMl);
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
    std::memset(config.filters[0].name, 'y', sizeof(config.filters[0].name));
    config.filters[0].recommendDays = 999999;
    config.filters[0].maxDays = 1;
    config.filters[0].lifeMl = 99999999;

    sanitizeConfig(config);

    TEST_ASSERT_EQUAL_UINT32(kMinVolumePresetMl, config.presets[0].value);
    TEST_ASSERT_EQUAL_UINT32(kMaxVolumePresetMl, config.presets[1].value);
    TEST_ASSERT_EQUAL_UINT32(kMinTimePresetSec, config.presets[2].value);
    TEST_ASSERT_EQUAL_UINT32(kMaxTimePresetSec, config.presets[3].value);
    TEST_ASSERT_EQUAL_CHAR('\0', config.presets[4].name[kPresetNameLength - 1]);
    TEST_ASSERT_EQUAL_CHAR('\0', config.filters[0].name[kFilterNameLength - 1]);
    TEST_ASSERT_EQUAL_UINT32(kMaxFilterLifeDays, config.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(kMaxFilterLifeDays, config.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(kMaxFilterLifeMl, config.filters[0].lifeMl);
}

void test_record_page_size_and_filter_life_helpers() {
    TEST_ASSERT_EQUAL_UINT16(kDefaultRecordPageSize, sanitizeRecordPageSize(0));
    TEST_ASSERT_EQUAL_UINT16(20, sanitizeRecordPageSize(20));
    TEST_ASSERT_EQUAL_UINT16(30, sanitizeRecordPageSize(30));
    TEST_ASSERT_EQUAL_UINT16(1, sanitizeRecordPageSize(1));
    TEST_ASSERT_EQUAL_UINT16(kMaxRecordPageSize, sanitizeRecordPageSize(999));

    FilterRecord filter = makeDefaultConfig().filters[0];
    filter.enabled = true;
    filter.recommendDays = 90;
    filter.maxDays = 180;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::Normal),
                            static_cast<unsigned>(filterLifeStatus(filter, 89)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::RecommendReplace),
                            static_cast<unsigned>(filterLifeStatus(filter, 90)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::Expired),
                            static_cast<unsigned>(filterLifeStatus(filter, 180)));
    filter.usedMl = 2000;
    filter.lifeMl = 2000;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::Expired),
                            static_cast<unsigned>(filterLifeStatus(filter, 1)));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_default_config_matches_product_defaults);
    RUN_TEST(test_default_presets_use_two_enabled_volume_presets);
    RUN_TEST(test_default_filters_support_six_lightweight_records);
    RUN_TEST(test_sanitize_config_clamps_scalar_ranges);
    RUN_TEST(test_sanitize_config_replaces_non_finite_pulse_factor);
    RUN_TEST(test_sanitize_config_clamps_preset_values_by_type);
    RUN_TEST(test_record_page_size_and_filter_life_helpers);
    return UNITY_END();
}
