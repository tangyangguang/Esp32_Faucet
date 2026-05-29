#pragma once

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kFaucetWebMaxRoutes = 18;

enum class FaucetWebMethod : std::uint8_t {
    Get = 0,
    Post = 1,
    Any = 2,
};

enum class FaucetWebRouteKind : std::uint8_t {
    Page = 0,
    Api = 1,
};

struct FaucetWebRoute {
    const char* path;
    FaucetWebMethod method;
    FaucetWebRouteKind kind;
    const char* title;
};

const FaucetWebRoute* faucetWebRoutes();
std::size_t faucetWebRouteCount();
bool faucetWebRouteAllowed(const char* path);
bool faucetWebRoutesFitEsp32Base(std::size_t maxRoutes = kFaucetWebMaxRoutes);

}  // namespace faucet
