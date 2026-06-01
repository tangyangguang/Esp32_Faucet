#include "web/FaucetWebPolicy.h"

#include <cstdio>

namespace faucet {

namespace {

const char* busyLocationFor(FaucetWebWriteTarget target) {
    switch (target) {
        case FaucetWebWriteTarget::Records:
            return "/faucet/records?error=busy";
        case FaucetWebWriteTarget::Calibration:
            return "/faucet/calibration?error=busy";
        case FaucetWebWriteTarget::Filters:
        default:
            return "/faucet/filters?error=busy";
    }
}

}  // namespace

bool faucetWebWriteBusyRedirect(bool waterTaskActive,
                                FaucetWebWriteTarget target,
                                char* location,
                                std::size_t locationLen) {
    if (!waterTaskActive) {
        return false;
    }
    if (location && locationLen > 0) {
        std::snprintf(location, locationLen, "%s", busyLocationFor(target));
    }
    return true;
}

}  // namespace faucet
