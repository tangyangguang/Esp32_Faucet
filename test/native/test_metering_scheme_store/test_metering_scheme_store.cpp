#include <unity.h>

#include "app/MeteringSchemeStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

constexpr std::uint32_t kTestMeteringSchemeStoreMagic = 0x314D5346UL;
constexpr std::uint16_t kTestCurrentMeteringSchemeStoreVersion = 6;

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

MeteringSchemeStoreHeader currentHeader(std::uint32_t activeSchemeId,
                                        std::uint32_t nextSchemeId,
                                        std::uint32_t slotCount) {
    MeteringSchemeStoreHeader header{
        kTestMeteringSchemeStoreMagic,
        kTestCurrentMeteringSchemeStoreVersion,
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

MeteringSchemeStoreHeader incompatibleLegacyHeader() {
    MeteringSchemeStoreHeader header{
        kTestMeteringSchemeStoreMagic,
        4,
        static_cast<std::uint16_t>(sizeof(MeteringSchemeStoreHeader)),
        static_cast<std::uint16_t>(sizeof(MeteringSchemeRecord) + 16),
        static_cast<std::uint16_t>(sizeof(MeteringSchemeCandidate)),
        1,
        2,
        1,
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

MeteringParameters manualParams(std::uint32_t stablePulsePerLiter = 360) {
    return MeteringParameters{12, 180, stablePulsePerLiter, 7000, 900};
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
    TEST_ASSERT_FALSE(active.deleted);
    TEST_ASSERT_FALSE(active.usedEver);
    TEST_ASSERT_EQUAL_UINT32(8, active.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(130, active.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(248, active.params.stablePulsePerLiter);
}

void test_create_manual_lists_only_not_deleted_schemes_by_default() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t firstId = 0;
    std::uint32_t secondId = 0;
    TEST_ASSERT_TRUE(store.createManual("实验一", manualParams(360), 1770000000, firstId));
    TEST_ASSERT_TRUE(store.createManual("实验二", manualParams(370), 1770000001, secondId));
    TEST_ASSERT_TRUE(store.deleteScheme(firstId, 1770000002));

    MeteringSchemeRecord visible[4]{};
    TEST_ASSERT_EQUAL_size_t(2, store.list(visible, 4, false));
    TEST_ASSERT_EQUAL_UINT32(1, visible[0].id);
    TEST_ASSERT_EQUAL_UINT32(secondId, visible[1].id);

    MeteringSchemeRecord all[4]{};
    TEST_ASSERT_EQUAL_size_t(3, store.list(all, 4, true));
    TEST_ASSERT_EQUAL_UINT32(firstId, all[1].id);
    TEST_ASSERT_TRUE(all[1].deleted);

    MeteringSchemeRecord deleted{};
    TEST_ASSERT_TRUE(store.findById(firstId, deleted));
    TEST_ASSERT_TRUE(deleted.deleted);
}

void test_set_active_scheme_updates_current_id_only_for_not_deleted_scheme() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("新方案", manualParams(), 1770000000, id));
    MeteringSchemeRecord before{};
    TEST_ASSERT_TRUE(store.findById(id, before));

    TEST_ASSERT_TRUE(store.setActiveScheme(id, 1770000030));

    TEST_ASSERT_EQUAL_UINT32(id, store.activeSchemeId());
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(store.activeScheme(active));
    TEST_ASSERT_EQUAL_UINT32(id, active.id);
    TEST_ASSERT_FALSE(active.deleted);
    TEST_ASSERT_EQUAL_UINT32(before.updatedAt, active.updatedAt);

    TEST_ASSERT_TRUE(store.deleteScheme(1, 1770000040));
    TEST_ASSERT_FALSE(store.setActiveScheme(1, 1770000050));
}

void test_mark_used_after_record_write_sets_used_once() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("运行方案", manualParams(), 1770000000, id));

    MeteringSchemeRecord before{};
    TEST_ASSERT_TRUE(store.findById(id, before));
    TEST_ASSERT_FALSE(before.usedEver);

    TEST_ASSERT_TRUE(store.markUsedAfterRecordWrite(id));
    MeteringSchemeRecord after{};
    TEST_ASSERT_TRUE(store.findById(id, after));
    TEST_ASSERT_TRUE(after.usedEver);

    backend.failWrite = true;
    TEST_ASSERT_TRUE(store.markUsedAfterRecordWrite(id));
}

void test_current_scheme_cannot_be_deleted_but_any_other_scheme_can() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("临时方案", manualParams(), 1770000000, id));
    TEST_ASSERT_TRUE(store.setActiveScheme(id, 1770000010));

    TEST_ASSERT_TRUE(store.deleteScheme(1, 1770000020));
    TEST_ASSERT_FALSE(store.deleteScheme(id, 1770000030));

    MeteringSchemeRecord current{};
    TEST_ASSERT_TRUE(store.activeScheme(current));
    TEST_ASSERT_EQUAL_UINT32(id, current.id);
    TEST_ASSERT_FALSE(current.deleted);
}

