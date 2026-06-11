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
constexpr std::size_t kLegacyMeteringSchemeMeterLabelLength = 32;
constexpr std::size_t kLegacyMeteringSchemeInstallationLabelLength = 32;
constexpr std::size_t kLegacyMeteringSchemeConditionLabelLength = 48;
constexpr std::size_t kLegacyMeteringSchemeUserNoteLength = 128;
constexpr std::size_t kLegacyMeteringSchemeSummaryLength = 192;
constexpr std::size_t kLegacyMeteringSchemeTraceIdCapacity = 12;

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
    char meterLabel[kLegacyMeteringSchemeMeterLabelLength]{};
    char installationLabel[kLegacyMeteringSchemeInstallationLabelLength]{};
    char conditionLabel[kLegacyMeteringSchemeConditionLabelLength]{};
    char userNote[kLegacyMeteringSchemeUserNoteLength]{};
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
    std::uint32_t sampleTraceIds[kLegacyMeteringSchemeTraceIdCapacity]{};
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    float maxErrorPercent = 0.0f;
    std::uint32_t startupDurationMinSec = 0;
    std::uint32_t startupDurationMaxSec = 0;
    std::uint32_t startupDurationMedianSec = 0;
    std::uint32_t startupDurationAvgSec = 0;
    char creationSummary[kLegacyMeteringSchemeSummaryLength]{};
    char lastModifiedSummary[kLegacyMeteringSchemeSummaryLength]{};
};

struct LegacyMeteringSchemeCandidateV1 {
    bool ready = false;
    LegacyMeteringParametersV1 params{};
    std::uint32_t generatedAt = 0;
    std::uint16_t sampleCount = 0;
    std::uint32_t sampleTraceIds[kLegacyMeteringSchemeTraceIdCapacity]{};
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    float maxErrorPercent = 0.0f;
    std::uint32_t startupDurationMinSec = 0;
    std::uint32_t startupDurationMaxSec = 0;
    std::uint32_t startupDurationMedianSec = 0;
    std::uint32_t startupDurationAvgSec = 0;
    char creationSummary[kLegacyMeteringSchemeSummaryLength]{};
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

MeteringSchemeStoreHeader currentHeader(std::uint32_t activeSchemeId,
                                   std::uint32_t nextSchemeId,
                                   std::uint32_t slotCount) {
    MeteringSchemeStoreHeader header{
        kTestMeteringSchemeStoreMagic,
        4,
        static_cast<std::uint16_t>(sizeof(MeteringSchemeStoreHeader)),
        static_cast<std::uint16_t>(sizeof(MeteringSchemeRecord)),
        0,
        activeSchemeId,
        nextSchemeId,
        slotCount,
        0,
        0,
    };
    header.checksum = testHeaderChecksum(header);
    return header;
}

MeteringSchemeStoreHeader legacyCandidateHeader(std::uint32_t activeSchemeId,
                                                std::uint32_t nextSchemeId,
                                                std::uint32_t slotCount) {
    MeteringSchemeStoreHeader header{
        kTestMeteringSchemeStoreMagic,
        3,
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
    bool failHeaderWriteOnce = false;
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
        if (failHeaderWriteOnce && offset == 0) {
            failHeaderWriteOnce = false;
            return false;
        }
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
    out.sourceType = MeteringSchemeSource::CalibrationSession;
    out.params = MeteringParameters{40, 553, 222};
    out.generatedAt = 1770000000;
    out.sampleCount = 3;
    out.minActualMl = 1500;
    out.maxActualMl = 7500;
    out.maxErrorMl = 28;
    out.maxErrorTenthPercent = 18;
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
    TEST_ASSERT_TRUE(active.recordUsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                            static_cast<unsigned>(active.state));
    TEST_ASSERT_EQUAL_UINT32(8, active.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(36, active.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(225, active.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, active.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, active.params.stableFlowMlPerMin);
}

void test_begin_upgrades_single_legacy_default_scheme_to_yfs201_builtin_default() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());

        MeteringSchemeRecord legacy{};
        TEST_ASSERT_TRUE(store.activeScheme(legacy));
        std::strncpy(legacy.name, "默认计量方案", sizeof(legacy.name) - 1);
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
    TEST_ASSERT_EQUAL_UINT32(8, active.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(36, active.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(225, active.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, active.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, active.params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Default),
                            static_cast<unsigned>(active.sourceType));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                            static_cast<unsigned>(active.state));
}

void test_save_candidate_as_new_reloads_after_restart() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        MeteringSchemeCandidate generated = candidate();
        std::uint32_t newId = 0;
        TEST_ASSERT_TRUE(store.saveCandidateAsNew(generated, "低压实验", 1770000100, newId));
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::CalibrationSession),
                            static_cast<unsigned>(list[1].sourceType));
    TEST_ASSERT_EQUAL_UINT32(5000, list[1].params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, list[1].params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT16(3, list[1].sampleCount);
    TEST_ASSERT_EQUAL_UINT32(1500, list[1].minActualMl);
    TEST_ASSERT_EQUAL_UINT32(7500, list[1].maxActualMl);
    TEST_ASSERT_EQUAL_UINT32(28, list[1].maxErrorMl);
    TEST_ASSERT_EQUAL_UINT16(18, list[1].maxErrorTenthPercent);
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

void test_manual_create_overwrites_oldest_non_current_when_fixed_slots_are_full() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t lastId = 0;
    for (std::size_t i = 1; i < kMeteringSchemeStoreSlotCount; ++i) {
        TEST_ASSERT_TRUE(store.createManual("填充方案", MeteringParameters{12, 180, 360}, 1770000000 + i, lastId));
    }
    TEST_ASSERT_EQUAL_UINT32(100, lastId);

    std::uint32_t overflowId = 0;
    TEST_ASSERT_TRUE(store.createManual("覆盖方案", MeteringParameters{14, 200, 380}, 1770000200, overflowId));
    TEST_ASSERT_EQUAL_UINT32(101, overflowId);

    MeteringSchemeRecord overwritten{};
    TEST_ASSERT_FALSE(store.findById(2, overwritten));
    MeteringSchemeRecord created{};
    TEST_ASSERT_TRUE(store.findById(overflowId, created));
    TEST_ASSERT_EQUAL_STRING("覆盖方案", created.name);
    TEST_ASSERT_EQUAL_UINT32(1, store.activeSchemeId());
}

