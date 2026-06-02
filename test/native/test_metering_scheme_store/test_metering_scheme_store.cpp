#include <unity.h>

#include "app/ConfigStore.h"
#include "app/MeteringSchemeStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

constexpr std::uint32_t kTestMeteringSchemeStoreMagic = 0x314D5346UL;

struct LegacyMeteringParametersV1 {
    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t stablePulsePerLiter;
};

struct LegacyMeteringSchemeRecordV1 {
    std::uint32_t id = 0;
    bool valid = false;
    bool enabled = false;
    char name[kMeteringSchemeNameLength]{};
    char meterLabel[kMeteringSchemeMeterLabelLength]{};
    char installationLabel[kMeteringSchemeInstallationLabelLength]{};
    char conditionLabel[kMeteringSchemeConditionLabelLength]{};
    char userNote[kMeteringSchemeUserNoteLength]{};
    LegacyMeteringParametersV1 params{};
    MeteringSchemeSource sourceType = MeteringSchemeSource::Default;
    std::uint32_t revision = 0;
    std::uint32_t createdAt = 0;
    std::uint32_t updatedAt = 0;
    std::uint32_t lastActivatedAt = 0;
    std::uint32_t useCount = 0;
    std::uint32_t lastUsedAt = 0;
    bool usageStatsDirty = false;
    std::uint16_t sampleCount = 0;
    std::uint32_t sampleTraceIds[kMeteringSchemeTraceIdCapacity]{};
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    float maxErrorPercent = 0.0f;
    std::uint32_t startupDurationMinSec = 0;
    std::uint32_t startupDurationMaxSec = 0;
    std::uint32_t startupDurationMedianSec = 0;
    std::uint32_t startupDurationAvgSec = 0;
    char creationSummary[kMeteringSchemeSummaryLength]{};
    char lastModifiedSummary[kMeteringSchemeSummaryLength]{};
};

struct LegacyMeteringSchemeCandidateV1 {
    bool ready = false;
    LegacyMeteringParametersV1 params{};
    std::uint32_t generatedAt = 0;
    std::uint16_t sampleCount = 0;
    std::uint32_t sampleTraceIds[kMeteringSchemeTraceIdCapacity]{};
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    float maxErrorPercent = 0.0f;
    std::uint32_t startupDurationMinSec = 0;
    std::uint32_t startupDurationMaxSec = 0;
    std::uint32_t startupDurationMedianSec = 0;
    std::uint32_t startupDurationAvgSec = 0;
    char creationSummary[kMeteringSchemeSummaryLength]{};
};

std::uint32_t testHeaderChecksum(MeteringSchemeStoreHeader header) {
    header.checksum = 0;
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&header);
    std::uint32_t sum = 2166136261UL;
    for (std::size_t i = 0; i < sizeof(MeteringSchemeStoreHeader); ++i) {
        sum ^= bytes[i];
        sum *= 16777619UL;
    }
    return sum;
}

MeteringSchemeStoreHeader legacyHeader(std::uint32_t activeSchemeId,
                                       std::uint32_t nextSchemeId,
                                       std::uint32_t slotCount) {
    MeteringSchemeStoreHeader header{
        kTestMeteringSchemeStoreMagic,
        1,
        static_cast<std::uint16_t>(sizeof(MeteringSchemeStoreHeader)),
        static_cast<std::uint16_t>(sizeof(LegacyMeteringSchemeRecordV1)),
        static_cast<std::uint16_t>(sizeof(LegacyMeteringSchemeCandidateV1)),
        activeSchemeId,
        nextSchemeId,
        slotCount,
        0,
        0,
    };
    header.checksum = testHeaderChecksum(header);
    return header;
}

