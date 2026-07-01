#include <unity.h>

#include "app/WaterRecordStore.h"
#include "app/WaterSensors.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace faucet;

namespace {

WaterRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl) {
    WaterRecord record{};
    record.startTime = startTime;
    record.volumeMl = volumeMl;
    record.targetValue = 1500;
    record.pulseCount = volumeMl;
    record.durationSec = 12;
    record.mode = WaterMode::Volume;
    record.result = WaterResult::Completed;
    record.meteringSchemeId = 1;
    return record;
}

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

class MemoryWaterRecordReader : public WaterRecordReader {
public:
    void append(const WaterRecord& record) {
        records.push_back(record);
    }

    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterRecord* output,
                         std::size_t outputCapacity) const override {
        if (!output || outputCapacity == 0 || pageSize == 0) {
            return 0;
        }
        const std::size_t startOffset = pageIndex * static_cast<std::size_t>(pageSize);
        if (startOffset >= records.size()) {
            return 0;
        }
        const std::size_t available = records.size() - startOffset;
        const std::size_t limit = std::min<std::size_t>({available, pageSize, outputCapacity});
        for (std::size_t i = 0; i < limit; ++i) {
            output[i] = records[records.size() - 1 - (startOffset + i)];
        }
        return limit;
    }

    std::size_t count() const override {
        return records.size();
    }

    bool ready() const override {
        return readyFlag;
    }

    const char* storageName() const override {
        return readyFlag ? "memory-test" : "unavailable";
    }

    std::vector<WaterRecord> records;
    bool readyFlag = true;
};

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
                             std::uint8_t sampleCount,
                             bool calibrated,
                             std::uint16_t flags = 0) {
    WaterRecord record = makeRecord(startTime, volumeMl);
    record.temperatureCentiC = tempAvg;
    record.tdsPpm = tdsAvg;
    record.sensorSampleCount = sampleCount;
    record.sensorFlags = calibrated ? flags : static_cast<std::uint16_t>(flags | kWaterSensorFlagTdsUncalibrated);
    return record;
}

}  // namespace

void test_water_record_sensor_fields_do_not_reuse_boot_id_storage() {
    WaterRecord record = makeRecord(832032000UL, 1500);
    markWaterRecordBootId(record, 0x12345678UL);
    record.temperatureCentiC = 2530;
    record.tdsPpm = 8;
    record.sensorSampleCount = 30;
    record.sensorFlags = 0x0002;

    TEST_ASSERT_EQUAL_size_t(40, sizeof(WaterRecord));
    TEST_ASSERT_EQUAL_UINT32(0x12345678UL, waterRecordBootId(record));
    TEST_ASSERT_EQUAL_INT16(2530, record.temperatureCentiC);
    TEST_ASSERT_EQUAL_UINT16(8, record.tdsPpm);
}

