#include <unity.h>

#include "app/WaterRecordStore.h"
#include "app/WaterSensors.h"

#include <cstring>

using namespace faucet;

namespace {

class SpyRecordReader : public WaterRecordReader {
public:
    std::size_t maxRequestedPageSize = 0;
    std::size_t calls = 0;
    bool oldRecords = false;

    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterRecord* output,
                         std::size_t outputCapacity) const override {
        ++const_cast<SpyRecordReader*>(this)->calls;
        if (pageSize > maxRequestedPageSize) {
            const_cast<SpyRecordReader*>(this)->maxRequestedPageSize = pageSize;
        }
        if (!oldRecords || !output || outputCapacity == 0 || pageIndex > 1) {
            return 0;
        }
        const std::size_t count = pageSize < outputCapacity ? pageSize : outputCapacity;
        for (std::size_t i = 0; i < count; ++i) {
            output[i] = makeRecord(pageIndex == 0 && i == 0 ? 832032000UL : 820000000UL, 1000);
        }
        return count;
    }

    static WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl) {
        return WaterRecord{
            startTime,
            volumeMl,
            1500,
            volumeMl,
            0,
            12,
            WaterMode::Volume,
            WaterResult::Completed,
            0,
            0,
            1,
            {0, 0, 0, 0},
        };
    }

    std::size_t count() const override {
        return 400;
    }

    bool ready() const override {
        return true;
    }

    const char* storageName() const override {
        return "spy";
    }
};

WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl) {
    return WaterRecord{
        startTime,
        volumeMl,
        1500,
        volumeMl,
        0,
        12,
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        0,
        1,
        {0, 0, 0, 0},
    };
}

WaterRecord makeRecord(std::uint32_t startTime,
                       std::uint32_t volumeMl,
                       std::uint16_t durationSec,
                       std::uint8_t selectedPreset,
                       WaterResult result) {
    WaterRecord record = makeRecord(startTime, volumeMl);
    record.durationSec = durationSec;
    record.selectedPreset = selectedPreset;
    record.result = result;
    return record;
}

WaterRecord makeSensorRecord(std::uint32_t startTime,
                             std::uint32_t volumeMl,
                             std::int16_t tempAvg,
                             std::uint16_t tdsAvg,
                             std::uint16_t sampleCount,
                             bool calibrated,
                             std::uint16_t flags = 0) {
    WaterRecord record = makeRecord(startTime, volumeMl);
    record.temperatureAvgCentiC = tempAvg;
    record.temperatureMinCentiC = static_cast<std::int16_t>(tempAvg - 10);
    record.temperatureMaxCentiC = static_cast<std::int16_t>(tempAvg + 10);
    record.tdsAvgPpm = tdsAvg;
    record.tdsMinPpm = tdsAvg > 1 ? static_cast<std::uint16_t>(tdsAvg - 1) : tdsAvg;
    record.tdsMaxPpm = static_cast<std::uint16_t>(tdsAvg + 1);
    record.tdsVoltageAvgMv = 24;
    record.sensorSampleCount = sampleCount;
    record.sensorFlags = flags;
    record.tdsCalibrationRevisionAtRun = calibrated ? 2 : 0;
    record.tdsCalibrationModeAtRun = static_cast<std::uint8_t>(calibrated ? TdsCalibrationMode::TwoPoint
                                                                          : TdsCalibrationMode::None);
    record.tdsCalibratedAtRun = calibrated ? 1 : 0;
    record.tdsTemperatureCompensatedAtRun = 1;
    return record;
}

}  // namespace

void test_water_record_sensor_fields_do_not_reuse_boot_id_storage() {
    WaterRecord record = makeRecord(832032000UL, 1500);
    markWaterRecordBootId(record, 0x12345678UL);
    record.temperatureAvgCentiC = 2530;
    record.temperatureMinCentiC = 2480;
    record.temperatureMaxCentiC = 2570;
    record.tdsAvgPpm = 8;
    record.tdsMinPpm = 6;
    record.tdsMaxPpm = 12;
    record.tdsVoltageAvgMv = 24;
    record.sensorSampleCount = 30;
    record.sensorFlags = 0x0002;
    record.tdsCalibrationRevisionAtRun = 7;
    record.tdsCalibrationModeAtRun = static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint);
    record.tdsCalibratedAtRun = 1;
    record.tdsTemperatureCompensatedAtRun = 1;
    record.tdsTempFallback25CAtRun = 0;

    TEST_ASSERT_EQUAL_size_t(64, sizeof(WaterRecord));
    TEST_ASSERT_EQUAL_UINT32(0x12345678UL, waterRecordBootId(record));
    TEST_ASSERT_EQUAL_INT16(2530, record.temperatureAvgCentiC);
    TEST_ASSERT_EQUAL_UINT16(8, record.tdsAvgPpm);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            record.tdsCalibrationModeAtRun);
}

