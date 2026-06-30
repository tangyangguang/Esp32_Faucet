#pragma once

#include <cstdint>
#include <limits>

namespace faucet {

inline std::uint32_t secondsToMillis(std::uint32_t seconds) {
    constexpr std::uint32_t maxMs = std::numeric_limits<std::uint32_t>::max();
    return seconds > maxMs / 1000UL ? maxMs : seconds * 1000UL;
}

inline std::uint32_t elapsedSince(std::uint32_t now, std::uint32_t start) {
    return now - start;
}

inline bool elapsedAtLeast(std::uint32_t now, std::uint32_t start, std::uint32_t duration) {
    return elapsedSince(now, start) >= duration;
}

}  // namespace faucet
