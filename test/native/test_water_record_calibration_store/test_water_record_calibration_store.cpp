#include <unity.h>

#include "app/WaterRecordCalibrationStore.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    bool failWrite = false;
    bool failRead = false;
    bool writeAtExtends = true;

    bool exists(const char* path) override {
        ++existsCalls;
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        ++fileSizeCalls;
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        ++createSizedCalls;
        if (failWrite || !path) {
            return false;
        }
        files[path] = std::vector<std::uint8_t>(size, 0);
        return true;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        ++appendCalls;
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
        ++readCalls;
        if (failRead || !path || !out) {
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
        ++writeCalls;
        if (failWrite || !path || (!data && len > 0)) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        if (!writeAtExtends && offset + len > file.size()) {
            return false;
        }
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

    std::size_t createSizedCalls = 0;
    std::size_t appendCalls = 0;
    std::size_t readCalls = 0;
    std::size_t writeCalls = 0;
    std::size_t removeCalls = 0;
    std::size_t existsCalls = 0;
    std::size_t fileSizeCalls = 0;

private:
    std::map<std::string, std::vector<std::uint8_t>> files;
};

WaterRecord makeRecord(std::uint32_t startTime,
                       std::uint32_t volumeMl,
                       std::uint32_t targetValue,
                       std::uint32_t pulseCount) {
    return WaterRecord{
        startTime,
        volumeMl,
        targetValue,
        pulseCount,
        30,
        200,
        WaterMode::Volume,
        WaterResult::Completed,
        1,
        0,
        1,
        {0, 0, 0, 0},
    };
}

WaterRecordCalibration makeCalibration(const WaterRecord& record, std::uint32_t actualMl) {
    WaterRecordCalibration calibration = makeWaterRecordCalibration(record);
    calibration.actualMl = actualMl;
    calibration.kind = WaterRecordCalibrationKind::PulsePerMl;
    calibration.oldPulsePerMl = 0.221f;
    calibration.newPulsePerMl = 0.230f;
    calibration.oldStartupCompensationMl = 0;
    calibration.newStartupCompensationMl = 0;
    calibration.calibratedAt = 832000000UL;
    calibration.calibrationCount = 1;
    return calibration;
}

}  // namespace

