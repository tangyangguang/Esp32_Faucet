#pragma once

#include "app/AppTypes.h"

#include <cstdint>

namespace faucet {

class FilterStore {
public:
    explicit FilterStore(const FilterConfig (&configs)[kFilterCount]);

    const FilterRecord (&records() const)[kFilterCount];
    const FilterRecord& record(std::size_t index) const;
    void applyRuntime(const FilterRuntime (&runtime)[kFilterCount]);
    void copyRuntime(FilterRuntime (&runtime)[kFilterCount]) const;
    bool updateFilter(std::size_t index, const FilterRecord& record);
    void addWater(std::uint32_t volumeMl);
    std::uint32_t usedDays(std::size_t index, std::uint32_t nowSeconds) const;

private:
    FilterRecord records_[kFilterCount];
};

}  // namespace faucet
