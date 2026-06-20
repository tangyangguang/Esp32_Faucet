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

bool recordHasSensorErrorFlags(const WaterRecord& record, bool includeUncalibratedSensors) {
    constexpr std::uint16_t uncalibratedMask = kWaterSensorFlagTdsUncalibrated;
    const std::uint16_t ignored = includeUncalibratedSensors ? uncalibratedMask : 0;
    return (record.sensorFlags & static_cast<std::uint16_t>(~ignored)) != 0;
}

bool recordHasUsableSensorSummary(const WaterRecord& record, bool includeUncalibratedSensors, bool& uncalibrated) {
    uncalibrated = record.tdsCalibratedAtRun == 0 ||
                   (record.sensorFlags & kWaterSensorFlagTdsUncalibrated) != 0;
    if (record.sensorSampleCount == 0) {
        return false;
    }
    if (recordHasSensorErrorFlags(record, includeUncalibratedSensors)) {
        return false;
    }
    if (uncalibrated && !includeUncalibratedSensors) {
        return false;
    }
    return true;
}

void applySensorSummaryToDaily(DailyUsageBucket& daily,
                               const WaterRecord& record,
                               std::int64_t& tempSum,
                               std::uint32_t& tdsSum,
                               std::uint32_t& sampleSum,
                               bool uncalibrated) {
    if (daily.sensorRecordCount == 0) {
        daily.temperatureMinCentiC = record.temperatureMinCentiC;
        daily.temperatureMaxCentiC = record.temperatureMaxCentiC;
        daily.tdsMinPpm = record.tdsMinPpm;
        daily.tdsMaxPpm = record.tdsMaxPpm;
    } else {
        daily.temperatureMinCentiC = std::min(daily.temperatureMinCentiC, record.temperatureMinCentiC);
        daily.temperatureMaxCentiC = std::max(daily.temperatureMaxCentiC, record.temperatureMaxCentiC);
        daily.tdsMinPpm = std::min(daily.tdsMinPpm, record.tdsMinPpm);
        daily.tdsMaxPpm = std::max(daily.tdsMaxPpm, record.tdsMaxPpm);
    }
    tempSum += static_cast<std::int64_t>(record.temperatureAvgCentiC) * record.sensorSampleCount;
    tdsSum += static_cast<std::uint32_t>(record.tdsAvgPpm) * record.sensorSampleCount;
    sampleSum += record.sensorSampleCount;
    incrementCount(daily.sensorRecordCount);
    if (uncalibrated) {
        incrementCount(daily.uncalibratedSensorRecordCount);
    }
}

}  // namespace

WaterRecordStore::WaterRecordStore(WaterRecord* records, std::size_t capacity)
    : records_(records), capacity_(capacity), oldestIndex_(0), count_(0) {}

bool WaterRecordStore::append(const WaterRecord& record) {
    if (!records_ || capacity_ == 0) {
        return false;
    }

    if (count_ < capacity_) {
        const std::size_t index = (oldestIndex_ + count_) % capacity_;
        records_[index] = record;
        ++count_;
        return true;
    }

    records_[oldestIndex_] = record;
    oldestIndex_ = (oldestIndex_ + 1) % capacity_;
    return true;
}

std::size_t WaterRecordStore::rewriteBootRelativeTimes(std::uint32_t bootId, std::uint32_t bootStartRealSec) {
    if (!records_ || bootId == 0) {
        return 0;
    }
    std::size_t changed = 0;
    for (std::size_t offset = 0; offset < count_; ++offset) {
        WaterRecord& record = records_[physicalIndexFromNewestOffset(offset)];
        if (waterRecordHasBootRelativeTime(record) && waterRecordBootId(record) == bootId) {
            record.startTime = bootStartRealSec + record.startTime;
            clearWaterRecordBootId(record);
            ++changed;
        }
    }
    return changed;
}

std::size_t WaterRecordStore::readPage(std::size_t pageIndex,
                                    std::uint16_t pageSize,
                                    WaterRecord* output,
                                    std::size_t outputCapacity) const {
    if (!output || outputCapacity == 0 || count_ == 0) {
        return 0;
    }

    const std::uint16_t sanitizedPageSize = sanitizeRecordPageSize(pageSize);
    const std::size_t startOffset = pageIndex * static_cast<std::size_t>(sanitizedPageSize);
    if (startOffset >= count_) {
        return 0;
    }

    const std::size_t available = count_ - startOffset;
    const std::size_t limit = std::min<std::size_t>({available, sanitizedPageSize, outputCapacity});
    for (std::size_t i = 0; i < limit; ++i) {
        output[i] = records_[physicalIndexFromNewestOffset(startOffset + i)];
    }
    return limit;
}

const WaterRecord* WaterRecordStore::newest(std::size_t offset) const {
    if (!records_ || offset >= count_) {
        return nullptr;
    }
    return &records_[physicalIndexFromNewestOffset(offset)];
}

void WaterRecordStore::clear() {
    oldestIndex_ = 0;
    count_ = 0;
}

std::size_t WaterRecordStore::count() const {
    return count_;
}

std::size_t WaterRecordStore::capacity() const {
    return capacity_;
}

bool WaterRecordStore::ready() const {
    return records_ && capacity_ > 0;
}

const char* WaterRecordStore::storageName() const {
    return ready() ? "ram" : "unavailable";
}

WaterRecordFileStatus WaterRecordStore::status() const {
    return ready() ? WaterRecordFileStatus::Ready : WaterRecordFileStatus::Unavailable;
}

bool WaterRecordStore::full() const {
    return count_ == capacity_ && capacity_ > 0;
}

std::size_t WaterRecordStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    return (oldestIndex_ + count_ - 1 - offset) % capacity_;
}

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
    std::unique_ptr<std::uint32_t[]> dailyTdsSums(new (std::nothrow) std::uint32_t[kUsageSummaryMaxDays]{});
    std::unique_ptr<std::uint32_t[]> dailySensorSamples(new (std::nothrow) std::uint32_t[kUsageSummaryMaxDays]{});
    std::unique_ptr<WaterRecord[]> records(new (std::nothrow) WaterRecord[kAggregationPageSize]{});
    if (!dailyTempSums || !dailyTdsSums || !dailySensorSamples || !records) {
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
                bool uncalibrated = false;
                const bool usable =
                    recordHasUsableSensorSummary(record, includeUncalibratedSensors, uncalibrated);
                if (usable) {
                    addSaturating(summary.sensorRecordCount, 1);
                    if (uncalibrated) {
                        addSaturating(summary.uncalibratedSensorRecordCount, 1);
                    }
                    applySensorSummaryToDaily(daily,
                                              record,
                                              dailyTempSums[day - firstDay],
                                              dailyTdsSums[day - firstDay],
                                              dailySensorSamples[day - firstDay],
                                              uncalibrated);
                } else if (uncalibrated) {
                    addSaturating(summary.uncalibratedSensorRecordCount, 1);
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
        const std::uint32_t samples = dailySensorSamples[i];
        if (samples == 0) {
            continue;
        }
        summary.days[i].temperatureAvgCentiC =
            static_cast<std::int16_t>(dailyTempSums[i] / static_cast<std::int64_t>(samples));
        summary.days[i].tdsAvgPpm = static_cast<std::uint16_t>(dailyTdsSums[i] / samples);
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
