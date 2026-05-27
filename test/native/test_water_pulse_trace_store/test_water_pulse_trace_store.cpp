#include <unity.h>

#include "app/WaterPulseTraceStore.h"

#include <initializer_list>

using namespace faucet;

namespace {

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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_trace_store_records_seconds_and_reports_memory_stats);
    RUN_TEST(test_trace_store_drops_oldest_when_memory_budget_is_exceeded);
    RUN_TEST(test_trace_analysis_finds_stable_start_after_slow_ramp);
    RUN_TEST(test_trace_bucket_aggregation_sums_pulses_by_selected_seconds);
    RUN_TEST(test_segmented_calibration_uses_two_valid_samples);
    return UNITY_END();
}
