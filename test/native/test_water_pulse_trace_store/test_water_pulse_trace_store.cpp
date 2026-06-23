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
        0,
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        0,
        1,
        {0, 0, 0, 0},
    };
}

void appendPulseBucket(WaterPulseTraceStore& store, std::uint32_t id, std::uint32_t value) {
    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    const std::uint32_t sec =
        trace->sampleCount == 0 ? 0 : store.sampleAt(*trace, trace->sampleCount - 1)->elapsedUs / 1000000UL + 1;
    for (std::uint32_t i = 0; i < value; ++i) {
        TEST_ASSERT_TRUE(store.appendRawEdge(id, sec * 1000000UL + i * 10000UL));
    }
}

void fillTrace(WaterPulseTraceStore& store, std::uint32_t id, std::initializer_list<std::uint16_t> values) {
    for (std::uint16_t value : values) {
        appendPulseBucket(store, id, value);
    }
}

}  // namespace

void test_trace_store_records_effective_pulses_into_500ms_buckets() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceBucketSample buckets[kPulseTraceMaxBucketsPerTrace]{};
    WaterPulseTraceSample startupEdges[kPulseTraceMaxStartupEdgesPerTrace]{};
    WaterPulseTraceStore store(
        traces, 1, buckets, kPulseTraceMaxBucketsPerTrace, startupEdges, kPulseTraceMaxStartupEdgesPerTrace, 1);

    const std::uint32_t id = store.beginTrace(1000, 1000);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 0));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 100000));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 510000));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 900000));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_size_t(2, trace->bucketCount);
    TEST_ASSERT_EQUAL_UINT16(2, store.bucketAt(*trace, 0)->pulseCount);
    TEST_ASSERT_EQUAL_UINT16(2, store.bucketAt(*trace, 1)->pulseCount);
    TEST_ASSERT_EQUAL_size_t(4, trace->startupEdgeCount);
    TEST_ASSERT_EQUAL_UINT32(900000, store.startupEdgeAt(*trace, 3)->elapsedUs);
}

void test_trace_store_filters_too_close_edges_without_bucket_counting() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceBucketSample buckets[8]{};
    WaterPulseTraceSample startupEdges[8]{};
    WaterPulseTraceStore store(traces, 1, buckets, 8, startupEdges, 8, 1);

    const std::uint32_t id = store.beginTrace(1000, 1000);
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 0));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 500));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 1500));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_UINT32(2, trace->totalPulses);
    TEST_ASSERT_EQUAL_UINT32(1, trace->minIntervalFilteredCount);
    TEST_ASSERT_EQUAL_UINT16(2, store.bucketAt(*trace, 0)->pulseCount);
    TEST_ASSERT_EQUAL_size_t(2, trace->startupEdgeCount);
}

void test_trace_store_bucket_overflow_keeps_counting_totals() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceBucketSample buckets[1]{};
    WaterPulseTraceSample startupEdges[4]{};
    WaterPulseTraceStore store(traces, 1, buckets, 1, startupEdges, 4, 1);

    const std::uint32_t id = store.beginTrace(1000, 1000);
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 0));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 600000));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 1100000));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_UINT32(3, trace->totalPulses);
    TEST_ASSERT_EQUAL_size_t(1, trace->bucketCount);
    TEST_ASSERT_TRUE((trace->flags & kPulseTraceFlagBucketOverflow) != 0);
}

void test_trace_store_records_seconds_and_reports_memory_stats() {
    WaterPulseTrace traces[4]{};
    WaterPulseTraceSample samples[32]{};
    WaterPulseTraceStore store(traces, 4, samples, 32, 4);

    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    appendPulseBucket(store, id, 2);
    appendPulseBucket(store, id, 3);
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 5, 1000), WaterPulseTraceState::Completed));

    WaterPulseTraceStats stats = store.stats();
    TEST_ASSERT_EQUAL_size_t(1, stats.traceCount);
    TEST_ASSERT_EQUAL_size_t(5, stats.sampleCount);
    TEST_ASSERT_EQUAL_size_t(4, stats.traceCapacity);
    TEST_ASSERT_EQUAL_size_t(kPulseTraceSamplesPerTrace, stats.sampleCapacityPerTrace);
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.usedBytes);

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_UINT16(5, trace->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(5, trace->totalPulses);
    TEST_ASSERT_FALSE(trace->truncated);
    TEST_ASSERT_EQUAL_UINT32(0, store.sampleAt(*trace, 0)->elapsedUs);
    TEST_ASSERT_EQUAL_UINT32(10000, store.sampleAt(*trace, 1)->elapsedUs);
    TEST_ASSERT_EQUAL_UINT32(1000000, store.sampleAt(*trace, 2)->elapsedUs);
}

