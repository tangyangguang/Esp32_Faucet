#pragma once

#include "app/AppConfig.h"
#include "app/AppTypes.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

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
    bool full() const;

private:
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;

    WaterRecord* records_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
};

}  // namespace faucet
