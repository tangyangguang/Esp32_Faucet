#include <unity.h>

#include "app/CalibrationSessionStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::size_t removeCalls = 0;

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
        ++removeCalls;
        files.erase(path ? path : "");
        return true;
    }
};

}  // namespace

void test_session_store_writes_and_reads_one_current_session() {
    MemoryFileBackend backend;
    CalibrationSessionFileStore store(backend, "/session.bin");
    TEST_ASSERT_TRUE(store.begin());

    CalibrationSessionRecord session = makeCalibrationSession(7, 1770000000);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = 2;
    TEST_ASSERT_TRUE(store.save(session));

    CalibrationSessionFileStore loaded(backend, "/session.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    CalibrationSessionRecord output{};
    TEST_ASSERT_TRUE(loaded.load(output));
    TEST_ASSERT_EQUAL_UINT32(7, output.sessionId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::ReadyToGenerate),
                            static_cast<unsigned>(output.status));
    TEST_ASSERT_EQUAL_UINT8(2, output.validSampleCount);
}

void test_session_store_preserves_corrupt_checksum_file() {
    MemoryFileBackend backend;
    CalibrationSessionFileStore store(backend, "/session.bin");
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.save(makeCalibrationSession(9, 1770000000)));

    backend.files["/session.bin"][sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t)] ^= 0x7f;

    CalibrationSessionFileStore loaded(backend, "/session.bin");
    TEST_ASSERT_FALSE(loaded.begin());
    TEST_ASSERT_FALSE(loaded.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::Corrupt),
                            static_cast<unsigned>(loaded.status()));
    TEST_ASSERT_TRUE(backend.exists("/session.bin"));
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    CalibrationSessionRecord output{};
    TEST_ASSERT_FALSE(loaded.load(output));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_session_store_writes_and_reads_one_current_session);
    RUN_TEST(test_session_store_preserves_corrupt_checksum_file);
    return UNITY_END();
}