void test_create_manual_reuses_deleted_slot_when_store_is_full_and_preserves_new_id() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t ids[kMeteringSchemeStoreSlotCount]{};
    ids[0] = 1;
    for (std::size_t i = 1; i < kMeteringSchemeStoreSlotCount; ++i) {
        TEST_ASSERT_TRUE(store.createManual("填充方案", manualParams(360 + i), 1770000000 + i, ids[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(50, ids[kMeteringSchemeStoreSlotCount - 1]);
    TEST_ASSERT_TRUE(store.deleteScheme(ids[10], 1770000200));

    std::uint32_t overflowId = 0;
    TEST_ASSERT_TRUE(store.createManual("覆盖已删除槽", manualParams(480), 1770000300, overflowId));
    TEST_ASSERT_EQUAL_UINT32(51, overflowId);

    MeteringSchemeRecord overwritten{};
    TEST_ASSERT_FALSE(store.findById(ids[10], overwritten));
    MeteringSchemeRecord created{};
    TEST_ASSERT_TRUE(store.findById(overflowId, created));
    TEST_ASSERT_EQUAL_STRING("覆盖已删除槽", created.name);
    TEST_ASSERT_FALSE(created.deleted);
}

void test_create_manual_fails_when_all_slots_are_not_deleted() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    for (std::size_t i = 1; i < kMeteringSchemeStoreSlotCount; ++i) {
        TEST_ASSERT_TRUE(store.createManual("填充方案", manualParams(360 + i), 1770000000 + i, id));
    }

    std::uint32_t overflowId = 0;
    TEST_ASSERT_FALSE(store.createManual("不应覆盖未删除", manualParams(480), 1770000300, overflowId));
    TEST_ASSERT_EQUAL_UINT32(0, overflowId);
}

void test_create_manual_header_failure_rolls_back_written_record() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        std::uint32_t failedId = 0;
        backend.failHeaderWriteOnce = true;
        TEST_ASSERT_FALSE(store.createManual("header fail", manualParams(), 1770000000, failedId));
        TEST_ASSERT_EQUAL_UINT32(2, failedId);
    }

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    MeteringSchemeRecord rolledBack{};
    TEST_ASSERT_FALSE(loaded.findById(2, rolledBack));

    std::uint32_t nextId = 0;
    TEST_ASSERT_TRUE(loaded.createManual("after restart", manualParams(380), 1770000001, nextId));
    TEST_ASSERT_EQUAL_UINT32(2, nextId);
}

void test_set_active_header_failure_rolls_back_record_timestamp() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());
    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("新方案", manualParams(), 1770000000, id));

    MeteringSchemeRecord before{};
    TEST_ASSERT_TRUE(store.findById(id, before));
    backend.failHeaderWriteOnce = true;

    TEST_ASSERT_FALSE(store.setActiveScheme(id, 1770000030));
    TEST_ASSERT_EQUAL_UINT32(1, store.activeSchemeId());

    MeteringSchemeRecord after{};
    TEST_ASSERT_TRUE(store.findById(id, after));
    TEST_ASSERT_EQUAL_UINT32(before.updatedAt, after.updatedAt);
}

