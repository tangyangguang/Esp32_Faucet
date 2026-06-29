#include <unity.h>

#include "app/MeteringSchemeStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    bool failHeaderWriteOnce = false;
    std::map<std::string, std::vector<std::uint8_t>> files;

    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        if (!path) {
            return false;
        }
        files[path] = std::vector<std::uint8_t>(size, 0);
        return true;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        if (!path || (!data && len > 0)) {
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
        if (!path) {
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
    TEST_ASSERT_EQUAL_UINT32(8, active.params.startupPulseCount);
}

void test_begin_rebuilds_incompatible_scheme_file() {
    MemoryFileBackend backend;
    MeteringSchemeStoreHeader oldHeader{};
    oldHeader.magic = 0x314D5346UL;
    oldHeader.version = 1;
    oldHeader.headerSize = sizeof(MeteringSchemeStoreHeader);
    oldHeader.recordSize = sizeof(MeteringSchemeRecord);
    oldHeader.activeSchemeId = 1;
    oldHeader.nextSchemeId = 2;
    oldHeader.slotCount = kMeteringSchemeStoreSlotCount;
    backend.files["/schemes.bin"].resize(sizeof(oldHeader), 0);
    std::memcpy(backend.files["/schemes.bin"].data(), &oldHeader, sizeof(oldHeader));

    MeteringSchemeStore store(backend, "/schemes.bin");

    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AppStorageStatus::Ready),
                            static_cast<std::uint8_t>(store.status()));
    TEST_ASSERT_EQUAL_UINT32(1, store.activeSchemeId());
    TEST_ASSERT_EQUAL_size_t(sizeof(MeteringSchemeStoreHeader) +
                                 kMeteringSchemeStoreSlotCount * sizeof(MeteringSchemeRecord),
                             backend.files["/schemes.bin"].size());

    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(store.activeScheme(active));
    TEST_ASSERT_EQUAL_STRING("YF-S201 默认计量方案", active.name);
    TEST_ASSERT_EQUAL_UINT32(8, active.params.startupPulseCount);
}

void test_list_returns_scheme_records_in_order() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());
    for (std::size_t i = 1; i < kMeteringSchemeStoreSlotCount; ++i) {
        std::uint32_t id = 0;
        TEST_ASSERT_TRUE(store.createManual("方案", manualParams(360 + static_cast<std::uint32_t>(i)), 1770000000 + i, id));
    }

    MeteringSchemeRecord records[kMeteringSchemeStoreSlotCount]{};
    TEST_ASSERT_EQUAL_size_t(kMeteringSchemeStoreSlotCount,
                             store.list(records, kMeteringSchemeStoreSlotCount));

    TEST_ASSERT_EQUAL_UINT32(1, records[0].id);
    TEST_ASSERT_EQUAL_UINT32(kMeteringSchemeStoreSlotCount, records[kMeteringSchemeStoreSlotCount - 1].id);
}

void test_set_active_scheme_updates_current_id() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    TEST_ASSERT_TRUE(store.createManual("新方案", manualParams(), 1770000000, id));
    TEST_ASSERT_TRUE(store.setActiveScheme(id));

    TEST_ASSERT_EQUAL_UINT32(id, store.activeSchemeId());
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(store.activeScheme(active));
    TEST_ASSERT_EQUAL_UINT32(id, active.id);
}

void test_full_store_overwrites_oldest_non_current_record() {
    MemoryFileBackend backend;
    MeteringSchemeStore store(backend, "/schemes.bin");
    TEST_ASSERT_TRUE(store.begin());

    std::uint32_t id = 0;
    for (std::size_t i = 1; i < kMeteringSchemeStoreSlotCount; ++i) {
        TEST_ASSERT_TRUE(store.createManual("填充方案", manualParams(360 + static_cast<std::uint32_t>(i)), 1770000000 + i, id));
        TEST_ASSERT_TRUE(store.setActiveScheme(id));
    }
    const std::uint32_t activeBefore = store.activeSchemeId();

    std::uint32_t overflowId = 0;
    TEST_ASSERT_TRUE(store.createManual("新当前参数", manualParams(480), 1770003000, overflowId));
    TEST_ASSERT_TRUE(store.setActiveScheme(overflowId));

    MeteringSchemeRecord oldDefault{};
    TEST_ASSERT_FALSE(store.findById(1, oldDefault));
    MeteringSchemeRecord priorActive{};
    TEST_ASSERT_TRUE(store.findById(activeBefore, priorActive));
    MeteringSchemeRecord created{};
    TEST_ASSERT_TRUE(store.findById(overflowId, created));
    TEST_ASSERT_EQUAL_STRING("新当前参数", created.name);
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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_begin_initializes_default_scheme_file);
    RUN_TEST(test_begin_rebuilds_incompatible_scheme_file);
    RUN_TEST(test_list_returns_scheme_records_in_order);
    RUN_TEST(test_set_active_scheme_updates_current_id);
    RUN_TEST(test_full_store_overwrites_oldest_non_current_record);
    RUN_TEST(test_create_manual_header_failure_rolls_back_written_record);
    return UNITY_END();
}
