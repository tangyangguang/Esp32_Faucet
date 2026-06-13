#pragma once

#include <cstdint>

namespace faucet {

class LcdRecoveryPolicy {
public:
    static constexpr std::uint32_t kUserActivityProbeMinMs = 200;
    static constexpr std::uint32_t kActiveProbeMs = 10000;
    static constexpr std::uint32_t kDisconnectedRetryMs = 1000;

    bool shouldProbe(bool frameOn, bool present, bool userActivity, std::uint32_t nowMs) {
        if (!frameOn) {
            return false;
        }
        if (userActivity && elapsedAtLeast(nowMs, lastUserActivityProbeMs_, kUserActivityProbeMinMs)) {
            lastUserActivityProbeMs_ = nowMs;
            return true;
        }
        if (present) {
            if (!elapsedAtLeast(nowMs, lastActiveProbeMs_, kActiveProbeMs)) {
                return false;
            }
            lastActiveProbeMs_ = nowMs;
            return true;
        }
        if (!elapsedAtLeast(nowMs, lastDisconnectedRetryMs_, kDisconnectedRetryMs)) {
            return false;
        }
        lastDisconnectedRetryMs_ = nowMs;
        return true;
    }

private:
    static bool elapsedAtLeast(std::uint32_t nowMs, std::uint32_t sinceMs, std::uint32_t intervalMs) {
        return static_cast<std::uint32_t>(nowMs - sinceMs) >= intervalMs;
    }

    std::uint32_t lastUserActivityProbeMs_ = 0;
    std::uint32_t lastActiveProbeMs_ = 0;
    std::uint32_t lastDisconnectedRetryMs_ = 0;
};

}  // namespace faucet
