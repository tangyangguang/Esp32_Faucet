#include <unity.h>

#include "app/AppConfig.h"
#include "app/AppController.h"
#include "web/FaucetWebJson.h"

#include <cstring>

using namespace faucet;

namespace {

AppSnapshot makeSnapshot() {
    AppSnapshot snapshot{
        WaterSnapshot{WaterState::Running, 1, true, 250, 0, WaterResult::Completed, WaterMode::Volume, 1500},
        ValveOutput{ValveState::OpeningFullPower, true, 100},
        StatisticsRecord{1000, 2000, 3000, 4000, 20260506, 202619, 202605},
    };
    snapshot.flowDroppedPulses = 7;
    return snapshot;
}

}  // namespace

void test_status_json_contains_no_remote_control_capability() {
    char json[256]{};

    TEST_ASSERT_TRUE(writeStatusJson(makeSnapshot(), json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"state\":\"running\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveOpen\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"volumeMl\":250"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"flowDroppedPulses\":7"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"waterControl\":false"));
    TEST_ASSERT_NULL(std::strstr(json, "start"));
    TEST_ASSERT_NULL(std::strstr(json, "stop"));
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

void test_config_json_contains_safety_and_display_settings() {
    char json[512]{};
    SystemConfig config = makeDefaultConfig();
    config.beepEnabled = false;

    TEST_ASSERT_TRUE(writeConfigJson(config, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"confirmTimeoutSec\":10"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"maxOutVolumeMl\":30000"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"valveHoldDutyPercent\":30"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"beepEnabled\":false"));
}

void test_presets_json_escapes_names_and_lists_nine_presets() {
    char json[1400]{};
    SystemConfig config = makeDefaultConfig();
    std::strncpy(config.presets[2].name, "A\"B", sizeof(config.presets[2].name) - 1);

    TEST_ASSERT_TRUE(writePresetsJson(config.presets, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"presets\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"index\":8"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "A\\\"B"));
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

void test_calibration_json_forbids_remote_calibration_start() {
    char json[256]{};
    SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_TRUE(writeCalibrationJson(config, json, sizeof(json)));

    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulsePerMl\":0.450"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"pulsePerLiter\":450.0"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"targetsMl\":[1500,7500,0,0]"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"webCanStartCalibration\":false"));
}

void test_water_logs_json_is_paged_and_read_only() {
    WaterLogRecord records[2]{
        {100, 1500, 30, WaterMode::Volume, WaterResult::Completed, {0, 0}},
        {200, 300, 10, WaterMode::Time, WaterResult::StoppedByUser, {0, 0}},
    };
    char json[512]{};

    TEST_ASSERT_TRUE(writeWaterLogsJson(records, 2, 1, 50, 60, "file", json, sizeof(json)));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"storage\":\"file\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"page\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"total\":60"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"mode\":\"volume\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"result\":\"stoppedByUser\""));
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
    RUN_TEST(test_stats_json_contains_all_periods);
    RUN_TEST(test_config_json_contains_safety_and_display_settings);
    RUN_TEST(test_presets_json_escapes_names_and_lists_nine_presets);
    RUN_TEST(test_filters_json_contains_runtime_fields);
    RUN_TEST(test_calibration_json_forbids_remote_calibration_start);
    RUN_TEST(test_water_logs_json_is_paged_and_read_only);
    RUN_TEST(test_json_writer_reports_small_buffers);
    return UNITY_END();
}
