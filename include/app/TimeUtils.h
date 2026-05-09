#pragma once

#include <cstdint>

namespace faucet {

inline std::uint32_t elapsedSince(std::uint32_t now, std::uint32_t start) {
    return now - start;
}

inline bool elapsedAtLeast(std::uint32_t now, std::uint32_t start, std::uint32_t duration) {
    return elapsedSince(now, start) >= duration;
}

}  // namespace faucet
