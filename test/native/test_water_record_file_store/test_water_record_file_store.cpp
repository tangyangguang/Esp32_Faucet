#include <unity.h>

#include "app/WaterRecordFileStore.h"
#include "../support/MemoryFileBackend.h"

#include <algorithm>

using namespace faucet;
using faucet_test::MemoryFileBackend;

namespace {

constexpr std::size_t kWaterRecordHeaderBytes = 64;

WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl) {
    WaterRecord record{};
    record.startTime = startTime;
    record.volumeMl = volumeMl;
    record.targetValue = 1500;
    record.pulseCount = volumeMl;
    record.durationSec = 10;
    record.mode = WaterMode::Volume;
    record.result = WaterResult::Completed;
    record.meteringSchemeId = 1;
    return record;
}

struct RecordFileFixture {
    MemoryFileBackend backend;
    WaterRecordFileStore store;

    explicit RecordFileFixture(std::size_t capacity)
        : store(backend, "/water.bin", capacity) {}

    void begin() {
        TEST_ASSERT_TRUE(store.begin());
    }
};

}  // namespace

void test_file_record_initializes_empty_file() {
    RecordFileFixture fixture(10);

    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Ready),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.count());
    TEST_ASSERT_TRUE(fixture.backend.exists("/water.bin"));
    TEST_ASSERT_EQUAL_INT64(static_cast<std::int64_t>(kWaterRecordHeaderBytes), fixture.backend.fileSize("/water.bin"));
    TEST_ASSERT_EQUAL_size_t(0, fixture.backend.createSizedCalls);
}

void test_file_record_reports_mismatched_schema_as_incompatible() {
    RecordFileFixture fixture(3);
    const std::uint8_t mismatchedHeader[kWaterRecordHeaderBytes] = {
        0x44, 0x52, 0x57, 0x46,  // FWRD
        0x01, 0x00,              // non-current version
        0x20, 0x00,              // non-current record size
        0x03, 0x00, 0x00, 0x00,
    };
    TEST_ASSERT_TRUE(fixture.backend.writeAt("/water.bin", 0, mismatchedHeader, sizeof(mismatchedHeader)));

    TEST_ASSERT_FALSE(fixture.store.begin());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::IncompatibleFormat),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_EQUAL_INT64(static_cast<std::int64_t>(kWaterRecordHeaderBytes),
                            fixture.backend.fileSize("/water.bin"));
    TEST_ASSERT_EQUAL_size_t(0, fixture.backend.removeCalls);
}

void test_file_record_appends_and_reads_newest_first() {
    RecordFileFixture fixture(4);
    fixture.begin();

    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(200, 2000)));
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(300, 3000)));

    WaterRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, fixture.store.readPage(0, 3, page, 3));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(200, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[2].startTime);
}

