#include <unity.h>

#include "app/CalibrationSampleStore.h"

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
    std::size_t writeCalls = 0;

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
        ++writeCalls;
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

CalibrationStoredTrace traceFor(std::uint32_t sessionId, std::uint8_t attemptIndex, std::uint32_t actualMl) {
    CalibrationStoredTrace trace{};
    trace.sessionId = sessionId;
    trace.attemptIndex = attemptIndex;
    trace.actualMl = actualMl;
    trace.savedAt = 1770000000 + attemptIndex;
    trace.trace.traceId = 100 + attemptIndex;
    trace.trace.startTime = 1770000000 + attemptIndex;
    trace.trace.sampleCount = 3;
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

}  // namespace

void test_session_trace_store_has_ten_slots_and_creates_file_on_begin() {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");

    TEST_ASSERT_FALSE(backend.exists("/session-traces.bin"));
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(backend.exists("/session-traces.bin"));
    TEST_ASSERT_EQUAL_size_t(10, kCalibrationSessionTraceSlots);
    TEST_ASSERT_EQUAL_size_t(10, store.capacity());
}

void test_session_trace_store_rebuilds_invalid_existing_file() {
    MemoryFileBackend backend;
    backend.files["/session-traces.bin"] = std::vector<std::uint8_t>(7, 0x55);
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");

    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::Ready),
                            static_cast<unsigned>(store.status()));
    TEST_ASSERT_TRUE(backend.exists("/session-traces.bin"));
    TEST_ASSERT_EQUAL_size_t(1, backend.removeCalls);
}

void test_session_trace_pending_then_valid_round_trips_samples() {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");
    TEST_ASSERT_TRUE(store.begin());
    WaterPulseTraceSample samples[3]{};
    fillSamples(samples, 10000);

    TEST_ASSERT_TRUE(backend.exists("/session-traces.bin"));
    TEST_ASSERT_TRUE(store.savePending(0, traceFor(11, 0, 0), samples, 3));
    TEST_ASSERT_TRUE(backend.exists("/session-traces.bin"));
    CalibrationStoredTrace pending{};
    TEST_ASSERT_TRUE(store.load(0, pending));
    TEST_ASSERT_TRUE(pending.pendingActual);
    TEST_ASSERT_FALSE(pending.valid);

    TEST_ASSERT_TRUE(store.commitValid(0, 1000, 1770000100));
    CalibrationStoredTrace loaded{};
    TEST_ASSERT_TRUE(store.load(0, loaded));
    TEST_ASSERT_FALSE(loaded.pendingActual);
    TEST_ASSERT_TRUE(loaded.valid);
    TEST_ASSERT_EQUAL_UINT32(1000, loaded.actualMl);
    WaterPulseTraceSample copied[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readSamples(0, copied, 3));
    TEST_ASSERT_EQUAL_UINT32(20000, copied[2].elapsedUs);
}

void test_session_trace_store_accepts_tenth_valid_sample_slot() {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");
    TEST_ASSERT_TRUE(store.begin());
    WaterPulseTraceSample samples[3]{};
    fillSamples(samples, 10000);

    const std::uint8_t slot = 9;
    TEST_ASSERT_TRUE(store.savePending(slot, traceFor(11, slot, 0), samples, 3));
    TEST_ASSERT_TRUE(store.commitValid(slot, 3000, 1770000100));

    CalibrationStoredTrace loaded{};
    TEST_ASSERT_TRUE(store.load(slot, loaded));
    TEST_ASSERT_TRUE(loaded.valid);
    TEST_ASSERT_EQUAL_UINT32(3000, loaded.actualMl);
    TEST_ASSERT_EQUAL_size_t(3, store.readSamples(slot, samples, 3));
}

void test_starting_new_session_reuses_existing_trace_file_without_clearing_slots() {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");
    TEST_ASSERT_TRUE(store.begin());
    WaterPulseTraceSample samples[3]{};
    fillSamples(samples, 10000);
    TEST_ASSERT_TRUE(store.savePending(0, traceFor(11, 0, 0), samples, 3));
    TEST_ASSERT_TRUE(store.commitValid(0, 1000, 1770000100));
    backend.writeCalls = 0;

    TEST_ASSERT_TRUE(store.clearForNewSession(12));

    CalibrationStoredTrace loaded{};
    TEST_ASSERT_TRUE(store.load(0, loaded));
    TEST_ASSERT_EQUAL_UINT32(11, loaded.sessionId);
    TEST_ASSERT_EQUAL_size_t(3, store.readSamples(0, samples, 3));
    TEST_ASSERT_EQUAL_size_t(0, backend.writeCalls);
}

void test_starting_new_session_creates_missing_trace_file() {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(backend.exists("/session-traces.bin"));

    TEST_ASSERT_TRUE(store.clearForNewSession(12));

    TEST_ASSERT_TRUE(backend.exists("/session-traces.bin"));
    CalibrationStoredTrace loaded{};
    TEST_ASSERT_FALSE(store.load(0, loaded));
}

