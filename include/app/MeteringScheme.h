#pragma once

#include "app/AppTypes.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kMeteringSchemeNameLength = 32;
constexpr std::size_t kMeteringSchemeStoreSlotCount = 6;
constexpr const char* kDefaultMeteringSchemeName = "YF-S201 默认计量方案";
constexpr std::uint32_t kDefaultStartupPulseCount = 8;
constexpr std::uint32_t kDefaultStartupVolumeMl = 130;
constexpr std::uint32_t kDefaultStablePulsePerLiter = 248;
constexpr std::uint32_t kDefaultStartupDurationMs = 5000;
constexpr std::uint32_t kDefaultStableFlowMlPerMin = 1950;
constexpr std::uint32_t kMinSegmentedPulsePerLiter = 50;
constexpr std::uint32_t kMaxSegmentedPulsePerLiter = 5000;
constexpr std::uint32_t kMaxSegmentedStartupPulseCount = 100000;
constexpr std::uint32_t kMaxSegmentedStartupVolumeMl = 20000;
constexpr std::uint32_t kMaxSegmentedStartupDurationMs = 60000;
constexpr std::uint32_t kMinStableFlowMlPerMin = 50;
constexpr std::uint32_t kMaxStableFlowMlPerMin = 30000;

enum class MeteringSchemeSource : std::uint8_t {
    Default = 0,
    CalibrationSession = 1,
};

struct MeteringSchemeRecord {
    std::uint32_t id = 0;
    bool recordUsed = false;
    char name[kMeteringSchemeNameLength]{};
    MeteringParameters params{};
    MeteringSchemeSource sourceType = MeteringSchemeSource::Default;
    std::uint32_t createdAt = 0;
    std::uint16_t sampleCount = 0;
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    std::uint16_t maxErrorTenthPercent = 0;
};

struct MeteringSchemeCandidate {
    bool ready = false;
    MeteringSchemeSource sourceType = MeteringSchemeSource::CalibrationSession;
    MeteringParameters params{};
    std::uint32_t generatedAt = 0;
    std::uint16_t sampleCount = 0;
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    std::uint16_t maxErrorTenthPercent = 0;
};

struct MeteringSchemeCollection {
    MeteringSchemeCollection() : records(nullptr), capacity(0), activeSchemeId(0), nextSchemeId(0) {}
    MeteringSchemeCollection(MeteringSchemeRecord* records_,
                             std::size_t capacity_,
                             std::uint32_t activeSchemeId_,
                             std::uint32_t nextSchemeId_)
        : records(records_),
          capacity(capacity_),
          activeSchemeId(activeSchemeId_),
          nextSchemeId(nextSchemeId_) {}

    MeteringSchemeRecord* records = nullptr;
    std::size_t capacity = 0;
    std::uint32_t activeSchemeId = 0;
    std::uint32_t nextSchemeId = 0;
};

struct MeteringTargetEstimate {
    bool valid = false;
    std::uint32_t targetMl = 0;
    std::uint32_t pulseCount = 0;
    std::uint32_t fullRunPulsePerLiter = 0;
};

MeteringParameters defaultMeteringParameters();
bool validMeteringSchemeParameters(const MeteringParameters& params);
std::uint32_t estimatePulsesForVolumeMl(const MeteringParameters& params, std::uint32_t targetMl);
std::uint32_t estimateDurationMsForVolumeMl(const MeteringParameters& params, std::uint32_t targetMl);
std::uint32_t estimateVolumeMlForDurationMs(const MeteringParameters& params, std::uint32_t durationMs);
std::uint32_t fullRunPulsePerLiter(std::uint32_t pulseCount, std::uint32_t volumeMl);
MeteringTargetEstimate meteringEstimateForTarget(const MeteringParameters& params, std::uint32_t targetMl);

bool initializeDefaultMeteringSchemes(MeteringSchemeCollection& schemes, std::uint32_t nowSeconds);
MeteringSchemeRecord* findMeteringSchemeById(MeteringSchemeCollection& schemes, std::uint32_t id);
const MeteringSchemeRecord* findMeteringSchemeById(const MeteringSchemeCollection& schemes, std::uint32_t id);
MeteringSchemeRecord* activeMeteringScheme(MeteringSchemeCollection& schemes);
const MeteringSchemeRecord* activeMeteringScheme(const MeteringSchemeCollection& schemes);

bool saveCandidateAsNewMeteringScheme(MeteringSchemeCollection& schemes,
                                      MeteringSchemeCandidate& candidate,
                                      const char* name,
                                      std::uint32_t nowSeconds,
                                      std::uint32_t& newSchemeId);

}  // namespace faucet
