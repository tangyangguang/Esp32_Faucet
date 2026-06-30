#include <unity.h>

#include "app/CalibrationSessionTraceStore.h"
#include "../support/MemoryFileBackend.h"

#include <vector>

using namespace faucet;
using faucet_test::MemoryFileBackend;

namespace {

CalibrationStoredTrace traceFor(std::uint32_t sessionId, std::uint8_t attemptIndex, std::uint32_t actualMl) {
    CalibrationStoredTrace trace{};
    trace.sessionId = sessionId;
    trace.attemptIndex = attemptIndex;
    trace.actualMl = actualMl;
    trace.savedAt = 1770000000 + attemptIndex;
    trace.trace.traceId = 100 + attemptIndex;
    trace.trace.startTime = 1770000000 + attemptIndex;
    trace.trace.bucketCount = 2;
    trace.trace.startupEdgeCount = 3;
    trace.trace.totalPulses = 3;
    trace.trace.actualMl = actualMl;
    trace.trace.finished = true;
    trace.trace.finalState = WaterPulseTraceState::Completed;
    return trace;
}

void fillSamples(WaterPulseTraceSample (&samples)[3], std::uint32_t baseUs) {
    samples[0].elapsedUs = 0;
    samples[1].elapsedUs = baseUs;
    samples[2].elapsedUs = baseUs * 2;
}

void fillBuckets(WaterPulseTraceBucketSample (&buckets)[2]) {
    buckets[0].pulseCount = 1;
    buckets[1].pulseCount = 2;
}

struct SessionTraceStoreFixture {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store;

    SessionTraceStoreFixture() : store(backend, "/session-traces.bin") {}

    void begin() {
        TEST_ASSERT_TRUE(store.begin());
    }
};

}  // namespace

void test_session_trace_store_has_six_slots_and_creates_file_on_begin() {
    SessionTraceStoreFixture fixture;

    TEST_ASSERT_FALSE(fixture.backend.exists("/session-traces.bin"));
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.backend.exists("/session-traces.bin"));
    TEST_ASSERT_EQUAL_size_t(6, kCalibrationSessionTraceSlots);
    TEST_ASSERT_EQUAL_size_t(6, fixture.store.capacity());
    const std::size_t expectedMin =
        24 + kCalibrationSessionTraceSlots * sizeof(CalibrationStoredTrace) +
        kCalibrationSessionTraceSlots * kPulseTraceMaxBucketsPerTrace * sizeof(WaterPulseTraceBucketSample) +
        kCalibrationSessionTraceSlots * kPulseTraceMaxStartupEdgesPerTrace * sizeof(WaterPulseTraceSample);
    TEST_ASSERT_TRUE(static_cast<std::size_t>(fixture.backend.fileSize("/session-traces.bin")) >= expectedMin);
}

void test_session_trace_store_rebuilds_invalid_existing_file() {
    SessionTraceStoreFixture fixture;
    fixture.backend.files["/session-traces.bin"] = std::vector<std::uint8_t>(7, 0x55);

    fixture.begin();
    TEST_ASSERT_TRUE(fixture.store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::Ready),
                            static_cast<unsigned>(fixture.store.status()));
    TEST_ASSERT_TRUE(fixture.backend.exists("/session-traces.bin"));
}

