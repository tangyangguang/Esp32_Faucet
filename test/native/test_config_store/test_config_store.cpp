#include <unity.h>

#include "app/ConfigStore.h"
#include "../support/FakeConfigBackend.h"

#include <cstring>

using namespace faucet;
using faucet_test::FakeConfigBackend;

void test_config_load_uses_defaults_without_matching_version() {
    FakeConfigBackend backend;
    ConfigStore store(backend);

    const SystemConfig config = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1500, config.presets[0].value);
    TEST_ASSERT_TRUE(config.filters[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::Defaults),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
}

void test_non_current_system_config_version_uses_defaults_until_explicit_save() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 1);
    backend.setInt("faucet_cfg", "confirm_s", 22);
    backend.setInt("faucet_cfg", "pulse_m", 620);
    backend.setStr("faucet_cfg", "f0_name", "CTO");
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::Defaults),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_EQUAL_INT32(1, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_INT32(620, backend.getInt("faucet_cfg", "pulse_m", 0));
    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.presets[0].value);
    char text[16]{};
    TEST_ASSERT_TRUE(backend.getStr("faucet_cfg", "f0_name", text, sizeof(text), ""));
    TEST_ASSERT_EQUAL_STRING("CTO", text);
    TEST_ASSERT_TRUE(store.saveSystemConfig(loaded));
    TEST_ASSERT_EQUAL_INT32(store.currentSystemConfigVersion(), backend.getInt("faucet_cfg", "ver", 0));
    const SystemConfig current = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, current.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::LoadedCurrent),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
}

void test_explicit_app_config_save_merges_fields_and_establishes_current_version() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "valve_s", 2);
    backend.setInt("faucet_cfg", "hold_pct", 50);
    backend.setInt("faucet_cfg", "noflow_s", 30);
    ConfigStore store(backend);
    SystemConfig live = makeDefaultConfig();
    live.presets[2].enabled = true;
    live.presets[2].value = 2500;

    const SystemConfig merged = store.loadSystemConfigForExplicitSave(live);

    TEST_ASSERT_EQUAL_UINT32(2, merged.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(50, merged.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(30, merged.noFlowTimeoutSec);
    TEST_ASSERT_TRUE(merged.presets[2].enabled);
    TEST_ASSERT_EQUAL_UINT32(2500, merged.presets[2].value);
    TEST_ASSERT_TRUE(store.saveSystemConfig(merged));
    TEST_ASSERT_EQUAL_INT32(store.currentSystemConfigVersion(), backend.getInt("faucet_cfg", "ver", 0));

    const SystemConfig current = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(2, current.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(50, current.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(30, current.noFlowTimeoutSec);
}

void test_config_save_and_load_round_trips_system_config() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 20;
    config.beepEnabled = false;
    config.displaySleepSec = 75;
    config.resultDisplaySec = 12;
    config.volumeAdjustStepMl = 250;
    config.timeAdjustStepSec = 15;
    config.pulseMinIntervalUs = 2500;
    config.presets[2].enabled = true;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 120;
    std::strncpy(config.presets[2].name, "Tea", sizeof(config.presets[2].name) - 1);
    config.filters[1].enabled = true;
    config.filters[1].recommendDays = 180;
    config.filters[1].maxDays = 365;
    config.filters[1].lifeMl = 2000000;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(20, loaded.confirmTimeoutSec);
    TEST_ASSERT_FALSE(loaded.beepEnabled);
    TEST_ASSERT_EQUAL_UINT32(75, loaded.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(12, loaded.resultDisplaySec);
    TEST_ASSERT_EQUAL_UINT32(250, loaded.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(15, loaded.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(2500, loaded.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "pulse_win_s", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "cal_an_us", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "cal_win_s", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "cal_tol", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "cal_span", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "cal_err", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "cal_rel", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "active_ms", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "ms1_sp", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "mc_sp", -7));
    TEST_ASSERT_TRUE(loaded.presets[2].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(PresetType::Time), static_cast<std::uint8_t>(loaded.presets[2].type));
    TEST_ASSERT_EQUAL_UINT32(120, loaded.presets[2].value);
    TEST_ASSERT_EQUAL_STRING("Tea", loaded.presets[2].name);
    TEST_ASSERT_TRUE(loaded.filters[1].enabled);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[1].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(365, loaded.filters[1].maxDays);
    TEST_ASSERT_EQUAL_UINT32(2000000, loaded.filters[1].lifeMl);
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "f1_start", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "f1_used", -7));
}

void test_config_save_and_load_round_trips_sensor_config() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.temperatureKind = TemperatureKind::Ntc50kB3950;
    config.temperatureOffsetCentiC = -35;
    config.temperatureCalibrated = true;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.tdsScale = 1.234f;
    config.tdsOffsetPpm = -4;
    config.tdsCalibrated = true;
    config.tdsTemperatureCompensationEnabled = false;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    TEST_ASSERT_EQUAL_INT32(21, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_INT32(1234, backend.getInt("faucet_cfg", "tds_scale_milli", 0));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_cal_mode", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_cal_rev", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_low_ref", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_low_raw", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_high_ref", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_high_raw", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_cal_temp", -7));
    TEST_ASSERT_EQUAL_INT32(-7, backend.getInt("faucet_cfg", "tds_cal_mv", -7));
    char sensorText[32]{};
    TEST_ASSERT_TRUE(backend.getStr("faucet_cfg", "temp_sensor", sensorText, sizeof(sensorText), ""));
    TEST_ASSERT_EQUAL_STRING("ntc50k_b3950", sensorText);
    TEST_ASSERT_TRUE(backend.getStr("faucet_cfg", "tds_sensor", sensorText, sizeof(sensorText), ""));
    TEST_ASSERT_EQUAL_STRING("tds_board_v1", sensorText);
    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_TRUE(temperatureSensorEnabled(loaded));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TemperatureKind::Ntc50kB3950),
                            static_cast<std::uint8_t>(loaded.temperatureKind));
    TEST_ASSERT_EQUAL_INT16(-35, loaded.temperatureOffsetCentiC);
    TEST_ASSERT_TRUE(loaded.temperatureCalibrated);
    TEST_ASSERT_TRUE(tdsSensorEnabled(loaded));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsKind::AnalogTdsAo),
                            static_cast<std::uint8_t>(loaded.tdsKind));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.234f, loaded.tdsScale);
    TEST_ASSERT_EQUAL_INT16(-4, loaded.tdsOffsetPpm);
    TEST_ASSERT_TRUE(loaded.tdsCalibrated);
    TEST_ASSERT_FALSE(loaded.tdsTemperatureCompensationEnabled);
}

