#include <unity.h>

#include "app/WaterPulseTraceStore.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

using namespace faucet;

namespace {

WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t pulses, std::uint32_t volumeMl) {
    WaterRecord record{};
    record.startTime = startTime;
    record.volumeMl = volumeMl;
    record.targetValue = volumeMl;
    record.pulseCount = pulses;
    record.mode = WaterMode::Volume;
    record.result = WaterResult::Completed;
    record.meteringSchemeId = 1;
    return record;
}

template <std::size_t TraceCapacity,
          std::size_t BucketCapacity,
          std::size_t StartupEdgeCapacity,
          std::size_t RecentTraceCapacity = TraceCapacity>
struct TraceStoreFixture {
    WaterPulseTrace traces[TraceCapacity]{};
    WaterPulseTraceBucketSample buckets[BucketCapacity]{};
    WaterPulseTraceSample startupEdges[StartupEdgeCapacity]{};
    WaterPulseTraceStore store{traces,
                               TraceCapacity,
                               buckets,
                               BucketCapacity,
                               startupEdges,
                               StartupEdgeCapacity,
                               RecentTraceCapacity};
};

void appendPulseBucket(WaterPulseTraceStore& store, std::uint32_t id, std::uint32_t value) {
    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    const std::uint32_t sec = static_cast<std::uint32_t>((trace->bucketCount + 1U) / 2U);
    for (std::uint32_t i = 0; i < value; ++i) {
        TEST_ASSERT_TRUE(store.appendPulseEdge(id, sec * 1000000UL + i * 10000UL));
    }
}

void fillTrace(WaterPulseTraceStore& store, std::uint32_t id, std::initializer_list<std::uint16_t> values) {
    for (std::uint16_t value : values) {
        appendPulseBucket(store, id, value);
    }
}

}  // namespace

void test_trace_store_records_effective_pulses_into_500ms_buckets() {
    TraceStoreFixture<1, kPulseTraceMaxBucketsPerTrace, kPulseTraceMaxStartupEdgesPerTrace, 1> fixture;
    WaterPulseTraceStore& store = fixture.store;

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
    TraceStoreFixture<1, 8, 8, 1> fixture;
    WaterPulseTraceStore& store = fixture.store;

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
    TraceStoreFixture<1, 1, 4, 1> fixture;
    WaterPulseTraceStore& store = fixture.store;

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
    TraceStoreFixture<4, 32, 32, 4> fixture;
    WaterPulseTraceStore& store = fixture.store;

    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    appendPulseBucket(store, id, 2);
    appendPulseBucket(store, id, 3);
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 5, 1000), WaterPulseTraceState::Completed));

    WaterPulseTraceStats stats = store.stats();
    TEST_ASSERT_EQUAL_size_t(1, stats.traceCount);
    TEST_ASSERT_EQUAL_size_t(3, stats.bucketCount);
    TEST_ASSERT_EQUAL_size_t(5, stats.startupEdgeCount);
    TEST_ASSERT_EQUAL_size_t(4, stats.traceCapacity);
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.usedBytes);

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_size_t(3, trace->bucketCount);
    TEST_ASSERT_EQUAL_size_t(5, trace->startupEdgeCount);
    TEST_ASSERT_EQUAL_UINT32(5, trace->totalPulses);
    TEST_ASSERT_EQUAL_UINT16(2, store.bucketAt(*trace, 0)->pulseCount);
    TEST_ASSERT_EQUAL_UINT16(0, store.bucketAt(*trace, 1)->pulseCount);
    TEST_ASSERT_EQUAL_UINT16(3, store.bucketAt(*trace, 2)->pulseCount);
    TEST_ASSERT_EQUAL_UINT32(1000000, store.startupEdgeAt(*trace, 2)->elapsedUs);
}

void test_trace_store_does_not_synthesize_edges_to_match_record_duration() {
    TraceStoreFixture<2, 16, 32, 2> fixture;
    WaterPulseTraceStore& store = fixture.store;

    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    appendPulseBucket(store, id, 8);
    appendPulseBucket(store, id, 8);
    WaterRecord record = makeRecord(1000, 16, 7500);
    record.durationSec = 5;
    TEST_ASSERT_TRUE(store.finishTrace(id, record, WaterPulseTraceState::Completed));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_size_t(3, trace->bucketCount);
    TEST_ASSERT_EQUAL_UINT32(16, trace->totalPulses);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterPulseTraceState::Completed),
                            static_cast<unsigned>(trace->finalState));
}

void test_trace_store_drops_oldest_when_recent_trace_count_is_exceeded() {
    TraceStoreFixture<4, 64, 64, 2> fixture;
    WaterPulseTraceStore& store = fixture.store;

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

void test_trace_store_marks_bucket_overflow_after_single_trace_bucket_limit() {
    TraceStoreFixture<2, kPulseTraceMaxBucketsPerTrace, kPulseTraceMaxStartupEdgesPerTrace, 2> fixture;
    WaterPulseTraceStore& store = fixture.store;
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    for (std::size_t i = 0; i < kPulseTraceMaxBucketsPerTrace; ++i) {
        TEST_ASSERT_TRUE(store.appendPulseEdge(
            id, static_cast<std::uint32_t>(i * kPulseTraceBucketMs * 1000UL)));
    }

    TEST_ASSERT_TRUE(store.appendPulseEdge(
        id, static_cast<std::uint32_t>(kPulseTraceMaxBucketsPerTrace * kPulseTraceBucketMs * 1000UL)));
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, kPulseTraceMaxBucketsPerTrace + 1, 1000),
                                      WaterPulseTraceState::Completed));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_TRUE((trace->flags & kPulseTraceFlagBucketOverflow) != 0);
    TEST_ASSERT_EQUAL_size_t(kPulseTraceMaxBucketsPerTrace, trace->bucketCount);
    TEST_ASSERT_EQUAL_UINT32(kPulseTraceMaxBucketsPerTrace + 1, trace->totalPulses);
    TEST_ASSERT_EQUAL_size_t(kPulseTraceMaxBucketsPerTrace, store.stats().bucketCount);
}

