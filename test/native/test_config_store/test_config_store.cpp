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
}

void test_config_save_and_load_round_trips_system_config() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 20;
    config.pulsePerMl = 0.62f;
    config.calibrationTargetsMl[0] = 1500;
    config.calibrationTargetsMl[1] = 7500;
    config.calibrationTargetsMl[2] = 10000;
    config.calibrationTargetsMl[3] = 0;
    config.beepEnabled = false;
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
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.62f, loaded.pulsePerMl);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.calibrationTargetsMl[0]);
    TEST_ASSERT_EQUAL_UINT32(7500, loaded.calibrationTargetsMl[1]);
    TEST_ASSERT_EQUAL_UINT32(10000, loaded.calibrationTargetsMl[2]);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.calibrationTargetsMl[3]);
    TEST_ASSERT_FALSE(loaded.beepEnabled);
    TEST_ASSERT_TRUE(loaded.presets[2].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(PresetType::Time), static_cast<std::uint8_t>(loaded.presets[2].type));
    TEST_ASSERT_EQUAL_UINT32(120, loaded.presets[2].value);
    TEST_ASSERT_EQUAL_STRING("Tea", loaded.presets[2].name);
    TEST_ASSERT_TRUE(loaded.filters[1].enabled);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[1].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(365, loaded.filters[1].maxDays);
    TEST_ASSERT_EQUAL_UINT32(2000000, loaded.filters[1].lifeMl);
    TEST_ASSERT_EQUAL_UINT32(1714502400, loaded.filters[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(123456, loaded.filters[1].usedMl);
}

void test_config_load_sanitizes_stored_values() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 1;
    config.pulsePerMl = 99.0f;
    config.calibrationTargetsMl[0] = 0;
    config.calibrationTargetsMl[1] = 0;
    config.calibrationTargetsMl[2] = 0;
    config.calibrationTargetsMl[3] = 0;
    config.presets[0].value = 1;

    TEST_ASSERT_TRUE(store.saveSystemConfig(config));

    const SystemConfig loaded = store.loadSystemConfig();
    TEST_ASSERT_EQUAL_UINT32(3, loaded.confirmTimeoutSec);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kMaxPulsePerMl, loaded.pulsePerMl);
    TEST_ASSERT_EQUAL_UINT32(1500, loaded.calibrationTargetsMl[0]);
    TEST_ASSERT_EQUAL_UINT32(7500, loaded.calibrationTargetsMl[1]);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.calibrationTargetsMl[2]);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.calibrationTargetsMl[3]);
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

void test_filter_runtime_round_trips_start_and_used_only() {
    FakeConfigBackend backend;
    ConfigStore store(backend);
    SystemConfig config = makeDefaultConfig();
    config.filters[0].startTime = 111;
    config.filters[0].usedMl = 4000000000UL;
    config.filters[1].startTime = 222;
    config.filters[1].usedMl = 333;

    TEST_ASSERT_TRUE(store.saveFilterRuntime(config.filters));

    SystemConfig loaded = makeDefaultConfig();
    store.loadFilterRuntime(loaded.filters);
    TEST_ASSERT_EQUAL_UINT32(111, loaded.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(4000000000UL, loaded.filters[0].usedMl);
    TEST_ASSERT_EQUAL_UINT32(222, loaded.filters[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(333, loaded.filters[1].usedMl);
    TEST_ASSERT_EQUAL_STRING("第1级滤芯", loaded.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(180, loaded.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.filters[0].lifeMl);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_config_load_uses_defaults_without_matching_version);
    RUN_TEST(test_config_save_and_load_round_trips_system_config);
    RUN_TEST(test_config_load_sanitizes_stored_values);
    RUN_TEST(test_config_reset_restores_defaults);
    RUN_TEST(test_config_save_reports_backend_failures);
    RUN_TEST(test_statistics_runtime_round_trips_uint32_values);
    RUN_TEST(test_statistics_runtime_rolls_loaded_periods);
    RUN_TEST(test_filter_runtime_round_trips_start_and_used_only);
    return UNITY_END();
}
