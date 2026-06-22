#include <unity.h>

#include "app/AppConfig.h"
#include "app/AppController.h"
#include "web/FaucetWebJson.h"

#include <cstring>

using namespace faucet;

namespace {

AppSnapshot makeSnapshot() {
    AppSnapshot snapshot{
        WaterSnapshot{WaterState::Running, 1, 0, true, 250, 42, WaterResult::Completed, WaterMode::Volume, 1500},
        ValveOutput{ValveState::OpeningFullPower, true, 100},
        StatisticsRecord{1000, 2000, 3000, 4000, 20260506, 202619, 202605},
    };
    snapshot.flowDroppedPulses = 7;
    snapshot.maxLoopIntervalUs = 82300;
    snapshot.maxAppTickUs = 1400;
    snapshot.maxBaseHandleUs = 76800;
    snapshot.currentFlowMlPerMin = 1870;
    snapshot.instantFlowMlPerMin = 1910;
    snapshot.windowFlowMlPerMin = 1840;
    snapshot.displayFlowMlPerMin = 1870;
    snapshot.runAverageFlowMlPerMin = 1660;
    snapshot.recentAverageFlowMlPerMin = 1810;
    snapshot.meteringParams = MeteringParameters{8, 130, 248};
    snapshot.temperatureSensorEnabled = true;
    snapshot.tdsSensorEnabled = true;
    snapshot.sensors.inputVoltageMv = SensorValue{true, 12100};
    snapshot.sensors.temperatureCentiC = SensorValue{true, 2530};
    snapshot.sensors.tdsPpm = SensorValue{true, 8};
    snapshot.sensors.tdsVoltageMv = SensorValue{true, 19};
    snapshot.sensors.tdsCalibrated = true;
    snapshot.sensors.tdsTemperatureCompensated = true;
    return snapshot;
}

}  // namespace

void test_status_json_contains_no_remote_control_capability() {
    char json[4096]{};

    TEST_ASSERT_TRUE(writeStatusJson(makeSnapshot(), json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"state\":\"running\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveOpen\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"volumeMl\":250"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"elapsedSec\":42"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetValue\":1500"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"lastResult\":\"completed\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"mode\":\"volume\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"selectedPreset\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"activePreset\":0"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulsePerLiter\":0"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"metering\":{\"startupPulseCount\":8,\"startupVolumeMl\":130,\"stablePulsePerLiter\":248,\"startupDurationMs\":5000,\"stableFlowMlPerMin\":1950}"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"calibration\":{\"status\":\"idle\",\"attemptCount\":0,\"validSampleCount\":0"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"canQuickGenerate\":false,\"recommended\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetEstimate\":{\"available\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetMl\":1500"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulseCount\":348"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"fullRunPulsePerLiter\":232"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"currentFlowMlPerMin\":1870"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"instantFlowMlPerMin\":1910"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"windowFlowMlPerMin\":1840"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"displayFlowMlPerMin\":1870"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"runAverageFlowMlPerMin\":1660"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"recentAverageFlowMlPerMin\":1810"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"flowDroppedPulses\":7"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"maxLoopIntervalUs\":82300"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"maxAppTickUs\":1400"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"maxBaseHandleUs\":76800"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveDutyPercent\":100"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveFullPowerSec\":5"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveHoldDutyPercent\":70"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"screenOn\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"waterControl\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"sensor\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"inputVoltageMv\":12100"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"temperature\":{\"enabled\":true,\"currentCentiC\":2530,\"calibrated\":false}"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"tds\":{\"enabled\":true,\"currentPpm\":8,\"voltageMv\":19,\"calibrated\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"temperatureCompensated\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"flags\":0"));
    TEST_ASSERT_NULL(std::strstr(json, "startWater"));
    TEST_ASSERT_NULL(std::strstr(json, "stop"));
}

void test_status_json_can_report_screen_state() {
    char json[4096]{};

    TEST_ASSERT_TRUE(writeStatusJson(makeSnapshot(), true, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"screenOn\":true"));
}

