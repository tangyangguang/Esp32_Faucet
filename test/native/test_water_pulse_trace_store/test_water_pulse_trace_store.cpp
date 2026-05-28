#include <unity.h>

#include "app/WaterPulseTraceStore.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryFileBackend : public WaterRecordFileBackend {
public:
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
        if (!path) {
            return false;
        }
        files[path] = std::vector<std::uint8_t>(size, 0);
        return true;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        ++appendCalls;
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
        ++readCalls;
        if (!path || !out) {
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
        if (!path || (!data && len > 0)) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        if (offset + len > file.size()) {
            return false;
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

    std::size_t fileCount() const {
        return files.size();
    }

    bool contains(const char* path) const {
        return files.find(path ? path : "") != files.end();
    }

    std::size_t sizeOf(const char* path) const {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? 0 : it->second.size();
    }

    void putFile(const char* path, const std::vector<std::uint8_t>& data) {
        files[path ? path : ""] = data;
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

WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t pulses, std::uint32_t volumeMl) {
    return WaterRecord{
        startTime,
        volumeMl,
        volumeMl,
        pulses,
        0,
        20,
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        0,
        0.221f,
        {0, 0, 0, 0},
    };
}

void fillTrace(WaterPulseTraceStore& store, std::uint32_t id, std::initializer_list<std::uint16_t> values) {
    for (std::uint16_t value : values) {
        TEST_ASSERT_TRUE(store.appendSecond(id, value, WaterPulseTraceState::Running));
    }
}

}  // namespace

void test_trace_store_records_seconds_and_reports_memory_stats() {
    WaterPulseTrace traces[4]{};
    WaterPulseTraceSample samples[32]{};
    WaterPulseTraceStore store(traces, 4, samples, 32, 512);

    const std::uint32_t id = store.beginTrace(1000);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    TEST_ASSERT_TRUE(store.appendSecond(id, 2, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.appendSecond(id, 3, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 5, 1000), WaterPulseTraceState::Completed));

    WaterPulseTraceStats stats = store.stats();
    TEST_ASSERT_EQUAL_size_t(1, stats.traceCount);
    TEST_ASSERT_EQUAL_size_t(2, stats.sampleCount);
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.usedBytes);
    TEST_ASSERT_EQUAL_UINT32(512, stats.budgetBytes);

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_UINT16(2, trace->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(5, trace->totalPulses);
    TEST_ASSERT_EQUAL_UINT16(2, store.sampleAt(*trace, 0)->pulseDelta);
    TEST_ASSERT_EQUAL_UINT16(3, store.sampleAt(*trace, 1)->pulseDelta);
}

void test_trace_store_drops_oldest_when_memory_budget_is_exceeded() {
    WaterPulseTrace traces[4]{};
    WaterPulseTraceSample samples[64]{};
    WaterPulseTraceStore store(traces, 4, samples, 64, sizeof(WaterPulseTrace) * 2 + sizeof(WaterPulseTraceSample) * 4);

    const std::uint32_t first = store.beginTrace(1000);
    TEST_ASSERT_TRUE(store.appendSecond(first, 1, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.appendSecond(first, 1, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.finishTrace(first, makeRecord(1000, 2, 1000), WaterPulseTraceState::Completed));

    const std::uint32_t second = store.beginTrace(2000);
    TEST_ASSERT_TRUE(store.appendSecond(second, 1, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.appendSecond(second, 1, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.finishTrace(second, makeRecord(2000, 2, 1000), WaterPulseTraceState::Completed));

    const std::uint32_t third = store.beginTrace(3000);
    TEST_ASSERT_TRUE(store.appendSecond(third, 1, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.appendSecond(third, 1, WaterPulseTraceState::Running));
    TEST_ASSERT_TRUE(store.finishTrace(third, makeRecord(3000, 2, 1000), WaterPulseTraceState::Completed));

    TEST_ASSERT_NULL(store.findById(first));
    TEST_ASSERT_NOT_NULL(store.findById(second));
    TEST_ASSERT_NOT_NULL(store.findById(third));
    TEST_ASSERT_EQUAL_size_t(2, store.stats().traceCount);
}

void test_trace_analysis_finds_stable_start_after_slow_ramp() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[64]{};
    WaterPulseTraceStore store(traces, 2, samples, 64, 2048);
    const std::uint32_t id = store.beginTrace(1000);
    const std::uint16_t values[] = {1, 2, 3, 5, 6, 7, 7, 7, 6, 7, 7, 7};
    for (std::uint16_t value : values) {
        TEST_ASSERT_TRUE(store.appendSecond(id, value, WaterPulseTraceState::Running));
    }
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 65, 7500), WaterPulseTraceState::Completed));
    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);

    const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(*trace, store);
    TEST_ASSERT_TRUE(analysis.stable);
    TEST_ASSERT_EQUAL_UINT16(5, analysis.stableStartSec);
    TEST_ASSERT_EQUAL_UINT32(17, analysis.startupPulseCount);
    TEST_ASSERT_GREATER_THAN_UINT16(0, analysis.confidence);
}

void test_trace_bucket_aggregation_sums_pulses_by_selected_seconds() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore store(traces, 1, samples, 16, 1024);
    const std::uint32_t id = store.beginTrace(1000);
    for (std::uint16_t value : {1, 2, 3, 4, 5}) {
        TEST_ASSERT_TRUE(store.appendSecond(id, value, WaterPulseTraceState::Running));
    }
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 15, 1000), WaterPulseTraceState::Completed));
    const WaterPulseTrace* trace = store.findById(id);
    WaterPulseTraceBucket buckets[3]{};

    const std::size_t count = aggregateWaterPulseTrace(*trace, store, 2, buckets, 3);

    TEST_ASSERT_EQUAL_size_t(3, count);
    TEST_ASSERT_EQUAL_UINT16(3, buckets[0].pulseDelta);
    TEST_ASSERT_EQUAL_UINT16(7, buckets[1].pulseDelta);
    TEST_ASSERT_EQUAL_UINT16(5, buckets[2].pulseDelta);
    TEST_ASSERT_EQUAL_UINT32(15, buckets[2].cumulativePulses);
}

