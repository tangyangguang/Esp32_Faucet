#pragma once

#include "app/WaterRecordFileStore.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

struct WaterRecordMeteringSnapshot {
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
    std::uint32_t meteringSchemeId;
    std::uint32_t meteringSchemeRevision;
    MeteringParameters params;
    std::uint8_t reserved[4];
};

WaterRecordMeteringSnapshot makeWaterRecordMeteringSnapshot(const WaterRecord& record);
bool sameWaterRecordMeteringSnapshotIdentity(const WaterRecordMeteringSnapshot& snapshot,
                                             const WaterRecord& record);

class WaterRecordMeteringSnapshotReader {
public:
    virtual ~WaterRecordMeteringSnapshotReader() = default;

    virtual bool find(const WaterRecord& record, WaterRecordMeteringSnapshot& output) const = 0;
    virtual std::size_t findAny(const WaterRecord* records,
                                std::size_t recordCount,
                                WaterRecordMeteringSnapshot* output,
                                bool* found) const;
    virtual std::size_t count() const = 0;
    virtual bool ready() const = 0;
    virtual const char* storageName() const = 0;
};

class WaterRecordMeteringSnapshotWriter {
public:
    virtual ~WaterRecordMeteringSnapshotWriter() = default;

    virtual bool upsert(const WaterRecordMeteringSnapshot& snapshot) = 0;
};

class WaterRecordMeteringSnapshotStore : public WaterRecordMeteringSnapshotReader,
                                         public WaterRecordMeteringSnapshotWriter {
public:
    WaterRecordMeteringSnapshotStore(WaterRecordMeteringSnapshot* entries, std::size_t capacity);

    bool upsert(const WaterRecordMeteringSnapshot& snapshot) override;
    bool find(const WaterRecord& record, WaterRecordMeteringSnapshot& output) const override;
    std::size_t findAny(const WaterRecord* records,
                        std::size_t recordCount,
                        WaterRecordMeteringSnapshot* output,
                        bool* found) const override;
    std::size_t count() const override;
    std::size_t capacity() const;
    bool ready() const override;
    const char* storageName() const override;

private:
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;
    std::size_t appendIndex();

    WaterRecordMeteringSnapshot* entries_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
};

class WaterRecordMeteringSnapshotFileStore : public WaterRecordMeteringSnapshotReader,
                                             public WaterRecordMeteringSnapshotWriter {
public:
    WaterRecordMeteringSnapshotFileStore(WaterRecordFileBackend& backend,
                                         const char* path,
                                         std::size_t capacity);

    bool begin();
    bool upsert(const WaterRecordMeteringSnapshot& snapshot) override;
    bool find(const WaterRecord& record, WaterRecordMeteringSnapshot& output) const override;
    std::size_t findAny(const WaterRecord* records,
                        std::size_t recordCount,
                        WaterRecordMeteringSnapshot* output,
                        bool* found) const override;
    std::size_t count() const override;
    std::size_t capacity() const;
    bool ready() const override;
    const char* storageName() const override;

private:
    bool initializeNewFile();
    bool migrateV1File();
    bool loadHeader();
    bool saveHeader() const;
    bool readEntry(std::size_t index, WaterRecordMeteringSnapshot& output) const;
    bool appendEntry(std::size_t index, const WaterRecordMeteringSnapshot& snapshot);
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
