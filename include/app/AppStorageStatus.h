#pragma once

#include <cstdint>

namespace faucet {

enum class AppStorageStatus : std::uint8_t {
    Ready,
    Unavailable,
    Missing,
    InvalidPath,
    BackendFailure,
    Corrupt,
    IncompatibleFormat,
};

}  // namespace faucet
