#pragma once

#include "app/AppConfig.h"
#include "app/AppTypes.h"
#include "app/WaterRecordStore.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class WaterRecordFileBackend {
public:
    virtual ~WaterRecordFileBackend() = default;

    virtual bool exists(const char* path) = 0;
    virtual std::int64_t fileSize(const char* path) = 0;
    virtual bool createSized(const char* path, std::size_t size) = 0;
    virtual bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) = 0;
    virtual bool readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) = 0;
    virtual bool writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) = 0;
    virtual bool removeFile(const char* path) = 0;
};

class WaterRecordFileStore : public WaterRecordReader {
public:
    WaterRecordFileStore(WaterRecordFileBackend& backend, const char* path, std::size_t capacity);

    bool begin();
    bool append(const WaterRecord& record);
    std::size_t rewriteBootRelativeTimes(std::uint32_t bootId, std::uint32_t bootStartRealSec);
    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterRecord* output,
                         std::size_t outputCapacity) const override;
    bool clear();

    std::size_t count() const override;
    std::size_t capacity() const;
    bool ready() const override;
    const char* storageName() const override;
    WaterRecordFileStatus status() const override;

private:
    bool initializeNewFile();
    bool loadHeader();
    bool saveHeader();
    bool appendRecord(std::size_t index, const WaterRecord& record);
    bool readRecordSpan(std::size_t firstIndex, WaterRecord* output, std::size_t count) const;
    std::size_t fileSizeBytes() const;
    std::size_t backupHeaderOffset() const;
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;
    std::size_t recordOffset(std::size_t index) const;

    WaterRecordFileBackend& backend_;
    const char* path_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
    bool ready_;
    WaterRecordFileStatus status_;
};

}  // namespace faucet
