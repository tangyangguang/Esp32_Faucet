#include "app/WaterRecordStore.h"

#include <algorithm>
#include <limits>

namespace faucet {
namespace {

bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

std::uint8_t daysInMonth(std::uint16_t year, std::uint8_t month) {
    static constexpr std::uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return month >= 1 && month <= 12 ? days[month - 1] : 0;
}

void dateFromDayIndex(std::uint32_t day, std::uint16_t& year, std::uint8_t& month, std::uint8_t& monthDay) {
    year = 2000;
    while (true) {
        const std::uint16_t yearDays = isLeapYear(year) ? 366 : 365;
        if (day < yearDays) {
            break;
        }
        day -= yearDays;
        ++year;
    }
    month = 1;
    while (month <= 12) {
        const std::uint8_t monthDays = daysInMonth(year, month);
        if (day < monthDays) {
            break;
        }
        day -= monthDays;
        ++month;
    }
    monthDay = static_cast<std::uint8_t>(day + 1U);
}

std::uint32_t monthStartDay(std::uint32_t day) {
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t monthDay = 0;
    dateFromDayIndex(day, year, month, monthDay);
    return day - static_cast<std::uint32_t>(monthDay - 1U);
}

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
    WaterRecord records[kQueryPageSize]{};
    std::size_t matched = 0;
    std::size_t copied = 0;
    const std::size_t total = reader.count();
    bool stop = false;
    for (std::size_t offset = 0; offset < total && !stop; offset += kQueryPageSize) {
        const std::size_t page = offset / kQueryPageSize;
        const std::size_t count = reader.readPage(page, kQueryPageSize, records, kQueryPageSize);
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

WaterUsageSummary aggregateWaterRecords(const WaterRecordReader& reader,
                                        std::uint32_t nowSeconds,
                                        std::uint8_t dayCount) {
    WaterUsageSummary summary{};
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
        return summary;
    }

    constexpr std::uint16_t kAggregationPageSize = kDefaultRecordPageSize;
    WaterRecord records[kAggregationPageSize]{};
    const std::size_t total = reader.count();
    for (std::size_t offset = 0; offset < total; offset += kAggregationPageSize) {
        const std::size_t page = offset / kAggregationPageSize;
        const std::size_t count = reader.readPage(page, kAggregationPageSize, records, kAggregationPageSize);
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
        }
        if (!pageHasWindowCandidate) {
            break;
        }
    }
    summary.last30DaysDailyAverageMl = (summary.last30DaysMl + 15UL) / 30UL;
    return summary;
}

}  // namespace faucet
