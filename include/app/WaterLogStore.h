#pragma once

#include "app/AppConfig.h"
#include "app/AppTypes.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class WaterLogReader {
public:
    virtual ~WaterLogReader() = default;

    virtual std::size_t readPage(std::size_t pageIndex,
                                 std::uint16_t pageSize,
                                 WaterLogRecord* output,
                                 std::size_t outputCapacity) const = 0;
    virtual std::size_t count() const = 0;
    virtual bool ready() const = 0;
    virtual const char* storageName() const = 0;
};

class WaterLogStore : public WaterLogReader {
public:
    WaterLogStore(WaterLogRecord* records, std::size_t capacity);

    bool append(const WaterLogRecord& record);
    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterLogRecord* output,
                         std::size_t outputCapacity) const override;
    const WaterLogRecord* newest(std::size_t offset) const;

    void clear();
    std::size_t count() const override;
    std::size_t capacity() const;
    bool ready() const override;
    const char* storageName() const override;
    bool full() const;

private:
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;

    WaterLogRecord* records_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
};

}  // namespace faucet