MeteringSchemeStoreHeader v2Header(std::uint32_t activeSchemeId,
                                   std::uint32_t nextSchemeId,
                                   std::uint32_t slotCount) {
    MeteringSchemeStoreHeader header{
        kTestMeteringSchemeStoreMagic,
        2,
        static_cast<std::uint16_t>(sizeof(MeteringSchemeStoreHeader)),
        static_cast<std::uint16_t>(sizeof(MeteringSchemeRecord)),
        static_cast<std::uint16_t>(sizeof(MeteringSchemeCandidate)),
        activeSchemeId,
        nextSchemeId,
        slotCount,
        0,
        0,
    };
    header.checksum = testHeaderChecksum(header);
    return header;
}

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    bool failWrite = false;
    std::string failWritePath;
    std::string failWriteAtPath;
    int failWriteAtCount = 0;
    std::size_t createSizedCalls = 0;
    std::size_t removeCalls = 0;

    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        ++createSizedCalls;
        if (failWrite || !path || shouldFailAnyWrite(path)) {
            return false;
        }
        files[path] = std::vector<std::uint8_t>(size, 0);
        return true;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        if (failWrite || !path || shouldFailAnyWrite(path) || (!data && len > 0)) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        const std::size_t oldSize = file.size();
        file.resize(oldSize + len, 0);
        if (len > 0) {
            std::memcpy(file.data() + oldSize, data, len);
        }
        return true;
    }

    bool readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) override {
        if (!out) {
            return false;
        }
        const auto it = files.find(path ? path : "");
        if (it == files.end() || offset + len > it->second.size()) {
            return false;
        }
        std::memcpy(out, it->second.data() + offset, len);
        return true;
    }

    bool writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) override {
        if (failWrite || !path || shouldFailAnyWrite(path) || shouldFailWriteAt(path)) {
            return false;
        }
        if (failWriteAtCount > 0) {
            --failWriteAtCount;
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        if (offset + len > file.size()) {
            file.resize(offset + len, 0);
        }
        if (data && len > 0) {
            std::memcpy(file.data() + offset, data, len);
        }
        return true;
    }

    bool removeFile(const char* path) override {
        ++removeCalls;
        files.erase(path ? path : "");
        return true;
    }

    void overwriteByte(const char* path, std::size_t offset, std::uint8_t value) {
        std::vector<std::uint8_t>& file = files[path ? path : ""];
        if (offset >= file.size()) {
            file.resize(offset + 1, 0);
        }
        file[offset] = value;
    }

private:
    bool shouldFailAnyWrite(const char* path) const {
        return path && !failWritePath.empty() && failWritePath == path;
    }

    bool shouldFailWriteAt(const char* path) const {
        return path && !failWriteAtPath.empty() && failWriteAtPath == path;
    }

    std::map<std::string, std::vector<std::uint8_t>> files;
};

class FakeConfigBackend : public ConfigBackend {
public:
    bool setInt(const char* ns, const char* key, std::int32_t value) override {
        ints[makeKey(ns, key)] = value;
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
        (void)ns;
        ints.clear();
        bools.clear();
        strings.clear();
        return true;
    }

private:
    std::string makeKey(const char* ns, const char* key) const {
        return std::string(ns ? ns : "") + "/" + (key ? key : "");
    }

    std::map<std::string, std::int32_t> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, std::string> strings;
};

MeteringSchemeCandidate candidate() {
    MeteringSchemeCandidate out{};
    out.ready = true;
    out.params = MeteringParameters{40, 553, 222};
    out.generatedAt = 1770000000;
    out.sampleCount = 3;
    out.sampleTraceIds[0] = 101;
    out.sampleTraceIds[1] = 102;
    out.sampleTraceIds[2] = 103;
    out.minActualMl = 1500;
    out.maxActualMl = 7500;
    out.maxErrorMl = 28;
    out.maxErrorPercent = 1.8f;
    out.startupDurationMinSec = 4;
    out.startupDurationMaxSec = 6;
    out.startupDurationMedianSec = 5;
    out.startupDurationAvgSec = 5;
    std::strncpy(out.creationSummary, "样本数量 3，最大误差 28ml", sizeof(out.creationSummary) - 1);
    return out;
}

}  // namespace