void test_trace_store_does_not_synthesize_edges_to_match_record_duration() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore store(traces, 2, samples, 16, 2);

    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    appendPulseBucket(store, id, 8);
    appendPulseBucket(store, id, 8);
    WaterRecord record = makeRecord(1000, 16, 7500);
    record.durationSec = 5;
    TEST_ASSERT_TRUE(store.finishTrace(id, record, WaterPulseTraceState::Completed));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_size_t(16, trace->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(16, trace->totalPulses);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterPulseTraceState::Completed),
                            static_cast<unsigned>(trace->finalState));
}

void test_trace_store_drops_oldest_when_recent_trace_count_is_exceeded() {
    WaterPulseTrace traces[4]{};
    WaterPulseTraceSample samples[64]{};
    WaterPulseTraceStore store(traces, 4, samples, 64, 2);

    const std::uint32_t first = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    appendPulseBucket(store, first, 1);
    appendPulseBucket(store, first, 1);
    TEST_ASSERT_TRUE(store.finishTrace(first, makeRecord(1000, 2, 1000), WaterPulseTraceState::Completed));

    const std::uint32_t second = store.beginTrace(2000, kDefaultPulseMinIntervalUs);
    appendPulseBucket(store, second, 1);
    appendPulseBucket(store, second, 1);
    TEST_ASSERT_TRUE(store.finishTrace(second, makeRecord(2000, 2, 1000), WaterPulseTraceState::Completed));

    const std::uint32_t third = store.beginTrace(3000, kDefaultPulseMinIntervalUs);
    appendPulseBucket(store, third, 1);
    appendPulseBucket(store, third, 1);
    TEST_ASSERT_TRUE(store.finishTrace(third, makeRecord(3000, 2, 1000), WaterPulseTraceState::Completed));

    TEST_ASSERT_NULL(store.findById(first));
    TEST_ASSERT_NOT_NULL(store.findById(second));
    TEST_ASSERT_NOT_NULL(store.findById(third));
    TEST_ASSERT_EQUAL_size_t(2, store.stats().traceCount);
}

void test_trace_store_marks_trace_truncated_after_single_trace_sample_limit() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[kPulseTraceSamplesPerTrace + 4]{};
    WaterPulseTraceStore store(traces, 2, samples, kPulseTraceSamplesPerTrace + 4, 2);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    for (std::size_t i = 0; i < kPulseTraceSamplesPerTrace; ++i) {
        appendPulseBucket(store, id, 1);
    }

    appendPulseBucket(store, id, 5);
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, kPulseTraceSamplesPerTrace + 5, 1000),
                                      WaterPulseTraceState::Completed));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_TRUE(trace->truncated);
    TEST_ASSERT_EQUAL_size_t(kPulseTraceSamplesPerTrace, trace->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(kPulseTraceSamplesPerTrace + 5, trace->totalPulses);
    TEST_ASSERT_EQUAL_size_t(kPulseTraceSamplesPerTrace, store.stats().sampleCount);
}

void test_trace_analysis_finds_stable_start_after_slow_ramp() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[80]{};
    WaterPulseTraceStore store(traces, 2, samples, 80, 2);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    const std::uint16_t values[] = {1, 2, 3, 5, 6, 7, 7, 7, 6, 7, 7, 7};
    for (std::uint16_t value : values) {
        appendPulseBucket(store, id, value);
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

void test_trace_analysis_rejects_pause_resume_trace_for_startup_calibration() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[80]{};
    WaterPulseTraceStore store(traces, 1, samples, 80, 1);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    const std::uint16_t values[] = {1, 2, 3, 5, 6, 7, 7, 7, 6, 7, 7, 7};
    for (std::uint16_t value : values) {
        appendPulseBucket(store, id, value);
    }
    TEST_ASSERT_TRUE(store.markResumedAfterPause(id));
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 65, 7500), WaterPulseTraceState::Completed));
    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);

    TEST_ASSERT_TRUE(trace->resumedAfterPause);
    TEST_ASSERT_FALSE(waterPulseTraceAnalysisEligible(*trace));
    TEST_ASSERT_FALSE(analyzeWaterPulseTrace(*trace, store).stable);
}

