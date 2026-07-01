#include "app/WaterRecordStore.h"

#include "app/DateTimeUtils.h"
#include "app/WaterSensors.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace faucet {
namespace {

void addSaturating(std::uint32_t& target, std::uint32_t value) {
    const std::uint32_t max = std::numeric_limits<std::uint32_t>::max();
    target = max - target < value ? max : target + value;
}

void incrementCount(std::uint16_t& count) {
    if (count < std::numeric_limits<std::uint16_t>::max()) {
        ++count;
    }
}

std::size_t volumeHistIndex(std::uint32_t volumeMl) {
    if (volumeMl < 500UL) {
        return 0;
    }
    if (volumeMl < 2000UL) {
        return 1;
    }
    if (volumeMl < 5000UL) {
        return 2;
    }
    if (volumeMl < 10000UL) {
        return 3;
    }
    return 4;
}

bool recordHasUsableTemperature(const WaterRecord& record) {
    constexpr std::uint16_t invalidMask =
        kWaterSensorFlagAdcOffline | kWaterSensorFlagTempInvalid | kWaterSensorFlagTempUnavailable;
    return record.sensorSampleCount > 0 && (record.sensorFlags & invalidMask) == 0;
}

bool recordHasUsableTds(const WaterRecord& record, bool includeUncalibratedSensors) {
    const bool uncalibrated = (record.sensorFlags & kWaterSensorFlagTdsUncalibrated) != 0;
    if (record.sensorSampleCount == 0) {
        return false;
    }
    constexpr std::uint16_t invalidMask =
        kWaterSensorFlagAdcOffline | kWaterSensorFlagTdsAdcOverflow | kWaterSensorFlagTdsInvalid |
        kWaterSensorFlagTdsUnavailable;
    if ((record.sensorFlags & invalidMask) != 0) {
        return false;
    }
    if (uncalibrated && !includeUncalibratedSensors) {
        return false;
    }
    return true;
}

bool recordHasUncalibratedTds(const WaterRecord& record) {
    constexpr std::uint16_t noTdsMask =
        kWaterSensorFlagAdcOffline | kWaterSensorFlagTdsAdcOverflow | kWaterSensorFlagTdsInvalid |
        kWaterSensorFlagTdsUnavailable;
    return record.sensorSampleCount > 0 && (record.sensorFlags & kWaterSensorFlagTdsUncalibrated) != 0 &&
           (record.sensorFlags & noTdsMask) == 0;
}

void applyTemperatureSummaryToDaily(DailyUsageBucket& daily,
                                    const WaterRecord& record,
                                    std::int64_t& tempSum,
                                    std::uint32_t& sampleSum) {
    if (daily.temperatureRecordCount == 0) {
        daily.temperatureMinCentiC = record.temperatureCentiC;
        daily.temperatureMaxCentiC = record.temperatureCentiC;
    } else {
        daily.temperatureMinCentiC = std::min(daily.temperatureMinCentiC, record.temperatureCentiC);
        daily.temperatureMaxCentiC = std::max(daily.temperatureMaxCentiC, record.temperatureCentiC);
    }
    tempSum += static_cast<std::int64_t>(record.temperatureCentiC) * record.sensorSampleCount;
    sampleSum += record.sensorSampleCount;
    incrementCount(daily.temperatureRecordCount);
}

void applyTdsSummaryToDaily(DailyUsageBucket& daily,
                            const WaterRecord& record,
                            std::uint64_t& tdsSum,
                            std::uint32_t& sampleSum) {
    if (daily.tdsRecordCount == 0) {
        daily.tdsMinPpm = record.tdsPpm;
        daily.tdsMaxPpm = record.tdsPpm;
    } else {
        daily.tdsMinPpm = std::min(daily.tdsMinPpm, record.tdsPpm);
        daily.tdsMaxPpm = std::max(daily.tdsMaxPpm, record.tdsPpm);
    }
    tdsSum += static_cast<std::uint64_t>(record.tdsPpm) * record.sensorSampleCount;
    sampleSum += record.sensorSampleCount;
    incrementCount(daily.tdsRecordCount);
}

}  // namespace