void test_status_json_uses_configured_valve_pwm_values() {
    char json[4096]{};
    SystemConfig config = makeDefaultConfig();
    config.valveFullPowerSec = 5;
    config.valveHoldDutyPercent = 45;
    AppSnapshot snapshot = makeSnapshot();
    snapshot.valve = ValveOutput{ValveState::Holding, true, 45};

    TEST_ASSERT_TRUE(writeStatusJson(snapshot, true, config, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveDutyPercent\":45"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveFullPowerSec\":5"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveHoldDutyPercent\":45"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"waterControl\":false"));
}

void test_status_json_can_include_config_runtime_status() {
    char json[4096]{};
    SystemConfig config = makeDefaultConfig();
    const ConfigRuntimeStatus status{"loaded_current", 19, 19};

    TEST_ASSERT_TRUE(writeStatusJson(makeSnapshot(), true, config, &status, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"config\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"status\":\"loaded_current\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"rawVersion\":19"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"currentVersion\":19"));
    TEST_ASSERT_NULL(std::strstr(json, "readOnly"));
    TEST_ASSERT_NULL(std::strstr(json, "password"));
}

void test_status_json_contains_next_preset_summary() {
    char json[4096]{};
    SystemConfig config = makeDefaultConfig();
    AppSnapshot snapshot = makeSnapshot();
    snapshot.water.selectedPreset = 1;
    snapshot.water.activePreset = 0;

    TEST_ASSERT_TRUE(writeStatusJson(snapshot, true, config, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"nextPreset\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"index\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"displayNumber\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"enabledOrdinal\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"enabledCount\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"name\":\"7.5L\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetValue\":7500"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetEstimate\":{\"available\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulseCount\":1836"));
}

void test_status_json_reports_time_target_estimate_from_stable_pulses() {
    char json[4096]{};
    AppSnapshot snapshot = makeSnapshot();
    snapshot.water.mode = WaterMode::Time;
    snapshot.water.targetValue = 249;
    snapshot.targetEstimatedVolumeMl = 1804;
    snapshot.targetEstimatedPulseCount = 406;
    snapshot.targetStablePulsePerSec = 1.63f;

    TEST_ASSERT_TRUE(writeStatusJson(snapshot, true, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetEstimate\":{\"available\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetMl\":1804"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulseCount\":406"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"stablePulsePerSec\":1.63"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"reason\":\"\""));
}

void test_status_json_reports_missing_time_estimate_reason() {
    char json[4096]{};
    AppSnapshot snapshot = makeSnapshot();
    snapshot.water.mode = WaterMode::Time;
    snapshot.water.targetValue = 249;
    snapshot.targetEstimateReason = "计量参数未就绪";

    TEST_ASSERT_TRUE(writeStatusJson(snapshot, true, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetEstimate\":{\"available\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"reason\":\"计量参数未就绪\""));
}

void test_status_json_reports_time_next_preset_estimate() {
    char json[4096]{};
    SystemConfig config = makeDefaultConfig();
    config.presets[1].type = PresetType::Time;
    config.presets[1].value = 249;
    AppSnapshot snapshot = makeSnapshot();
    snapshot.water.selectedPreset = 1;
    snapshot.selectedPresetEstimatedVolumeMl = 1804;
    snapshot.selectedPresetEstimatedPulseCount = 406;
    snapshot.selectedPresetStablePulsePerSec = 1.63f;

    TEST_ASSERT_TRUE(writeStatusJson(snapshot, true, config, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"nextPreset\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"mode\":\"time\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetEstimate\":{\"available\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetMl\":1804"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulseCount\":406"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"stablePulsePerSec\":1.63"));
}

void test_stats_json_contains_all_periods() {
    char json[256]{};
    const StatisticsRecord stats{1, 2, 3, 4000000000UL, 20260506, 202619, 202605};

    TEST_ASSERT_TRUE(writeStatsJson(stats, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"todayMl\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"weekMl\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"monthMl\":3"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"totalMl\":4000000000"));
}

