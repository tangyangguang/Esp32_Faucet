#include "web/FaucetWebRoutes.h"

#include <cstring>

namespace faucet {
namespace {

constexpr FaucetWebRoute kRoutes[] = {
    {"/faucet", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "总览"},
    {"/faucet/presets", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "预设"},
    {"/faucet/filters", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "滤芯"},
    {"/faucet/logs", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "记录"},
    {"/faucet/stats", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "统计"},
    {"/faucet/filters/edit", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/status", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/presets", FaucetWebMethod::Any, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/logs", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/stats", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/filters", FaucetWebMethod::Any, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/filters/reset", FaucetWebMethod::Post, FaucetWebRouteKind::Api, nullptr},
};

bool equals(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

}  // namespace

const FaucetWebRoute* faucetWebRoutes() {
    return kRoutes;
}

std::size_t faucetWebRouteCount() {
    return sizeof(kRoutes) / sizeof(kRoutes[0]);
}

bool faucetWebRouteAllowed(const char* path) {
    if (!path || path[0] != '/') {
        return false;
    }
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (equals(path, kRoutes[i].path)) {
            return true;
        }
    }
    return false;
}

bool faucetWebRoutesFitEsp32Base(std::size_t maxRoutes) {
    return faucetWebRouteCount() <= maxRoutes;
}

}  // namespace faucet