void test_manual_create_prefers_disabled_non_current_slot_when_fixed_slots_are_full() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t ids[kMeteringSchemeStoreSlotCount]{};
    for (std::size_t i = 1; i < kMeteringSchemeStoreSlotCount; ++i) {
        TEST_ASSERT_TRUE(store.createManual("填充方案", MeteringParameters{12, 180, 360}, 1770000000 + i, ids[i]));
    }
    TEST_ASSERT_TRUE(store.disableScheme(ids[50], 1770000200));

    std::uint32_t overflowId = 0;
    TEST_ASSERT_TRUE(store.createManual("覆盖停用方案", MeteringParameters{14, 200, 380}, 1770000300, overflowId));

    MeteringSchemeRecord disabledVictim{};
    TEST_ASSERT_FALSE(store.findById(ids[50], disabledVictim));
    MeteringSchemeRecord smallerAvailable{};
    TEST_ASSERT_TRUE(store.findById(ids[1], smallerAvailable));
    MeteringSchemeRecord created{};
    TEST_ASSERT_TRUE(store.findById(overflowId, created));
    TEST_ASSERT_EQUAL_STRING("覆盖停用方案", created.name);
}

void test_manual_create_header_failure_rolls_back_written_record() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        std::uint32_t failedId = 0;
        backend.failHeaderWriteOnce = true;
        TEST_ASSERT_FALSE(store.createManual("header fail",
                                             MeteringParameters{12, 180, 360},
                                             1770000000,
                                             failedId));
        TEST_ASSERT_EQUAL_UINT32(2, failedId);
    }

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    MeteringSchemeRecord rolledBack{};
    TEST_ASSERT_FALSE(loaded.findById(2, rolledBack));

    std::uint32_t nextId = 0;
    TEST_ASSERT_TRUE(loaded.createManual("after restart",
                                         MeteringParameters{14, 200, 380},
                                         1770000001,
                                         nextId));
    TEST_ASSERT_EQUAL_UINT32(2, nextId);
}

