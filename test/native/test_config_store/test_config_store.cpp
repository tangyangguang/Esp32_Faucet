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
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::Defaults),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_EQUAL_INT32(1, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.presets[0].value);
    TEST_ASSERT_TRUE(store.saveSystemConfig(loaded));
    TEST_ASSERT_EQUAL_INT32(store.currentSystemConfigVersion(), backend.getInt("faucet_cfg", "ver", 0));
    const SystemConfig current = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, current.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::LoadedCurrent),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
}

void test_non_current_config_load_does_not_write_storage() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 1);
    backend.setInt("faucet_cfg", "pulse_m", 620);
    backend.setStr("faucet_cfg", "f0_name", "CTO");
    backend.setInt("faucet_cfg", "f0_life_d", 90);
    backend.failWrites = true;
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::Defaults),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_EQUAL_INT32(1, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_INT32(620, backend.getInt("faucet_cfg", "pulse_m", 0));
    TEST_ASSERT_EQUAL_INT32(90, backend.getInt("faucet_cfg", "f0_life_d", 0));
    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, loaded.confirmTimeoutSec);
    char text[16]{};
    TEST_ASSERT_TRUE(backend.getStr("faucet_cfg", "f0_name", text, sizeof(text), ""));
    TEST_ASSERT_EQUAL_STRING("CTO", text);
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
    config.pulseObservationWindowSec = 24;
    config.calibrationAnalysisPulseMinIntervalUs = 1500;
    config.calibrationStableWindowSec = 5;
    config.calibrationStableTolerancePercent = 30;
    config.calibrationMinVolumeSpanMl = 1500;
    config.calibrationMaxErrorMl = 150;
    config.calibrationMaxRelativeErrorTenthPercent = 35;
    config.presets[2].enabled = true;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 120;
    std::strncpy(config.presets[2].name, "Tea", sizeof(config.presets[2].name) - 1);
    config.filters[1].enabled = true;
    config.filters[1].recommendDays = 180;
    config.filters[1].maxDays = 365;
    config.filters[1].lifeMl = 2000000;
    config.filters[1].startTime = 1714502400;
    config.filters[1].usedMl = 123456;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(20, loaded.confirmTimeoutSec);
    TEST_ASSERT_FALSE(loaded.beepEnabled);
    TEST_ASSERT_EQUAL_UINT32(75, loaded.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(12, loaded.resultDisplaySec);
    TEST_ASSERT_EQUAL_UINT32(250, loaded.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(15, loaded.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(2500, loaded.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(24, loaded.pulseObservationWindowSec);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.calibrationAnalysisPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(5, loaded.calibrationStableWindowSec);
    TEST_ASSERT_EQUAL_UINT8(30, loaded.calibrationStableTolerancePercent);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.calibrationMinVolumeSpanMl);
    TEST_ASSERT_EQUAL_UINT32(150, loaded.calibrationMaxErrorMl);
    TEST_ASSERT_EQUAL_UINT16(35, loaded.calibrationMaxRelativeErrorTenthPercent);
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
    TEST_ASSERT_EQUAL_UINT32(1714502400, loaded.filters[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.filters[1].usedMl);
}

void test_config_save_and_load_round_trips_sensor_config() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.sensorVrefMv = 3275;
    config.temperatureEnabled = true;
    config.temperatureKind = TemperatureKind::Ntc50kB3950;
    config.temperatureOffsetCentiC = -35;
    config.temperatureCalibrated = true;
    config.tdsEnabled = true;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.tdsCalibrationMode = TdsCalibrationMode::MultiPoint;
    config.tdsCalibrationRevision = 7;
    config.tdsScale = 1.234f;
    config.tdsOffsetPpm = -4;
    config.tdsLowReferencePpm = 1;
    config.tdsLowRawPpm = 3;
    config.tdsHighReferencePpm = 160;
    config.tdsHighRawPpm = 150;
    config.tdsCalibrationTime = 1720000000UL;
    config.tdsCalibrationTemperatureCentiC = 2430;
    config.tdsCalibrationVoltageMv = 410;
    config.tdsCalibrated = true;
    config.tdsTemperatureCompensationEnabled = false;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    TEST_ASSERT_EQUAL_INT32(19, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_INT32(1234, backend.getInt("faucet_cfg", "tds_scale_milli", 0));
    char sensorText[32]{};
    TEST_ASSERT_TRUE(backend.getStr("faucet_cfg", "temp_sensor", sensorText, sizeof(sensorText), ""));
    TEST_ASSERT_EQUAL_STRING("ntc50k_b3950", sensorText);
    TEST_ASSERT_TRUE(backend.getStr("faucet_cfg", "tds_sensor", sensorText, sizeof(sensorText), ""));
    TEST_ASSERT_EQUAL_STRING("tds_board_v1", sensorText);
    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT16(3300, loaded.sensorVrefMv);
    TEST_ASSERT_TRUE(loaded.temperatureEnabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TemperatureKind::Ntc50kB3950),
                            static_cast<std::uint8_t>(loaded.temperatureKind));
    TEST_ASSERT_EQUAL_INT16(-35, loaded.temperatureOffsetCentiC);
    TEST_ASSERT_TRUE(loaded.temperatureCalibrated);
    TEST_ASSERT_TRUE(loaded.tdsEnabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsKind::AnalogTdsAo),
                            static_cast<std::uint8_t>(loaded.tdsKind));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::MultiPoint),
                            static_cast<std::uint8_t>(loaded.tdsCalibrationMode));
    TEST_ASSERT_EQUAL_UINT16(7, loaded.tdsCalibrationRevision);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.234f, loaded.tdsScale);
    TEST_ASSERT_EQUAL_INT16(-4, loaded.tdsOffsetPpm);
    TEST_ASSERT_EQUAL_UINT16(1, loaded.tdsLowReferencePpm);
    TEST_ASSERT_EQUAL_UINT16(3, loaded.tdsLowRawPpm);
    TEST_ASSERT_EQUAL_UINT16(160, loaded.tdsHighReferencePpm);
    TEST_ASSERT_EQUAL_UINT16(150, loaded.tdsHighRawPpm);
    TEST_ASSERT_EQUAL_UINT32(1720000000UL, loaded.tdsCalibrationTime);
    TEST_ASSERT_EQUAL_INT16(2430, loaded.tdsCalibrationTemperatureCentiC);
    TEST_ASSERT_EQUAL_UINT16(410, loaded.tdsCalibrationVoltageMv);
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
    config.pulseObservationWindowSec = 999999;
    config.calibrationAnalysisPulseMinIntervalUs = 999999;
    config.calibrationStableWindowSec = 1;
    config.calibrationStableTolerancePercent = 99;
    config.calibrationMinVolumeSpanMl = 1;
    config.calibrationMaxErrorMl = 1;
    config.calibrationMaxRelativeErrorTenthPercent = 1;
    config.presets[0].value = 1;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(3, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kMaxVolumeAdjustStepMl, loaded.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxTimeAdjustStepSec, loaded.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kMinPulseMinIntervalUs, loaded.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kMaxPulseObservationWindowSec, loaded.pulseObservationWindowSec);
    TEST_ASSERT_EQUAL_UINT32(kMaxPulseMinIntervalUs, loaded.calibrationAnalysisPulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kMinCalibrationStableWindowSec, loaded.calibrationStableWindowSec);
    TEST_ASSERT_EQUAL_UINT8(kMaxCalibrationStableTolerancePercent, loaded.calibrationStableTolerancePercent);
    TEST_ASSERT_EQUAL_UINT32(kMinCalibrationMinVolumeSpanMl, loaded.calibrationMinVolumeSpanMl);
    TEST_ASSERT_EQUAL_UINT32(kMinCalibrationMaxErrorMl, loaded.calibrationMaxErrorMl);
    TEST_ASSERT_EQUAL_UINT16(kMinCalibrationMaxRelativeErrorTenthPercent,
                             loaded.calibrationMaxRelativeErrorTenthPercent);
    TEST_ASSERT_EQUAL_UINT32(kMinVolumePresetMl, loaded.presets[0].value);
}