void test_begin_initializes_default_scheme_file() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");

    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.ready());
    TEST_ASSERT_EQUAL_UINT32(1, store.activeSchemeId());
    TEST_ASSERT_TRUE(backend.exists("/schemes.bin"));

    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(store.activeScheme(active));
    TEST_ASSERT_EQUAL_STRING("YF-S201 默认计量方案", active.name);
    TEST_ASSERT_EQUAL_STRING("YF-S201", active.meterLabel);
    TEST_ASSERT_TRUE(active.enabled);
    TEST_ASSERT_EQUAL_UINT32(8, active.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(36, active.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(225, active.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, active.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, active.params.stableFlowMlPerMin);
    TEST_ASSERT_NOT_NULL(std::strstr(active.creationSummary, "YF-S201"));
    TEST_ASSERT_NOT_NULL(std::strstr(active.creationSummary, "启动约 5 秒"));
}

void test_begin_upgrades_single_legacy_default_scheme_to_yfs201_builtin_default() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());

        MeteringSchemeRecord legacy{};
        TEST_ASSERT_TRUE(store.activeScheme(legacy));
        std::strncpy(legacy.name, "默认计量方案", sizeof(legacy.name) - 1);
        legacy.meterLabel[0] = '\0';
        legacy.conditionLabel[0] = '\0';
        legacy.params = MeteringParameters{0, 0, 450};
        TEST_ASSERT_TRUE(store.updateScheme(legacy, 1770000000));
    }

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());

    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(loaded.activeScheme(active));
    TEST_ASSERT_EQUAL_UINT32(1, loaded.activeSchemeId());
    TEST_ASSERT_EQUAL_UINT32(1, active.id);
    TEST_ASSERT_EQUAL_STRING("YF-S201 默认计量方案", active.name);
    TEST_ASSERT_EQUAL_STRING("YF-S201", active.meterLabel);
    TEST_ASSERT_EQUAL_UINT32(8, active.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(36, active.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(225, active.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, active.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, active.params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Default),
                            static_cast<unsigned>(active.sourceType));
    TEST_ASSERT_TRUE(active.enabled);
}

void test_save_candidate_reloads_after_restart() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        MeteringSchemeCandidate generated = candidate();
        TEST_ASSERT_TRUE(store.saveCandidate(generated));
        std::uint32_t newId = 0;
        TEST_ASSERT_TRUE(store.saveCandidateAsNew("低压实验", 1770000100, newId));
        TEST_ASSERT_EQUAL_UINT32(2, newId);
    }

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_EQUAL_UINT32(1, loaded.activeSchemeId());
    MeteringSchemeRecord list[4]{};
    TEST_ASSERT_EQUAL_size_t(2, loaded.list(list, 4, true));
    TEST_ASSERT_EQUAL_UINT32(2, list[1].id);
    TEST_ASSERT_EQUAL_STRING("低压实验", list[1].name);
    TEST_ASSERT_EQUAL_UINT32(553, list[1].params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(5000, list[1].params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, list[1].params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(5, list[1].startupDurationAvgSec);
}

void test_manual_create_reuses_deleted_unused_slot_with_new_id() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t firstId = 0;
    std::uint32_t secondId = 0;
    TEST_ASSERT_TRUE(store.createManual("实验一", MeteringParameters{12, 180, 360, 7000, 900}, 1770000000, firstId));
    TEST_ASSERT_TRUE(store.createManual("实验二", MeteringParameters{13, 190, 370}, 1770000001, secondId));
    TEST_ASSERT_TRUE(store.deleteScheme(firstId));

    MeteringSchemeRecord removed{};
    TEST_ASSERT_FALSE(store.findById(firstId, removed));

    std::uint32_t thirdId = 0;
    TEST_ASSERT_TRUE(store.createManual("实验三", MeteringParameters{14, 200, 380}, 1770000002, thirdId));
    TEST_ASSERT_NOT_EQUAL(firstId, thirdId);
    TEST_ASSERT_EQUAL_UINT32(secondId + 1, thirdId);

    MeteringSchemeRecord created{};
    TEST_ASSERT_TRUE(store.findById(thirdId, created));
    TEST_ASSERT_EQUAL_STRING("实验三", created.name);
    TEST_ASSERT_EQUAL_UINT32(5000, created.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, created.params.stableFlowMlPerMin);
}