void test_file_record_rolls_after_capacity() {
    RecordFileFixture fixture(3);
    fixture.begin();

    for (std::uint32_t i = 1; i <= 5; ++i) {
        TEST_ASSERT_TRUE(fixture.store.append(makeRecord(i * 100, i * 1000)));
    }

    WaterRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, fixture.store.readPage(0, 3, page, 3));
    TEST_ASSERT_EQUAL_UINT32(500, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(400, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(300, page[2].startTime);
}

void test_file_record_reads_page_in_contiguous_spans() {
    RecordFileFixture fixture(5);
    fixture.begin();

    for (std::uint32_t i = 1; i <= 7; ++i) {
        TEST_ASSERT_TRUE(fixture.store.append(makeRecord(i * 100, i * 1000)));
    }

    fixture.backend.readCalls = 0;
    WaterRecord page[5]{};
    TEST_ASSERT_EQUAL_size_t(5, fixture.store.readPage(0, 5, page, 5));
    TEST_ASSERT_EQUAL_UINT32(700, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(600, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(500, page[2].startTime);
    TEST_ASSERT_EQUAL_UINT32(400, page[3].startTime);
    TEST_ASSERT_EQUAL_UINT32(300, page[4].startTime);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(2, fixture.backend.readCalls);
}

void test_file_record_persists_header_and_records_across_instances() {
    MemoryFileBackend backend;
    {
        WaterRecordFileStore store(backend, "/water.bin", 5);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
        TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    }

    WaterRecordFileStore loaded(backend, "/water.bin", 5);
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_EQUAL_size_t(2, loaded.count());

    WaterRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, loaded.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(200, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[1].startTime);
}

void test_file_record_capacity_mismatch_preserves_existing_file() {
    MemoryFileBackend backend;
    {
        WaterRecordFileStore store(backend, "/water.bin", 5);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    }

    const std::int64_t originalSize = backend.fileSize("/water.bin");
    const std::size_t createCalls = backend.createSizedCalls;
    WaterRecordFileStore loaded(backend, "/water.bin", 4);
    TEST_ASSERT_FALSE(loaded.begin());
    TEST_ASSERT_FALSE(loaded.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::IncompatibleFormat),
                            static_cast<std::uint8_t>(loaded.status()));
    TEST_ASSERT_EQUAL_size_t(0, loaded.count());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(originalSize, backend.fileSize("/water.bin"));

    WaterRecordFileStore original(backend, "/water.bin", 5);
    TEST_ASSERT_TRUE(original.begin());
    TEST_ASSERT_EQUAL_size_t(1, original.count());
}

void test_file_record_corrupt_header_preserves_existing_file() {
    RecordFileFixture fixture(3);
    const std::uint8_t bad[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(fixture.backend.writeAt("/water.bin", 0, bad, sizeof(bad)));
    const std::size_t createCalls = fixture.backend.createSizedCalls;

    TEST_ASSERT_FALSE(fixture.store.begin());
    TEST_ASSERT_FALSE(fixture.store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Corrupt),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.count());
    TEST_ASSERT_EQUAL_size_t(createCalls, fixture.backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, fixture.backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(4, fixture.backend.fileSize("/water.bin"));
}

void test_file_record_corrupt_state_reports_missing_after_external_format_and_recovers_on_append() {
    RecordFileFixture fixture(3);
    const std::uint8_t bad[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(fixture.backend.writeAt("/water.bin", 0, bad, sizeof(bad)));
    TEST_ASSERT_FALSE(fixture.store.begin());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Corrupt),
                            static_cast<std::uint8_t>(fixture.store.status()));

    TEST_ASSERT_TRUE(fixture.backend.removeFile("/water.bin"));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Missing),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(300, 3000)));
    TEST_ASSERT_TRUE(fixture.store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Ready),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_EQUAL_size_t(1, fixture.store.count());
}

void test_file_record_clear_keeps_file_ready() {
    RecordFileFixture fixture(3);
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(100, 1000)));

    TEST_ASSERT_TRUE(fixture.store.clear());

    WaterRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.count());
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.readPage(0, 1, page, 1));
}

void test_file_record_reports_zero_after_external_remove_and_recovers_on_append() {
    RecordFileFixture fixture(3);
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_size_t(2, fixture.store.count());

    TEST_ASSERT_TRUE(fixture.backend.removeFile("/water.bin"));

    WaterRecord page[2]{};
    TEST_ASSERT_FALSE(fixture.store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Missing),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_EQUAL_STRING("unavailable", fixture.store.storageName());
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.count());
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.readPage(0, 2, page, 2));

    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(300, 3000)));
    TEST_ASSERT_TRUE(fixture.store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Ready),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_EQUAL_STRING("file", fixture.store.storageName());
    TEST_ASSERT_EQUAL_size_t(1, fixture.store.count());
    TEST_ASSERT_EQUAL_size_t(1, fixture.store.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
}

void test_file_record_grows_record_file_as_records_are_appended() {
    RecordFileFixture fixture(3);
    fixture.begin();

    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(100, 1000)));
    TEST_ASSERT_EQUAL_INT64(static_cast<std::int64_t>(kWaterRecordHeaderBytes + sizeof(WaterRecord)),
                            fixture.backend.fileSize("/water.bin"));

    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_INT64(static_cast<std::int64_t>(kWaterRecordHeaderBytes + sizeof(WaterRecord) * 2),
                            fixture.backend.fileSize("/water.bin"));
}

void test_file_record_reports_backend_failures() {
    RecordFileFixture fixture(3);
    fixture.backend.failWrite = true;

    TEST_ASSERT_FALSE(fixture.store.begin());
    TEST_ASSERT_FALSE(fixture.store.ready());
}

