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

FilterStore::FilterStore(const FilterConfig (&configs)[kFilterCount]) {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        records_[i] = mergeFilterRecord(configs[i], FilterRuntime{});
    }
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

void FilterStore::applyRuntime(const FilterRuntime (&runtime)[kFilterCount]) {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        records_[i].startTime = runtime[i].startTime;
        records_[i].usedMl = runtime[i].usedMl;
        records_[i].startBootId = runtime[i].startBootId;
    }
}

void FilterStore::copyRuntime(FilterRuntime (&runtime)[kFilterCount]) const {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        runtime[i] = filterRuntimeFromRecord(records_[i]);
    }
}

bool FilterStore::updateFilter(std::size_t index, const FilterRecord& record) {
    if (index >= kFilterCount) {
        return false;
    }
    records_[index] = record;
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
