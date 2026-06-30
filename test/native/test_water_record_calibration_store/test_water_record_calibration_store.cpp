#include <unity.h>

#include "app/WaterRecordCalibrationStore.h"
#include "../support/MemoryFileBackend.h"

#include <algorithm>
#include <cstdint>

using namespace faucet;
using faucet_test::MemoryFileBackend;

namespace {

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
    calibration.calibrationCount = 1;
    return calibration;
}

struct RecordCalibrationStoreFixture {
    WaterRecordCalibration entries[4]{};
    WaterRecordCalibrationStore store;

    RecordCalibrationStoreFixture() : store(entries, 4) {}
};

struct RecordCalibrationFileFixture {
    MemoryFileBackend backend;
    WaterRecordCalibrationFileStore store;

    explicit RecordCalibrationFileFixture(std::size_t capacity = 4)
        : store(backend, "/cal.bin", capacity) {}

    void begin() {
        TEST_ASSERT_TRUE(store.begin());
    }
};

}  // namespace

void test_record_calibration_store_finds_saved_calibration_by_record_identity() {
    RecordCalibrationStoreFixture fixture;
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    const WaterRecordCalibration calibration = makeCalibration(record, 7000);

    TEST_ASSERT_TRUE(fixture.store.upsert(calibration));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(fixture.store.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(7000, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(1, found.calibrationCount);
}

void test_record_calibration_store_recalibration_overwrites_actual_and_increments_count() {
    RecordCalibrationStoreFixture fixture;
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);

    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(record, 7000)));
    WaterRecordCalibration recalibration = makeCalibration(record, 6900);
    TEST_ASSERT_TRUE(fixture.store.upsert(recalibration));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(fixture.store.find(record, found));
    TEST_ASSERT_EQUAL_size_t(1, fixture.store.count());
    TEST_ASSERT_EQUAL_UINT32(6900, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(2, found.calibrationCount);
}

void test_record_calibration_store_identity_excludes_similar_records() {
    RecordCalibrationStoreFixture fixture;
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    WaterRecord similar = record;
    similar.pulseCount = 1292;

    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(record, 7000)));

    WaterRecordCalibration found{};
    TEST_ASSERT_FALSE(fixture.store.find(similar, found));
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
    RecordCalibrationFileFixture fixture;
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(record, 7000)));
    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(record, 6900)));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(fixture.store.find(record, found));
    TEST_ASSERT_EQUAL_size_t(1, fixture.store.count());
    TEST_ASSERT_EQUAL_UINT32(6900, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(2, found.calibrationCount);
}

void test_record_calibration_file_store_appends_first_entry_without_write_at_extend() {
    RecordCalibrationFileFixture fixture;
    fixture.backend.writeAtExtends = false;
    const WaterRecord record = makeRecord(832000100UL, 5840, 7000, 1291);

    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(record, 7000)));

    WaterRecordCalibration found{};
    TEST_ASSERT_TRUE(fixture.store.find(record, found));
    TEST_ASSERT_EQUAL_UINT32(7000, found.actualMl);
    TEST_ASSERT_EQUAL_UINT16(1, found.calibrationCount);
}

void test_record_calibration_file_store_matches_page_records_with_single_scan() {
    RecordCalibrationFileFixture fixture(48);
    const WaterRecord newest = makeRecord(832004000UL, 5500, 7000, 1210);
    const WaterRecord middle = makeRecord(832002000UL, 5300, 7000, 1170);
    const WaterRecord oldest = makeRecord(832000100UL, 5100, 7000, 1130);
    const WaterRecord missing = makeRecord(832000900UL, 5900, 7000, 1300);

    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(oldest, 5000)));
    for (std::size_t i = 1; i < 20; ++i) {
        TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(makeRecord(832000100UL + static_cast<std::uint32_t>(i) * 100UL,
                                                                         5100 + static_cast<std::uint32_t>(i),
                                                                         7000,
                                                                         1130 + static_cast<std::uint32_t>(i)),
                                                           5000 + static_cast<std::uint32_t>(i))));
    }
    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(middle, 5350)));
    for (std::size_t i = 21; i < 40; ++i) {
        TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(makeRecord(832000100UL + static_cast<std::uint32_t>(i) * 100UL,
                                                                         5100 + static_cast<std::uint32_t>(i),
                                                                         7000,
                                                                         1130 + static_cast<std::uint32_t>(i)),
                                                           5000 + static_cast<std::uint32_t>(i))));
    }
    TEST_ASSERT_TRUE(fixture.store.upsert(makeCalibration(newest, 5600)));

    WaterRecord page[] = {newest, missing, middle, oldest};
    WaterRecordCalibration matches[4]{};
    bool found[4]{};
    fixture.backend.readCalls = 0;

    TEST_ASSERT_EQUAL_size_t(3, fixture.store.findAny(page, 4, matches, found));

    TEST_ASSERT_TRUE(found[0]);
    TEST_ASSERT_FALSE(found[1]);
    TEST_ASSERT_TRUE(found[2]);
    TEST_ASSERT_TRUE(found[3]);
    TEST_ASSERT_EQUAL_UINT32(5600, matches[0].actualMl);
    TEST_ASSERT_EQUAL_UINT32(5350, matches[2].actualMl);
    TEST_ASSERT_EQUAL_UINT32(5000, matches[3].actualMl);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(2, fixture.backend.readCalls);
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
