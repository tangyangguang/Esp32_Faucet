#include <unity.h>

#include "app/WaterRecordFileStore.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    bool failWrite = false;
    bool failAppend = false;
    bool failWriteAt = false;
    bool failRead = false;
    std::size_t createSizedCalls = 0;
    std::size_t removeCalls = 0;
    std::size_t readCalls = 0;

    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
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
        if (failWrite || failAppend || !path || (!data && len > 0)) {
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
        if (failRead || !out) {
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
        if (failWrite || failWriteAt || !path) {
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
    std::map<std::string, std::vector<std::uint8_t>> files;
};

WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl) {
    return WaterRecord{
        startTime,
        volumeMl,
        1500,
        volumeMl,
        0,
        10,
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        0,
        1,
        {0, 0, 0, 0},
    };
}

}  // namespace

void test_file_record_initializes_empty_file() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 10);

    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Ready),
                            static_cast<std::uint8_t>(store.status()));
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(10, store.capacity());
    TEST_ASSERT_TRUE(backend.exists("/water.bin"));
    TEST_ASSERT_EQUAL_INT64(24 + static_cast<int>(sizeof(WaterRecord) * 10) + 24,
                            backend.fileSize("/water.bin"));
}

void test_file_record_appends_and_reads_newest_first() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 4);
    TEST_ASSERT_TRUE(store.begin());

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));

    WaterRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readPage(0, 3, page, 3));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(200, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[2].startTime);
}

void test_file_record_rolls_after_capacity() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());

    for (std::uint32_t i = 1; i <= 5; ++i) {
        TEST_ASSERT_TRUE(store.append(makeRecord(i * 100, i * 1000)));
    }

    WaterRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readPage(0, 3, page, 3));
    TEST_ASSERT_EQUAL_UINT32(500, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(400, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(300, page[2].startTime);
}

void test_file_record_reads_page_in_contiguous_spans() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 5);
    TEST_ASSERT_TRUE(store.begin());

    for (std::uint32_t i = 1; i <= 7; ++i) {
        TEST_ASSERT_TRUE(store.append(makeRecord(i * 100, i * 1000)));
    }

    backend.readCalls = 0;
    WaterRecord page[5]{};
    TEST_ASSERT_EQUAL_size_t(5, store.readPage(0, 5, page, 5));
    TEST_ASSERT_EQUAL_UINT32(700, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(600, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(500, page[2].startTime);
    TEST_ASSERT_EQUAL_UINT32(400, page[3].startTime);
    TEST_ASSERT_EQUAL_UINT32(300, page[4].startTime);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(2, backend.readCalls);
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
    TEST_ASSERT_EQUAL_size_t(4, loaded.capacity());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(originalSize, backend.fileSize("/water.bin"));

    WaterRecordFileStore original(backend, "/water.bin", 5);
    TEST_ASSERT_TRUE(original.begin());
    TEST_ASSERT_EQUAL_size_t(1, original.count());
}

void test_file_record_corrupt_header_preserves_existing_file() {
    MemoryFileBackend backend;
    const std::uint8_t bad[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(backend.writeAt("/water.bin", 0, bad, sizeof(bad)));
    const std::size_t createCalls = backend.createSizedCalls;

    WaterRecordFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Corrupt),
                            static_cast<std::uint8_t>(store.status()));
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(3, store.capacity());
    TEST_ASSERT_EQUAL_size_t(createCalls, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_INT64(4, backend.fileSize("/water.bin"));
}

void test_file_record_clear_keeps_file_ready() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));

    TEST_ASSERT_TRUE(store.clear());

    WaterRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(0, store.readPage(0, 1, page, 1));
}

void test_file_record_reports_zero_after_external_remove_and_recovers_on_append() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_size_t(2, store.count());

    TEST_ASSERT_TRUE(backend.removeFile("/water.bin"));

    WaterRecord page[2]{};
    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Missing),
                            static_cast<std::uint8_t>(store.status()));
    TEST_ASSERT_EQUAL_STRING("unavailable", store.storageName());
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(0, store.readPage(0, 2, page, 2));

    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));
    TEST_ASSERT_TRUE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterRecordFileStatus::Ready),
                            static_cast<std::uint8_t>(store.status()));
    TEST_ASSERT_EQUAL_STRING("file", store.storageName());
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_size_t(1, store.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
}

void test_file_record_preallocates_record_slots_and_backup_header() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_EQUAL_INT64(24 + static_cast<int>(sizeof(WaterRecord) * 3) + 24,
                            backend.fileSize("/water.bin"));

    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_INT64(24 + static_cast<int>(sizeof(WaterRecord) * 3) + 24,
                            backend.fileSize("/water.bin"));
}

void test_file_record_reports_backend_failures() {
    MemoryFileBackend backend;
    backend.failWrite = true;
    WaterRecordFileStore store(backend, "/water.bin", 3);

    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_FALSE(store.ready());
}

void test_file_record_append_failure_keeps_runtime_state() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));

    backend.failWriteAt = true;
    TEST_ASSERT_FALSE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_size_t(1, store.count());

    WaterRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(1, store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_UINT32(100, page[0].startTime);
}

void test_file_record_header_failure_rolls_back_runtime_state() {
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));

    backend.failWriteAt = true;
    TEST_ASSERT_FALSE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_size_t(1, store.count());

    backend.failWriteAt = false;
    WaterRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(1, store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_UINT32(100, page[0].startTime);
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
    MemoryFileBackend backend;
    WaterRecordFileStore store(backend, "/water.bin", 4);
    TEST_ASSERT_TRUE(store.begin());
    WaterRecord current = makeRecord(21, 1500);
    markWaterRecordBootId(current, 12);
    WaterRecord old = makeRecord(31, 500);
    markWaterRecordBootId(old, 11);

    TEST_ASSERT_TRUE(store.append(current));
    TEST_ASSERT_TRUE(store.append(old));

    TEST_ASSERT_EQUAL_size_t(1, store.rewriteBootRelativeTimes(12, 815500000));
    WaterRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, store.readPage(0, 2, page, 2));
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
    RUN_TEST(test_file_record_appends_and_reads_newest_first);
    RUN_TEST(test_file_record_rolls_after_capacity);
    RUN_TEST(test_file_record_reads_page_in_contiguous_spans);
    RUN_TEST(test_file_record_persists_header_and_records_across_instances);
    RUN_TEST(test_file_record_capacity_mismatch_preserves_existing_file);
    RUN_TEST(test_file_record_corrupt_header_preserves_existing_file);
    RUN_TEST(test_file_record_clear_keeps_file_ready);
    RUN_TEST(test_file_record_reports_zero_after_external_remove_and_recovers_on_append);
    RUN_TEST(test_file_record_preallocates_record_slots_and_backup_header);
    RUN_TEST(test_file_record_reports_backend_failures);
    RUN_TEST(test_file_record_append_failure_keeps_runtime_state);
    RUN_TEST(test_file_record_header_failure_rolls_back_runtime_state);
    RUN_TEST(test_file_record_recovers_from_corrupt_primary_header_using_backup);
    RUN_TEST(test_file_record_rewrites_current_boot_relative_times);
    return UNITY_END();
}