void test_trace_analysis_finds_stable_start_after_slow_ramp() {
    TraceStoreFixture<2, 80, 80, 2> fixture;
    WaterPulseTraceStore& store = fixture.store;
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

void test_trace_analysis_allows_bucket_overflow_when_captured_buckets_are_stable() {
    TraceStoreFixture<2, 80, 80, 2> fixture;
    WaterPulseTraceStore& store = fixture.store;
    const std::uint32_t id = store.beginTrace(1000, kDefaultPulseMinIntervalUs);
    fillTrace(store, id, {1, 2, 3, 5, 6, 7, 7, 7, 6, 7, 7, 7});
    TEST_ASSERT_TRUE(store.finishTrace(id, makeRecord(1000, 65, 7500), WaterPulseTraceState::Completed));
    WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    trace->flags |= kPulseTraceFlagBucketOverflow;

    const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(*trace, store);

    TEST_ASSERT_TRUE(waterPulseTraceAnalysisEligible(*trace));
    TEST_ASSERT_TRUE(analysis.stable);
    TEST_ASSERT_EQUAL_UINT16(5, analysis.stableStartSec);
    TEST_ASSERT_EQUAL_UINT32(17, analysis.startupPulseCount);
}

void test_trace_bucket_aggregation_sums_pulses_by_selected_seconds() {
    TraceStoreFixture<1, 16, 16, 1> fixture;
    WaterPulseTraceStore& store = fixture.store;
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
    TraceStoreFixture<1, 16, 16, 1> fixture;
    WaterPulseTraceStore& store = fixture.store;
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

void test_segmented_calibration_accepts_no_startup_compensation_fit() {
    SegmentedCalibrationSample samples[3]{};
    samples[0].actualMl = 500;
    samples[0].totalPulses = 1055;
    samples[0].stablePulseCount = 1055;
    samples[0].stablePulsePerSec = 35.0f;
    samples[1].actualMl = 750;
    samples[1].totalPulses = 1506;
    samples[1].stablePulseCount = 1506;
    samples[1].stablePulsePerSec = 34.0f;
    samples[2].actualMl = 900;
    samples[2].totalPulses = 1816;
    samples[2].stablePulseCount = 1816;
    samples[2].stablePulsePerSec = 33.0f;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 3, result));

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_UINT32(0, result.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, result.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(0, result.startupDurationMs);
    TEST_ASSERT_UINT32_WITHIN(20, 2030, result.stablePulsePerLiter);
}

void test_segmented_calibration_keeps_detected_startup_when_equivalent_volume_is_zero() {
    SegmentedCalibrationSample samples[3]{};
    samples[0].actualMl = 1000;
    samples[0].totalPulses = 2220;
    samples[0].startupPulseCount = 120;
    samples[0].stablePulseCount = 2100;
    samples[0].startupDurationSec = 5;
    samples[0].stablePulsePerSec = 60.0f;
    samples[1].actualMl = 1500;
    samples[1].totalPulses = 3120;
    samples[1].startupPulseCount = 120;
    samples[1].stablePulseCount = 3000;
    samples[1].startupDurationSec = 5;
    samples[1].stablePulsePerSec = 60.0f;
    samples[2].actualMl = 1800;
    samples[2].totalPulses = 3720;
    samples[2].startupPulseCount = 120;
    samples[2].stablePulseCount = 3600;
    samples[2].startupDurationSec = 5;
    samples[2].stablePulsePerSec = 60.0f;

    SegmentedCalibrationResult result{};
    TEST_ASSERT_TRUE(computeSegmentedCalibration(samples, 3, result));

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_UINT32(120, result.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, result.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(5000, result.startupDurationMs);
    TEST_ASSERT_TRUE(result.stablePulsePerLiter >= 1700 && result.stablePulsePerLiter <= 2000);
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
    TraceStoreFixture<2, 120, 120, 2> fixture;
    WaterPulseTraceStore& store = fixture.store;
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
    TraceStoreFixture<2, 16, 16, 2> fixture;
    WaterPulseTraceStore& ram = fixture.store;
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
    RUN_TEST(test_trace_store_marks_bucket_overflow_after_single_trace_bucket_limit);
    RUN_TEST(test_trace_analysis_finds_stable_start_after_slow_ramp);
    RUN_TEST(test_trace_analysis_allows_bucket_overflow_when_captured_buckets_are_stable);
    RUN_TEST(test_trace_bucket_aggregation_sums_pulses_by_selected_seconds);
    RUN_TEST(test_trace_bucket_aggregation_accepts_four_second_bucket);
    RUN_TEST(test_segmented_calibration_uses_two_valid_samples);
    RUN_TEST(test_segmented_calibration_accepts_no_startup_compensation_fit);
    RUN_TEST(test_segmented_calibration_keeps_detected_startup_when_equivalent_volume_is_zero);
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
