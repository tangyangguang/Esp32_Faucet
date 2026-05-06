#pragma once

#include "app/AppConfig.h"
#include "app/AppTypes.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class WaterLogFileBackend {
public:
    virtual ~WaterLogFileBackend() = default;

    virtual bool exists(const char* path) = 0;
    virtual std::int64_t fileSize(const char* path) = 0;
    virtual bool createSized(const char* path, std::size_t size) = 0;
    virtual bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) = 0;
    virtual bool readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) = 0;
    virtual bool writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) = 0;
    virtual bool removeFile(const char* path) = 0;
};

class WaterLogFileStore {
public:
    WaterLogFileStore(WaterLogFileBackend& backend, const char* path, std::size_t capacity);

    bool begin();
    bool append(const WaterLogRecord& record);
    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterLogRecord* output,
                         std::size_t outputCapacity) const;
    bool clear();

    std::size_t count() const;
    std::size_t capacity() const;
    bool ready() const;

private:
    bool initializeNewFile();
    bool loadHeader();
    bool saveHeader() const;
    bool appendRecord(std::size_t index, const WaterLogRecord& record);
    std::size_t fileSizeBytes() const;
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;
    std::size_t recordOffset(std::size_t index) const;

    WaterLogFileBackend& backend_;
    const char* path_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
    bool ready_;
};

}  // namespace faucet