void test_used_scheme_delete_is_rejected_and_disable_is_explicit() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("已使用", MeteringParameters{12, 180, 360}, 1770000000, id));
    TEST_ASSERT_TRUE(store.incrementUsageAfterRecordWrite(id, 1770000010));

    TEST_ASSERT_FALSE(store.deleteScheme(id));
    TEST_ASSERT_TRUE(store.disableScheme(id, 1770000020));

    MeteringSchemeRecord scheme{};
    TEST_ASSERT_TRUE(store.findById(id, scheme));
    TEST_ASSERT_TRUE(scheme.valid);
    TEST_ASSERT_FALSE(scheme.enabled);
    TEST_ASSERT_EQUAL_UINT32(1, scheme.useCount);

    TEST_ASSERT_TRUE(store.enableScheme(id, 1770000030));
    TEST_ASSERT_TRUE(store.findById(id, scheme));
    TEST_ASSERT_TRUE(scheme.enabled);
    TEST_ASSERT_EQUAL_UINT32(id, store.activeSchemeId());
    TEST_ASSERT_EQUAL_UINT32(1770000030, scheme.updatedAt);
    TEST_ASSERT_EQUAL_UINT32(1770000030, scheme.lastActivatedAt);
}

void test_delete_keeps_at_least_one_valid_scheme() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("临时方案", MeteringParameters{12, 180, 360}, 1770000000, id));
    TEST_ASSERT_TRUE(store.enableScheme(id, 1770000010));

    TEST_ASSERT_TRUE(store.deleteScheme(1));
    TEST_ASSERT_FALSE(store.deleteScheme(id));

    MeteringSchemeRecord list[4]{};
    TEST_ASSERT_EQUAL_size_t(1, store.list(list, 4, true));
    TEST_ASSERT_EQUAL_UINT32(id, store.activeSchemeId());
    TEST_ASSERT_EQUAL_UINT32(id, list[0].id);
    TEST_ASSERT_TRUE(list[0].valid);
    TEST_ASSERT_TRUE(list[0].enabled);
}

void test_enable_updates_active_id_and_last_activated() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("新方案", MeteringParameters{12, 180, 360}, 1770000000, id));
    TEST_ASSERT_TRUE(store.enableScheme(id, 1770000030));

    TEST_ASSERT_EQUAL_UINT32(id, store.activeSchemeId());
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(store.activeScheme(active));
    TEST_ASSERT_EQUAL_UINT32(id, active.id);
    TEST_ASSERT_EQUAL_UINT32(1770000030, active.lastActivatedAt);
}

void test_increment_usage_marks_dirty_when_record_update_fails() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("待统计", MeteringParameters{12, 180, 360}, 1770000000, id));
    backend.failWriteAtCount = 1;

    TEST_ASSERT_FALSE(store.incrementUsageAfterRecordWrite(id, 1770000040));

    MeteringSchemeRecord scheme{};
    TEST_ASSERT_TRUE(store.findById(id, scheme));
    TEST_ASSERT_EQUAL_UINT32(0, scheme.useCount);
    TEST_ASSERT_EQUAL_UINT32(0, scheme.lastUsedAt);
    TEST_ASSERT_TRUE(scheme.usageStatsDirty);
    TEST_ASSERT_FALSE(store.deleteScheme(id));
}

