#include "web/FaucetWebRoutes.h"

#include <cstring>

namespace faucet {
namespace {

constexpr FaucetWebRoute kRoutes[] = {
    {"/faucet", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "出水龙头"},
    {"/faucet/config", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "出水配置"},
    {"/faucet/logs", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "出水记录"},
    {"/faucet/stats", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "用水统计"},
    {"/faucet/filters", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "滤芯状态"},
    {"/faucet/calibration", FaucetWebMethod::Get, FaucetWebRouteKind::Page, "流量校准"},
    {"/api/faucet/status", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/config", FaucetWebMethod::Any, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/presets", FaucetWebMethod::Any, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/logs", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/stats", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/filters", FaucetWebMethod::Get, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/filters/reset", FaucetWebMethod::Post, FaucetWebRouteKind::Api, nullptr},
    {"/api/faucet/calibration", FaucetWebMethod::Any, FaucetWebRouteKind::Api, nullptr},
};

bool containsToken(const char* path, const char* token) {
    return path && token && std::strstr(path, token) != nullptr;
}

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

    if (equals(path, "/api/faucet/start") || equals(path, "/api/faucet/stop") ||
        equals(path, "/api/faucet/pause") || equals(path, "/api/faucet/resume")) {
        return false;
    }
    if (containsToken(path, "/api/faucet/water/")) {
        return false;
    }
    return true;
}

bool faucetWebRoutesFitEsp32Base(std::size_t maxRoutes) {
    return faucetWebRouteCount() <= maxRoutes;
}

}  // namespace faucet
