#pragma once

#include <cstddef>

namespace faucet {

enum class FaucetWebWriteTarget {
    Records,
    Calibration,
    Metering,
    Filters,
};

bool faucetWebWriteBusyRedirect(bool waterTaskActive,
                                FaucetWebWriteTarget target,
                                char* location,
                                std::size_t locationLen);

}  // namespace faucet
