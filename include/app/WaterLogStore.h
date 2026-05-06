#pragma once

#include "app/AppConfig.h"
#include "app/AppTypes.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class WaterLogStore {
public:
    WaterLogStore(WaterLogRecord* records, std::size_t capacity);

    bool append(const WaterLogRecord& record);
    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterLogRecord* output,
                         std::size_t outputCapacity) const;
    const WaterLogRecord* newest(std::size_t offset) const;

    void clear();
    std::size_t count() const;
    std::size_t capacity() const;
    bool full() const;

private:
    std::size_t physicalIndexFromNewestOffset(std::size_t offset) const;

    WaterLogRecord* records_;
    std::size_t capacity_;
    std::size_t oldestIndex_;
    std::size_t count_;
};

}  // namespace faucet