void test_config_reset_restores_defaults() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 42;
    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    TEST_ASSERT_TRUE(store.resetSystemConfig());

    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.presets[0].value);
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

void test_filter_runtime_round_trips_used_and_boot_only() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.filters[0].startTime = 111;
    config.filters[0].usedMl = 4000000000UL;
    config.filters[0].startBootId = 9;
    config.filters[1].startTime = 222;
    config.filters[1].usedMl = 333;
    config.filters[1].startBootId = 10;

    TEST_ASSERT_TRUE(store.saveFilterRuntime(config.filters));

    SystemConfig loaded = makeDefaultConfig();
    loaded.filters[0].startTime = 999;
    loaded.filters[1].startTime = 888;
    store.loadFilterRuntime(loaded.filters);
    TEST_ASSERT_EQUAL_UINT32(999, loaded.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(4000000000UL, loaded.filters[0].usedMl);
    TEST_ASSERT_EQUAL_UINT32(9, loaded.filters[0].startBootId);
    TEST_ASSERT_EQUAL_UINT32(888, loaded.filters[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(333, loaded.filters[1].usedMl);
    TEST_ASSERT_EQUAL_UINT32(10, loaded.filters[1].startBootId);
    TEST_ASSERT_EQUAL_STRING("第1级滤芯", loaded.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.filters[0].lifeMl);
}

void test_filter_runtime_ignores_values_in_system_config_namespace() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 15);
    backend.setInt("faucet_cfg", "f0_start", 1714502400);
    backend.setInt("faucet_cfg", "f0_used", 123456);
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();

    store.loadFilterRuntime(config.filters);

    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].usedMl);
    TEST_ASSERT_EQUAL_INT32(0, backend.getInt("faucet_run", "ver", 0));
}

