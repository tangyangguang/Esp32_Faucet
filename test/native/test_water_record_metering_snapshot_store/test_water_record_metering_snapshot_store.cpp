#include <unity.h>

#include "app/WaterRecordMeteringSnapshotStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        files[path ? path : ""] = std::vector<std::uint8_t>(size, 0);
        return path != nullptr;
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
        if (!path || !out) {
            return false;
        }
        const auto it = files.find(path);
        if (it == files.end() || offset + len > it->second.size()) {
            return false;
        }
        std::memcpy(out, it->second.data() + offset, len);
        return true;
    }

    bool writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) override {
        if (!path || (!data && len > 0)) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        if (offset + len > file.size()) {
            file.resize(offset + len, 0);
        }
        if (len > 0) {
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

WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl, std::uint32_t pulseCount) {
    return WaterRecord{
        startTime,
        volumeMl,
        1500,
        pulseCount,
        3,
        10,
        WaterMode::Volume,
        WaterResult::Completed,
        1,
        0,
        0.22f,
        {0, 0, 0, 0},
    };
}

WaterRecordMeteringSnapshot makeSnapshot(const WaterRecord& record, std::uint32_t schemeId) {
    WaterRecordMeteringSnapshot snapshot = makeWaterRecordMeteringSnapshot(record);
    snapshot.meteringSchemeId = schemeId;
    snapshot.meteringSchemeRevision = 2;
    snapshot.params = MeteringParameters{40, 553, 222};
    return snapshot;
}

}  // namespace

void test_ram_snapshot_store_upserts_by_water_record_identity() {
    WaterRecordMeteringSnapshot entries[3]{};
    WaterRecordMeteringSnapshotStore store(entries, 3);
    const WaterRecord record = makeRecord(100, 1500, 333);

    TEST_ASSERT_TRUE(store.upsert(makeSnapshot(record, 2)));
    TEST_ASSERT_TRUE(store.upsert(makeSnapshot(record, 3)));

    WaterRecordMeteringSnapshot found{};
    TEST_ASSERT_TRUE(store.find(record, found));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT32(3, found.meteringSchemeId);
    TEST_ASSERT_EQUAL_UINT32(40, found.params.startupPulseCount);
}

void test_file_snapshot_store_reloads_after_restart() {
    MemoryFileBackend backend;
    const WaterRecord record = makeRecord(100, 1500, 333);
    {
        WaterRecordMeteringSnapshotFileStore store(backend, "/metering.bin", 4);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.upsert(makeSnapshot(record, 2)));
    }

    WaterRecordMeteringSnapshotFileStore loaded(backend, "/metering.bin", 4);
    TEST_ASSERT_TRUE(loaded.begin());
    WaterRecordMeteringSnapshot found{};
    TEST_ASSERT_TRUE(loaded.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(2, found.meteringSchemeId);
    TEST_ASSERT_EQUAL_UINT32(222, found.params.stablePulsePerLiter);
}

void test_find_any_matches_record_pages() {
    MemoryFileBackend backend;
    WaterRecordMeteringSnapshotFileStore store(backend, "/metering.bin", 8);
    TEST_ASSERT_TRUE(store.begin());
    const WaterRecord first = makeRecord(100, 1500, 333);
    const WaterRecord second = makeRecord(200, 2500, 555);
    const WaterRecord missing = makeRecord(300, 3500, 777);
    TEST_ASSERT_TRUE(store.upsert(makeSnapshot(first, 2)));
    TEST_ASSERT_TRUE(store.upsert(makeSnapshot(second, 4)));

    WaterRecord page[] = {second, missing, first};
    WaterRecordMeteringSnapshot output[3]{};
    bool found[3]{};

    TEST_ASSERT_EQUAL_size_t(2, store.findAny(page, 3, output, found));
    TEST_ASSERT_TRUE(found[0]);
    TEST_ASSERT_FALSE(found[1]);
    TEST_ASSERT_TRUE(found[2]);
    TEST_ASSERT_EQUAL_UINT32(4, output[0].meteringSchemeId);
    TEST_ASSERT_EQUAL_UINT32(2, output[2].meteringSchemeId);
}

void test_file_store_overwrites_oldest_when_capacity_is_full() {
    MemoryFileBackend backend;
    WaterRecordMeteringSnapshotFileStore store(backend, "/metering.bin", 2);
    TEST_ASSERT_TRUE(store.begin());
    const WaterRecord first = makeRecord(100, 1500, 333);
    const WaterRecord second = makeRecord(200, 2500, 555);
    const WaterRecord third = makeRecord(300, 3500, 777);
    TEST_ASSERT_TRUE(store.upsert(makeSnapshot(first, 2)));
    TEST_ASSERT_TRUE(store.upsert(makeSnapshot(second, 3)));
    TEST_ASSERT_TRUE(store.upsert(makeSnapshot(third, 4)));

    WaterRecordMeteringSnapshot found{};
    TEST_ASSERT_FALSE(store.find(first, found));
    TEST_ASSERT_TRUE(store.find(second, found));
    TEST_ASSERT_TRUE(store.find(third, found));
    TEST_ASSERT_EQUAL_size_t(2, store.count());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ram_snapshot_store_upserts_by_water_record_identity);
    RUN_TEST(test_file_snapshot_store_reloads_after_restart);
    RUN_TEST(test_find_any_matches_record_pages);
    RUN_TEST(test_file_store_overwrites_oldest_when_capacity_is_full);
    return UNITY_END();
}
