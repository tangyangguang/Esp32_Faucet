#pragma once

#include "app/AppTypes.h"

#include <cstdint>

namespace faucet {

class FilterStore {
public:
    explicit FilterStore(const FilterRecord (&records)[kFilterCount]);

    const FilterRecord (&records() const)[kFilterCount];
    const FilterRecord& record(std::size_t index) const;
    bool updateFilter(std::size_t index, const FilterRecord& record);
    void addWater(std::uint32_t volumeMl);
    std::uint32_t usedDays(std::size_t index, std::uint32_t nowSeconds) const;

private:
    FilterRecord records_[kFilterCount];
};

}  // namespace faucet