void test_begin_repairs_next_scheme_id_from_existing_records() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        std::uint32_t id = 0;
        TEST_ASSERT_TRUE(store.createManual("existing", manualParams(), 1770000000, id));
        TEST_ASSERT_EQUAL_UINT32(2, id);
    }
    const MeteringSchemeStoreHeader staleHeader =
        currentHeader(1, 2, static_cast<std::uint32_t>(kMeteringSchemeStoreSlotCount));
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin",
                                     0,
                                     reinterpret_cast<const std::uint8_t*>(&staleHeader),
                                     sizeof(staleHeader)));

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    MeteringSchemeRecord existing{};
    TEST_ASSERT_TRUE(loaded.findById(2, existing));

    std::uint32_t nextId = 0;
    TEST_ASSERT_TRUE(loaded.createManual("after repair", manualParams(380), 1770000001, nextId));
    TEST_ASSERT_EQUAL_UINT32(3, nextId);
}

void test_begin_reports_backend_failure_when_next_id_repair_cannot_be_saved() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        std::uint32_t id = 0;
        TEST_ASSERT_TRUE(store.createManual("existing", manualParams(), 1770000000, id));
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
    TEST_ASSERT_FALSE(loaded.begin());
    TEST_ASSERT_FALSE(loaded.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::BackendFailure),
                            static_cast<unsigned>(loaded.status()));
}

void test_begin_rejects_legacy_scheme_file_without_migration() {
    MemoryFileBackend backend;
    const MeteringSchemeStoreHeader header = incompatibleLegacyHeader();
    TEST_ASSERT_TRUE(backend.writeAt("/schemes.bin", 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)));
    const std::int64_t originalSize = backend.fileSize("/schemes.bin");

    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::IncompatibleFormat),
                            static_cast<unsigned>(store.status()));
    TEST_ASSERT_TRUE(backend.exists("/schemes.bin"));
    TEST_ASSERT_EQUAL_INT64(originalSize, backend.fileSize("/schemes.bin"));
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
}

void test_begin_ignores_stale_temp_when_current_file_is_valid() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
        std::uint32_t id = 0;
        TEST_ASSERT_TRUE(store.createManual("当前方案", manualParams(), 1770000000, id));
        TEST_ASSERT_TRUE(store.setActiveScheme(id, 1770000001));
    }

    const MeteringSchemeStoreHeader header = currentHeader(99, 100, 1);
    MeteringSchemeRecord stale{};
    initializeManualMeteringScheme(stale, 99, "陈旧临时方案", manualParams(370), 1770000000);
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

void test_begin_preserves_corrupt_scheme_file() {
    MemoryFileBackend backend;
    {
        MeteringSchemeStore store(backend, "/schemes.bin");
        TEST_ASSERT_TRUE(store.begin());
    }
    const std::int64_t originalSize = backend.fileSize("/schemes.bin");
    backend.overwriteByte("/schemes.bin", 0, 0x00);

    MeteringSchemeStore loaded(backend, "/schemes.bin");
    TEST_ASSERT_FALSE(loaded.begin());
    TEST_ASSERT_FALSE(loaded.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::IncompatibleFormat),
                            static_cast<unsigned>(loaded.status()));
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_TRUE(backend.exists("/schemes.bin"));
    TEST_ASSERT_EQUAL_INT64(originalSize, backend.fileSize("/schemes.bin"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_begin_initializes_default_scheme_file);
    RUN_TEST(test_create_manual_lists_only_not_deleted_schemes_by_default);
    RUN_TEST(test_set_active_scheme_updates_current_id_only_for_not_deleted_scheme);
    RUN_TEST(test_mark_used_after_record_write_sets_used_once);
    RUN_TEST(test_current_scheme_cannot_be_deleted_but_any_other_scheme_can);
    RUN_TEST(test_create_manual_reuses_deleted_slot_when_store_is_full_and_preserves_new_id);
    RUN_TEST(test_create_manual_fails_when_all_slots_are_not_deleted);
    RUN_TEST(test_create_manual_header_failure_rolls_back_written_record);
    RUN_TEST(test_set_active_header_failure_rolls_back_record_timestamp);
    RUN_TEST(test_begin_repairs_next_scheme_id_from_existing_records);
    RUN_TEST(test_begin_reports_backend_failure_when_next_id_repair_cannot_be_saved);
    RUN_TEST(test_begin_rejects_legacy_scheme_file_without_migration);
    RUN_TEST(test_begin_ignores_stale_temp_when_current_file_is_valid);
    RUN_TEST(test_begin_preserves_corrupt_scheme_file);
    return UNITY_END();
}