void test_record_append_keeps_newest_first() {
    WaterRecord records[4]{};
    WaterRecordStore store(records, 4);

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));

    TEST_ASSERT_EQUAL_UINT32(300, store.newest(0)->startTime);
    TEST_ASSERT_EQUAL_UINT32(200, store.newest(1)->startTime);
    TEST_ASSERT_EQUAL_UINT32(100, store.newest(2)->startTime);
    TEST_ASSERT_NULL(store.newest(3));
}

void test_record_rolls_when_capacity_is_full() {
    WaterRecord records[3]{};
    WaterRecordStore store(records, 3);

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(400, 4000)));

    TEST_ASSERT_TRUE(store.full());
    TEST_ASSERT_EQUAL_size_t(3, store.count());
    TEST_ASSERT_EQUAL_UINT32(400, store.newest(0)->startTime);
    TEST_ASSERT_EQUAL_UINT32(300, store.newest(1)->startTime);
    TEST_ASSERT_EQUAL_UINT32(200, store.newest(2)->startTime);
}

void test_record_reads_pages_newest_first() {
    WaterRecord records[6]{};
    WaterRecordStore store(records, 6);
    for (std::uint32_t i = 1; i <= 6; ++i) {
        TEST_ASSERT_TRUE(store.append(makeRecord(i * 100, i * 1000)));
    }

    WaterRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, store.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(600, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(500, page[1].startTime);

    TEST_ASSERT_EQUAL_size_t(2, store.readPage(2, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(200, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[1].startTime);
}

void test_record_page_size_is_sanitized_and_limited_by_output() {
    WaterRecord records[5]{};
    WaterRecordStore store(records, 5);
    for (std::uint32_t i = 1; i <= 5; ++i) {
        TEST_ASSERT_TRUE(store.append(makeRecord(i, i)));
    }

    WaterRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readPage(0, 0, page, 3));
    TEST_ASSERT_EQUAL_UINT32(5, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(3, page[2].startTime);
}

void test_record_rejects_missing_storage() {
    WaterRecordStore store(nullptr, 0);
    WaterRecord page[1]{};

    TEST_ASSERT_FALSE(store.append(makeRecord(1, 1)));
    TEST_ASSERT_EQUAL_size_t(0, store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_size_t(0, store.capacity());
}

void test_record_clear_resets_count_and_order() {
    WaterRecord records[2]{};
    WaterRecordStore store(records, 2);
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));

    store.clear();

    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_FALSE(store.full());
    TEST_ASSERT_NULL(store.newest(0));
}

void test_record_rewrites_current_boot_relative_times() {
    WaterRecord records[4]{};
    WaterRecordStore store(records, 4);
    WaterRecord current = makeRecord(21, 1500);
    markWaterRecordBootId(current, 9);
    WaterRecord previous = makeRecord(30, 500);
    markWaterRecordBootId(previous, 8);

    TEST_ASSERT_TRUE(store.append(current));
    TEST_ASSERT_TRUE(store.append(previous));

    TEST_ASSERT_EQUAL_size_t(1, store.rewriteBootRelativeTimes(9, 815500000));
    TEST_ASSERT_EQUAL_UINT32(30, store.newest(0)->startTime);
    TEST_ASSERT_EQUAL_UINT32(8, waterRecordBootId(*store.newest(0)));
    TEST_ASSERT_EQUAL_UINT32(815500021, store.newest(1)->startTime);
    TEST_ASSERT_EQUAL_UINT32(0, waterRecordBootId(*store.newest(1)));
}

void test_record_aggregate_uses_calendar_month_and_real_daily_buckets() {
    WaterRecord records[8]{};
    WaterRecordStore store(records, 8);
    TEST_ASSERT_TRUE(store.append(makeRecord(832032000UL, 12000, 60, 1, WaterResult::Completed)));      // 2026-05-16 00:00
    TEST_ASSERT_TRUE(store.append(makeRecord(831686400UL, 6000, 30, 2, WaterResult::StoppedByUser)));  // 2026-05-12 00:00
    TEST_ASSERT_TRUE(store.append(makeRecord(830995200UL, 5000, 25, 1, WaterResult::FlowError)));      // 2026-05-04 00:00
    TEST_ASSERT_TRUE(store.append(makeRecord(829612800UL, 7000, 40, 0, WaterResult::Completed)));      // 2026-04-18 00:00
    WaterRecord unknown = makeRecord(20, 900, 7, 3, WaterResult::SafetyStopped);
    markWaterRecordBootId(unknown, 42);
    TEST_ASSERT_TRUE(store.append(unknown));

    const WaterUsageSummary summary = aggregateWaterRecords(store, 832032000UL, 30);

    TEST_ASSERT_EQUAL_UINT32(12000, summary.todayMl);
    TEST_ASSERT_EQUAL_UINT32(1, summary.todayCount);
    TEST_ASSERT_EQUAL_UINT32(23000, summary.monthMl);
    TEST_ASSERT_EQUAL_UINT32(3, summary.monthCount);
    TEST_ASSERT_EQUAL_UINT32(30000, summary.last30DaysMl);
    TEST_ASSERT_EQUAL_UINT32(4, summary.last30DaysCount);
    TEST_ASSERT_EQUAL_UINT32(1000, summary.last30DaysDailyAverageMl);
    TEST_ASSERT_EQUAL_UINT32(30000, summary.totalMl);
    TEST_ASSERT_EQUAL_UINT32(4, summary.totalCount);
    TEST_ASSERT_EQUAL_UINT32(900, summary.unknownMl);
    TEST_ASSERT_EQUAL_UINT32(7, summary.unknownDurationSec);
    TEST_ASSERT_EQUAL_UINT32(1, summary.unknownCount);
    TEST_ASSERT_EQUAL_UINT32(30, summary.dayCount);
    TEST_ASSERT_EQUAL_UINT32(832032000UL / 86400UL - 29UL, summary.days[0].dayIndex);
    TEST_ASSERT_EQUAL_UINT32(12000, summary.days[29].volumeMl);
    TEST_ASSERT_EQUAL_UINT32(1, summary.days[29].count);
    TEST_ASSERT_EQUAL_UINT32(60, summary.days[29].durationSec);
    TEST_ASSERT_EQUAL_UINT32(17000, summary.presetCounts[1].volumeMl);
    TEST_ASSERT_EQUAL_UINT32(2, summary.presetCounts[1].count);
    TEST_ASSERT_EQUAL_UINT32(1, summary.presetCounts[2].count);
    TEST_ASSERT_EQUAL_UINT32(4, summary.hourBuckets[0].count);
    TEST_ASSERT_EQUAL_UINT32(1, summary.resultCounts[static_cast<std::size_t>(WaterResult::FlowError)]);
    TEST_ASSERT_EQUAL_UINT32(3, summary.volumeHist[3].count);
    TEST_ASSERT_EQUAL_UINT32(1, summary.volumeHist[4].count);
}

void test_record_aggregate_uses_practical_volume_histogram_ranges() {
    WaterRecord records[8]{};
    WaterRecordStore store(records, 8);
    constexpr std::uint32_t today = 832032000UL;
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 499)));
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 500)));
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 1999)));
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 2000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 4999)));
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 5000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 9999)));
    TEST_ASSERT_TRUE(store.append(makeRecord(today, 10000)));

    const WaterUsageSummary summary = aggregateWaterRecords(store, today, 30);

    TEST_ASSERT_EQUAL_UINT32(1, summary.volumeHist[0].count);
    TEST_ASSERT_EQUAL_UINT32(2, summary.volumeHist[1].count);
    TEST_ASSERT_EQUAL_UINT32(2, summary.volumeHist[2].count);
    TEST_ASSERT_EQUAL_UINT32(2, summary.volumeHist[3].count);
    TEST_ASSERT_EQUAL_UINT32(1, summary.volumeHist[4].count);
}