void test_trace_store_records_pause_windows() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore store(traces, 1, samples, 16, 1);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    fillTrace(store, id, {2, 2});

    TEST_ASSERT_TRUE(store.markPaused(id, 2500000));
    TEST_ASSERT_TRUE(store.markResumedAfterPause(id, 4700000));
    fillTrace(store, id, {2});
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 6, 1000), WaterPulseTraceState::Completed, 7000000));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_TRUE(trace->resumedAfterPause);
    TEST_ASSERT_EQUAL_UINT8(1, trace->pauseWindowCount);
    TEST_ASSERT_EQUAL_UINT32(2500000, trace->pauseWindows[0].startElapsedUs);
    TEST_ASSERT_EQUAL_UINT32(4700000, trace->pauseWindows[0].endElapsedUs);
    TEST_ASSERT_FALSE(waterPulseTraceAnalysisEligible(*trace));
}

void test_trace_store_closes_open_pause_window_on_finish() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore store(traces, 1, samples, 16, 1);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    fillTrace(store, id, {2, 2});

    TEST_ASSERT_TRUE(store.markPaused(id, 2500000));
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 4, 1000), WaterPulseTraceState::PauseTimeout, 12100000));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_FALSE(trace->resumedAfterPause);
    TEST_ASSERT_EQUAL_UINT8(1, trace->pauseWindowCount);
    TEST_ASSERT_EQUAL_UINT32(2500000, trace->pauseWindows[0].startElapsedUs);
    TEST_ASSERT_EQUAL_UINT32(12100000, trace->pauseWindows[0].endElapsedUs);
    TEST_ASSERT_TRUE(waterPulseTraceAnalysisEligible(*trace));
}

void test_trace_bucket_aggregation_sums_pulses_by_selected_seconds() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore store(traces, 1, samples, 16, 1);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    for (std::uint16_t value : {1, 2, 3, 4, 5}) {
        appendPulseBucket(store, id, value);
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

void test_trace_bucket_aggregation_accepts_four_second_bucket() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore store(traces, 1, samples, 16, 1);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    for (std::uint16_t value : {1, 2, 3, 4, 5}) {
        appendPulseBucket(store, id, value);
    }
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 15, 1000), WaterPulseTraceState::Completed));
    const WaterPulseTrace* trace = store.findById(id);
    WaterPulseTraceBucket buckets[2]{};

    const std::size_t count = aggregateWaterPulseTrace(*trace, store, 4, buckets, 2);

    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_EQUAL_UINT16(10, buckets[0].pulseDelta);
    TEST_ASSERT_EQUAL_UINT16(5, buckets[1].pulseDelta);
    TEST_ASSERT_EQUAL_UINT32(15, buckets[1].cumulativePulses);
}

