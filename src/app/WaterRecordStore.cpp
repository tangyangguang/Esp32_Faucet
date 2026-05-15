#include "app/WaterRecordStore.h"

#include <algorithm>

namespace faucet {

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

bool WaterRecordStore::full() const {
    return count_ == capacity_ && capacity_ > 0;
}

std::size_t WaterRecordStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    return (oldestIndex_ + count_ - 1 - offset) % capacity_;
}

}  // namespace faucet