void test_record_aggregation_excludes_uncalibrated_sensors_by_default() {
    WaterRecord records[4]{};
    WaterRecordStore store(records, 4);
    constexpr std::uint32_t today = 832032000UL;
    TEST_ASSERT_TRUE(store.append(makeSensorRecord(today, 1000, 2500, 8, 10, true)));
    TEST_ASSERT_TRUE(store.append(makeSensorRecord(today, 1000, 2800, 160, 30, false, kWaterSensorFlagTdsUncalibrated)));

    const WaterUsageSummary summary = aggregateWaterRecords(store, today, 30);

    TEST_ASSERT_EQUAL_UINT32(1, summary.sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT32(1, summary.uncalibratedSensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(1, summary.days[29].sensorRecordCount);
    TEST_ASSERT_EQUAL_INT16(2500, summary.days[29].temperatureAvgCentiC);
    TEST_ASSERT_EQUAL_UINT16(8, summary.days[29].tdsAvgPpm);
}

void test_record_aggregation_can_include_uncalibrated_sensors_when_requested() {
    WaterRecord records[4]{};
    WaterRecordStore store(records, 4);
    constexpr std::uint32_t today = 832032000UL;
    TEST_ASSERT_TRUE(store.append(makeSensorRecord(today, 1000, 2500, 8, 10, true)));
    TEST_ASSERT_TRUE(store.append(makeSensorRecord(today, 1000, 2800, 160, 30, false, kWaterSensorFlagTdsUncalibrated)));

    const WaterUsageSummary summary = aggregateWaterRecords(store, today, 30, true);

    TEST_ASSERT_EQUAL_UINT32(2, summary.sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT32(1, summary.uncalibratedSensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(2, summary.days[29].sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(1, summary.days[29].uncalibratedSensorRecordCount);
    TEST_ASSERT_EQUAL_INT16(2725, summary.days[29].temperatureAvgCentiC);
    TEST_ASSERT_EQUAL_UINT16(122, summary.days[29].tdsAvgPpm);
    TEST_ASSERT_EQUAL_INT16(2490, summary.days[29].temperatureMinCentiC);
    TEST_ASSERT_EQUAL_INT16(2810, summary.days[29].temperatureMaxCentiC);
    TEST_ASSERT_EQUAL_UINT16(7, summary.days[29].tdsMinPpm);
    TEST_ASSERT_EQUAL_UINT16(161, summary.days[29].tdsMaxPpm);
}

void test_record_aggregate_reads_small_pages_for_web_stack_safety() {
    SpyRecordReader reader;

    aggregateWaterRecords(reader, 832032000UL, 30);

    TEST_ASSERT_GREATER_THAN_size_t(0, reader.calls);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(kDefaultRecordPageSize, reader.maxRequestedPageSize);
}

void test_record_aggregate_stops_after_records_older_than_window() {
    SpyRecordReader reader;
    reader.oldRecords = true;

    const WaterUsageSummary summary = aggregateWaterRecords(reader, 832032000UL, 30);

    TEST_ASSERT_LESS_OR_EQUAL_size_t(2, reader.calls);
    TEST_ASSERT_EQUAL_UINT32(1000, summary.todayMl);
}

void test_record_query_filters_real_records_by_time_range_and_paginates_matches() {
    WaterRecord records[6]{};
    WaterRecordStore store(records, 6);
    TEST_ASSERT_TRUE(store.append(makeRecord(831686400UL, 1000)));
    WaterRecord unknown = makeRecord(20, 2000);
    markWaterRecordBootId(unknown, 7);
    TEST_ASSERT_TRUE(store.append(unknown));
    TEST_ASSERT_TRUE(store.append(makeRecord(831772800UL, 3000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(831859200UL, 4000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(831945600UL, 5000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(832032000UL, 6000)));

    WaterRecordFilter filter{};
    filter.hasStart = true;
    filter.startTime = 831772800UL;
    filter.hasEnd = true;
    filter.endTime = 831945600UL;
    WaterRecord page[2]{};
    std::size_t total = 0;

    TEST_ASSERT_EQUAL_size_t(2, queryWaterRecords(store, filter, 0, 2, page, 2, &total));
    TEST_ASSERT_EQUAL_size_t(3, total);
    TEST_ASSERT_EQUAL_UINT32(831945600UL, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(831859200UL, page[1].startTime);

    TEST_ASSERT_EQUAL_size_t(1, queryWaterRecords(store, filter, 1, 2, page, 2, &total));
    TEST_ASSERT_EQUAL_size_t(3, total);
    TEST_ASSERT_EQUAL_UINT32(831772800UL, page[0].startTime);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_water_record_sensor_fields_do_not_reuse_boot_id_storage);
    RUN_TEST(test_record_append_keeps_newest_first);
    RUN_TEST(test_record_rolls_when_capacity_is_full);
    RUN_TEST(test_record_reads_pages_newest_first);
    RUN_TEST(test_record_page_size_is_sanitized_and_limited_by_output);
    RUN_TEST(test_record_rejects_missing_storage);
    RUN_TEST(test_record_clear_resets_count_and_order);
    RUN_TEST(test_record_rewrites_current_boot_relative_times);
    RUN_TEST(test_record_aggregate_uses_calendar_month_and_real_daily_buckets);
    RUN_TEST(test_record_aggregate_uses_practical_volume_histogram_ranges);
    RUN_TEST(test_record_aggregation_excludes_uncalibrated_sensors_by_default);
    RUN_TEST(test_record_aggregation_can_include_uncalibrated_sensors_when_requested);
    RUN_TEST(test_record_aggregate_reads_small_pages_for_web_stack_safety);
    RUN_TEST(test_record_aggregate_stops_after_records_older_than_window);
    RUN_TEST(test_record_query_filters_real_records_by_time_range_and_paginates_matches);
    return UNITY_END();
}