void test_segmented_calibration_uses_two_valid_samples() {
    SegmentedCalibrationSample samples[2]{};
    samples[0].actualMl = 1500;
    samples[0].totalPulses = 250;
    samples[0].startupPulseCount = 40;
    samples[0].stablePulseCount = 210;
    samples[0].startupDurationSec = 5;
    samples[0].stablePulsePerSec = 37.0f;
    samples[1].actualMl = 7500;
    samples[1].totalPulses = 1580;
    samples[1].startupPulseCount = 40;
    samples[1].stablePulseCount = 1540;
    samples[1].startupDurationSec = 5;
    samples[1].stablePulsePerSec = 37.0f;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 2, result));

    TEST_ASSERT_EQUAL_UINT32(222, result.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(553, result.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT16(5, result.startupDurationSec);
    TEST_ASSERT_EQUAL_UINT32(5000, result.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(10000, result.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(40, result.startupPulseCount);
}

void test_segmented_calibration_rejects_time_estimate_params_out_of_range() {
    SegmentedCalibrationSample samples[2]{};
    samples[0].actualMl = 1500;
    samples[0].totalPulses = 250;
    samples[0].startupPulseCount = 40;
    samples[0].stablePulseCount = 210;
    samples[0].startupDurationSec = 5;
    samples[0].stablePulsePerSec = 200.0f;
    samples[1].actualMl = 7500;
    samples[1].totalPulses = 1580;
    samples[1].startupPulseCount = 40;
    samples[1].stablePulseCount = 1540;
    samples[1].startupDurationSec = 5;
    samples[1].stablePulsePerSec = 200.0f;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_FALSE(computeSegmentedCalibration(samples, 2, result));

    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL(SegmentedCalibrationRejectReason::InvalidFit, result.rejectReason);
}

void test_segmented_calibration_fits_all_valid_samples() {
    SegmentedCalibrationSample samples[4]{};
    samples[0].actualMl = 1500;
    samples[0].totalPulses = 250;
    samples[0].startupPulseCount = 40;
    samples[0].stablePulseCount = 210;
    samples[0].startupDurationSec = 5;
    samples[1].actualMl = 3000;
    samples[1].totalPulses = 600;
    samples[1].startupPulseCount = 42;
    samples[1].stablePulseCount = 560;
    samples[1].startupDurationSec = 5;
    samples[2].actualMl = 7500;
    samples[2].totalPulses = 1580;
    samples[2].startupPulseCount = 41;
    samples[2].stablePulseCount = 1540;
    samples[2].startupDurationSec = 6;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 3, result));

    TEST_ASSERT_EQUAL_UINT16(3, result.sampleCount);
    TEST_ASSERT_EQUAL_UINT32(1500, result.minActualMl);
    TEST_ASSERT_EQUAL_UINT32(7500, result.maxActualMl);
    TEST_ASSERT_UINT32_WITHIN(3, 222, result.stablePulsePerLiter);
    TEST_ASSERT_UINT32_WITHIN(10, 513, result.startupVolumeMl);
    TEST_ASSERT_GREATER_THAN_UINT32(0, result.maxErrorMl);
}

void test_segmented_calibration_rejects_error_above_configured_limits() {
    SegmentedCalibrationSample samples[4]{};
    samples[0].actualMl = 1500;
    samples[0].totalPulses = 250;
    samples[0].startupPulseCount = 40;
    samples[0].stablePulseCount = 210;
    samples[0].startupDurationSec = 5;
    samples[1].actualMl = 3000;
    samples[1].totalPulses = 600;
    samples[1].startupPulseCount = 40;
    samples[1].stablePulseCount = 560;
    samples[1].startupDurationSec = 5;
    samples[2].actualMl = 7600;
    samples[2].totalPulses = 1580;
    samples[2].startupPulseCount = 40;
    samples[2].stablePulseCount = 1540;
    samples[2].startupDurationSec = 5;
    samples[3].actualMl = 5200;
    samples[3].totalPulses = 940;
    samples[3].startupPulseCount = 40;
    samples[3].stablePulseCount = 900;
    samples[3].startupDurationSec = 5;
    SegmentedCalibrationOptions options = defaultSegmentedCalibrationOptions();
    options.maxErrorMl = 20;
    options.maxRelativeErrorTenthPercent = 5;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 4, options, result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(SegmentedCalibrationRejectReason::None),
                            static_cast<unsigned>(result.rejectReason));
    TEST_ASSERT_TRUE((result.qualityWarnings & kSegmentedCalibrationQualityErrorHigh) != 0);
    TEST_ASSERT_GREATER_THAN_UINT32(options.maxErrorMl, result.maxErrorMl);
}

void test_segmented_calibration_warns_when_volume_span_is_small() {
    SegmentedCalibrationSample samples[2]{};
    samples[0].actualMl = 1000;
    samples[0].totalPulses = 240;
    samples[0].startupPulseCount = 20;
    samples[0].stablePulseCount = 220;
    samples[0].startupDurationSec = 4;
    samples[1].actualMl = 1200;
    samples[1].totalPulses = 290;
    samples[1].startupPulseCount = 22;
    samples[1].stablePulseCount = 270;
    samples[1].startupDurationSec = 5;
    SegmentedCalibrationOptions options = defaultSegmentedCalibrationOptions();
    options.minVolumeSpanMl = 500;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 2, options, result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(SegmentedCalibrationRejectReason::None),
                            static_cast<unsigned>(result.rejectReason));
    TEST_ASSERT_TRUE((result.qualityWarnings & kSegmentedCalibrationQualityVolumeSpanSmall) != 0);
    TEST_ASSERT_EQUAL_UINT32(1000, result.minActualMl);
    TEST_ASSERT_EQUAL_UINT32(1200, result.maxActualMl);
}