void test_non_current_config_does_not_merge_filter_runtime_storage() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 15);
    backend.setBool("faucet_cfg", "f0_en", true);
    backend.setStr("faucet_cfg", "f0_name", "PP");
    backend.setInt("faucet_run", "ver", 1);
    backend.setStr("faucet_run", "f0_start", "1714502400");
    backend.setStr("faucet_run", "f0_used", "123456");
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::Defaults),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_EQUAL_INT32(15, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_TRUE(loaded.filters[0].enabled);
    TEST_ASSERT_EQUAL_STRING("第1级滤芯", loaded.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.filters[0].startTime);
    TEST_ASSERT_EQUAL_INT32(0, backend.getInt("faucet_cfg", "f0_start", 0));
}

void test_filter_runtime_future_version_keeps_current_records_and_storage() {
    FakeConfigBackend backend;
    backend.setInt("faucet_run", "ver", 255);
    backend.setStr("faucet_run", "f0_used", "123456");
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.filters[0].usedMl = 77;

    store.loadFilterRuntime(config.filters);

    TEST_ASSERT_EQUAL_UINT32(77, config.filters[0].usedMl);
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
    RUN_TEST(test_non_current_config_load_does_not_write_storage);
    RUN_TEST(test_config_save_and_load_round_trips_system_config);
    RUN_TEST(test_config_save_and_load_round_trips_sensor_config);
    RUN_TEST(test_config_load_sanitizes_stored_values);
    RUN_TEST(test_config_reset_restores_defaults);
    RUN_TEST(test_config_save_reports_backend_failures);
    RUN_TEST(test_config_save_does_not_mark_current_version_after_partial_write_failure);
    RUN_TEST(test_statistics_runtime_round_trips_uint32_values);
    RUN_TEST(test_statistics_runtime_rolls_loaded_periods);
    RUN_TEST(test_statistics_runtime_future_version_uses_defaults_without_erasing_storage);
    RUN_TEST(test_filter_runtime_round_trips_used_and_boot_only);
    RUN_TEST(test_filter_runtime_ignores_values_in_system_config_namespace);
    RUN_TEST(test_non_current_config_does_not_merge_filter_runtime_storage);
    RUN_TEST(test_filter_runtime_future_version_keeps_current_records_and_storage);
    return UNITY_END();
}
