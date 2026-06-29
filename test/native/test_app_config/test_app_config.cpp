#include <unity.h>

#include "app/AppConfig.h"
#include "app/CalibrationSampleStore.h"

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
    TEST_ASSERT_EQUAL_UINT32(90, config.pauseTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultVolumeAdjustStepMl, config.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultTimeAdjustStepSec, config.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultPulseMinIntervalUs, config.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(1000, config.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(100, kMinPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(100000, kMaxPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(1, kRecentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT32(kDefaultPulseObservationWindowSec, config.pulseObservationWindowSec);
    TEST_ASSERT_EQUAL_UINT32(10, config.pulseObservationWindowSec);
    TEST_ASSERT_EQUAL_UINT32(1, kMinPulseObservationWindowSec);
    TEST_ASSERT_EQUAL_UINT32(60, kMaxPulseObservationWindowSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultCalibrationAnalysisPulseMinIntervalUs, config.calibrationAnalysisPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(0, config.calibrationAnalysisPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kDefaultCalibrationStableWindowSec, config.calibrationStableWindowSec);
    TEST_ASSERT_EQUAL_UINT32(4, config.calibrationStableWindowSec);
    TEST_ASSERT_EQUAL_UINT8(kDefaultCalibrationStableTolerancePercent, config.calibrationStableTolerancePercent);
    TEST_ASSERT_EQUAL_UINT8(25, config.calibrationStableTolerancePercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultCalibrationMinVolumeSpanMl, config.calibrationMinVolumeSpanMl);
    TEST_ASSERT_EQUAL_UINT32(1000, config.calibrationMinVolumeSpanMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultCalibrationMaxErrorMl, config.calibrationMaxErrorMl);
    TEST_ASSERT_EQUAL_UINT32(100, config.calibrationMaxErrorMl);
    TEST_ASSERT_EQUAL_UINT16(kDefaultCalibrationMaxRelativeErrorTenthPercent,
                             config.calibrationMaxRelativeErrorTenthPercent);
    TEST_ASSERT_EQUAL_UINT16(50, config.calibrationMaxRelativeErrorTenthPercent);
    TEST_ASSERT_EQUAL_UINT32(500, kPulseTraceBucketMs);
    TEST_ASSERT_EQUAL_UINT32(15000, kPulseTraceStartupDetailMs);
    TEST_ASSERT_EQUAL_size_t(1200, kPulseTraceMaxBucketsPerTrace);
    TEST_ASSERT_EQUAL_size_t(4096, kPulseTraceMaxStartupEdgesPerTrace);
    TEST_ASSERT_EQUAL_UINT32(kDefaultValveFullPowerSec, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT32(5, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kDefaultValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultDisplaySleepSec, config.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(60, config.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultResultDisplaySec, config.resultDisplaySec);
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

void test_sensor_config_defaults_disabled() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_EQUAL_UINT16(3300, config.sensorVrefMv);
    TEST_ASSERT_FALSE(config.temperatureEnabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TemperatureKind::None),
                            static_cast<std::uint8_t>(config.temperatureKind));
    TEST_ASSERT_EQUAL_INT16(0, config.temperatureOffsetCentiC);
    TEST_ASSERT_FALSE(config.temperatureCalibrated);
    TEST_ASSERT_FALSE(config.tdsEnabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsKind::None), static_cast<std::uint8_t>(config.tdsKind));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::None),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsCalibrationRevision);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, config.tdsScale);
    TEST_ASSERT_EQUAL_INT16(0, config.tdsOffsetPpm);
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsLowReferencePpm);
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsLowRawPpm);
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsHighReferencePpm);
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsHighRawPpm);
    TEST_ASSERT_EQUAL_UINT32(0, config.tdsCalibrationTime);
    TEST_ASSERT_EQUAL_INT16(0, config.tdsCalibrationTemperatureCentiC);
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsCalibrationVoltageMv);
    TEST_ASSERT_FALSE(config.tdsCalibrated);
    TEST_ASSERT_TRUE(config.tdsTemperatureCompensationEnabled);
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
    config.pulseMinIntervalUs = 1;
    config.pulseObservationWindowSec = 0;
    config.calibrationAnalysisPulseMinIntervalUs = 1;
    config.calibrationStableWindowSec = 1;
    config.calibrationStableTolerancePercent = 1;
    config.calibrationMinVolumeSpanMl = 1;
    config.calibrationMaxErrorMl = 1;
    config.calibrationMaxRelativeErrorTenthPercent = 1;
    config.valveFullPowerSec = 0;
    config.valveHoldDutyPercent = 1;
    config.displaySleepSec = 999999;
    config.resultDisplaySec = 999999;

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
    TEST_ASSERT_EQUAL_UINT32(kMinPulseMinIntervalUs, config.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kMinPulseObservationWindowSec, config.pulseObservationWindowSec);
    TEST_ASSERT_EQUAL_UINT32(kMinPulseMinIntervalUs, config.calibrationAnalysisPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kMinCalibrationStableWindowSec, config.calibrationStableWindowSec);
    TEST_ASSERT_EQUAL_UINT8(kMinCalibrationStableTolerancePercent, config.calibrationStableTolerancePercent);
    TEST_ASSERT_EQUAL_UINT32(kMinCalibrationMinVolumeSpanMl, config.calibrationMinVolumeSpanMl);
    TEST_ASSERT_EQUAL_UINT32(kMinCalibrationMaxErrorMl, config.calibrationMaxErrorMl);
    TEST_ASSERT_EQUAL_UINT16(kMinCalibrationMaxRelativeErrorTenthPercent,
                             config.calibrationMaxRelativeErrorTenthPercent);
    TEST_ASSERT_EQUAL_UINT32(1, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kMinValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(300, config.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(60, config.resultDisplaySec);

    config = makeDefaultConfig();
    config.displaySleepSec = 5;
    sanitizeConfig(config);
    TEST_ASSERT_EQUAL_UINT32(kDefaultDisplaySleepSec, config.displaySleepSec);

    config = makeDefaultConfig();
    config.pulseMinIntervalUs = 999999;
    sanitizeConfig(config);
    TEST_ASSERT_EQUAL_UINT32(kMaxPulseMinIntervalUs, config.pulseMinIntervalUs);

    config = makeDefaultConfig();
    config.pulseObservationWindowSec = 999999;
    sanitizeConfig(config);
    TEST_ASSERT_EQUAL_UINT32(kMaxPulseObservationWindowSec, config.pulseObservationWindowSec);

    config = makeDefaultConfig();
    config.calibrationAnalysisPulseMinIntervalUs = 0;
    sanitizeConfig(config);
    TEST_ASSERT_EQUAL_UINT32(0, config.calibrationAnalysisPulseMinIntervalUs);

    config = makeDefaultConfig();
    config.calibrationAnalysisPulseMinIntervalUs = 999999;
    config.calibrationStableWindowSec = 999999;
    config.calibrationStableTolerancePercent = 99;
    config.calibrationMinVolumeSpanMl = 999999;
    config.calibrationMaxErrorMl = 999999;
    config.calibrationMaxRelativeErrorTenthPercent = 9999;
    sanitizeConfig(config);
    TEST_ASSERT_EQUAL_UINT32(kMaxPulseMinIntervalUs, config.calibrationAnalysisPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kMaxCalibrationStableWindowSec, config.calibrationStableWindowSec);
    TEST_ASSERT_EQUAL_UINT8(kMaxCalibrationStableTolerancePercent, config.calibrationStableTolerancePercent);
    TEST_ASSERT_EQUAL_UINT32(kMaxCalibrationMinVolumeSpanMl, config.calibrationMinVolumeSpanMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxCalibrationMaxErrorMl, config.calibrationMaxErrorMl);
    TEST_ASSERT_EQUAL_UINT16(kMaxCalibrationMaxRelativeErrorTenthPercent,
                             config.calibrationMaxRelativeErrorTenthPercent);
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
    TEST_ASSERT_EQUAL_UINT16(10, kDefaultRecordPageSize);
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

void test_calibration_trace_storage_limits_match_small_session_scope() {
    TEST_ASSERT_EQUAL_size_t(6, kCalibrationSessionTraceSlots);
    TEST_ASSERT_EQUAL_UINT32(500, kPulseTraceBucketMs);
    TEST_ASSERT_EQUAL_UINT32(15000, kPulseTraceStartupDetailMs);
    TEST_ASSERT_EQUAL_size_t(1200, kPulseTraceMaxBucketsPerTrace);
    TEST_ASSERT_EQUAL_size_t(4096, kPulseTraceMaxStartupEdgesPerTrace);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_default_config_matches_product_defaults);
    RUN_TEST(test_default_presets_use_two_enabled_volume_presets);
    RUN_TEST(test_default_filters_support_six_lightweight_records);
    RUN_TEST(test_sensor_config_defaults_disabled);
    RUN_TEST(test_sanitize_config_clamps_scalar_ranges);
    RUN_TEST(test_sanitize_config_clamps_preset_values_by_type);
    RUN_TEST(test_record_page_size_and_filter_life_helpers);
    RUN_TEST(test_calibration_trace_storage_limits_match_small_session_scope);
    return UNITY_END();
}