void test_segmented_calibration_still_requires_two_samples() {
    SegmentedCalibrationSample samples[1]{};
    samples[0].actualMl = 1000;
    samples[0].totalPulses = 240;
    samples[0].startupPulseCount = 20;
    samples[0].stablePulseCount = 220;
    samples[0].startupDurationSec = 4;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_FALSE(computeSegmentedCalibration(samples, 1, result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(SegmentedCalibrationRejectReason::NotEnoughSamples),
                            static_cast<unsigned>(result.rejectReason));
}

void test_segmented_calibration_removes_single_outlier_before_final_fit() {
    SegmentedCalibrationSample samples[3]{};
    samples[0].actualMl = 1500;
    samples[0].totalPulses = 250;
    samples[0].startupPulseCount = 40;
    samples[0].stablePulseCount = 210;
    samples[0].startupDurationSec = 5;
    samples[1].actualMl = 4200;
    samples[1].totalPulses = 600;
    samples[1].startupPulseCount = 40;
    samples[1].stablePulseCount = 560;
    samples[1].startupDurationSec = 5;
    samples[2].actualMl = 7500;
    samples[2].totalPulses = 1580;
    samples[2].startupPulseCount = 40;
    samples[2].stablePulseCount = 1540;
    samples[2].startupDurationSec = 5;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 3, result));

    TEST_ASSERT_EQUAL_UINT16(2, result.sampleCount);
    TEST_ASSERT_EQUAL_UINT16(1, result.excludedSampleCount);
    TEST_ASSERT_EQUAL_UINT32(222, result.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(553, result.startupVolumeMl);
}

void test_trace_analysis_options_change_stable_window() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[120]{};
    WaterPulseTraceStore store(traces, 2, samples, 120, 2);
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    fillTrace(store, id, {1, 2, 2, 3, 9, 9, 10, 9, 10, 9, 10, 9});
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 83, 5000), WaterPulseTraceState::Completed));
    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);

    SegmentedCalibrationOptions strict = defaultSegmentedCalibrationOptions();
    strict.stableWindowSec = 8;
    TEST_ASSERT_FALSE(analyzeWaterPulseTrace(*trace, store, strict).stable);

    SegmentedCalibrationOptions relaxed = defaultSegmentedCalibrationOptions();
    relaxed.stableWindowSec = 3;
    TEST_ASSERT_TRUE(analyzeWaterPulseTrace(*trace, store, relaxed).stable);
}

void test_trace_store_updates_actual_ml_by_record() {
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[16]{};
    WaterPulseTraceStore ram(traces, 2, samples, 16, 2);
    const std::uint32_t id = ram.beginTrace(1000, kDefaultPulseMinIntervalUs);
    fillTrace(ram, id, {2, 3, 4});
    WaterRecord record = makeRecord(1000, 9, 1000);
    TEST_ASSERT_TRUE(ram.finishTrace(id, record, WaterPulseTraceState::Completed));

    TEST_ASSERT_TRUE(ram.setActualMlByRecord(record, 1502));

    const WaterPulseTrace* trace = ram.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_UINT32(1502, trace->actualMl);
}
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_trace_store_records_effective_pulses_into_500ms_buckets);
    RUN_TEST(test_trace_store_filters_too_close_edges_without_bucket_counting);
    RUN_TEST(test_trace_store_bucket_overflow_keeps_counting_totals);
    RUN_TEST(test_trace_store_records_seconds_and_reports_memory_stats);
    RUN_TEST(test_trace_store_does_not_synthesize_edges_to_match_record_duration);
    RUN_TEST(test_trace_store_drops_oldest_when_recent_trace_count_is_exceeded);
    RUN_TEST(test_trace_store_marks_trace_truncated_after_single_trace_sample_limit);
    RUN_TEST(test_trace_analysis_finds_stable_start_after_slow_ramp);
    RUN_TEST(test_trace_analysis_rejects_pause_resume_trace_for_startup_calibration);
    RUN_TEST(test_trace_store_records_pause_windows);
    RUN_TEST(test_trace_store_closes_open_pause_window_on_finish);
    RUN_TEST(test_trace_bucket_aggregation_sums_pulses_by_selected_seconds);
    RUN_TEST(test_trace_bucket_aggregation_accepts_four_second_bucket);
    RUN_TEST(test_segmented_calibration_uses_two_valid_samples);
    RUN_TEST(test_segmented_calibration_rejects_time_estimate_params_out_of_range);
    RUN_TEST(test_segmented_calibration_fits_all_valid_samples);
    RUN_TEST(test_segmented_calibration_rejects_error_above_configured_limits);
    RUN_TEST(test_segmented_calibration_warns_when_volume_span_is_small);
    RUN_TEST(test_segmented_calibration_still_requires_two_samples);
    RUN_TEST(test_segmented_calibration_removes_single_outlier_before_final_fit);
    RUN_TEST(test_trace_analysis_options_change_stable_window);
    RUN_TEST(test_trace_store_updates_actual_ml_by_record);
    return UNITY_END();
}