void test_usage_summary_json_contains_aggregated_series() {
    WaterUsageSummary summary{};
    summary.todayMl = 12000;
    summary.monthMl = 18000;
    summary.last30DaysMl = 18000;
    summary.last30DaysDailyAverageMl = 600;
    summary.unknownCount = 2;
    summary.todayDay = 9630;
    summary.monthStartDay = 9615;
    summary.sensorRecordCount = 2;
    summary.uncalibratedSensorRecordCount = 1;
    summary.dayCount = 2;
    summary.days[0] = DailyUsageBucket{9629, 6000, 30, 3};
    summary.days[1] = DailyUsageBucket{9630, 12000, 60, 4};
    summary.days[1].temperatureAvgCentiC = 2530;
    summary.days[1].tdsAvgPpm = 8;
    summary.days[1].sensorRecordCount = 2;
    summary.presetCounts[1] = CountVolumeBucket{12000, 4};
    summary.hourBuckets[7] = CountVolumeBucket{6000, 3};
    summary.resultCounts[static_cast<std::size_t>(WaterResult::FlowError)] = 1;
    summary.volumeHist[4] = CountVolumeBucket{18000, 7};

    char json[4096]{};
    TEST_ASSERT_TRUE(writeUsageSummaryJson(summary, 22530, json, sizeof(json)));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"todayMl\":12000"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"monthMl\":18000"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"totalMl\":22530"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"last30DaysDailyAverageMl\":600"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"dailySeries\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"day\":9630"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"temperatureAvgCentiC\":2530"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"tdsAvgPpm\":8"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"sensorRecordCount\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"uncalibratedSensorRecordCount\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"presetCounts\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"hour\":7"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"resultCounts\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"volumeHist\""));
}

void test_usage_summary_json_handles_full_daily_sensor_series() {
    WaterUsageSummary summary{};
    summary.todayMl = 31000;
    summary.monthMl = 465000;
    summary.last30DaysMl = 465000;
    summary.last30DaysDailyAverageMl = 15500;
    summary.todayDay = 9630;
    summary.monthStartDay = 9601;
    summary.sensorRecordCount = 30;
    summary.uncalibratedSensorRecordCount = 4;
    summary.invalidSensorRecordCount = 1;
    summary.dayCount = kUsageSummaryMaxDays;
    for (std::size_t i = 0; i < kUsageSummaryMaxDays; ++i) {
        DailyUsageBucket& day = summary.days[i];
        day.dayIndex = 9601 + static_cast<std::uint32_t>(i);
        day.volumeMl = 1000 + static_cast<std::uint32_t>(i * 1000);
        day.durationSec = 60 + static_cast<std::uint32_t>(i);
        day.count = static_cast<std::uint16_t>(i + 1);
        day.temperatureAvgCentiC = static_cast<std::int16_t>(1800 + i * 10);
        day.temperatureMinCentiC = static_cast<std::int16_t>(1700 + i * 10);
        day.temperatureMaxCentiC = static_cast<std::int16_t>(1900 + i * 10);
        day.tdsAvgPpm = static_cast<std::uint16_t>(5 + i);
        day.tdsMinPpm = static_cast<std::uint16_t>(4 + i);
        day.tdsMaxPpm = static_cast<std::uint16_t>(6 + i);
        day.sensorRecordCount = 1;
    }

    char tooSmall[8192]{};
    TEST_ASSERT_FALSE(writeUsageSummaryJson(summary, 465000, tooSmall, sizeof(tooSmall)));

    char json[32768]{};
    TEST_ASSERT_TRUE(writeUsageSummaryJson(summary, 465000, json, sizeof(json)));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"day\":9630"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"tdsMaxPpm\":35"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"invalidSensorRecordCount\":1"));
}