void test_migrates_legacy_config_slots_and_candidate_once() {
    MemoryFileBackend files;
    FakeConfigBackend config;
    config.setInt("faucet_cfg", "active_ms", 1);
    config.setBool("faucet_cfg", "ms0_valid", true);
    config.setStr("faucet_cfg", "ms0_name", "默认旧槽");
    config.setInt("faucet_cfg", "ms0_sp", 0);
    config.setInt("faucet_cfg", "ms0_sv", 0);
    config.setInt("faucet_cfg", "ms0_pl", kDefaultStablePulsePerLiter);
    config.setBool("faucet_cfg", "ms1_valid", true);
    config.setStr("faucet_cfg", "ms1_name", "低压实验");
    config.setInt("faucet_cfg", "ms1_sp", 40);
    config.setInt("faucet_cfg", "ms1_sv", 553);
    config.setInt("faucet_cfg", "ms1_pl", 222);
    config.setStr("faucet_cfg", "ms1_create", "旧参数槽样本说明");
    config.setInt("faucet_cfg", "ms1_mod_at", 1770000001);
    config.setBool("faucet_cfg", "ms2_valid", true);
    config.setStr("faucet_cfg", "ms2_name", "参数槽 3");
    config.setInt("faucet_cfg", "ms2_sp", 0);
    config.setInt("faucet_cfg", "ms2_sv", 0);
    config.setInt("faucet_cfg", "ms2_pl", kDefaultStablePulsePerLiter);
    config.setBool("faucet_cfg", "mc_ready", true);
    config.setInt("faucet_cfg", "mc_sp", 41);
    config.setInt("faucet_cfg", "mc_sv", 520);
    config.setInt("faucet_cfg", "mc_pl", 224);
    config.setStr("faucet_cfg", "mc_note", "旧候选说明");
    config.setInt("faucet_cfg", "mc_at", 1770000100);

    MeteringSchemeStore store(files, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.migrateLegacyFromConfig(config, 1770000200));

    MeteringSchemeRecord list[4]{};
    TEST_ASSERT_EQUAL_size_t(1, store.list(list, 4, true));
    TEST_ASSERT_EQUAL_UINT32(1, store.activeSchemeId());
    TEST_ASSERT_EQUAL_STRING("低压实验", list[0].name);
    TEST_ASSERT_EQUAL_UINT32(40, list[0].params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, list[0].params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, list[0].params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, list[0].params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, list[0].params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Migrated),
                            static_cast<unsigned>(list[0].sourceType));
    TEST_ASSERT_EQUAL_UINT32(0, list[0].useCount);
    TEST_ASSERT_EQUAL_UINT32(1, list[0].revision);
    TEST_ASSERT_NOT_NULL(std::strstr(list[0].creationSummary, "旧参数槽样本说明"));

    MeteringSchemeCandidate migratedCandidate{};
    TEST_ASSERT_TRUE(store.loadCandidate(migratedCandidate));
    TEST_ASSERT_TRUE(migratedCandidate.ready);
    TEST_ASSERT_EQUAL_UINT32(41, migratedCandidate.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(520, migratedCandidate.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(224, migratedCandidate.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, migratedCandidate.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, migratedCandidate.params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(1770000100, migratedCandidate.generatedAt);
    TEST_ASSERT_NOT_NULL(std::strstr(migratedCandidate.creationSummary, "旧候选说明"));

    TEST_ASSERT_TRUE(store.migrateLegacyFromConfig(config, 1770000300));
    TEST_ASSERT_EQUAL_size_t(1, store.list(list, 4, true));
}

void test_begin_migrates_v1_scheme_file_to_time_estimate_params() {
    MemoryFileBackend backend;
    const MeteringSchemeStoreHeader header = legacyHeader(7, 8, 1);
    LegacyMeteringSchemeCandidateV1 candidate{};
    candidate.ready = true;
    candidate.params = LegacyMeteringParametersV1{41, 520, 224};
    candidate.generatedAt = 1770000100;
    std::strncpy(candidate.creationSummary, "旧候选", sizeof(candidate.creationSummary) - 1);
    LegacyMeteringSchemeRecordV1 record{};
    record.id = 7;
    record.valid = true;
    record.enabled = true;
    std::strncpy(record.name, "旧文件方案", sizeof(record.name) - 1);
    record.params = LegacyMeteringParametersV1{40, 553, 222};
    record.sourceType = MeteringSchemeSource::Manual;
    record.revision = 3;
    record.createdAt = 1770000000;
    record.updatedAt = 1770000001;
    record.lastActivatedAt = 1770000002;
    record.sampleCount = 2;
    std::strncpy(record.creationSummary, "旧文件记录", sizeof(record.creationSummary) - 1);

    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin", 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     sizeof(MeteringSchemeStoreHeader),
                                     reinterpret_cast<const std::uint8_t*>(&candidate),
                                     sizeof(candidate)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     sizeof(MeteringSchemeStoreHeader) + sizeof(candidate),
                                     reinterpret_cast<const std::uint8_t*>(&record),
                                     sizeof(record)));
    const std::size_t createCalls = backend.createSizedCalls;

    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_GREATER_THAN_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_UINT32(7, store.activeSchemeId());

    MeteringSchemeRecord migrated{};
    TEST_ASSERT_TRUE(store.activeScheme(migrated));
    TEST_ASSERT_EQUAL_STRING("旧文件方案", migrated.name);
    TEST_ASSERT_EQUAL_UINT32(40, migrated.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, migrated.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, migrated.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, migrated.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, migrated.params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(3, migrated.revision);
    TEST_ASSERT_EQUAL_UINT16(2, migrated.sampleCount);

    MeteringSchemeCandidate migratedCandidate{};
    TEST_ASSERT_TRUE(store.loadCandidate(migratedCandidate));
    TEST_ASSERT_TRUE(migratedCandidate.ready);
    TEST_ASSERT_EQUAL_UINT32(41, migratedCandidate.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(520, migratedCandidate.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(224, migratedCandidate.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, migratedCandidate.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, migratedCandidate.params.stableFlowMlPerMin);
}

void test_begin_recovers_v1_scheme_migration_from_completed_temp_file() {
    MemoryFileBackend backend;
    const MeteringSchemeStoreHeader header = legacyHeader(7, 8, 1);
    LegacyMeteringSchemeCandidateV1 candidate{};
    LegacyMeteringSchemeRecordV1 record{};
    record.id = 7;
    record.valid = true;
    record.enabled = true;
    std::strncpy(record.name, "旧文件方案", sizeof(record.name) - 1);
    record.params = LegacyMeteringParametersV1{40, 553, 222};
    record.sourceType = MeteringSchemeSource::Manual;
    record.revision = 3;

    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin", 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     sizeof(MeteringSchemeStoreHeader),
                                     reinterpret_cast<const std::uint8_t*>(&candidate),
                                     sizeof(candidate)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     sizeof(MeteringSchemeStoreHeader) + sizeof(candidate),
                                     reinterpret_cast<const std::uint8_t*>(&record),
                                     sizeof(record)));

    backend.failWriteAtPath = "/schemes.bin";
    {
        MeteringSchemeStore interrupted(backend, "/schemes.bin");
        TEST_ASSERT_FALSE(interrupted.begin());
    }
    backend.failWriteAtPath.clear();

    MeteringSchemeStore recovered(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(recovered.begin());
    MeteringSchemeRecord migrated{};
    TEST_ASSERT_TRUE(recovered.activeScheme(migrated));
    TEST_ASSERT_EQUAL_STRING("旧文件方案", migrated.name);
    TEST_ASSERT_EQUAL_UINT32(5000, migrated.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, migrated.params.stableFlowMlPerMin);
}

void test_begin_ignores_stale_scheme_migration_temp_when_current_file_is_valid() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        std::uint32_t id = 0;
        TEST_ASSERT_TRUE(store.createManual("当前方案", MeteringParameters{12, 180, 360}, 1770000000, id));
        TEST_ASSERT_TRUE(store.enableScheme(id, 1770000001));
    }

    const MeteringSchemeStoreHeader header = v2Header(99, 100, 1);
    MeteringSchemeCandidate candidate{};
    MeteringSchemeRecord stale{};
    initializeManualMeteringScheme(stale, 99, "陈旧临时方案", MeteringParameters{13, 190, 370}, 1770000000);
    TEST_ASSERT_TRUE(backend.createSized("/schemes.bin.tmp",
                                         sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate) +
                                             sizeof(MeteringSchemeRecord)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin.tmp",
                                     0,
                                     reinterpret_cast<const std::uint8_t*>(&header),
                                     sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin.tmp",
                                     sizeof(MeteringSchemeStoreHeader),
                                     reinterpret_cast<const std::uint8_t*>(&candidate),
                                     sizeof(candidate)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin.tmp",
                                     sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate),
                                     reinterpret_cast<const std::uint8_t*>(&stale),
                                     sizeof(stale)));

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(loaded.activeScheme(active));
    TEST_ASSERT_EQUAL_STRING("当前方案", active.name);
}

