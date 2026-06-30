#include <unity.h>

#include "app/CalibrationSessionStore.h"
#include "../support/MemoryFileBackend.h"

#include <vector>

using namespace faucet;
using faucet_test::MemoryFileBackend;

namespace {

struct SessionFileStoreFixture {
    MemoryFileBackend backend;
    CalibrationSessionFileStore store;

    SessionFileStoreFixture() : store(backend, "/session.bin") {}

    void begin() {
        TEST_ASSERT_TRUE(store.begin());
    }
};

}  // namespace

void test_session_store_writes_and_reads_one_current_session() {
    SessionFileStoreFixture fixture;
    fixture.begin();

    CalibrationSessionRecord session = makeCalibrationSession(7, 1770000000);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = 2;
    TEST_ASSERT_TRUE(fixture.store.save(session));

    CalibrationSessionFileStore loaded(fixture.backend, "/session.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    CalibrationSessionRecord output{};
    TEST_ASSERT_TRUE(loaded.load(output));
    TEST_ASSERT_EQUAL_UINT32(7, output.sessionId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::ReadyToGenerate),
                            static_cast<unsigned>(output.status));
    TEST_ASSERT_EQUAL_UINT8(2, output.validSampleCount);
}

void test_session_store_rebuilds_corrupt_checksum_file_as_empty_session() {
    SessionFileStoreFixture fixture;
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.save(makeCalibrationSession(9, 1770000000)));

    fixture.backend.files["/session.bin"][sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t)] ^= 0x7f;

    CalibrationSessionFileStore loaded(fixture.backend, "/session.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_TRUE(loaded.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::Ready),
                            static_cast<unsigned>(loaded.status()));
    TEST_ASSERT_TRUE(fixture.backend.exists("/session.bin"));
    CalibrationSessionRecord output{};
    TEST_ASSERT_TRUE(loaded.load(output));
    TEST_ASSERT_EQUAL_UINT32(0, output.sessionId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Idle),
                            static_cast<unsigned>(output.status));
}

void test_session_store_rebuilds_too_small_file_as_empty_session() {
    SessionFileStoreFixture fixture;
    fixture.backend.files["/session.bin"] = std::vector<std::uint8_t>(7, 0x55);

    CalibrationSessionFileStore loaded(fixture.backend, "/session.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_TRUE(loaded.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::Ready),
                            static_cast<unsigned>(loaded.status()));
    CalibrationSessionRecord output{};
    TEST_ASSERT_TRUE(loaded.load(output));
    TEST_ASSERT_EQUAL_UINT32(0, output.sessionId);
}

void test_session_store_ignores_trailing_bytes_when_header_is_current() {
    SessionFileStoreFixture fixture;
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.save(makeCalibrationSession(9, 1770000000)));
    fixture.backend.files["/session.bin"].resize(1380, 0xaa);

    CalibrationSessionFileStore loaded(fixture.backend, "/session.bin");
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_TRUE(loaded.ready());
    TEST_ASSERT_EQUAL_size_t(1380, fixture.backend.files["/session.bin"].size());
    CalibrationSessionRecord output{};
    TEST_ASSERT_TRUE(loaded.load(output));
    TEST_ASSERT_EQUAL_UINT32(9, output.sessionId);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_session_store_writes_and_reads_one_current_session);
    RUN_TEST(test_session_store_rebuilds_corrupt_checksum_file_as_empty_session);
    RUN_TEST(test_session_store_rebuilds_too_small_file_as_empty_session);
    RUN_TEST(test_session_store_ignores_trailing_bytes_when_header_is_current);
    return UNITY_END();
}