void test_long_term_sample_store_has_exactly_five_slots_and_creates_file_on_begin() {
    MemoryFileBackend backend;
    CalibrationLongTermSampleStore store(backend, "/samples.bin");

    TEST_ASSERT_FALSE(backend.exists("/samples.bin"));
    TEST_ASSERT_TRUE(store.begin());
    TEST_ASSERT_TRUE(backend.exists("/samples.bin"));
    TEST_ASSERT_EQUAL_size_t(5, kCalibrationLongTermSampleSlots);
    TEST_ASSERT_EQUAL_size_t(5, store.capacity());
}

void test_long_term_sample_store_preserves_invalid_existing_file() {
    MemoryFileBackend backend;
    backend.files["/samples.bin"] = std::vector<std::uint8_t>(7, 0x55);
    CalibrationLongTermSampleStore store(backend, "/samples.bin");

    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(AppStorageStatus::Corrupt),
                            static_cast<unsigned>(store.status()));
    TEST_ASSERT_TRUE(backend.exists("/samples.bin"));
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    CalibrationStoredTrace samples[kCalibrationLongTermSampleSlots]{};
    TEST_ASSERT_EQUAL_size_t(0, store.list(samples, kCalibrationLongTermSampleSlots));
}

void test_long_term_sample_store_refuses_the_sixth_sample() {
    MemoryFileBackend backend;
    CalibrationLongTermSampleStore store(backend, "/samples.bin");
    TEST_ASSERT_TRUE(store.begin());
    WaterPulseTraceSample samples[3]{};
    fillSamples(samples, 10000);

    std::uint32_t sampleId = 0;
    for (std::uint8_t i = 0; i < kCalibrationLongTermSampleSlots; ++i) {
        TEST_ASSERT_TRUE(store.save(traceFor(20, i, 1000 + i), samples, 3, sampleId));
        TEST_ASSERT_NOT_EQUAL_UINT32(0, sampleId);
    }

    TEST_ASSERT_FALSE(store.save(traceFor(20, 11, 2000), samples, 3, sampleId));
}

void test_long_term_sample_remove_clears_index_and_frees_slot() {
    MemoryFileBackend backend;
    CalibrationLongTermSampleStore store(backend, "/samples.bin");
    TEST_ASSERT_TRUE(store.begin());
    WaterPulseTraceSample samples[3]{};
    fillSamples(samples, 10000);
    std::uint32_t removedId = 0;
    std::uint32_t sampleId = 0;
    for (std::uint8_t i = 0; i < kCalibrationLongTermSampleSlots; ++i) {
        TEST_ASSERT_TRUE(store.save(traceFor(20, i, 1000 + i), samples, 3, sampleId));
        if (i == 3) {
            removedId = sampleId;
        }
    }

    TEST_ASSERT_TRUE(store.remove(removedId));
    CalibrationStoredTrace removed{};
    TEST_ASSERT_FALSE(store.load(removedId, removed));
    TEST_ASSERT_TRUE(store.save(traceFor(21, 9, 3000), samples, 3, sampleId));
}

void test_long_term_sample_store_reads_samples_by_sample_id() {
    MemoryFileBackend backend;
    CalibrationLongTermSampleStore store(backend, "/samples.bin");
    TEST_ASSERT_TRUE(store.begin());
    WaterPulseTraceSample samples[3]{};
    fillSamples(samples, 12000);
    std::uint32_t sampleId = 0;
    TEST_ASSERT_TRUE(store.save(traceFor(20, 1, 1800), samples, 3, sampleId));

    WaterPulseTraceSample copied[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readSamples(sampleId, copied, 3));
    TEST_ASSERT_EQUAL_UINT32(24000, copied[2].elapsedUs);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_session_trace_store_has_ten_slots_and_creates_file_on_begin);
    RUN_TEST(test_session_trace_store_rebuilds_invalid_existing_file);
    RUN_TEST(test_session_trace_pending_then_valid_round_trips_samples);
    RUN_TEST(test_session_trace_store_accepts_tenth_valid_sample_slot);
    RUN_TEST(test_starting_new_session_reuses_existing_trace_file_without_clearing_slots);
    RUN_TEST(test_starting_new_session_creates_missing_trace_file);
    RUN_TEST(test_long_term_sample_store_has_exactly_five_slots_and_creates_file_on_begin);
    RUN_TEST(test_long_term_sample_store_preserves_invalid_existing_file);
    RUN_TEST(test_long_term_sample_store_refuses_the_sixth_sample);
    RUN_TEST(test_long_term_sample_remove_clears_index_and_frees_slot);
    RUN_TEST(test_long_term_sample_store_reads_samples_by_sample_id);
    return UNITY_END();
}
