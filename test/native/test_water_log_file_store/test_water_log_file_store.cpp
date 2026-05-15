#include <unity.h>

#include "app/WaterLogFileStore.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterLogFileBackend {
public:
    bool failWrite = false;
    bool failAppend = false;
    bool failWriteAt = false;
    bool failRead = false;

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
        files.erase(path ? path : "");
        return true;
    }

private:
    std::map<std::string, std::vector<std::uint8_t>> files;
};

WaterLogRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl) {
    return WaterLogRecord{
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
        1.0f,
        {0, 0, 0, 0},
    };
}

}  // namespace

void test_file_log_initializes_empty_file() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 10);

    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.ready());
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(10, store.capacity());
    TEST_ASSERT_TRUE(backend.exists("/water.bin"));
    TEST_ASSERT_EQUAL_INT64(24, backend.fileSize("/water.bin"));
}

void test_file_log_appends_and_reads_newest_first() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 4);
    TEST_ASSERT_TRUE(store.begin());

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));

    WaterLogRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readPage(0, 3, page, 3));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(200, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[2].startTime);
}

void test_file_log_rolls_after_capacity() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());

    for (std::uint32_t i = 1; i <= 5; ++i) {
        TEST_ASSERT_TRUE(store.append(makeRecord(i * 100, i * 1000)));
    }

    WaterLogRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readPage(0, 3, page, 3));
    TEST_ASSERT_EQUAL_UINT32(500, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(400, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(300, page[2].startTime);
}

void test_file_log_persists_header_and_records_across_instances() {
    MemoryFileBackend backend;
    {
        WaterLogFileStore store(backend, "/water.bin", 5);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
        TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    }

    WaterLogFileStore loaded(backend, "/water.bin", 5);
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_EQUAL_size_t(2, loaded.count());

    WaterLogRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, loaded.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(200, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[1].startTime);
}

void test_file_log_reinitializes_capacity_mismatch() {
    MemoryFileBackend backend;
    {
        WaterLogFileStore store(backend, "/water.bin", 5);
        TEST_ASSERT_TRUE(store.begin());
        TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    }

    WaterLogFileStore loaded(backend, "/water.bin", 4);
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_EQUAL_size_t(0, loaded.count());
    TEST_ASSERT_EQUAL_size_t(4, loaded.capacity());
}

void test_file_log_reinitializes_corrupt_header() {
    MemoryFileBackend backend;
    const std::uint8_t bad[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(backend.writeAt("/water.bin", 0, bad, sizeof(bad)));

    WaterLogFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(3, store.capacity());
}

void test_file_log_clear_keeps_file_ready() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));

    TEST_ASSERT_TRUE(store.clear());

    WaterLogRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(0, store.readPage(0, 1, page, 1));
}

void test_file_log_reports_zero_after_external_remove_and_recovers_on_append() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_size_t(2, store.count());

    TEST_ASSERT_TRUE(backend.removeFile("/water.bin"));

    WaterLogRecord page[2]{};
    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_EQUAL_STRING("unavailable", store.storageName());
    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_EQUAL_size_t(0, store.readPage(0, 2, page, 2));

    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));
    TEST_ASSERT_TRUE(store.ready());
    TEST_ASSERT_EQUAL_STRING("file", store.storageName());
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_size_t(1, store.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(300, page[0].startTime);
}

void test_file_log_grows_records_on_demand() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_EQUAL_INT64(24 + static_cast<int>(sizeof(WaterLogRecord)), backend.fileSize("/water.bin"));

    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_INT64(24 + static_cast<int>(sizeof(WaterLogRecord) * 2), backend.fileSize("/water.bin"));
}

void test_file_log_reports_backend_failures() {
    MemoryFileBackend backend;
    backend.failWrite = true;
    WaterLogFileStore store(backend, "/water.bin", 3);

    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_FALSE(store.ready());
}

void test_file_log_append_failure_keeps_runtime_state() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));

    backend.failAppend = true;
    TEST_ASSERT_FALSE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_size_t(1, store.count());

    WaterLogRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(1, store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_UINT32(100, page[0].startTime);
}

void test_file_log_header_failure_rolls_back_runtime_state() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 3);
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));

    backend.failWriteAt = true;
    TEST_ASSERT_FALSE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_EQUAL_size_t(1, store.count());

    backend.failWriteAt = false;
    WaterLogRecord page[1]{};
    TEST_ASSERT_EQUAL_size_t(1, store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_UINT32(100, page[0].startTime);
}

void test_file_log_rewrites_current_boot_relative_times() {
    MemoryFileBackend backend;
    WaterLogFileStore store(backend, "/water.bin", 4);
    TEST_ASSERT_TRUE(store.begin());
    WaterLogRecord current = makeRecord(21, 1500);
    markWaterLogBootId(current, 12);
    WaterLogRecord old = makeRecord(31, 500);
    markWaterLogBootId(old, 11);

    TEST_ASSERT_TRUE(store.append(current));
    TEST_ASSERT_TRUE(store.append(old));

    TEST_ASSERT_EQUAL_size_t(1, store.rewriteBootRelativeTimes(12, 815500000));
    WaterLogRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, store.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(31, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(11, waterLogBootId(page[0]));
    TEST_ASSERT_EQUAL_UINT32(815500021, page[1].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, waterLogBootId(page[1]));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_file_log_initializes_empty_file);
    RUN_TEST(test_file_log_appends_and_reads_newest_first);
    RUN_TEST(test_file_log_rolls_after_capacity);
    RUN_TEST(test_file_log_persists_header_and_records_across_instances);
    RUN_TEST(test_file_log_reinitializes_capacity_mismatch);
    RUN_TEST(test_file_log_reinitializes_corrupt_header);
    RUN_TEST(test_file_log_clear_keeps_file_ready);
    RUN_TEST(test_file_log_reports_zero_after_external_remove_and_recovers_on_append);
    RUN_TEST(test_file_log_grows_records_on_demand);
    RUN_TEST(test_file_log_reports_backend_failures);
    RUN_TEST(test_file_log_append_failure_keeps_runtime_state);
    RUN_TEST(test_file_log_header_failure_rolls_back_runtime_state);
    RUN_TEST(test_file_log_rewrites_current_boot_relative_times);
    return UNITY_END();
}