void test_session_trace_valid_round_trips_compact_trace() {
    SessionTraceStoreFixture fixture;
    fixture.begin();
    WaterPulseTraceBucketSample buckets[2]{};
    WaterPulseTraceSample startup[3]{};
    fillBuckets(buckets);
    fillSamples(startup, 10000);

    TEST_ASSERT_TRUE(fixture.backend.exists("/session-traces.bin"));
    TEST_ASSERT_TRUE(fixture.store.saveValid(0, traceFor(11, 0, 0), buckets, 2, startup, 3, 1000, 1770000100));
    TEST_ASSERT_TRUE(fixture.backend.exists("/session-traces.bin"));
    CalibrationStoredTrace loaded{};
    TEST_ASSERT_TRUE(fixture.store.load(0, loaded));
    TEST_ASSERT_TRUE(loaded.valid);
    TEST_ASSERT_EQUAL_UINT32(1000, loaded.actualMl);
    TEST_ASSERT_EQUAL_UINT32(1770000100, loaded.savedAt);
    TEST_ASSERT_EQUAL_UINT32(1000, loaded.trace.actualMl);
    TEST_ASSERT_EQUAL_size_t(2, loaded.trace.bucketCount);
    TEST_ASSERT_EQUAL_size_t(3, loaded.trace.startupEdgeCount);
    WaterPulseTraceBucketSample copiedBuckets[2]{};
    WaterPulseTraceSample copiedStartup[3]{};
    TEST_ASSERT_EQUAL_size_t(2, fixture.store.readBuckets(0, copiedBuckets, 2));
    TEST_ASSERT_EQUAL_size_t(3, fixture.store.readStartupEdges(0, copiedStartup, 3));
    TEST_ASSERT_EQUAL_UINT16(2, copiedBuckets[1].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(20000, copiedStartup[2].elapsedUs);
}

void test_session_trace_store_accepts_sixth_valid_sample_slot() {
    SessionTraceStoreFixture fixture;
    fixture.begin();
    WaterPulseTraceBucketSample buckets[2]{};
    WaterPulseTraceSample startup[3]{};
    fillBuckets(buckets);
    fillSamples(startup, 10000);

    const std::uint8_t slot = 5;
    TEST_ASSERT_TRUE(fixture.store.saveValid(slot, traceFor(11, slot, 0), buckets, 2, startup, 3, 3000, 1770000100));

    CalibrationStoredTrace loaded{};
    TEST_ASSERT_TRUE(fixture.store.load(slot, loaded));
    TEST_ASSERT_TRUE(loaded.valid);
    TEST_ASSERT_EQUAL_UINT32(3000, loaded.actualMl);
    TEST_ASSERT_EQUAL_size_t(2, fixture.store.readBuckets(slot, buckets, 2));
    TEST_ASSERT_EQUAL_size_t(3, fixture.store.readStartupEdges(slot, startup, 3));
}

void test_starting_new_session_reuses_existing_trace_file_without_clearing_slots() {
    SessionTraceStoreFixture fixture;
    fixture.begin();
    WaterPulseTraceBucketSample buckets[2]{};
    WaterPulseTraceSample startup[3]{};
    fillBuckets(buckets);
    fillSamples(startup, 10000);
    TEST_ASSERT_TRUE(fixture.store.saveValid(0, traceFor(11, 0, 0), buckets, 2, startup, 3, 1000, 1770000100));

    TEST_ASSERT_TRUE(fixture.store.clearForNewSession());

    CalibrationStoredTrace loaded{};
    TEST_ASSERT_TRUE(fixture.store.load(0, loaded));
    TEST_ASSERT_EQUAL_UINT32(11, loaded.sessionId);
    TEST_ASSERT_EQUAL_size_t(2, fixture.store.readBuckets(0, buckets, 2));
    TEST_ASSERT_EQUAL_size_t(3, fixture.store.readStartupEdges(0, startup, 3));
}

void test_starting_new_session_creates_missing_trace_file() {
    SessionTraceStoreFixture fixture;
    fixture.begin();
    TEST_ASSERT_TRUE(fixture.backend.exists("/session-traces.bin"));

    TEST_ASSERT_TRUE(fixture.store.clearForNewSession());

    TEST_ASSERT_TRUE(fixture.backend.exists("/session-traces.bin"));
    CalibrationStoredTrace loaded{};
    TEST_ASSERT_FALSE(fixture.store.load(0, loaded));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_session_trace_store_has_six_slots_and_creates_file_on_begin);
    RUN_TEST(test_session_trace_store_rebuilds_invalid_existing_file);
    RUN_TEST(test_session_trace_valid_round_trips_compact_trace);
    RUN_TEST(test_session_trace_store_accepts_sixth_valid_sample_slot);
    RUN_TEST(test_starting_new_session_reuses_existing_trace_file_without_clearing_slots);
    RUN_TEST(test_starting_new_session_creates_missing_trace_file);
    return UNITY_END();
}
