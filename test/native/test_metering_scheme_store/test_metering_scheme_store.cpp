#include <unity.h>

#include "app/ConfigStore.h"
#include "app/MeteringSchemeStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    bool failWrite = false;
    int failWriteAtCount = 0;

    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        if (failWrite || !path) {
            return false;
        }
        files[path] = std::vector<std::uint8_t>(size, 0);
        return true;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        if (failWrite || !path || (!data && len > 0)) {
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
        if (failWrite || !path) {
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
        files.erase(path ? path : "");
        return true;
    }

private:
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
    TEST_ASSERT_EQUAL_STRING("默认计量方案", active.name);
    TEST_ASSERT_TRUE(active.enabled);
    TEST_ASSERT_EQUAL_UINT32(kDefaultStablePulsePerLiter, active.params.stablePulsePerLiter);
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
    TEST_ASSERT_EQUAL_UINT32(5, list[1].startupDurationAvgSec);
}

void test_manual_create_reuses_deleted_unused_slot_with_new_id() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t firstId = 0;
    std::uint32_t secondId = 0;
    TEST_ASSERT_TRUE(store.createManual("实验一", MeteringParameters{12, 180, 360}, 1770000000, firstId));
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
    TEST_ASSERT_EQUAL_UINT32(1770000100, migratedCandidate.generatedAt);
    TEST_ASSERT_NOT_NULL(std::strstr(migratedCandidate.creationSummary, "旧候选说明"));

    TEST_ASSERT_TRUE(store.migrateLegacyFromConfig(config, 1770000300));
    TEST_ASSERT_EQUAL_size_t(1, store.list(list, 4, true));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_begin_initializes_default_scheme_file);
    RUN_TEST(test_save_candidate_reloads_after_restart);
    RUN_TEST(test_manual_create_reuses_deleted_unused_slot_with_new_id);
    RUN_TEST(test_used_scheme_delete_is_rejected_and_disable_is_explicit);
    RUN_TEST(test_enable_updates_active_id_and_last_activated);
    RUN_TEST(test_increment_usage_marks_dirty_when_record_update_fails);
    RUN_TEST(test_migrates_legacy_config_slots_and_candidate_once);
    return UNITY_END();
}
