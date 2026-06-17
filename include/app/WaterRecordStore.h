#pragma once

#include "app/AppConfig.h"
#include "app/AppTypes.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kUsageSummaryMaxDays = 30;
constexpr std::size_t kUsageResultCount = 5;
constexpr std::size_t kUsageVolumeHistCount = 5;

struct DailyUsageBucket {
    std::uint32_t dayIndex;
    std::uint32_t volumeMl;
    std::uint32_t durationSec;
    std::uint16_t count;
    std::int16_t temperatureAvgCentiC;
    std::int16_t temperatureMinCentiC;
    std::int16_t temperatureMaxCentiC;
    std::uint16_t tdsAvgPpm;
    std::uint16_t tdsMinPpm;
    std::uint16_t tdsMaxPpm;
    std::uint16_t sensorRecordCount;
    std::uint16_t uncalibratedSensorRecordCount;
};

struct CountVolumeBucket {
    std::uint32_t volumeMl;
    std::uint16_t count;
};

struct WaterUsageSummary {
    DailyUsageBucket days[kUsageSummaryMaxDays];
    CountVolumeBucket presetCounts[kPresetCount];
    CountVolumeBucket hourBuckets[24];
    CountVolumeBucket volumeHist[kUsageVolumeHistCount];
    std::uint32_t resultCounts[kUsageResultCount];
    std::uint32_t todayMl;
    std::uint32_t todayCount;
    std::uint32_t monthMl;
    std::uint32_t monthCount;
    std::uint32_t last30DaysMl;
    std::uint32_t last30DaysCount;
    std::uint32_t last30DaysDailyAverageMl;
    std::uint32_t totalMl;
    std::uint32_t totalCount;
    std::uint32_t unknownMl;
    std::uint32_t unknownDurationSec;
    std::uint32_t unknownCount;
    std::uint32_t sensorRecordCount;
    std::uint32_t uncalibratedSensorRecordCount;
    std::uint32_t invalidSensorRecordCount;
    std::uint32_t monthStartDay;
    std::uint32_t todayDay;
    std::uint8_t dayCount;
};

enum class WaterRecordFileStatus : std::uint8_t {
    Ready = 0,
    Unavailable = 1,
    Missing = 2,
    InvalidPath = 3,
    InvalidCapacity = 4,
    BackendFailure = 5,
    Corrupt = 6,
    IncompatibleFormat = 7,
};

class WaterRecordReader {
public:
    virtual ~WaterRecordReader() = default;

    virtual std::size_t readPage(std::size_t pageIndex,
                                 std::uint16_t pageSize,
                                 WaterRecord* output,
                                 std::size_t outputCapacity) const = 0;
    virtual std::size_t count() const = 0;
    virtual bool ready() const = 0;
    virtual const char* storageName() const = 0;
    virtual WaterRecordFileStatus status() const {
        return ready() ? WaterRecordFileStatus::Ready : WaterRecordFileStatus::Unavailable;
    }
};

class WaterRecordStore : public WaterRecordReader {
public:
    WaterRecordStore(WaterRecord* records, std::size_t capacity);

    bool append(const WaterRecord& record);
    std::size_t rewriteBootRelativeTimes(std::uint32_t bootId, std::uint32_t bootStartRealSec);
    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterRecord* output,
                         std::size_t outputCapacity) const override;
    const WaterRecord* newest(std::size_t offset) const;

    void clear();
    std::size_t count() const override;
    std::size_t capacity() const;
    bool ready() const override;
    const char* storageName() const override;
    WaterRecordFileStatus status() const override;
    bool full() const;

private:
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;

    WaterRecord* records_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
};

struct WaterRecordFilter {
    bool hasStart;
    bool hasEnd;
    std::uint32_t startTime;
    std::uint32_t endTime;
};

std::size_t queryWaterRecords(const WaterRecordReader& reader,
                              const WaterRecordFilter& filter,
                              std::size_t pageIndex,
                              std::uint16_t pageSize,
                              WaterRecord* output,
                              std::size_t outputCapacity,
                              std::size_t* totalMatches = nullptr);

WaterUsageSummary aggregateWaterRecords(const WaterRecordReader& reader,
                                        std::uint32_t nowSeconds,
                                        std::uint8_t dayCount = kUsageSummaryMaxDays,
                                        bool includeUncalibratedSensors = false);

}  // namespace faucet
