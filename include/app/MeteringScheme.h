#pragma once

#include "app/AppConfig.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kMeteringSchemeNameLength = 32;
constexpr std::size_t kMeteringSchemeMeterLabelLength = 32;
constexpr std::size_t kMeteringSchemeInstallationLabelLength = 32;
constexpr std::size_t kMeteringSchemeConditionLabelLength = 48;
constexpr std::size_t kMeteringSchemeUserNoteLength = 128;
constexpr std::size_t kMeteringSchemeSummaryLength = 192;
constexpr std::size_t kMeteringSchemeTraceIdCapacity = 12;

enum class MeteringSchemeSource : std::uint8_t {
    Default = 0,
    Generated = 1,
    Manual = 2,
    Migrated = 3,
};

enum class MeteringSchemeEditKind : std::uint8_t {
    NameOnly = 0,
    MeteringOrApplicability = 1,
    NoChange = 2,
};

struct MeteringSchemeRecord {
    std::uint32_t id = 0;
    bool valid = false;
    bool enabled = false;
    char name[kMeteringSchemeNameLength]{};
    char meterLabel[kMeteringSchemeMeterLabelLength]{};
    char installationLabel[kMeteringSchemeInstallationLabelLength]{};
    char conditionLabel[kMeteringSchemeConditionLabelLength]{};
    char userNote[kMeteringSchemeUserNoteLength]{};
    MeteringParameters params{};
    MeteringSchemeSource sourceType = MeteringSchemeSource::Default;
    std::uint32_t revision = 0;
    std::uint32_t createdAt = 0;
    std::uint32_t updatedAt = 0;
    std::uint32_t lastActivatedAt = 0;
    std::uint32_t useCount = 0;
    std::uint32_t lastUsedAt = 0;
    bool usageStatsDirty = false;
    std::uint16_t sampleCount = 0;
    std::uint32_t sampleTraceIds[kMeteringSchemeTraceIdCapacity]{};
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    float maxErrorPercent = 0.0f;
    std::uint32_t startupDurationMinSec = 0;
    std::uint32_t startupDurationMaxSec = 0;
    std::uint32_t startupDurationMedianSec = 0;
    std::uint32_t startupDurationAvgSec = 0;
    char creationSummary[kMeteringSchemeSummaryLength]{};
    char lastModifiedSummary[kMeteringSchemeSummaryLength]{};
};

struct MeteringSchemeCandidate {
    bool ready = false;
    MeteringParameters params{};
    std::uint32_t generatedAt = 0;
    std::uint16_t sampleCount = 0;
    std::uint32_t sampleTraceIds[kMeteringSchemeTraceIdCapacity]{};
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    float maxErrorPercent = 0.0f;
    std::uint32_t startupDurationMinSec = 0;
    std::uint32_t startupDurationMaxSec = 0;
    std::uint32_t startupDurationMedianSec = 0;
    std::uint32_t startupDurationAvgSec = 0;
    char creationSummary[kMeteringSchemeSummaryLength]{};
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

struct MeteringSchemeEdit {
    char name[kMeteringSchemeNameLength]{};
    char meterLabel[kMeteringSchemeMeterLabelLength]{};
    char installationLabel[kMeteringSchemeInstallationLabelLength]{};
    char conditionLabel[kMeteringSchemeConditionLabelLength]{};
    char userNote[kMeteringSchemeUserNoteLength]{};
    MeteringParameters params{};
};

MeteringParameters defaultMeteringParameters();
bool validMeteringSchemeParameters(const MeteringParameters& params);

bool initializeDefaultMeteringSchemes(MeteringSchemeCollection& schemes, std::uint32_t nowSeconds);
void initializeManualMeteringScheme(MeteringSchemeRecord& scheme,
                                    std::uint32_t id,
                                    const char* name,
                                    const MeteringParameters& params,
                                    std::uint32_t nowSeconds);
std::size_t meteringSchemeCount(const MeteringSchemeCollection& schemes, bool includeDisabled);
MeteringSchemeRecord* findMeteringSchemeById(MeteringSchemeCollection& schemes, std::uint32_t id);
const MeteringSchemeRecord* findMeteringSchemeById(const MeteringSchemeCollection& schemes, std::uint32_t id);
MeteringSchemeRecord* activeMeteringScheme(MeteringSchemeCollection& schemes);
const MeteringSchemeRecord* activeMeteringScheme(const MeteringSchemeCollection& schemes);

bool saveCandidateAsNewMeteringScheme(MeteringSchemeCollection& schemes,
                                      MeteringSchemeCandidate& candidate,
                                      const char* name,
                                      std::uint32_t nowSeconds,
                                      std::uint32_t& newSchemeId);
bool createManualMeteringScheme(MeteringSchemeCollection& schemes,
                                const char* name,
                                const MeteringParameters& params,
                                std::uint32_t nowSeconds,
                                std::uint32_t& newSchemeId);

MeteringSchemeEdit makeMeteringSchemeEdit(const MeteringSchemeRecord& scheme);
MeteringSchemeEditKind classifyMeteringSchemeEdit(const MeteringSchemeRecord& scheme,
                                                  const MeteringSchemeEdit& edit);
bool updateMeteringSchemeRecord(MeteringSchemeRecord& scheme,
                                const MeteringSchemeEdit& edit,
                                std::uint32_t nowSeconds);

bool canDisableMeteringScheme(const MeteringSchemeRecord& scheme,
                              std::uint32_t activeSchemeId,
                              std::size_t enabledSchemeCount);
bool canPhysicallyDeleteMeteringScheme(const MeteringSchemeRecord& scheme, std::uint32_t activeSchemeId);

}  // namespace faucet