void test_config_load_sanitizes_stored_values() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 1;
    config.volumeAdjustStepMl = 999999;
    config.timeAdjustStepSec = 999999;
    config.pulseMinIntervalUs = 1;
    config.presets[0].value = 1;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(3, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kMaxVolumeAdjustStepMl, loaded.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxTimeAdjustStepSec, loaded.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kMinPulseMinIntervalUs, loaded.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kMinVolumePresetMl, loaded.presets[0].value);
}

void test_config_save_reports_backend_failures() {
    FakeConfigBackend backend;
    backend.failWrites = true;
    ConfigStore store(backend);

    TEST_ASSERT_FALSE(store.saveSystemConfig(makeDefaultConfig()));
}

void test_config_save_does_not_mark_current_version_after_partial_write_failure() {
    FakeConfigBackend backend;
    backend.failKey = "faucet_cfg/p0_name";
    ConfigStore store(backend);

    TEST_ASSERT_FALSE(store.saveSystemConfig(makeDefaultConfig()));

    TEST_ASSERT_EQUAL_INT32(-1, backend.getInt("faucet_cfg", "ver", -1));
    char name[kPresetNameLength]{};
    TEST_ASSERT_FALSE(backend.getStr("faucet_cfg", "p0_name", name, sizeof(name), ""));
}

void test_statistics_runtime_round_trips_uint32_values() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    StatisticsRecord record{};
    record.todayMl = 100;
    record.weekMl = 200;
    record.monthMl = 300;
    record.totalMl = 4000000000UL;
    record.lastDayKey = 20260506;
    record.lastWeekKey = 202619;
    record.lastMonthKey = 202605;

    TEST_ASSERT_TRUE(store.saveStatistics(record));

    const StatisticsRecord loaded = store.loadStatistics({20260506, 202619, 202605});
    TEST_ASSERT_EQUAL_UINT32(100, loaded.todayMl);
    TEST_ASSERT_EQUAL_UINT32(200, loaded.weekMl);
    TEST_ASSERT_EQUAL_UINT32(300, loaded.monthMl);
    TEST_ASSERT_EQUAL_UINT32(4000000000UL, loaded.totalMl);
    TEST_ASSERT_EQUAL_UINT32(20260506, loaded.lastDayKey);
}

void test_statistics_runtime_rolls_loaded_periods() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    StatisticsRecord record{};
    record.todayMl = 100;
    record.weekMl = 200;
    record.monthMl = 300;
    record.totalMl = 400;
    record.lastDayKey = 20260505;
    record.lastWeekKey = 202619;
    record.lastMonthKey = 202605;
    TEST_ASSERT_TRUE(store.saveStatistics(record));

    const StatisticsRecord loaded = store.loadStatistics({20260506, 202619, 202605});
    TEST_ASSERT_EQUAL_UINT32(0, loaded.todayMl);
    TEST_ASSERT_EQUAL_UINT32(200, loaded.weekMl);
    TEST_ASSERT_EQUAL_UINT32(300, loaded.monthMl);
    TEST_ASSERT_EQUAL_UINT32(400, loaded.totalMl);
    TEST_ASSERT_EQUAL_UINT32(20260506, loaded.lastDayKey);
}

