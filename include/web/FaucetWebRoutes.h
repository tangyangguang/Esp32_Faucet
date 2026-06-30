#pragma once

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kFaucetWebMaxRoutes = 24;

enum class FaucetWebMethod : std::uint8_t {
    Get = 0,
    Post = 1,
};

enum class FaucetWebHandler : std::uint8_t {
    HomePage = 0,
    RecordsPage,
    StatsPage,
    PresetsPage,
    FiltersPage,
    CalibrationPage,
    AppCss,
    CalibrationPost,
    FlowCalibrationPage,
    FlowCalibrationPost,
    FilterEditPage,
    RecordDetailPage,
    CalibrationDetailPage,
    StatusApi,
    TodayOverviewApi,
    PresetsApi,
    RecordsApi,
    StatsApi,
    FiltersApi,
    FiltersResetApi,
};

struct FaucetWebRoute {
    const char* path;
    FaucetWebMethod method;
    const char* title;
    FaucetWebHandler handler;
};

const FaucetWebRoute* faucetWebRoutes();
std::size_t faucetWebRouteCount();
bool faucetWebRoutesFitEsp32Base(std::size_t maxRoutes = kFaucetWebMaxRoutes);

}  // namespace faucet
