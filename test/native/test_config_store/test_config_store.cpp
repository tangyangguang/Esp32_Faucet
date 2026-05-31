#include <unity.h>

#include "app/ConfigStore.h"

#include <cstring>
#include <map>
#include <string>

using namespace faucet;

namespace {

class FakeConfigBackend : public ConfigBackend {
public:
    bool failWrites = false;

    bool setInt(const char* ns, const char* key, std::int32_t value) override {
        if (failWrites) {
            return false;
        }
        ints[makeKey(ns, key)] = value;
        return true;
    }

    std::int32_t getInt(const char* ns, const char* key, std::int32_t def) override {
        const auto it = ints.find(makeKey(ns, key));
        return it == ints.end() ? def : it->second;
    }

    bool setBool(const char* ns, const char* key, bool value) override {
        if (failWrites) {
            return false;
        }
        bools[makeKey(ns, key)] = value;
        return true;
    }

    bool getBool(const char* ns, const char* key, bool def) override {
        const auto it = bools.find(makeKey(ns, key));
        return it == bools.end() ? def : it->second;
    }

    bool setStr(const char* ns, const char* key, const char* value) override {
        if (failWrites) {
            return false;
        }
        strings[makeKey(ns, key)] = value ? value : "";
        return true;
    }

    bool getStr(const char* ns, const char* key, char* out, std::size_t len, const char* def) override {
        if (!out || len == 0) {
            return false;
        }
        const auto it = strings.find(makeKey(ns, key));
        const std::string value = it == strings.end() ? std::string(def ? def : "") : it->second;
        std::strncpy(out, value.c_str(), len - 1);
        out[len - 1] = '\0';
        return it != strings.end();
    }

