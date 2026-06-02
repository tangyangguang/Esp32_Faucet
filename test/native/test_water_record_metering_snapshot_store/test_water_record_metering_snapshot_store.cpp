#include <unity.h>

#include "app/WaterRecordMeteringSnapshotStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

constexpr std::uint32_t kTestSnapshotMagic = 0x46574D53UL;

struct SnapshotHeaderV1 {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t recordSize;
    std::uint32_t capacity;
    std::uint32_t count;
    std::uint32_t oldestIndex;
    std::uint32_t reserved;
};

SnapshotHeaderV1 snapshotHeader(std::uint16_t version,
                                std::uint16_t recordSize,
                                std::uint32_t capacity,
                                std::uint32_t count,
                                std::uint32_t oldestIndex) {
    return SnapshotHeaderV1{
        kTestSnapshotMagic,
        version,
        recordSize,
        capacity,
        count,
        oldestIndex,
        0,
    };
}

struct LegacyMeteringParametersV1 {
    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t stablePulsePerLiter;
};

struct WaterRecordMeteringSnapshotV1 {
    std::uint32_t startTime;
    std::uint32_t volumeMl;
    std::uint32_t targetValue;
    std::uint32_t pulseCount;
    std::uint32_t rejectedPulseCount;
    std::uint16_t durationSec;
    WaterMode mode;
    WaterResult result;
    std::uint8_t selectedPreset;
    std::uint8_t reserved0;
    float pulsePerMlAtRun;
    std::uint32_t meteringSchemeId;
    std::uint32_t meteringSchemeRevision;
    LegacyMeteringParametersV1 params;
    std::uint8_t reserved[4];
};

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    std::size_t createSizedCalls = 0;
    std::size_t removeCalls = 0;
    std::string failWritePath;
    std::string failWriteAtPath;

    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        ++createSizedCalls;
        if (shouldFailAnyWrite(path)) {
            return false;
        }
        files[path ? path : ""] = std::vector<std::uint8_t>(size, 0);
        return path != nullptr;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        if (!path || shouldFailAnyWrite(path) || (!data && len > 0)) {
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
        if (!path || shouldFailAnyWrite(path) || shouldFailWriteAt(path) || (!data && len > 0)) {
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
        ++removeCalls;
        files.erase(path ? path : "");
        return true;
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

WaterRecordMeteringSnapshotV1 makeLegacySnapshot(const WaterRecord& record, std::uint32_t schemeId) {
    WaterRecordMeteringSnapshotV1 snapshot{};
    snapshot.startTime = record.startTime;
    snapshot.volumeMl = record.volumeMl;
    snapshot.targetValue = record.targetValue;
    snapshot.pulseCount = record.pulseCount;
    snapshot.rejectedPulseCount = record.rejectedPulseCount;
    snapshot.durationSec = record.durationSec;
    snapshot.mode = record.mode;
    snapshot.result = record.result;
    snapshot.selectedPreset = record.selectedPreset;
    snapshot.pulsePerMlAtRun = record.pulsePerMlAtRun;
    snapshot.meteringSchemeId = schemeId;
    snapshot.meteringSchemeRevision = 2;
    snapshot.params = LegacyMeteringParametersV1{40, 553, 222};
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
    TEST_ASSERT_EQUAL_UINT32(5000, found.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, found.params.stableFlowMlPerMin);
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
    TEST_ASSERT_EQUAL_UINT32(5000, found.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, found.params.stableFlowMlPerMin);
}

void test_file_snapshot_store_migrates_v1_records_to_time_estimate_params() {
    MemoryFileBackend backend;
    const WaterRecord record = makeRecord(100, 1500, 333);
    const SnapshotHeaderV1 header{
        kTestSnapshotMagic,
        1,
        static_cast<std::uint16_t>(sizeof(WaterRecordMeteringSnapshotV1)),
        4,
        1,
        0,
        0,
    };
    const WaterRecordMeteringSnapshotV1 legacy = makeLegacySnapshot(record, 2);
    TEST_ASSERT_TRUE(backend.writeAt("/metering.bin", 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/metering.bin", sizeof(header), reinterpret_cast<const std::uint8_t*>(&legacy), sizeof(legacy)));
    const std::size_t createCalls = backend.createSizedCalls;

    WaterRecordMeteringSnapshotFileStore store(backend, "/metering.bin", 4);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_GREATER_THAN_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(1, store.count());

    WaterRecordMeteringSnapshot found{};
    TEST_ASSERT_TRUE(store.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(2, found.meteringSchemeId);
    TEST_ASSERT_EQUAL_UINT32(40, found.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, found.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, found.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, found.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, found.params.stableFlowMlPerMin);
}

void test_file_snapshot_store_recovers_v1_migration_from_completed_temp_file() {
    MemoryFileBackend backend;
    const WaterRecord record = makeRecord(100, 1500, 333);
    const SnapshotHeaderV1 header{
        kTestSnapshotMagic,
        1,
        static_cast<std::uint16_t>(sizeof(WaterRecordMeteringSnapshotV1)),
        4,
        1,
        0,
        0,
    };
    const WaterRecordMeteringSnapshotV1 legacy = makeLegacySnapshot(record, 2);
    TEST_ASSERT_TRUE(backend.writeAt("/metering.bin", 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/metering.bin", sizeof(header), reinterpret_cast<const std::uint8_t*>(&legacy), sizeof(legacy)));

    backend.failWriteAtPath = "/metering.bin";
    {
        WaterRecordMeteringSnapshotFileStore interrupted(backend, "/metering.bin", 4);
        TEST_ASSERT_FALSE(interrupted.begin());
    }
    backend.failWriteAtPath.clear();

    WaterRecordMeteringSnapshotFileStore recovered(backend, "/metering.bin", 4);
    TEST_ASSERT_TRUE(recovered.begin());
    WaterRecordMeteringSnapshot found{};
    TEST_ASSERT_TRUE(recovered.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(5000, found.params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, found.params.stableFlowMlPerMin);
}

void test_file_snapshot_store_ignores_stale_migration_temp_when_current_file_is_valid() {
    MemoryFileBackend backend;
    const WaterRecord record = makeRecord(100, 1500, 333);
    {
        WaterRecordMeteringSnapshotFileStore store(backend, "/metering.bin", 4);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.upsert(makeSnapshot(record, 2)));
    }

    const SnapshotHeaderV1 header = snapshotHeader(2, sizeof(WaterRecordMeteringSnapshot), 4, 1, 0);
    const WaterRecordMeteringSnapshot stale = makeSnapshot(record, 9);
    TEST_ASSERT_TRUE(backend.createSized("/metering.bin.tmp", sizeof(header)));
    TEST_ASSERT_TRUE(backend.writeAt("/metering.bin.tmp", 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)));
    TEST_ASSERT_TRUE(backend.appendBytes("/metering.bin.tmp",
                                         reinterpret_cast<const std::uint8_t*>(&stale),
                                         sizeof(stale)));

    WaterRecordMeteringSnapshotFileStore loaded(backend, "/metering.bin", 4);
    TEST_ASSERT_TRUE(loaded.begin());
    WaterRecordMeteringSnapshot found{};
    TEST_ASSERT_TRUE(loaded.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(2, found.meteringSchemeId);
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

void test_file_snapshot_store_corrupt_header_preserves_existing_file() {
    MemoryFileBackend backend;
    const std::uint8_t bad[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(backend.writeAt("/metering.bin", 0, bad, sizeof(bad)));
    const std::size_t createCalls = backend.createSizedCalls;

    WaterRecordMeteringSnapshotFileStore store(backend, "/metering.bin", 4);
    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(4, backend.fileSize("/metering.bin"));
}

void test_file_snapshot_store_capacity_mismatch_preserves_existing_file() {
    MemoryFileBackend backend;
    const WaterRecord record = makeRecord(100, 1500, 333);
    {
        WaterRecordMeteringSnapshotFileStore store(backend, "/metering.bin", 4);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.upsert(makeSnapshot(record, 2)));
    }
    const std::int64_t originalSize = backend.fileSize("/metering.bin");
    const std::size_t createCalls = backend.createSizedCalls;

    WaterRecordMeteringSnapshotFileStore mismatched(backend, "/metering.bin", 3);
    TEST_ASSERT_FALSE(mismatched.begin());
    TEST_ASSERT_FALSE(mismatched.ready());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(originalSize, backend.fileSize("/metering.bin"));

    WaterRecordMeteringSnapshotFileStore original(backend, "/metering.bin", 4);
    TEST_ASSERT_TRUE(original.begin());
    TEST_ASSERT_EQUAL_size_t(1, original.count());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ram_snapshot_store_upserts_by_water_record_identity);
    RUN_TEST(test_file_snapshot_store_reloads_after_restart);
    RUN_TEST(test_file_snapshot_store_migrates_v1_records_to_time_estimate_params);
    RUN_TEST(test_file_snapshot_store_recovers_v1_migration_from_completed_temp_file);
    RUN_TEST(test_file_snapshot_store_ignores_stale_migration_temp_when_current_file_is_valid);
    RUN_TEST(test_find_any_matches_record_pages);
    RUN_TEST(test_file_store_overwrites_oldest_when_capacity_is_full);
    RUN_TEST(test_file_snapshot_store_corrupt_header_preserves_existing_file);
    RUN_TEST(test_file_snapshot_store_capacity_mismatch_preserves_existing_file);
    return UNITY_END();
}