void test_begin_repairs_next_scheme_id_from_existing_records() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        std::uint32_t id = 0;
        TEST_ASSERT_TRUE(store.createManual("existing",
                                            MeteringParameters{12, 180, 360},
                                            1770000000,
                                            id));
        TEST_ASSERT_EQUAL_UINT32(2, id);
    }
    const MeteringSchemeStoreHeader staleHeader =
        currentHeader(1, 2, static_cast<std::uint32_t>(kMeteringSchemeStoreSlotCount));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     0,
                                     reinterpret_cast<const std::uint8_t*>(&staleHeader),
                                     sizeof(staleHeader)));

    backend.failHeaderWriteOnce = true;
    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    MeteringSchemeRecord existing{};
    TEST_ASSERT_TRUE(loaded.findById(2, existing));

    std::uint32_t nextId = 0;
    TEST_ASSERT_TRUE(loaded.createManual("after repair",
                                         MeteringParameters{14, 200, 380},
                                         1770000001,
                                         nextId));
    TEST_ASSERT_EQUAL_UINT32(3, nextId);
    MeteringSchemeRecord schemes[kMeteringSchemeStoreSlotCount]{};
    const std::size_t count = loaded.list(schemes, kMeteringSchemeStoreSlotCount, true);
    std::size_t id2Count = 0;
    std::size_t id3Count = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (schemes[i].id == 2) {
            ++id2Count;
        }
        if (schemes[i].id == 3) {
            ++id3Count;
        }
    }
    TEST_ASSERT_EQUAL_size_t(1, id2Count);
    TEST_ASSERT_EQUAL_size_t(1, id3Count);
}

void test_used_scheme_delete_is_rejected_and_disable_is_explicit() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("已使用", MeteringParameters{12, 180, 360}, 1770000000, id));
    TEST_ASSERT_TRUE(store.markUsedAfterRecordWrite(id));
    TEST_ASSERT_TRUE(store.markUsedAfterRecordWrite(id));

    TEST_ASSERT_FALSE(store.deleteScheme(id));
    TEST_ASSERT_TRUE(store.disableScheme(id, 1770000020));

    MeteringSchemeRecord scheme{};
    TEST_ASSERT_TRUE(store.findById(id, scheme));
    TEST_ASSERT_TRUE(scheme.recordUsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Disabled),
                            static_cast<unsigned>(scheme.state));
    TEST_ASSERT_TRUE(scheme.usedEver);

    TEST_ASSERT_TRUE(store.enableScheme(id, 1770000030));
    TEST_ASSERT_TRUE(store.findById(id, scheme));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                            static_cast<unsigned>(scheme.state));
    TEST_ASSERT_EQUAL_UINT32(id, store.activeSchemeId());
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
    TEST_ASSERT_TRUE(list[0].recordUsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                            static_cast<unsigned>(list[0].state));
}

void test_enable_updates_active_id_only() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                            static_cast<unsigned>(active.state));
}

