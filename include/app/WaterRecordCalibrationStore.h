#pragma once

#include "app/WaterRecordFileStore.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

enum class WaterRecordCalibrationKind : std::uint8_t {
    PulsePerMl = 0,
    StartupCompensation = 1,
};

struct WaterRecordCalibration {
    std::uint32_t startTime;
    std::uint32_t volumeMl;
    std::uint32_t targetValue;
    std::uint32_t pulseCount;
    std::uint32_t rejectedPulseCount;
    std::uint16_t durationSec;
    WaterMode mode;
    WaterResult result;
    std::uint8_t selectedPreset;
    std::uint8_t reserved0;
    float pulsePerMlAtRun;
    std::uint32_t actualMl;
    std::uint32_t calibratedAt;
    float oldPulsePerMl;
    float newPulsePerMl;
    std::uint32_t oldStartupCompensationMl;
    std::uint32_t newStartupCompensationMl;
    std::uint16_t calibrationCount;
    WaterRecordCalibrationKind kind;
    std::uint8_t reserved[5];
};

WaterRecordCalibration makeWaterRecordCalibration(const WaterRecord& record);
bool sameWaterRecordCalibrationIdentity(const WaterRecordCalibration& calibration, const WaterRecord& record);

class WaterRecordCalibrationReader {
public:
    virtual ~WaterRecordCalibrationReader() = default;

    virtual bool find(const WaterRecord& record, WaterRecordCalibration& output) const = 0;
    virtual std::size_t findAny(const WaterRecord* records,
                                std::size_t recordCount,
                                WaterRecordCalibration* output,
                                bool* found) const;
    virtual std::size_t count() const = 0;
    virtual bool ready() const = 0;
    virtual const char* storageName() const = 0;
};

class WaterRecordCalibrationWriter {
public:
    virtual ~WaterRecordCalibrationWriter() = default;

    virtual bool upsert(const WaterRecordCalibration& calibration) = 0;
};

class WaterRecordCalibrationStore : public WaterRecordCalibrationReader, public WaterRecordCalibrationWriter {
public:
    WaterRecordCalibrationStore(WaterRecordCalibration* entries, std::size_t capacity);

    bool upsert(const WaterRecordCalibration& calibration) override;
    bool find(const WaterRecord& record, WaterRecordCalibration& output) const override;
    std::size_t findAny(const WaterRecord* records,
                        std::size_t recordCount,
                        WaterRecordCalibration* output,
                        bool* found) const override;
    std::size_t count() const override;
    std::size_t capacity() const;
    bool ready() const override;
    const char* storageName() const override;

private:
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;
    std::size_t appendIndex();

    WaterRecordCalibration* entries_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
};

class WaterRecordCalibrationFileStore : public WaterRecordCalibrationReader, public WaterRecordCalibrationWriter {
public:
    WaterRecordCalibrationFileStore(WaterRecordFileBackend& backend, const char* path, std::size_t capacity);

    bool begin();
    bool upsert(const WaterRecordCalibration& calibration) override;
    bool find(const WaterRecord& record, WaterRecordCalibration& output) const override;
    std::size_t findAny(const WaterRecord* records,
                        std::size_t recordCount,
                        WaterRecordCalibration* output,
                        bool* found) const override;
    std::size_t count() const override;
    std::size_t capacity() const;
    bool ready() const override;
    const char* storageName() const override;
    bool clear();

private:
    bool initializeNewFile();
    bool loadHeader();
    bool saveHeader() const;
    bool readEntry(std::size_t index, WaterRecordCalibration& output) const;
    bool readEntries(std::size_t firstIndex, WaterRecordCalibration* output, std::size_t count) const;
    bool appendEntry(std::size_t index, const WaterRecordCalibration& calibration);
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;
    std::size_t entryOffset(std::size_t index) const;

    WaterRecordFileBackend& backend_;
    const char* path_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
    bool ready_;
};

}  // namespace faucet
