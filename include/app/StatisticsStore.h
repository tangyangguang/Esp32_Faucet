#pragma once

#include "app/AppTypes.h"

#include <cstdint>

namespace faucet {

struct PeriodKeys {
    std::uint32_t dayKey;
    std::uint32_t weekKey;
    std::uint32_t monthKey;
};

class StatisticsStore {
public:
    StatisticsStore();
    explicit StatisticsStore(const StatisticsRecord& record);

    const StatisticsRecord& record() const;
    void reset(const PeriodKeys& keys);
    void addWater(std::uint32_t volumeMl, const PeriodKeys& keys);
    void rollPeriods(const PeriodKeys& keys);

private:
    StatisticsRecord record_;
};

}  // namespace faucet
