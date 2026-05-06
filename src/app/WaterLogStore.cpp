#include "app/WaterLogStore.h"

#include <algorithm>

namespace faucet {

WaterLogStore::WaterLogStore(WaterLogRecord* records, std::size_t capacity)
    : records_(records), capacity_(capacity), oldestIndex_(0), count_(0) {}

bool WaterLogStore::append(const WaterLogRecord& record) {
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

std::size_t WaterLogStore::readPage(std::size_t pageIndex,
                                    std::uint16_t pageSize,
                                    WaterLogRecord* output,
                                    std::size_t outputCapacity) const {
    if (!output || outputCapacity == 0 || count_ == 0) {
        return 0;
    }

    const std::uint16_t sanitizedPageSize = sanitizeLogPageSize(pageSize);
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

const WaterLogRecord* WaterLogStore::newest(std::size_t offset) const {
    if (!records_ || offset >= count_) {
        return nullptr;
    }
    return &records_[physicalIndexFromNewestOffset(offset)];
}

void WaterLogStore::clear() {
    oldestIndex_ = 0;
    count_ = 0;
}

std::size_t WaterLogStore::count() const {
    return count_;
}

std::size_t WaterLogStore::capacity() const {
    return capacity_;
}

bool WaterLogStore::full() const {
    return count_ == capacity_ && capacity_ > 0;
}

std::size_t WaterLogStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    return (oldestIndex_ + count_ - 1 - offset) % capacity_;
}

}  // namespace faucet