void test_migrates_legacy_config_slots_without_candidate() {
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
    config.setInt("faucet_cfg", "ms1_mod_at", 1770000001);
    config.setBool("faucet_cfg", "ms2_valid", true);
    config.setStr("faucet_cfg", "ms2_name", "参数槽 3");
    config.setInt("faucet_cfg", "ms2_sp", 0);
    config.setInt("faucet_cfg", "ms2_sv", 0);
    config.setInt("faucet_cfg", "ms2_pl", kDefaultStablePulsePerLiter);
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
    TEST_ASSERT_EQUAL_UINT32(1, list[0].revision);

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
    record.useCount = 2;
    record.sampleCount = 2;

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
    TEST_ASSERT_TRUE(migrated.usedEver);

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

void test_begin_migrates_v3_candidate_file_to_compact_v4_layout() {
    MemoryFileBackend backend;
    const MeteringSchemeStoreHeader header = legacyCandidateHeader(7, 8, 1);
    MeteringSchemeCandidate candidate{};
    candidate.ready = true;
    candidate.params = MeteringParameters{41, 520, 224, 5000, 480};
    MeteringSchemeRecord record{};
    initializeManualMeteringScheme(record, 7, "v3方案", MeteringParameters{40, 553, 222, 5000, 480}, 1770000000);
    record.sourceType = MeteringSchemeSource::Manual;
    record.revision = 3;

    TEST_ASSERT_TRUE(backend.createSized("/schemes.bin",
                                         sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate) +
                                             sizeof(MeteringSchemeRecord)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin", 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     sizeof(MeteringSchemeStoreHeader),
                                     reinterpret_cast<const std::uint8_t*>(&candidate),
                                     sizeof(candidate)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate),
                                     reinterpret_cast<const std::uint8_t*>(&record),
                                     sizeof(record)));

    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_EQUAL_INT64(static_cast<std::int64_t>(sizeof(MeteringSchemeStoreHeader) +
                                                     kMeteringSchemeStoreSlotCount * sizeof(MeteringSchemeRecord)),
                            backend.fileSize("/schemes.bin"));
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(store.activeScheme(active));
    TEST_ASSERT_EQUAL_STRING("v3方案", active.name);
    TEST_ASSERT_EQUAL_UINT32(553, active.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(7, store.activeSchemeId());
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

    const MeteringSchemeStoreHeader header = currentHeader(99, 100, 1);
    MeteringSchemeRecord stale{};
    initializeManualMeteringScheme(stale, 99, "陈旧临时方案", MeteringParameters{13, 190, 370}, 1770000000);
    TEST_ASSERT_TRUE(backend.createSized("/schemes.bin.tmp",
                                         sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeRecord)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin.tmp",
                                     0,
                                     reinterpret_cast<const std::uint8_t*>(&header),
                                     sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin.tmp",
                                     sizeof(MeteringSchemeStoreHeader),
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

void test_begin_rebuilds_corrupt_scheme_file_with_default_scheme() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
    }
    backend.overwriteByte("/schemes.bin", 0, 0x00);

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_TRUE(loaded.ready());
    TEST_ASSERT_EQUAL_size_t(1, backend.removeCalls);
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(loaded.activeScheme(active));
    TEST_ASSERT_TRUE(active.recordUsed);
    TEST_ASSERT_EQUAL_UINT32(loaded.activeSchemeId(), active.id);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_begin_initializes_default_scheme_file);
    RUN_TEST(test_begin_upgrades_single_legacy_default_scheme_to_yfs201_builtin_default);
    RUN_TEST(test_save_candidate_as_new_reloads_after_restart);
    RUN_TEST(test_manual_create_reuses_deleted_unused_slot_with_new_id);
    RUN_TEST(test_manual_create_overwrites_oldest_non_current_when_fixed_slots_are_full);
    RUN_TEST(test_manual_create_prefers_disabled_non_current_slot_when_fixed_slots_are_full);
    RUN_TEST(test_manual_create_header_failure_rolls_back_written_record);
    RUN_TEST(test_begin_repairs_next_scheme_id_from_existing_records);
    RUN_TEST(test_used_scheme_delete_is_rejected_and_disable_is_explicit);
    RUN_TEST(test_delete_keeps_at_least_one_valid_scheme);
    RUN_TEST(test_enable_updates_active_id_only);
    RUN_TEST(test_migrates_legacy_config_slots_without_candidate);
    RUN_TEST(test_begin_migrates_v1_scheme_file_to_time_estimate_params);
    RUN_TEST(test_begin_recovers_v1_scheme_migration_from_completed_temp_file);
    RUN_TEST(test_begin_migrates_v3_candidate_file_to_compact_v4_layout);
    RUN_TEST(test_begin_ignores_stale_scheme_migration_temp_when_current_file_is_valid);
    RUN_TEST(test_legacy_migration_avoids_large_metering_scheme_stack_arrays);
    RUN_TEST(test_begin_rebuilds_corrupt_scheme_file_with_default_scheme);
    return UNITY_END();
}
