#include "app/StatisticsStore.h"

#include <limits>

namespace faucet {
namespace {

std::uint32_t saturatingAdd(std::uint32_t a, std::uint32_t b) {
    const std::uint32_t max = std::numeric_limits<std::uint32_t>::max();
    return max - a < b ? max : a + b;
}

}  // namespace

StatisticsStore::StatisticsStore() : record_{} {}

StatisticsStore::StatisticsStore(const StatisticsRecord& record) : record_(record) {}

const StatisticsRecord& StatisticsStore::record() const {
    return record_;
}

void StatisticsStore::reset(const PeriodKeys& keys) {
    record_ = {};
    record_.lastDayKey = keys.dayKey;
    record_.lastWeekKey = keys.weekKey;
    record_.lastMonthKey = keys.monthKey;
}

void StatisticsStore::addWater(std::uint32_t volumeMl, const PeriodKeys& keys) {
    rollPeriods(keys);
    record_.todayMl = saturatingAdd(record_.todayMl, volumeMl);
    record_.weekMl = saturatingAdd(record_.weekMl, volumeMl);
    record_.monthMl = saturatingAdd(record_.monthMl, volumeMl);
    record_.totalMl = saturatingAdd(record_.totalMl, volumeMl);
}

void StatisticsStore::rollPeriods(const PeriodKeys& keys) {
    if (record_.lastDayKey != keys.dayKey) {
        record_.todayMl = 0;
        record_.lastDayKey = keys.dayKey;
    }
    if (record_.lastWeekKey != keys.weekKey) {
        record_.weekMl = 0;
        record_.lastWeekKey = keys.weekKey;
    }
    if (record_.lastMonthKey != keys.monthKey) {
        record_.monthMl = 0;
        record_.lastMonthKey = keys.monthKey;
    }
}

}  // namespace faucet