void test_record_rejects_missing_storage() {
    MemoryWaterRecordReader store;
    store.readyFlag = false;
    WaterRecord page[1]{};

    TEST_ASSERT_EQUAL_size_t(0, store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_size_t(0, queryWaterRecords(store, {}, 0, 1, page, 1));
}

void test_record_aggregate_uses_calendar_month_and_real_daily_buckets() {
    MemoryWaterRecordReader store;
    store.append(makeRecord(832032000UL, 12000, 60, 1, WaterResult::Completed));      // 2026-05-16 00:00
    store.append(makeRecord(831686400UL, 6000, 30, 2, WaterResult::StoppedByUser));  // 2026-05-12 00:00
    store.append(makeRecord(830995200UL, 5000, 25, 1, WaterResult::FlowError));      // 2026-05-04 00:00
    store.append(makeRecord(829612800UL, 7000, 40, 0, WaterResult::Completed));      // 2026-04-18 00:00
    WaterRecord unknown = makeRecord(20, 900, 7, 3, WaterResult::SafetyStopped);
    markWaterRecordBootId(unknown, 42);
    store.append(unknown);

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
    MemoryWaterRecordReader store;
    constexpr std::uint32_t today = 832032000UL;
    store.append(makeRecord(today, 499));
    store.append(makeRecord(today, 500));
    store.append(makeRecord(today, 1999));
    store.append(makeRecord(today, 2000));
    store.append(makeRecord(today, 4999));
    store.append(makeRecord(today, 5000));
    store.append(makeRecord(today, 9999));
    store.append(makeRecord(today, 10000));

    const WaterUsageSummary summary = aggregateWaterRecords(store, today, 30);

    TEST_ASSERT_EQUAL_UINT32(1, summary.volumeHist[0].count);
    TEST_ASSERT_EQUAL_UINT32(2, summary.volumeHist[1].count);
    TEST_ASSERT_EQUAL_UINT32(2, summary.volumeHist[2].count);
    TEST_ASSERT_EQUAL_UINT32(2, summary.volumeHist[3].count);
    TEST_ASSERT_EQUAL_UINT32(1, summary.volumeHist[4].count);
}

void test_record_aggregation_excludes_uncalibrated_sensors_by_default() {
    MemoryWaterRecordReader store;
    constexpr std::uint32_t today = 832032000UL;
    store.append(makeSensorRecord(today, 1000, 2500, 8, 10, true));
    store.append(makeSensorRecord(today, 1000, 2800, 160, 30, false, kWaterSensorFlagTdsUncalibrated));

    const WaterUsageSummary summary = aggregateWaterRecords(store, today, 30);

    TEST_ASSERT_EQUAL_UINT32(2, summary.sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT32(1, summary.uncalibratedSensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(2, summary.days[29].sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(2, summary.days[29].temperatureRecordCount);
    TEST_ASSERT_EQUAL_UINT16(1, summary.days[29].tdsRecordCount);
    TEST_ASSERT_EQUAL_INT16(2725, summary.days[29].temperatureAvgCentiC);
    TEST_ASSERT_EQUAL_UINT16(8, summary.days[29].tdsAvgPpm);
}

void test_record_aggregation_can_include_uncalibrated_sensors_when_requested() {
    MemoryWaterRecordReader store;
    constexpr std::uint32_t today = 832032000UL;
    store.append(makeSensorRecord(today, 1000, 2500, 8, 10, true));
    store.append(makeSensorRecord(today, 1000, 2800, 160, 30, false, kWaterSensorFlagTdsUncalibrated));

    const WaterUsageSummary summary = aggregateWaterRecords(store, today, 30, true);

    TEST_ASSERT_EQUAL_UINT32(2, summary.sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT32(1, summary.uncalibratedSensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(2, summary.days[29].sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(1, summary.days[29].uncalibratedSensorRecordCount);
    TEST_ASSERT_EQUAL_INT16(2725, summary.days[29].temperatureAvgCentiC);
    TEST_ASSERT_EQUAL_UINT16(122, summary.days[29].tdsAvgPpm);
    TEST_ASSERT_EQUAL_INT16(2500, summary.days[29].temperatureMinCentiC);
    TEST_ASSERT_EQUAL_INT16(2800, summary.days[29].temperatureMaxCentiC);
    TEST_ASSERT_EQUAL_UINT16(8, summary.days[29].tdsMinPpm);
    TEST_ASSERT_EQUAL_UINT16(160, summary.days[29].tdsMaxPpm);
}

void test_record_aggregation_tracks_temperature_and_tds_validity_separately() {
    MemoryWaterRecordReader store;
    constexpr std::uint32_t today = 832032000UL;
    store.append(makeSensorRecord(
        today, 1000, 0, 80, 5, true, kWaterSensorFlagTempUnavailable | kWaterSensorFlagTdsTempFallback25C));

    const WaterUsageSummary summary = aggregateWaterRecords(store, today, 30);

    TEST_ASSERT_EQUAL_UINT32(1, summary.sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(1, summary.days[29].sensorRecordCount);
    TEST_ASSERT_EQUAL_UINT16(0, summary.days[29].temperatureRecordCount);
    TEST_ASSERT_EQUAL_UINT16(1, summary.days[29].tdsRecordCount);
    TEST_ASSERT_EQUAL_INT16(0, summary.days[29].temperatureAvgCentiC);
    TEST_ASSERT_EQUAL_UINT16(80, summary.days[29].tdsAvgPpm);
    TEST_ASSERT_EQUAL_UINT16(80, summary.days[29].tdsMinPpm);
    TEST_ASSERT_EQUAL_UINT16(80, summary.days[29].tdsMaxPpm);
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
    MemoryWaterRecordReader store;
    store.append(makeRecord(831686400UL, 1000));
    WaterRecord unknown = makeRecord(20, 2000);
    markWaterRecordBootId(unknown, 7);
    store.append(unknown);
    store.append(makeRecord(831772800UL, 3000));
    store.append(makeRecord(831859200UL, 4000));
    store.append(makeRecord(831945600UL, 5000));
    store.append(makeRecord(832032000UL, 6000));

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
    RUN_TEST(test_record_rejects_missing_storage);
    RUN_TEST(test_record_aggregate_uses_calendar_month_and_real_daily_buckets);
    RUN_TEST(test_record_aggregate_uses_practical_volume_histogram_ranges);
    RUN_TEST(test_record_aggregation_excludes_uncalibrated_sensors_by_default);
    RUN_TEST(test_record_aggregation_can_include_uncalibrated_sensors_when_requested);
    RUN_TEST(test_record_aggregation_tracks_temperature_and_tds_validity_separately);
    RUN_TEST(test_record_aggregate_reads_small_pages_for_web_stack_safety);
    RUN_TEST(test_record_aggregate_stops_after_records_older_than_window);
    RUN_TEST(test_record_query_filters_real_records_by_time_range_and_paginates_matches);
    return UNITY_END();
}
