#include <unity.h>

#define ESP32BASE_WEB_NATIVE_TEST 1

#include "app/AppController.h"
#include "app/ConfigStore.h"
#include "app/WaterRecordCalibrationStore.h"
#include "app/WaterRecordMeteringSnapshotStore.h"
#include "web/FaucetWeb.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../../../Esp32Base/src/web/Esp32BaseWeb.cpp"
#include "../../../src/web/FaucetWeb.cpp"

using namespace faucet;

namespace {

class FakeConfigBackend : public ConfigBackend {
public:
    std::uint32_t filterRuntimeWrites = 0;

    bool setInt(const char* ns, const char* key, std::int32_t value) override {
        ints[makeKey(ns, key)] = value;
        countFilterRuntimeWrite(ns);
        return true;
    }

    std::int32_t getInt(const char* ns, const char* key, std::int32_t def) override {
        const auto it = ints.find(makeKey(ns, key));
        return it == ints.end() ? def : it->second;
    }

    bool setBool(const char* ns, const char* key, bool value) override {
        bools[makeKey(ns, key)] = value;
        return true;
    }

    bool getBool(const char* ns, const char* key, bool def) override {
        const auto it = bools.find(makeKey(ns, key));
        return it == bools.end() ? def : it->second;
    }

    bool setStr(const char* ns, const char* key, const char* value) override {
        strings[makeKey(ns, key)] = value ? value : "";
        countFilterRuntimeWrite(ns);
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
        const std::string prefix = std::string(ns ? ns : "") + "/";
        erasePrefix(ints, prefix);
        erasePrefix(bools, prefix);
        erasePrefix(strings, prefix);
        return true;
    }

private:
    static std::string makeKey(const char* ns, const char* key) {
        return std::string(ns ? ns : "") + "/" + (key ? key : "");
    }

    void countFilterRuntimeWrite(const char* ns) {
        if (std::strcmp(ns ? ns : "", "faucet_run") == 0) {
            ++filterRuntimeWrites;
        }
    }

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

    std::map<std::string, std::int32_t> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, std::string> strings;
};

class MemoryRecordWriter : public WaterRecordWriter {
public:
    bool append(const WaterRecord& record) override {
        records.push_back(record);
        return true;
    }

    std::vector<WaterRecord> records;
};

AppTickInput appInput(ButtonLevels levels, std::uint32_t nowMs, std::uint32_t nowUs) {
    return AppTickInput{
        levels,
        nowMs,
        nowUs,
        1714502400,
        {20260506, 202619, 202605},
        true,
        true,
        7,
    };
}

void pressAndReleaseOk(AppController& app, std::uint32_t baseMs) {
    app.tick(appInput({false, true, false, false}, baseMs, baseMs * 1000UL));
    app.tick(appInput({false, true, false, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL));
    app.tick(appInput({false, false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL));
    app.tick(appInput({false, false, false, false}, baseMs + 60 + kButtonDebounceMs,
                      (baseMs + 60 + kButtonDebounceMs) * 1000UL));
}

void setRunning(AppController& app) {
    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

void applyTestMeteringScheme(AppController& app) {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(scheme, 99, "native", MeteringParameters{0, 0, 1000}, 1714502300);
    TEST_ASSERT_TRUE(app.applyActiveMeteringScheme(scheme));
}

std::uint32_t testNowSeconds() {
    return 1714502400UL;
}

std::uint32_t testBootId() {
    return 7UL;
}

struct WebFixture {
    SystemConfig config = makeDefaultConfig();
    FakeConfigBackend backend;
    ConfigStore configStore{backend};
    StatisticsStore statistics;
    FilterStore filters{config.filters};
    WaterRecord recordsStorage[4]{};
    WaterRecordStore records{recordsStorage, 4};
    WaterRecordCalibration calibrationsStorage[4]{};
    WaterRecordCalibrationStore calibrations{calibrationsStorage, 4};
    WaterRecordMeteringSnapshot snapshotsStorage[4]{};
    WaterRecordMeteringSnapshotStore snapshots{snapshotsStorage, 4};
    MemoryRecordWriter recordWriter;
    AppController app{config, statistics, filters, recordWriter};

    WebFixture() {
        applyTestMeteringScheme(app);
        FaucetWebContext context{};
        context.config = &config;
        context.configStore = &configStore;
        context.app = &app;
        context.filters = &filters;
        context.records = &records;
        context.recordCalibrations = &calibrations;
        context.recordCalibrationWriter = &calibrations;
        context.recordMeteringSnapshots = &snapshots;
        context.recordMeteringSnapshotWriter = &snapshots;
        context.nowSeconds = testNowSeconds;
        context.bootId = testBootId;
        setFaucetWebContext(context);
    }
};

void registerRoutes() {
    Esp32BaseWeb::nativeTestReset();
    TEST_ASSERT_TRUE(registerFaucetWeb());
}

}  // namespace

void test_filter_reset_handler_rejects_missing_auth_before_context_work() {
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(false);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);
}

void test_filter_reset_handler_rejects_cross_origin_post() {
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(false);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(403, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("Forbidden", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_filter_reset_handler_returns_invalid_index_without_runtime_write() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("index", "999");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(400, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_index\"}", Esp32BaseWeb::nativeTestResponse().body.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, fixture.backend.filterRuntimeWrites);
}

void test_filter_reset_handler_redirects_busy_before_runtime_write() {
    WebFixture fixture;
    const std::uint32_t originalStartTime = fixture.filters.record(0).startTime;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("index", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_UINT32(originalStartTime, fixture.filters.record(0).startTime);
    TEST_ASSERT_EQUAL_UINT32(0, fixture.backend.filterRuntimeWrites);
}

void test_records_handler_redirects_trace_save_busy_before_trace_store_work() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/records");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "save");
    Esp32BaseWeb::nativeTestSetParam("trace", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/records", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/records?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_records_handler_redirects_trace_delete_busy_to_calibration_context() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/records");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "delete");
    Esp32BaseWeb::nativeTestSetParam("trace", "1");
    Esp32BaseWeb::nativeTestSetParam("from", "calibration");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/records", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_filter_reset_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_filter_reset_handler_rejects_cross_origin_post);
    RUN_TEST(test_filter_reset_handler_returns_invalid_index_without_runtime_write);
    RUN_TEST(test_filter_reset_handler_redirects_busy_before_runtime_write);
    RUN_TEST(test_records_handler_redirects_trace_save_busy_before_trace_store_work);
    RUN_TEST(test_records_handler_redirects_trace_delete_busy_to_calibration_context);
    return UNITY_END();
}