void test_record_calibration_store_finds_saved_calibration_by_record_identity() {
    WaterRecordCalibration entries[4]{};
    WaterRecordCalibrationStore store(entries, 4);
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    const WaterRecordCalibration calibration = makeCalibration(record, 7000);

    TEST_ASSERT_TRUE(store.upsert(calibration));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(store.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(7000, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(1, found.calibrationCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordCalibrationKind::PulsePerMl),
                            static_cast<std::uint8_t>(found.kind));
}

void test_record_calibration_store_recalibration_overwrites_actual_and_increments_count() {
    WaterRecordCalibration entries[4]{};
    WaterRecordCalibrationStore store(entries, 4);
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);

    TEST_ASSERT_TRUE(store.upsert(makeCalibration(record, 7000)));
    WaterRecordCalibration recalibration = makeCalibration(record, 6900);
    recalibration.newPulsePerMl = 0.187f;
    recalibration.calibratedAt = 832000360UL;
    TEST_ASSERT_TRUE(store.upsert(recalibration));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(store.find(record, found));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT32(6900, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(2, found.calibrationCount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.187f, found.newPulsePerMl);
    TEST_ASSERT_EQUAL_UINT32(832000360UL, found.calibratedAt);
}

void test_record_calibration_store_identity_excludes_similar_records() {
    WaterRecordCalibration entries[4]{};
    WaterRecordCalibrationStore store(entries, 4);
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    WaterRecord similar = record;
    similar.pulseCount = 1292;

    TEST_ASSERT_TRUE(store.upsert(makeCalibration(record, 7000)));

    WaterRecordCalibration found{};
    TEST_ASSERT_FALSE(store.find(similar, found));
}

void test_record_calibration_file_store_persists_saved_calibration() {
    MemoryFileBackend backend;
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    {
        WaterRecordCalibrationFileStore store(backend, "/cal.bin", 4);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.upsert(makeCalibration(record, 7000)));
    }

    WaterRecordCalibrationFileStore loaded(backend, "/cal.bin", 4);
    TEST_ASSERT_TRUE(loaded.begin());

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(loaded.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(7000, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(1, found.calibrationCount);
}

void test_record_calibration_file_store_overwrites_matching_record() {
    MemoryFileBackend backend;
    WaterRecordCalibrationFileStore store(backend, "/cal.bin", 4);
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.upsert(makeCalibration(record, 7000)));
    TEST_ASSERT_TRUE(store.upsert(makeCalibration(record, 6900)));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(store.find(record, found));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT32(6900, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(2, found.calibrationCount);
}

void test_record_calibration_file_store_appends_first_entry_without_write_at_extend() {
    MemoryFileBackend backend;
    backend.writeAtExtends = false;
    WaterRecordCalibrationFileStore store(backend, "/cal.bin", 4);
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);

    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.upsert(makeCalibration(record, 7000)));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(store.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(7000, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(1, found.calibrationCount);
}

void test_record_calibration_file_store_matches_page_records_with_single_scan() {
    MemoryFileBackend backend;
    WaterRecordCalibrationFileStore store(backend, "/cal.bin", 48);
    const WaterRecord newest = makeRecord(832004000UL, 5500, 7000, 1210);
    const WaterRecord middle = makeRecord(832002000UL, 5300, 7000, 1170);
    const WaterRecord oldest = makeRecord(832000100UL, 5100, 7000, 1130);
    const WaterRecord missing = makeRecord(832000900UL, 5900, 7000, 1300);

    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.upsert(makeCalibration(oldest, 5000)));
    for (std::size_t i = 1; i < 20; ++i) {
        TEST_ASSERT_TRUE(store.upsert(makeCalibration(makeRecord(832000100UL + static_cast<std::uint32_t>(i) * 100UL,
                                                                 5100 + static_cast<std::uint32_t>(i),
                                                                 7000,
                                                                 1130 + static_cast<std::uint32_t>(i)),
                                                   5000 + static_cast<std::uint32_t>(i))));
    }
    TEST_ASSERT_TRUE(store.upsert(makeCalibration(middle, 5350)));
    for (std::size_t i = 21; i < 40; ++i) {
        TEST_ASSERT_TRUE(store.upsert(makeCalibration(makeRecord(832000100UL + static_cast<std::uint32_t>(i) * 100UL,
                                                                 5100 + static_cast<std::uint32_t>(i),
                                                                 7000,
                                                                 1130 + static_cast<std::uint32_t>(i)),
                                                   5000 + static_cast<std::uint32_t>(i))));
    }
    TEST_ASSERT_TRUE(store.upsert(makeCalibration(newest, 5600)));

    WaterRecord page[] = {newest, missing, middle, oldest};
    WaterRecordCalibration matches[4]{};
    bool found[4]{};
    backend.readCalls = 0;

    TEST_ASSERT_EQUAL_size_t(3, store.findAny(page, 4, matches, found));

    TEST_ASSERT_TRUE(found[0]);
    TEST_ASSERT_FALSE(found[1]);
    TEST_ASSERT_TRUE(found[2]);
    TEST_ASSERT_TRUE(found[3]);
    TEST_ASSERT_EQUAL_UINT32(5600, matches[0].actualMl);
    TEST_ASSERT_EQUAL_UINT32(5350, matches[2].actualMl);
    TEST_ASSERT_EQUAL_UINT32(5000, matches[3].actualMl);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(2, backend.readCalls);
}

void test_record_calibration_file_store_corrupt_header_preserves_existing_file() {
    MemoryFileBackend backend;
    const std::uint8_t bad[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(backend.writeAt("/cal.bin", 0, bad, sizeof(bad)));
    const std::size_t createCalls = backend.createSizedCalls;

    WaterRecordCalibrationFileStore store(backend, "/cal.bin", 4);
    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(4, backend.fileSize("/cal.bin"));
}

void test_record_calibration_file_store_capacity_mismatch_preserves_existing_file() {
    MemoryFileBackend backend;
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    {
        WaterRecordCalibrationFileStore store(backend, "/cal.bin", 4);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.upsert(makeCalibration(record, 7000)));
    }
    const std::int64_t originalSize = backend.fileSize("/cal.bin");
    const std::size_t createCalls = backend.createSizedCalls;

    WaterRecordCalibrationFileStore mismatched(backend, "/cal.bin", 3);
    TEST_ASSERT_FALSE(mismatched.begin());
    TEST_ASSERT_FALSE(mismatched.ready());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(originalSize, backend.fileSize("/cal.bin"));

    WaterRecordCalibrationFileStore original(backend, "/cal.bin", 4);
    TEST_ASSERT_TRUE(original.begin());
    TEST_ASSERT_EQUAL_size_t(1, original.count());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_record_calibration_store_finds_saved_calibration_by_record_identity);
    RUN_TEST(test_record_calibration_store_recalibration_overwrites_actual_and_increments_count);
    RUN_TEST(test_record_calibration_store_identity_excludes_similar_records);
    RUN_TEST(test_record_calibration_file_store_persists_saved_calibration);
    RUN_TEST(test_record_calibration_file_store_overwrites_matching_record);
    RUN_TEST(test_record_calibration_file_store_appends_first_entry_without_write_at_extend);
    RUN_TEST(test_record_calibration_file_store_matches_page_records_with_single_scan);
    RUN_TEST(test_record_calibration_file_store_corrupt_header_preserves_existing_file);
    RUN_TEST(test_record_calibration_file_store_capacity_mismatch_preserves_existing_file);
    return UNITY_END();
}