std::size_t queryWaterRecords(const WaterRecordReader& reader,
                              const WaterRecordFilter& filter,
                              std::size_t pageIndex,
                              std::uint16_t pageSize,
                              WaterRecord* output,
                              std::size_t outputCapacity,
                              std::size_t* totalMatches) {
    if (totalMatches) {
        *totalMatches = 0;
    }
    if (!reader.ready() || !output || outputCapacity == 0) {
        return 0;
    }

    const std::uint16_t sanitizedPageSize = sanitizeRecordPageSize(pageSize);
    if (!filter.hasStart && !filter.hasEnd) {
        if (totalMatches) {
            *totalMatches = reader.count();
        }
        return reader.readPage(pageIndex, sanitizedPageSize, output, outputCapacity);
    }

    const std::size_t startOffset = pageIndex * static_cast<std::size_t>(sanitizedPageSize);
    const std::size_t limit = std::min<std::size_t>(sanitizedPageSize, outputCapacity);
    constexpr std::uint16_t kQueryPageSize = kDefaultRecordPageSize;
    std::unique_ptr<WaterRecord[]> records(new (std::nothrow) WaterRecord[kQueryPageSize]{});
    if (!records) {
        return 0;
    }
    std::size_t matched = 0;
    std::size_t copied = 0;
    const std::size_t total = reader.count();
    bool stop = false;
    for (std::size_t offset = 0; offset < total && !stop; offset += kQueryPageSize) {
        const std::size_t page = offset / kQueryPageSize;
        const std::size_t count = reader.readPage(page, kQueryPageSize, records.get(), kQueryPageSize);
        if (count == 0) {
            break;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const WaterRecord& record = records[i];
            if (!waterRecordHasRealTime(record)) {
                continue;
            }
            if (filter.hasEnd && record.startTime > filter.endTime) {
                continue;
            }
            if (filter.hasStart && record.startTime < filter.startTime) {
                stop = true;
                break;
            }
            if (matched >= startOffset && copied < limit) {
                output[copied++] = record;
            }
            ++matched;
        }
    }
    if (totalMatches) {
        *totalMatches = matched;
    }
    return copied;
}