void test_segmented_calibration_uses_two_valid_samples() {
    SegmentedCalibrationSample samples[2]{};
    samples[0].actualMl = 1500;
    samples[0].totalPulses = 250;
    samples[0].startupPulseCount = 40;
    samples[0].stablePulseCount = 210;
    samples[0].startupDurationSec = 5;
    samples[1].actualMl = 7500;
    samples[1].totalPulses = 1580;
    samples[1].startupPulseCount = 40;
    samples[1].stablePulseCount = 1540;
    samples[1].startupDurationSec = 5;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 2, result));

    TEST_ASSERT_EQUAL_UINT32(222, result.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(553, result.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(72, result.startupPulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(211, result.overallPulsePerLiter);
    TEST_ASSERT_EQUAL_UINT16(5, result.startupDurationSec);
    TEST_ASSERT_EQUAL_UINT32(40, result.startupPulseCount);
}

void test_saved_trace_file_store_persists_and_deletes_selected_trace() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore ram(traces, 2, samples, 16, 1024);
    const std::uint32_t id = ram.beginTrace(1000);
    fillTrace(ram, id, {2, 3, 4});
    TEST_ASSERT_TRUE(ram.finishTrace(id, makeRecord(1000, 9, 1000), WaterPulseTraceState::Completed));
    const WaterPulseTrace* trace = ram.findById(id);
    TEST_ASSERT_NOT_NULL(trace);

    MemoryFileBackend backend;
    WaterPulseTraceFileStore saved(backend, "/faucet_pulse_traces_v2.bin", 8, 4);
    TEST_ASSERT_EQUAL_size_t(0, backend.fileCount());
    TEST_ASSERT_TRUE(saved.begin());
    TEST_ASSERT_EQUAL_size_t(0, backend.fileCount());
    WaterPulseTraceSample copy[8]{};
    for (std::size_t i = 0; i < trace->sampleCount; ++i) {
        copy[i] = *ram.sampleAt(*trace, i);
    }
    std::uint32_t savedId = 0;
    TEST_ASSERT_TRUE(saved.save(*trace, copy, trace->sampleCount, &savedId));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, savedId);
    TEST_ASSERT_NOT_EQUAL_UINT32(id, savedId);
    TEST_ASSERT_EQUAL_size_t(1, backend.fileCount());
    TEST_ASSERT_TRUE(backend.contains("/faucet_pulse_traces_v2.bin"));

    WaterPulseTraceFileStats stats = saved.stats();
    TEST_ASSERT_EQUAL_size_t(1, stats.savedCount);
    TEST_ASSERT_EQUAL_size_t(4, stats.maxCount);
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.usedBytes);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(stats.usedBytes, stats.maxBytes);

    WaterPulseTrace loaded{};
    TEST_ASSERT_TRUE(saved.findById(savedId, loaded));
    TEST_ASSERT_EQUAL_UINT32(9, loaded.totalPulses);
    TEST_ASSERT_TRUE(saved.containsRecord(trace->record));

    WaterPulseTraceSample loadedSamples[8]{};
    TEST_ASSERT_EQUAL_size_t(3, saved.readSamples(savedId, loadedSamples, 8));
    TEST_ASSERT_EQUAL_UINT16(2, loadedSamples[0].pulseDelta);
    TEST_ASSERT_EQUAL_UINT16(4, loadedSamples[2].pulseDelta);

    TEST_ASSERT_TRUE(saved.remove(savedId));
    TEST_ASSERT_FALSE(saved.findById(savedId, loaded));
    stats = saved.stats();
    TEST_ASSERT_EQUAL_size_t(0, stats.savedCount);
    TEST_ASSERT_EQUAL_size_t(1, backend.fileCount());
}