void test_statistics_runtime_future_version_uses_defaults_without_erasing_storage() {
    FakeConfigBackend backend;
    backend.setInt("faucet_stat", "ver", 255);
    backend.setStr("faucet_stat", "today", "123");
    backend.setStr("faucet_stat", "total", "456");
    ConfigStore store(backend);

    const StatisticsRecord loaded = store.loadStatistics({20260506, 202619, 202605});

    TEST_ASSERT_EQUAL_UINT32(0, loaded.todayMl);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.totalMl);
    TEST_ASSERT_EQUAL_UINT32(20260506, loaded.lastDayKey);
    TEST_ASSERT_EQUAL_INT32(255, backend.getInt("faucet_stat", "ver", 0));
    char text[16]{};
    TEST_ASSERT_TRUE(backend.getStr("faucet_stat", "today", text, sizeof(text), ""));
    TEST_ASSERT_EQUAL_STRING("123", text);
}

void test_filter_runtime_round_trips_start_used_and_boot() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    FilterRuntime runtime[kFilterCount]{};
    runtime[0].startTime = 111;
    runtime[0].usedMl = 4000000000UL;
    runtime[0].startBootId = 9;
    runtime[1].startTime = 222;
    runtime[1].usedMl = 333;
    runtime[1].startBootId = 10;

    TEST_ASSERT_TRUE(store.saveFilterRuntime(runtime));

    FilterRuntime loaded[kFilterCount]{};
    loaded[0].startTime = 999;
    loaded[1].startTime = 888;
    store.loadFilterRuntime(loaded);
    TEST_ASSERT_EQUAL_UINT32(111, loaded[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(4000000000UL, loaded[0].usedMl);
    TEST_ASSERT_EQUAL_UINT32(9, loaded[0].startBootId);
    TEST_ASSERT_EQUAL_UINT32(222, loaded[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(333, loaded[1].usedMl);
    TEST_ASSERT_EQUAL_UINT32(10, loaded[1].startBootId);
}

void test_filter_runtime_ignores_values_in_system_config_namespace() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 15);
    backend.setInt("faucet_cfg", "f0_start", 1714502400);
    backend.setInt("faucet_cfg", "f0_used", 123456);
    ConfigStore store(backend);
    FilterRuntime runtime[kFilterCount]{};

    store.loadFilterRuntime(runtime);

    TEST_ASSERT_EQUAL_UINT32(0, runtime[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, runtime[0].usedMl);
    TEST_ASSERT_EQUAL_INT32(0, backend.getInt("faucet_run", "ver", 0));
}

void test_system_config_load_does_not_merge_filter_runtime_storage() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    backend.setInt("faucet_cfg", "ver", store.currentSystemConfigVersion());
    backend.setBool("faucet_cfg", "f0_en", true);
    backend.setStr("faucet_cfg", "f0_name", "PP");
    backend.setInt("faucet_run", "ver", 1);
    backend.setStr("faucet_run", "f0_start", "1714502400");
    backend.setStr("faucet_run", "f0_used", "123456");

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::LoadedCurrent),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_TRUE(loaded.filters[0].enabled);
    TEST_ASSERT_EQUAL_STRING("PP", loaded.filters[0].name);
    TEST_ASSERT_EQUAL_INT32(0, backend.getInt("faucet_cfg", "f0_start", 0));
}

void test_filter_runtime_future_version_keeps_current_records_and_storage() {
    FakeConfigBackend backend;
    backend.setInt("faucet_run", "ver", 255);
    backend.setStr("faucet_run", "f0_used", "123456");
    ConfigStore store(backend);
    FilterRuntime runtime[kFilterCount]{};
    runtime[0].usedMl = 77;

    store.loadFilterRuntime(runtime);

    TEST_ASSERT_EQUAL_UINT32(77, runtime[0].usedMl);
    TEST_ASSERT_EQUAL_INT32(255, backend.getInt("faucet_run", "ver", 0));
    char text[16]{};
    TEST_ASSERT_TRUE(backend.getStr("faucet_run", "f0_used", text, sizeof(text), ""));
    TEST_ASSERT_EQUAL_STRING("123456", text);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_config_load_uses_defaults_without_matching_version);
    RUN_TEST(test_non_current_system_config_version_uses_defaults_until_explicit_save);
    RUN_TEST(test_explicit_app_config_save_merges_fields_and_establishes_current_version);
    RUN_TEST(test_config_save_and_load_round_trips_system_config);
    RUN_TEST(test_config_save_and_load_round_trips_sensor_config);
    RUN_TEST(test_config_load_sanitizes_stored_values);
    RUN_TEST(test_config_save_reports_backend_failures);
    RUN_TEST(test_config_save_does_not_mark_current_version_after_partial_write_failure);
    RUN_TEST(test_statistics_runtime_round_trips_uint32_values);
    RUN_TEST(test_statistics_runtime_rolls_loaded_periods);
    RUN_TEST(test_statistics_runtime_future_version_uses_defaults_without_erasing_storage);
    RUN_TEST(test_filter_runtime_round_trips_start_used_and_boot);
    RUN_TEST(test_filter_runtime_ignores_values_in_system_config_namespace);
    RUN_TEST(test_system_config_load_does_not_merge_filter_runtime_storage);
    RUN_TEST(test_filter_runtime_future_version_keeps_current_records_and_storage);
    return UNITY_END();
}