void test_legacy_migration_avoids_large_metering_scheme_stack_arrays() {
    FILE* file = std::fopen("src/app/MeteringSchemeStore.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[90000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* migration = std::strstr(buffer, "bool MeteringSchemeStore::migrateLegacyFromConfig");
    TEST_ASSERT_NOT_NULL(migration);
    const char* nextFunction = std::strstr(migration, "bool MeteringSchemeStore::validPath()");
    TEST_ASSERT_NOT_NULL(nextFunction);

    const char* existingAllocation =
        std::strstr(migration, "new (std::nothrow) MeteringSchemeRecord[2]");
    const char* migratedAllocation =
        std::strstr(migration, "new (std::nothrow) MeteringSchemeRecord[kLegacyMeteringSlotCount]");
    const char* existingStackArray = std::strstr(migration, "MeteringSchemeRecord existing[2]");
    const char* migratedStackArray = std::strstr(migration, "MeteringSchemeRecord migrated[kLegacyMeteringSlotCount]");
    TEST_ASSERT_NOT_NULL(existingAllocation);
    TEST_ASSERT_NOT_NULL(migratedAllocation);
    TEST_ASSERT_TRUE(existingAllocation < nextFunction);
    TEST_ASSERT_TRUE(migratedAllocation < nextFunction);
    TEST_ASSERT_TRUE(existingStackArray == nullptr || existingStackArray > nextFunction);
    TEST_ASSERT_TRUE(migratedStackArray == nullptr || migratedStackArray > nextFunction);
}

void test_begin_preserves_corrupt_scheme_file_without_reinitializing() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
    }
    const std::size_t createCalls = backend.createSizedCalls;
    backend.overwriteByte("/schemes.bin", 0, 0x00);

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_FALSE(loaded.begin());
    TEST_ASSERT_FALSE(loaded.ready());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_begin_initializes_default_scheme_file);
    RUN_TEST(test_begin_upgrades_single_legacy_default_scheme_to_yfs201_builtin_default);
    RUN_TEST(test_save_candidate_reloads_after_restart);
    RUN_TEST(test_manual_create_reuses_deleted_unused_slot_with_new_id);
    RUN_TEST(test_used_scheme_delete_is_rejected_and_disable_is_explicit);
    RUN_TEST(test_delete_keeps_at_least_one_valid_scheme);
    RUN_TEST(test_enable_updates_active_id_and_last_activated);
    RUN_TEST(test_increment_usage_marks_dirty_when_record_update_fails);
    RUN_TEST(test_migrates_legacy_config_slots_and_candidate_once);
    RUN_TEST(test_begin_migrates_v1_scheme_file_to_time_estimate_params);
    RUN_TEST(test_begin_recovers_v1_scheme_migration_from_completed_temp_file);
    RUN_TEST(test_begin_ignores_stale_scheme_migration_temp_when_current_file_is_valid);
    RUN_TEST(test_legacy_migration_avoids_large_metering_scheme_stack_arrays);
    RUN_TEST(test_begin_preserves_corrupt_scheme_file_without_reinitializing);
    return UNITY_END();
}