void test_file_record_append_failure_keeps_runtime_state() {
    RecordFileFixture fixture(3);
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(100, 1000)));

    fixture.backend.failWriteAt = true;
    TEST_ASSERT_FALSE(fixture.store.append(makeRecord(200, 2000)));
    TEST_ASSERT_FALSE(fixture.store.ready());
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.count());

    fixture.backend.failWriteAt = false;
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(300, 3000)));
    WaterRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(1, fixture.store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
}

void test_file_record_append_failure_marks_store_unready_for_reader_fallback() {
    RecordFileFixture fixture(3);
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(100, 1000)));

    fixture.backend.failWriteAt = true;
    TEST_ASSERT_FALSE(fixture.store.append(makeRecord(200, 2000)));

    TEST_ASSERT_FALSE(fixture.store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::BackendFailure),
                            static_cast<std::uint8_t>(fixture.store.status()));
    TEST_ASSERT_EQUAL_STRING("unavailable", fixture.store.storageName());
}

void test_file_record_header_failure_rolls_back_runtime_state() {
    RecordFileFixture fixture(3);
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(100, 1000)));

    fixture.backend.failWriteAt = true;
    TEST_ASSERT_FALSE(fixture.store.append(makeRecord(200, 2000)));
    TEST_ASSERT_FALSE(fixture.store.ready());
    TEST_ASSERT_EQUAL_size_t(0, fixture.store.count());

    fixture.backend.failWriteAt = false;
    TEST_ASSERT_TRUE(fixture.store.append(makeRecord(300, 3000)));
    WaterRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(1, fixture.store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
}

void test_file_record_recovers_from_corrupt_primary_header_using_backup() {
    MemoryFileBackend backend;
    {
        WaterRecordFileStore store(backend, "/water.bin", 3);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
        TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    }

    backend.overwriteByte("/water.bin", 0, 0x00);

    WaterRecordFileStore loaded(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_TRUE(loaded.ready());
    TEST_ASSERT_EQUAL_size_t(2, loaded.count());
    WaterRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, loaded.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(200, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[1].startTime);
}

void test_file_record_rewrites_current_boot_relative_times() {
    RecordFileFixture fixture(4);
    fixture.begin();
    WaterRecord current = makeRecord(21, 1500);
    markWaterRecordBootId(current, 12);
    WaterRecord old = makeRecord(31, 500);
    markWaterRecordBootId(old, 11);

    TEST_ASSERT_TRUE(fixture.store.append(current));
    TEST_ASSERT_TRUE(fixture.store.append(old));

    TEST_ASSERT_EQUAL_size_t(1, fixture.store.rewriteBootRelativeTimes(12, 815500000));
    WaterRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, fixture.store.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(31, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(11, waterRecordBootId(page[0]));
    TEST_ASSERT_EQUAL_UINT32(815500021, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, waterRecordBootId(page[1]));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_file_record_initializes_empty_file);
    RUN_TEST(test_file_record_reports_mismatched_schema_as_incompatible);
    RUN_TEST(test_file_record_appends_and_reads_newest_first);
    RUN_TEST(test_file_record_rolls_after_capacity);
    RUN_TEST(test_file_record_reads_page_in_contiguous_spans);
    RUN_TEST(test_file_record_persists_header_and_records_across_instances);
    RUN_TEST(test_file_record_capacity_mismatch_preserves_existing_file);
    RUN_TEST(test_file_record_corrupt_header_preserves_existing_file);
    RUN_TEST(test_file_record_corrupt_state_reports_missing_after_external_format_and_recovers_on_append);
    RUN_TEST(test_file_record_clear_keeps_file_ready);
    RUN_TEST(test_file_record_reports_zero_after_external_remove_and_recovers_on_append);
    RUN_TEST(test_file_record_grows_record_file_as_records_are_appended);
    RUN_TEST(test_file_record_reports_backend_failures);
    RUN_TEST(test_file_record_append_failure_keeps_runtime_state);
    RUN_TEST(test_file_record_append_failure_marks_store_unready_for_reader_fallback);
    RUN_TEST(test_file_record_header_failure_rolls_back_runtime_state);
    RUN_TEST(test_file_record_recovers_from_corrupt_primary_header_using_backup);
    RUN_TEST(test_file_record_rewrites_current_boot_relative_times);
    return UNITY_END();
}