void test_saved_trace_file_store_begin_does_not_touch_flash() {
    MemoryFileBackend backend;
    WaterPulseTraceFileStore saved(backend, "/faucet_pulse_traces_v2.bin", 8, 4);

    TEST_ASSERT_TRUE(saved.begin());

    TEST_ASSERT_EQUAL_size_t(0, backend.fileCount());
    TEST_ASSERT_EQUAL_size_t(0, backend.createSizedCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.appendCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.writeCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.readCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.removeCalls);
    TEST_ASSERT_EQUAL_size_t(0, backend.existsCalls);
}

void test_saved_trace_file_store_duplicate_save_reuses_existing_slot() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore ram(traces, 2, samples, 16, 1024);
    const std::uint32_t id = ram.beginTrace(1000);
    fillTrace(ram, id, {2, 3, 4});
    TEST_ASSERT_TRUE(ram.finishTrace(id, makeRecord(1000, 9, 1000), WaterPulseTraceState::Completed));
    const WaterPulseTrace* trace = ram.findById(id);
    TEST_ASSERT_NOT_NULL(trace);

    WaterPulseTraceSample copy[8]{};
    for (std::size_t i = 0; i < trace->sampleCount; ++i) {
        copy[i] = *ram.sampleAt(*trace, i);
    }

    MemoryFileBackend backend;
    WaterPulseTraceFileStore saved(backend, "/faucet_pulse_traces_v2.bin", 8, 2);
    TEST_ASSERT_TRUE(saved.begin());
    std::uint32_t firstSavedId = 0;
    std::uint32_t secondSavedId = 0;
    TEST_ASSERT_TRUE(saved.save(*trace, copy, trace->sampleCount, &firstSavedId));
    TEST_ASSERT_TRUE(saved.save(*trace, copy, trace->sampleCount, &secondSavedId));

    TEST_ASSERT_EQUAL_UINT32(firstSavedId, secondSavedId);
    TEST_ASSERT_EQUAL_size_t(1, backend.fileCount());
    TEST_ASSERT_EQUAL_size_t(1, saved.stats().savedCount);
}

