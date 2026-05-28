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
        files.erase(path ? path : "");
        return true;
    }

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
        0.221f,
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
    return UNITY_END();
}