void test_presets_json_escapes_names_and_lists_nine_presets() {
    char json[1400]{};
    SystemConfig config = makeDefaultConfig();
    std::strncpy(config.presets[2].name, "A\"B\nC\t", sizeof(config.presets[2].name) - 1);

    TEST_ASSERT_TRUE(writePresetsJson(config.presets, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"presets\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"index\":8"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "A\\\"B"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\\nC\\t"));
    TEST_ASSERT_NULL(std::strchr(json, '\n'));
}

void test_filters_json_contains_runtime_fields() {
    char json[1024]{};
    SystemConfig config = makeDefaultConfig();
    config.filters[0].recommendDays = 180;
    config.filters[0].maxDays = 365;
    config.filters[0].lifeMl = 2000000;
    config.filters[0].startTime = 1714502400;
    config.filters[0].usedMl = 123456;

    TEST_ASSERT_TRUE(writeFiltersJson(config.filters, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"filters\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"index\":5"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"recommendDays\":180"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"maxDays\":365"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"lifeMl\":2000000"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"startTime\":1714502400"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"usedMl\":123456"));
}

void test_water_records_json_is_paged_and_read_only() {
    WaterRecord records[2]{
        {100, 1500, 1500, 675, 2, 30, WaterMode::Volume, WaterResult::Completed, 0, 0, 7, {0, 0, 0, 0}},
        {200, 300, 60, 135, 1, 10, WaterMode::Time, WaterResult::StoppedByUser, 1, 0, 8, {0, 0, 0, 0}},
    };
    records[0].temperatureAvgCentiC = 2530;
    records[0].tdsAvgPpm = 8;
    records[0].sensorSampleCount = 12;
    records[0].tdsCalibratedAtRun = 1;
    char json[2048]{};

    TEST_ASSERT_TRUE(writeWaterRecordsJson(records, 2, 1, 50, 60, "file", "ready", json, sizeof(json)));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"storage\":\"file\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"storageStatus\":\"ready\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"records\""));
    TEST_ASSERT_NULL(std::strstr(json, "\"logs\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"page\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"total\":60"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"mode\":\"volume\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"result\":\"stoppedByUser\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetValue\":60"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulseCount\":675"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"rejectedPulseCount\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"meteringSchemeId\":7"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"meteringSchemeId\":8"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"averageFlowMlPerMin\":3000"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"averageFlowMlPerMin\":1800"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"temperatureAvgCentiC\":2530"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"tdsAvgPpm\":8"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"sensorSampleCount\":12"));
    TEST_ASSERT_NULL(std::strstr(json, "\"stablePulsePerLiterAtRun\""));
    TEST_ASSERT_NULL(std::strstr(json, "\"pulsePerMlAtRun\""));
    TEST_ASSERT_NULL(std::strstr(json, "startWater"));
}

void test_json_writer_reports_small_buffers() {
    char json[8]{};
    TEST_ASSERT_FALSE(writeStatusJson(makeSnapshot(), json, sizeof(json)));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_status_json_contains_no_remote_control_capability);
    RUN_TEST(test_status_json_can_report_screen_state);
    RUN_TEST(test_status_json_uses_configured_valve_pwm_values);
    RUN_TEST(test_status_json_can_include_config_runtime_status);
    RUN_TEST(test_status_json_contains_next_preset_summary);
    RUN_TEST(test_status_json_reports_time_target_estimate_from_stable_pulses);
    RUN_TEST(test_status_json_reports_missing_time_estimate_reason);
    RUN_TEST(test_status_json_reports_time_next_preset_estimate);
    RUN_TEST(test_stats_json_contains_all_periods);
    RUN_TEST(test_usage_summary_json_contains_aggregated_series);
    RUN_TEST(test_usage_summary_json_handles_full_daily_sensor_series);
    RUN_TEST(test_presets_json_escapes_names_and_lists_nine_presets);
    RUN_TEST(test_filters_json_contains_runtime_fields);
    RUN_TEST(test_water_records_json_is_paged_and_read_only);
    RUN_TEST(test_json_writer_reports_small_buffers);
    return UNITY_END();
}
