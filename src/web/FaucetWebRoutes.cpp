#include "web/FaucetWebRoutes.h"

#include <cstring>

namespace faucet {
namespace {

constexpr FaucetWebRoute kRoutes[] = {
    {"/index", FaucetWebMethod::Get, "首页", FaucetWebHandler::HomePage},
    {"/faucet/records", FaucetWebMethod::Get, "记录", FaucetWebHandler::RecordsPage},
    {"/faucet/stats", FaucetWebMethod::Get, "统计", FaucetWebHandler::StatsPage},
    {"/faucet/presets", FaucetWebMethod::Get, "预设", FaucetWebHandler::PresetsPage},
    {"/faucet/filters", FaucetWebMethod::Get, "滤芯", FaucetWebHandler::FiltersPage},
    {"/faucet/calibration", FaucetWebMethod::Get, "校准", FaucetWebHandler::CalibrationPage},
    {"/faucet/app.css", FaucetWebMethod::Get, nullptr, FaucetWebHandler::AppCss},
    {"/faucet/calibration", FaucetWebMethod::Post, nullptr, FaucetWebHandler::CalibrationPost},
    {"/faucet/calibration/flow", FaucetWebMethod::Get, nullptr, FaucetWebHandler::FlowCalibrationPage},
    {"/faucet/calibration/flow", FaucetWebMethod::Post, nullptr, FaucetWebHandler::FlowCalibrationPost},
    {"/faucet/filters/edit", FaucetWebMethod::Get, nullptr, FaucetWebHandler::FilterEditPage},
    {"/faucet/records/detail", FaucetWebMethod::Get, nullptr, FaucetWebHandler::RecordDetailPage},
    {"/faucet/calibration/detail", FaucetWebMethod::Get, nullptr, FaucetWebHandler::CalibrationDetailPage},
    {"/api/faucet/status", FaucetWebMethod::Get, nullptr, FaucetWebHandler::StatusApi},
    {"/api/faucet/today", FaucetWebMethod::Get, nullptr, FaucetWebHandler::TodayOverviewApi},
    {"/api/faucet/presets", FaucetWebMethod::Get, nullptr, FaucetWebHandler::PresetsApi},
    {"/api/faucet/presets", FaucetWebMethod::Post, nullptr, FaucetWebHandler::PresetsApi},
    {"/api/faucet/records", FaucetWebMethod::Get, nullptr, FaucetWebHandler::RecordsApi},
    {"/api/faucet/records", FaucetWebMethod::Post, nullptr, FaucetWebHandler::RecordsApi},
    {"/api/faucet/stats", FaucetWebMethod::Get, nullptr, FaucetWebHandler::StatsApi},
    {"/api/faucet/filters", FaucetWebMethod::Get, nullptr, FaucetWebHandler::FiltersApi},
    {"/api/faucet/filters", FaucetWebMethod::Post, nullptr, FaucetWebHandler::FiltersApi},
    {"/api/faucet/filters/reset", FaucetWebMethod::Post, nullptr, FaucetWebHandler::FiltersResetApi},
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
