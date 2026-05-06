#include "app/FilterStore.h"

#include <algorithm>
#include <limits>

namespace faucet {
namespace {

std::uint32_t saturatingAdd(std::uint32_t a, std::uint32_t b) {
    const std::uint32_t max = std::numeric_limits<std::uint32_t>::max();
    return max - a < b ? max : a + b;
}

}  // namespace

FilterStore::FilterStore(const FilterRecord (&records)[kFilterCount]) {
    std::copy(records, records + kFilterCount, records_);
}

const FilterRecord (&FilterStore::records() const)[kFilterCount] {
    return records_;
}

const FilterRecord& FilterStore::record(std::size_t index) const {
    if (index >= kFilterCount) {
        return records_[0];
    }
    return records_[index];
}

bool FilterStore::resetFilter(std::size_t index, std::uint32_t nowSeconds) {
    if (index >= kFilterCount) {
        return false;
    }
    records_[index].startTime = nowSeconds;
    records_[index].usedMl = 0;
    return true;
}

void FilterStore::addWater(std::uint32_t volumeMl) {
    for (auto& record : records_) {
        if (record.enabled) {
            record.usedMl = saturatingAdd(record.usedMl, volumeMl);
        }
    }
}

std::uint32_t FilterStore::usedDays(std::size_t index, std::uint32_t nowSeconds) const {
    if (index >= kFilterCount || records_[index].startTime == 0 || nowSeconds <= records_[index].startTime) {
        return 0;
    }
    constexpr std::uint32_t secondsPerDay = 24UL * 60UL * 60UL;
    return (nowSeconds - records_[index].startTime) / secondsPerDay;
}

}  // namespace faucet