    bool clearNamespace(const char* ns) override {
        const std::string prefix = std::string(ns) + "/";
        erasePrefix(ints, prefix);
        erasePrefix(bools, prefix);
        erasePrefix(strings, prefix);
        return true;
    }

private:
    template <typename T>
    void erasePrefix(std::map<std::string, T>& values, const std::string& prefix) {
        for (auto it = values.begin(); it != values.end();) {
            if (it->first.rfind(prefix, 0) == 0) {
                it = values.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string makeKey(const char* ns, const char* key) const {
        return std::string(ns ? ns : "") + "/" + (key ? key : "");
    }

    std::map<std::string, std::int32_t> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, std::string> strings;
};

}  // namespace

void test_config_load_uses_defaults_without_matching_version() {
    FakeConfigBackend backend;
    ConfigStore store(backend);

    const SystemConfig config = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1500, config.presets[0].value);
    TEST_ASSERT_TRUE(config.filters[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::DefaultsNoVersion),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_FALSE(store.systemConfigReadOnly());
}

void test_config_migrates_v1_without_losing_user_values() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 1);
    backend.setInt("faucet_cfg", "confirm_s", 22);
    backend.setInt("faucet_cfg", "pulse_m", 620);
    backend.setBool("faucet_cfg", "p2_en", true);
    backend.setInt("faucet_cfg", "p2_type", 1);
    backend.setInt("faucet_cfg", "p2_val", 150);
    backend.setStr("faucet_cfg", "p2_name", "Tea");
    backend.setBool("faucet_cfg", "f0_en", true);
    backend.setStr("faucet_cfg", "f0_name", "CTO");
    backend.setInt("faucet_cfg", "f0_life_d", 90);
    backend.setInt("faucet_cfg", "f0_life_ml", 300000);
    backend.setInt("faucet_cfg", "f0_start", 1714502400);
    backend.setInt("faucet_cfg", "f0_used", 123456);
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::MigratedLegacy),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_FALSE(store.systemConfigReadOnly());
    TEST_ASSERT_EQUAL_INT32(12, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_INT32(90, backend.getInt("faucet_cfg", "f0_life_min", 0));
    TEST_ASSERT_EQUAL_INT32(90, backend.getInt("faucet_cfg", "f0_life_max", 0));
    TEST_ASSERT_EQUAL_UINT32(22, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultVolumeAdjustStepMl, loaded.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultTimeAdjustStepSec, loaded.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultPulseMinIntervalUs, loaded.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kDefaultRecentPulseTraceCount, loaded.recentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT32(620, activeMeteringParameters(loaded).stablePulsePerLiter);
    TEST_ASSERT_TRUE(loaded.presets[2].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(PresetType::Time), static_cast<std::uint8_t>(loaded.presets[2].type));
    TEST_ASSERT_EQUAL_UINT32(150, loaded.presets[2].value);
    TEST_ASSERT_EQUAL_STRING("Tea", loaded.presets[2].name);
    TEST_ASSERT_TRUE(loaded.filters[0].enabled);
    TEST_ASSERT_EQUAL_STRING("CTO", loaded.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(90, loaded.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(90, loaded.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(300000, loaded.filters[0].lifeMl);
    TEST_ASSERT_EQUAL_UINT32(1714502400, loaded.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(123456, loaded.filters[0].usedMl);
}

void test_config_migrates_legacy_fields_when_version_is_missing() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "confirm_s", 23);
    backend.setInt("faucet_cfg", "pulse_m", 615);
    backend.setBool("faucet_cfg", "f0_en", true);
    backend.setStr("faucet_cfg", "f0_name", "Carbon");
    backend.setInt("faucet_cfg", "f0_life_d", 120);
    backend.setInt("faucet_cfg", "f0_life_ml", 450000);
    backend.setInt("faucet_cfg", "f0_start", 1714502400);
    backend.setInt("faucet_cfg", "f0_used", 654321);
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::MigratedLegacy),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_FALSE(store.systemConfigReadOnly());
    TEST_ASSERT_EQUAL_INT32(12, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_UINT32(23, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(615, activeMeteringParameters(loaded).stablePulsePerLiter);
    TEST_ASSERT_EQUAL_STRING("Carbon", loaded.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(120, loaded.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(120, loaded.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(450000, loaded.filters[0].lifeMl);
    TEST_ASSERT_EQUAL_UINT32(1714502400, loaded.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(654321, loaded.filters[0].usedMl);
}

void test_config_migrates_v2_filter_ranges_and_single_calibration_target() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 2);
    backend.setBool("faucet_cfg", "f1_en", true);
    backend.setStr("faucet_cfg", "f1_name", "RO");
    backend.setInt("faucet_cfg", "f1_life_min", 360);
    backend.setInt("faucet_cfg", "f1_life_max", 720);
    backend.setInt("faucet_cfg", "f1_life_ml", 9000000);
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::MigratedLegacy),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_EQUAL_INT32(12, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_TRUE(loaded.filters[1].enabled);
    TEST_ASSERT_EQUAL_STRING("RO", loaded.filters[1].name);
    TEST_ASSERT_EQUAL_UINT32(360, loaded.filters[1].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(720, loaded.filters[1].maxDays);
    TEST_ASSERT_EQUAL_UINT32(9000000, loaded.filters[1].lifeMl);
}

void test_config_migration_failure_preserves_legacy_storage_without_current_version() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 1);
    backend.setInt("faucet_cfg", "pulse_m", 620);
    backend.setStr("faucet_cfg", "f0_name", "CTO");
    backend.setInt("faucet_cfg", "f0_life_d", 90);
    backend.failWrites = true;
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::MigratedLegacy),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_EQUAL_UINT32(620, activeMeteringParameters(loaded).stablePulsePerLiter);
    TEST_ASSERT_EQUAL_INT32(1, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_INT32(620, backend.getInt("faucet_cfg", "pulse_m", 0));
    TEST_ASSERT_EQUAL_INT32(90, backend.getInt("faucet_cfg", "f0_life_d", 0));
    char text[16]{};
    TEST_ASSERT_TRUE(backend.getStr("faucet_cfg", "f0_name", text, sizeof(text), ""));
    TEST_ASSERT_EQUAL_STRING("CTO", text);
}

void test_config_future_version_loads_current_fields_read_only_without_overwrite() {
    FakeConfigBackend backend;
    backend.setInt("faucet_cfg", "ver", 99);
    backend.setInt("faucet_cfg", "confirm_s", 24);
    backend.setBool("faucet_cfg", "beep", false);
    backend.setBool("faucet_cfg", "p0_en", false);
    backend.setInt("faucet_cfg", "p0_type", 1);
    backend.setInt("faucet_cfg", "p0_val", 45);
    backend.setStr("faucet_cfg", "p0_name", "Legacy");
    ConfigStore store(backend);

    const SystemConfig loaded = store.loadSystemConfig();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ConfigStore::LoadStatus::LoadedFutureVersionReadOnly),
                            static_cast<std::uint8_t>(store.lastSystemConfigLoadStatus()));
    TEST_ASSERT_TRUE(store.systemConfigReadOnly());
    TEST_ASSERT_EQUAL_INT32(99, backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_UINT32(24, loaded.confirmTimeoutSec);
    TEST_ASSERT_FALSE(loaded.beepEnabled);
    TEST_ASSERT_FALSE(loaded.presets[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(PresetType::Time), static_cast<std::uint8_t>(loaded.presets[0].type));
    TEST_ASSERT_EQUAL_UINT32(45, loaded.presets[0].value);
    TEST_ASSERT_EQUAL_STRING("Legacy", loaded.presets[0].name);
    TEST_ASSERT_FALSE(store.saveSystemConfig(makeDefaultConfig()));
    TEST_ASSERT_EQUAL_INT32(99, backend.getInt("faucet_cfg", "ver", 0));
}

void test_config_save_and_load_round_trips_system_config() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 20;
    config.beepEnabled = false;
    config.displaySleepSec = 45;
    config.resultDisplaySec = 12;
    config.lcdI2cAddress = 0x3F;
    config.volumeAdjustStepMl = 250;
    config.timeAdjustStepSec = 15;
    config.pulseMinIntervalUs = 2500;
    config.recentPulseTraceCount = 2;
    config.activeMeteringSlot = 1;
    config.meteringSlots[1].params = MeteringParameters{40, 553, 222};
    std::strncpy(config.meteringSlots[1].name, "实验参数", sizeof(config.meteringSlots[1].name) - 1);
    std::strncpy(config.meteringSlots[1].creationNote, "样本数量 3，容量范围 1.5L-7.5L", sizeof(config.meteringSlots[1].creationNote) - 1);
    config.meteringCandidate.ready = true;
    config.meteringCandidate.params = MeteringParameters{41, 520, 224};
    std::strncpy(config.meteringCandidate.note, "样本数量 3，最大误差 35ml", sizeof(config.meteringCandidate.note) - 1);
    config.meteringCandidate.generatedAt = 1770000000;
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
    TEST_ASSERT_EQUAL_UINT32(45, loaded.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(12, loaded.resultDisplaySec);
    TEST_ASSERT_EQUAL_UINT8(0x3F, loaded.lcdI2cAddress);
    TEST_ASSERT_EQUAL_UINT32(250, loaded.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(15, loaded.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(2500, loaded.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(2, loaded.recentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT8(1, loaded.activeMeteringSlot);
    TEST_ASSERT_EQUAL_STRING("实验参数", loaded.meteringSlots[1].name);
    TEST_ASSERT_EQUAL_UINT32(40, loaded.meteringSlots[1].params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, loaded.meteringSlots[1].params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, loaded.meteringSlots[1].params.stablePulsePerLiter);
    TEST_ASSERT_NOT_NULL(std::strstr(loaded.meteringSlots[1].creationNote, "样本数量 3"));
    TEST_ASSERT_TRUE(loaded.meteringCandidate.ready);
    TEST_ASSERT_EQUAL_UINT32(41, loaded.meteringCandidate.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(520, loaded.meteringCandidate.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(224, loaded.meteringCandidate.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(1770000000, loaded.meteringCandidate.generatedAt);
    TEST_ASSERT_TRUE(loaded.presets[2].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(PresetType::Time), static_cast<std::uint8_t>(loaded.presets[2].type));
    TEST_ASSERT_EQUAL_UINT32(120, loaded.presets[2].value);
    TEST_ASSERT_EQUAL_STRING("Tea", loaded.presets[2].name);
    TEST_ASSERT_TRUE(loaded.filters[1].enabled);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[1].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(365, loaded.filters[1].maxDays);
    TEST_ASSERT_EQUAL_UINT32(2000000, loaded.filters[1].lifeMl);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.filters[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.filters[1].usedMl);
}

void test_config_load_sanitizes_stored_values() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 1;
    config.volumeAdjustStepMl = 999999;
    config.timeAdjustStepSec = 999999;
    config.pulseMinIntervalUs = 1;
    config.recentPulseTraceCount = 999999;
    config.meteringSlots[0].params = MeteringParameters{999999, 999999, 999999};
    config.presets[0].value = 1;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(3, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kMaxVolumeAdjustStepMl, loaded.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxTimeAdjustStepSec, loaded.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kMinPulseMinIntervalUs, loaded.pulseMinIntervalUs);
    TEST_ASSERT_EQUAL_UINT32(kMaxRecentPulseTraceCount, loaded.recentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedStartupPulseCount, loaded.meteringSlots[0].params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedStartupVolumeMl, loaded.meteringSlots[0].params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedPulsePerLiter, loaded.meteringSlots[0].params.stablePulsePerLiter);
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
    TEST_ASSERT_FALSE(store.systemConfigReadOnly());
    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, loaded.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.presets[0].value);
}

void test_config_save_reports_backend_failures() {
    FakeConfigBackend backend;
    backend.failWrites = true;
    ConfigStore store(backend);

    TEST_ASSERT_FALSE(store.saveSystemConfig(makeDefaultConfig()));
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

void test_filter_runtime_round_trips_start_and_used_only() {
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
    store.loadFilterRuntime(loaded.filters);
    TEST_ASSERT_EQUAL_UINT32(111, loaded.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(4000000000UL, loaded.filters[0].usedMl);
    TEST_ASSERT_EQUAL_UINT32(9, loaded.filters[0].startBootId);
    TEST_ASSERT_EQUAL_UINT32(222, loaded.filters[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(333, loaded.filters[1].usedMl);
    TEST_ASSERT_EQUAL_UINT32(10, loaded.filters[1].startBootId);
    TEST_ASSERT_EQUAL_STRING("第1级滤芯", loaded.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.filters[0].lifeMl);
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
    RUN_TEST(test_config_migrates_v1_without_losing_user_values);
    RUN_TEST(test_config_migrates_legacy_fields_when_version_is_missing);
    RUN_TEST(test_config_migrates_v2_filter_ranges_and_single_calibration_target);
    RUN_TEST(test_config_migration_failure_preserves_legacy_storage_without_current_version);
    RUN_TEST(test_config_future_version_loads_current_fields_read_only_without_overwrite);
    RUN_TEST(test_config_save_and_load_round_trips_system_config);
    RUN_TEST(test_config_load_sanitizes_stored_values);
    RUN_TEST(test_config_reset_restores_defaults);
    RUN_TEST(test_config_save_reports_backend_failures);
    RUN_TEST(test_statistics_runtime_round_trips_uint32_values);
    RUN_TEST(test_statistics_runtime_rolls_loaded_periods);
    RUN_TEST(test_statistics_runtime_future_version_uses_defaults_without_erasing_storage);
    RUN_TEST(test_filter_runtime_round_trips_start_and_used_only);
    RUN_TEST(test_filter_runtime_future_version_keeps_current_records_and_storage);
    return UNITY_END();
}