void test_saved_trace_file_store_refuses_new_trace_when_capacity_full() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore ram(traces, 2, samples, 16, 1024);
    const std::uint32_t first = ram.beginTrace(1000);
    fillTrace(ram, first, {1, 1});
    TEST_ASSERT_TRUE(ram.finishTrace(first, makeRecord(1000, 2, 1000), WaterPulseTraceState::Completed));
    const WaterPulseTrace* firstTrace = ram.findById(first);
    TEST_ASSERT_NOT_NULL(firstTrace);
    WaterPulseTraceSample firstSamples[4]{};
    for (std::size_t i = 0; i < firstTrace->sampleCount; ++i) {
        firstSamples[i] = *ram.sampleAt(*firstTrace, i);
    }

    MemoryFileBackend backend;
    WaterPulseTraceFileStore saved(backend, "/faucet_pulse_traces_v2.bin", 8, 1);
    TEST_ASSERT_TRUE(saved.begin());
    std::uint32_t firstSavedId = 0;
    TEST_ASSERT_TRUE(saved.save(*firstTrace, firstSamples, firstTrace->sampleCount, &firstSavedId));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, firstSavedId);

    WaterPulseTrace second = *firstTrace;
    second.record = makeRecord(2000, 3, 1500);
    second.totalPulses = 3;
    WaterPulseTraceSample secondSamples[2] = {
        WaterPulseTraceSample{1, WaterPulseTraceState::Running, 0},
        WaterPulseTraceSample{2, WaterPulseTraceState::Completed, 0},
    };
    std::uint32_t secondSavedId = 0;
    WaterPulseTraceSaveStatus status = WaterPulseTraceSaveStatus::Ok;
    TEST_ASSERT_FALSE(saved.save(second, secondSamples, 2, &secondSavedId, &status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterPulseTraceSaveStatus::LimitReached),
                            static_cast<unsigned>(status));
    TEST_ASSERT_EQUAL_UINT32(0, secondSavedId);
    TEST_ASSERT_EQUAL_size_t(1, backend.fileCount());
    TEST_ASSERT_EQUAL_size_t(1, saved.stats().savedCount);

    WaterPulseTrace firstLoaded{};
    TEST_ASSERT_TRUE(saved.findById(firstSavedId, firstLoaded));
    TEST_ASSERT_EQUAL_UINT32(2, firstLoaded.totalPulses);
}

void test_saved_trace_file_store_matches_page_records_in_one_call() {
    WaterPulseTrace traces[4]{};
    WaterPulseTraceSample samples[32]{};
    WaterPulseTraceStore ram(traces, 4, samples, 32, 2048);
    const std::uint32_t first = ram.beginTrace(1000);
    fillTrace(ram, first, {1, 2});
    TEST_ASSERT_TRUE(ram.finishTrace(first, makeRecord(1000, 3, 1000), WaterPulseTraceState::Completed));
    const std::uint32_t second = ram.beginTrace(2000);
    fillTrace(ram, second, {3, 4});
    TEST_ASSERT_TRUE(ram.finishTrace(second, makeRecord(2000, 7, 2000), WaterPulseTraceState::Completed));
    const std::uint32_t third = ram.beginTrace(3000);
    fillTrace(ram, third, {5, 6});
    TEST_ASSERT_TRUE(ram.finishTrace(third, makeRecord(3000, 11, 3000), WaterPulseTraceState::Completed));

    MemoryFileBackend backend;
    WaterPulseTraceFileStore saved(backend, "/faucet_pulse_traces_v2.bin", 8, 4);
    TEST_ASSERT_TRUE(saved.begin());
    for (std::size_t i = 0; i < ram.count(); ++i) {
        const WaterPulseTrace* trace = ram.traceAt(i);
        TEST_ASSERT_NOT_NULL(trace);
        WaterPulseTraceSample copy[8]{};
        for (std::size_t sample = 0; sample < trace->sampleCount; ++sample) {
            copy[sample] = *ram.sampleAt(*trace, sample);
        }
        TEST_ASSERT_TRUE(saved.save(*trace, copy, trace->sampleCount));
    }

    const WaterPulseTrace* newestTrace = ram.traceAt(2);
    const WaterPulseTrace* oldestTrace = ram.traceAt(0);
    TEST_ASSERT_NOT_NULL(newestTrace);
    TEST_ASSERT_NOT_NULL(oldestTrace);
    WaterRecord page[] = {
        newestTrace->record,
        makeRecord(4000, 9, 4000),
        oldestTrace->record,
    };
    WaterPulseTrace matches[3]{};
    bool found[3]{};

    TEST_ASSERT_EQUAL_size_t(2, saved.findByRecords(page, 3, matches, found));

    TEST_ASSERT_TRUE(found[0]);
    TEST_ASSERT_FALSE(found[1]);
    TEST_ASSERT_TRUE(found[2]);
    TEST_ASSERT_EQUAL_UINT32(11, matches[0].totalPulses);
    TEST_ASSERT_EQUAL_UINT32(3, matches[2].totalPulses);
}