bool aggregateWaterRecordsInto(const WaterRecordReader& reader,
                               std::uint32_t nowSeconds,
                               std::uint8_t dayCount,
                               bool includeUncalibratedSensors,
                               WaterUsageSummary& summary) {
    std::memset(&summary, 0, sizeof(summary));
    if (dayCount == 0 || dayCount > kUsageSummaryMaxDays) {
        dayCount = kUsageSummaryMaxDays;
    }
    summary.dayCount = dayCount;
    const bool hasRealNow = nowSeconds >= kMinRealDateSeconds;
    summary.todayDay = hasRealNow ? nowSeconds / 86400UL : 0;
    summary.monthStartDay = hasRealNow ? monthStartDay(summary.todayDay) : 0;
    const std::uint32_t firstDay =
        hasRealNow && summary.todayDay >= dayCount - 1U ? summary.todayDay - (dayCount - 1U) : 0;
    const std::uint32_t firstNeededDay = summary.monthStartDay < firstDay ? summary.monthStartDay : firstDay;
    for (std::size_t i = 0; i < dayCount; ++i) {
        summary.days[i].dayIndex = firstDay + static_cast<std::uint32_t>(i);
    }
    if (!reader.ready()) {
        return true;
    }

    constexpr std::uint16_t kAggregationPageSize = kDefaultRecordPageSize;
    std::unique_ptr<std::int64_t[]> dailyTempSums(new (std::nothrow) std::int64_t[kUsageSummaryMaxDays]{});
    std::unique_ptr<std::uint64_t[]> dailyTdsSums(new (std::nothrow) std::uint64_t[kUsageSummaryMaxDays]{});
    std::unique_ptr<std::uint32_t[]> dailyTempSamples(new (std::nothrow) std::uint32_t[kUsageSummaryMaxDays]{});
    std::unique_ptr<std::uint32_t[]> dailyTdsSamples(new (std::nothrow) std::uint32_t[kUsageSummaryMaxDays]{});
    std::unique_ptr<WaterRecord[]> records(new (std::nothrow) WaterRecord[kAggregationPageSize]{});
    if (!dailyTempSums || !dailyTdsSums || !dailyTempSamples || !dailyTdsSamples || !records) {
        return false;
    }
    const std::size_t total = reader.count();
    for (std::size_t offset = 0; offset < total; offset += kAggregationPageSize) {
        const std::size_t page = offset / kAggregationPageSize;
        const std::size_t count = reader.readPage(page, kAggregationPageSize, records.get(), kAggregationPageSize);
        if (count == 0) {
            break;
        }
        bool pageHasWindowCandidate = !hasRealNow;
        for (std::size_t i = 0; i < count; ++i) {
            const WaterRecord& record = records[i];
            if (!waterRecordHasRealTime(record)) {
                ++summary.unknownCount;
                addSaturating(summary.unknownMl, record.volumeMl);
                addSaturating(summary.unknownDurationSec, record.durationSec);
                continue;
            }

            addSaturating(summary.totalMl, record.volumeMl);
            addSaturating(summary.totalCount, 1);
            if (!hasRealNow) {
                continue;
            }
            const std::uint32_t day = record.startTime / 86400UL;
            if (day > summary.todayDay) {
                continue;
            }
            if (day >= firstNeededDay) {
                pageHasWindowCandidate = true;
            }
            if (day == summary.todayDay) {
                addSaturating(summary.todayMl, record.volumeMl);
                addSaturating(summary.todayCount, 1);
            }
            if (day >= summary.monthStartDay) {
                addSaturating(summary.monthMl, record.volumeMl);
                addSaturating(summary.monthCount, 1);
            }
            if (summary.todayDay - day < kUsageSummaryMaxDays) {
                addSaturating(summary.last30DaysMl, record.volumeMl);
                addSaturating(summary.last30DaysCount, 1);
            }
            if (day < firstDay || day > summary.todayDay) {
                continue;
            }

            DailyUsageBucket& daily = summary.days[day - firstDay];
            addSaturating(daily.volumeMl, record.volumeMl);
            addSaturating(daily.durationSec, record.durationSec);
            incrementCount(daily.count);

            if (record.selectedPreset < kPresetCount) {
                CountVolumeBucket& preset = summary.presetCounts[record.selectedPreset];
                addSaturating(preset.volumeMl, record.volumeMl);
                incrementCount(preset.count);
            }
            CountVolumeBucket& hour = summary.hourBuckets[(record.startTime % 86400UL) / 3600UL];
            addSaturating(hour.volumeMl, record.volumeMl);
            incrementCount(hour.count);

            const std::size_t resultIndex = static_cast<std::size_t>(record.result);
            if (resultIndex < kUsageResultCount) {
                ++summary.resultCounts[resultIndex];
            }

            CountVolumeBucket& hist = summary.volumeHist[volumeHistIndex(record.volumeMl)];
            addSaturating(hist.volumeMl, record.volumeMl);
            incrementCount(hist.count);

            if (record.sensorSampleCount > 0) {
                const bool temperatureUsable = recordHasUsableTemperature(record);
                const bool tdsUsable = recordHasUsableTds(record, includeUncalibratedSensors);
                const bool hasUncalibratedTds = recordHasUncalibratedTds(record);
                if (temperatureUsable || tdsUsable) {
                    addSaturating(summary.sensorRecordCount, 1);
                    if (hasUncalibratedTds) {
                        addSaturating(summary.uncalibratedSensorRecordCount, 1);
                        incrementCount(daily.uncalibratedSensorRecordCount);
                    }
                    incrementCount(daily.sensorRecordCount);
                    if (temperatureUsable) {
                        applyTemperatureSummaryToDaily(daily,
                                                       record,
                                                       dailyTempSums[day - firstDay],
                                                       dailyTempSamples[day - firstDay]);
                    }
                    if (tdsUsable) {
                        applyTdsSummaryToDaily(daily, record, dailyTdsSums[day - firstDay], dailyTdsSamples[day - firstDay]);
                    }
                } else if (hasUncalibratedTds) {
                    addSaturating(summary.uncalibratedSensorRecordCount, 1);
                    incrementCount(daily.uncalibratedSensorRecordCount);
                } else {
                    addSaturating(summary.invalidSensorRecordCount, 1);
                }
            }
        }
        if (!pageHasWindowCandidate) {
            break;
        }
    }
    for (std::size_t i = 0; i < dayCount; ++i) {
        const std::uint32_t tempSamples = dailyTempSamples[i];
        if (tempSamples > 0) {
            summary.days[i].temperatureAvgCentiC =
                static_cast<std::int16_t>(dailyTempSums[i] / static_cast<std::int64_t>(tempSamples));
        }
        const std::uint32_t tdsSamples = dailyTdsSamples[i];
        if (tdsSamples > 0) {
            summary.days[i].tdsAvgPpm = static_cast<std::uint16_t>(dailyTdsSums[i] / tdsSamples);
        }
    }
    summary.last30DaysDailyAverageMl = (summary.last30DaysMl + 15UL) / 30UL;
    return true;
}

WaterUsageSummary aggregateWaterRecords(const WaterRecordReader& reader,
                                        std::uint32_t nowSeconds,
                                        std::uint8_t dayCount,
                                        bool includeUncalibratedSensors) {
    std::unique_ptr<WaterUsageSummary> summary(new (std::nothrow) WaterUsageSummary);
    if (!summary) {
        return WaterUsageSummary{};
    }
    if (!aggregateWaterRecordsInto(reader, nowSeconds, dayCount, includeUncalibratedSensors, *summary)) {
        return *summary;
    }
    return *summary;
}

}  // namespace faucet