void test_saved_trace_file_store_corrupt_file_degrades_without_crashing() {
    MemoryFileBackend backend;
    backend.putFile("/faucet_pulse_traces_v2.bin", std::vector<std::uint8_t>{1, 2, 3, 4, 5});
    WaterPulseTraceFileStore saved(backend, "/faucet_pulse_traces_v2.bin", 8, 2);

    TEST_ASSERT_TRUE(saved.begin());

    WaterPulseTrace loaded{};
    TEST_ASSERT_FALSE(saved.findById(1, loaded));
    WaterPulseTraceFileStats stats = saved.stats();
    TEST_ASSERT_TRUE(stats.corrupt);
    TEST_ASSERT_EQUAL_size_t(0, stats.savedCount);
}

void test_saved_trace_file_store_legacy_blob_is_only_removed_explicitly() {
    MemoryFileBackend backend;
    backend.putFile("/faucet_saved_traces_v1.bin", std::vector<std::uint8_t>{1, 2, 3});
    WaterPulseTraceFileStore saved(
        backend,
        "/faucet_pulse_traces_v2.bin",
        8,
        2,
        "/fpt_",
        "/faucet_saved_traces_v1.bin");

    TEST_ASSERT_TRUE(saved.begin());
    TEST_ASSERT_TRUE(backend.contains("/faucet_saved_traces_v1.bin"));
    TEST_ASSERT_TRUE(saved.legacyBlobExists());
    TEST_ASSERT_TRUE(saved.removeLegacyBlob());
    TEST_ASSERT_FALSE(backend.contains("/faucet_saved_traces_v1.bin"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_trace_store_records_seconds_and_reports_memory_stats);
    RUN_TEST(test_trace_store_drops_oldest_when_memory_budget_is_exceeded);
    RUN_TEST(test_trace_analysis_finds_stable_start_after_slow_ramp);
    RUN_TEST(test_trace_bucket_aggregation_sums_pulses_by_selected_seconds);
    RUN_TEST(test_segmented_calibration_uses_two_valid_samples);
    RUN_TEST(test_saved_trace_file_store_persists_and_deletes_selected_trace);
    RUN_TEST(test_saved_trace_file_store_begin_does_not_touch_flash);
    RUN_TEST(test_saved_trace_file_store_duplicate_save_reuses_existing_slot);
    RUN_TEST(test_saved_trace_file_store_refuses_new_trace_when_capacity_full);
    RUN_TEST(test_saved_trace_file_store_matches_page_records_in_one_call);
    RUN_TEST(test_saved_trace_file_store_corrupt_file_degrades_without_crashing);
    RUN_TEST(test_saved_trace_file_store_legacy_blob_is_only_removed_explicitly);
    return UNITY_END();
}
