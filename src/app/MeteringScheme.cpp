#include "app/MeteringScheme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

template <std::size_t N>
void copyText(char (&dest)[N], const char* src) {
    if (N == 0) {
        return;
    }
    dest[0] = '\0';
    if (!src) {
        return;
    }
    std::strncpy(dest, src, N - 1);
    dest[N - 1] = '\0';
}

MeteringSchemeRecord* findFreeSchemeSlot(MeteringSchemeCollection& schemes) {
    if (!schemes.records) {
        return nullptr;
    }
    for (std::size_t i = 0; i < schemes.capacity; ++i) {
        if (!schemes.records[i].recordUsed) {
            return &schemes.records[i];
        }
    }
    return nullptr;
}

void initializeCommonScheme(MeteringSchemeRecord& scheme,
                            std::uint32_t id,
                            const char* name,
                            const MeteringParameters& params,
                            MeteringSchemeSource source,
                            std::uint32_t nowSeconds) {
    scheme = MeteringSchemeRecord{};
    scheme.id = id;
    scheme.recordUsed = true;
    copyText(scheme.name, name && name[0] ? name : "未命名计量方案");
    scheme.params = params;
    scheme.sourceType = source;
    scheme.createdAt = nowSeconds;
}

void copyCandidateMetadata(MeteringSchemeRecord& scheme, const MeteringSchemeCandidate& candidate) {
    scheme.sourceType = candidate.sourceType;
    scheme.sampleCount = candidate.sampleCount;
    scheme.minActualMl = candidate.minActualMl;
    scheme.maxActualMl = candidate.maxActualMl;
    scheme.maxErrorMl = candidate.maxErrorMl;
    scheme.maxErrorTenthPercent = candidate.maxErrorTenthPercent;
}

std::uint32_t saturatingU32(std::uint64_t value) {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(value);
}

}  // namespace

MeteringParameters defaultMeteringParameters() {
    return MeteringParameters{
        kDefaultStartupPulseCount,
        kDefaultStartupVolumeMl,
        kDefaultStablePulsePerLiter,
        kDefaultStartupDurationMs,
        kDefaultStableFlowMlPerMin,
    };
}

bool validMeteringSchemeParameters(const MeteringParameters& params) {
    if (params.stablePulsePerLiter < kMinSegmentedPulsePerLiter ||
        params.stablePulsePerLiter > kMaxSegmentedPulsePerLiter ||
        params.startupPulseCount > kMaxSegmentedStartupPulseCount ||
        params.startupVolumeMl > kMaxSegmentedStartupVolumeMl ||
        params.startupDurationMs > kMaxSegmentedStartupDurationMs ||
        params.stableFlowMlPerMin < kMinStableFlowMlPerMin ||
        params.stableFlowMlPerMin > kMaxStableFlowMlPerMin) {
        return false;
    }
    return (params.startupPulseCount == 0 && params.startupVolumeMl == 0) ||
           (params.startupPulseCount > 0 && params.startupVolumeMl > 0);
}

std::uint32_t estimatePulsesForVolumeMl(const MeteringParameters& params, std::uint32_t targetMl) {
    if (!validMeteringSchemeParameters(params) || targetMl == 0 || params.stablePulsePerLiter == 0) {
        return 0;
    }
    if (params.startupPulseCount > 0 && params.startupVolumeMl > 0 && targetMl <= params.startupVolumeMl) {
        const std::uint64_t pulses =
            (static_cast<std::uint64_t>(targetMl) * params.startupPulseCount + params.startupVolumeMl - 1ULL) /
            params.startupVolumeMl;
        return pulses > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(pulses);
    }
    const std::uint32_t stableTargetMl =
        targetMl > params.startupVolumeMl ? targetMl - params.startupVolumeMl : 0;
    const std::uint64_t stablePulses =
        (static_cast<std::uint64_t>(stableTargetMl) * params.stablePulsePerLiter + 999ULL) / 1000ULL;
    const std::uint64_t totalPulses = static_cast<std::uint64_t>(params.startupPulseCount) + stablePulses;
    return totalPulses > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(totalPulses);
}

std::uint32_t estimateDurationMsForVolumeMl(const MeteringParameters& params, std::uint32_t targetMl) {
    if (!validMeteringSchemeParameters(params) || targetMl == 0 || params.stableFlowMlPerMin == 0) {
        return 0;
    }
    if (params.startupDurationMs == 0 || params.startupVolumeMl == 0) {
        return saturatingU32((static_cast<std::uint64_t>(targetMl) * 60000ULL +
                              params.stableFlowMlPerMin / 2ULL) /
                             params.stableFlowMlPerMin);
    }
    if (targetMl <= params.startupVolumeMl) {
        return saturatingU32((static_cast<std::uint64_t>(targetMl) * params.startupDurationMs +
                              params.startupVolumeMl / 2ULL) /
                             params.startupVolumeMl);
    }
    const std::uint64_t stableMl = targetMl - params.startupVolumeMl;
    const std::uint64_t stableMs =
        (stableMl * 60000ULL + params.stableFlowMlPerMin / 2ULL) / params.stableFlowMlPerMin;
    return saturatingU32(static_cast<std::uint64_t>(params.startupDurationMs) + stableMs);
}

std::uint32_t estimateVolumeMlForDurationMs(const MeteringParameters& params, std::uint32_t durationMs) {
    if (!validMeteringSchemeParameters(params) || durationMs == 0 || params.stableFlowMlPerMin == 0) {
        return 0;
    }
    if (params.startupDurationMs == 0 || params.startupVolumeMl == 0) {
        return saturatingU32((static_cast<std::uint64_t>(durationMs) * params.stableFlowMlPerMin + 30000ULL) /
                             60000ULL);
    }
    if (durationMs <= params.startupDurationMs) {
        return saturatingU32((static_cast<std::uint64_t>(durationMs) * params.startupVolumeMl +
                              params.startupDurationMs / 2ULL) /
                             params.startupDurationMs);
    }
    const std::uint64_t stableMs = durationMs - params.startupDurationMs;
    const std::uint64_t stableMl = (stableMs * params.stableFlowMlPerMin + 30000ULL) / 60000ULL;
    return saturatingU32(static_cast<std::uint64_t>(params.startupVolumeMl) + stableMl);
}

std::uint32_t fullRunPulsePerLiter(std::uint32_t pulseCount, std::uint32_t volumeMl) {
    if (pulseCount == 0 || volumeMl == 0) {
        return 0;
    }
    const std::uint64_t value =
        (static_cast<std::uint64_t>(pulseCount) * 1000ULL + volumeMl / 2ULL) / volumeMl;
    return value > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(value);
}

MeteringTargetEstimate meteringEstimateForTarget(const MeteringParameters& params, std::uint32_t targetMl) {
    MeteringTargetEstimate estimate{};
    estimate.targetMl = targetMl;
    estimate.pulseCount = estimatePulsesForVolumeMl(params, targetMl);
    estimate.fullRunPulsePerLiter = fullRunPulsePerLiter(estimate.pulseCount, targetMl);
    estimate.valid = estimate.pulseCount > 0 && estimate.fullRunPulsePerLiter > 0;
    return estimate;
}

bool initializeDefaultMeteringSchemes(MeteringSchemeCollection& schemes, std::uint32_t nowSeconds) {
    if (!schemes.records || schemes.capacity == 0) {
        return false;
    }
    for (std::size_t i = 0; i < schemes.capacity; ++i) {
        schemes.records[i] = MeteringSchemeRecord{};
    }
    initializeCommonScheme(schemes.records[0],
                           1,
                           kDefaultMeteringSchemeName,
                           defaultMeteringParameters(),
                           MeteringSchemeSource::Default,
                           nowSeconds);
    schemes.activeSchemeId = 1;
    schemes.nextSchemeId = 2;
    return true;
}

void initializeManualMeteringScheme(MeteringSchemeRecord& scheme,
                                    std::uint32_t id,
                                    const char* name,
                                    const MeteringParameters& params,
    std::uint32_t nowSeconds) {
    initializeCommonScheme(scheme, id, name, params, MeteringSchemeSource::Manual, nowSeconds);
}

std::size_t meteringSchemeCount(const MeteringSchemeCollection& schemes, bool includeDeleted) {
    if (!schemes.records) {
        return 0;
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < schemes.capacity; ++i) {
        const MeteringSchemeRecord& scheme = schemes.records[i];
        if (scheme.recordUsed) {
            ++count;
        }
    }
    return count;
}

MeteringSchemeRecord* findMeteringSchemeById(MeteringSchemeCollection& schemes, std::uint32_t id) {
    if (!schemes.records || id == 0) {
        return nullptr;
    }
    for (std::size_t i = 0; i < schemes.capacity; ++i) {
        if (schemes.records[i].recordUsed && schemes.records[i].id == id) {
            return &schemes.records[i];
        }
    }
    return nullptr;
}

const MeteringSchemeRecord* findMeteringSchemeById(const MeteringSchemeCollection& schemes, std::uint32_t id) {
    if (!schemes.records || id == 0) {
        return nullptr;
    }
    for (std::size_t i = 0; i < schemes.capacity; ++i) {
        if (schemes.records[i].recordUsed && schemes.records[i].id == id) {
            return &schemes.records[i];
        }
    }
    return nullptr;
}

MeteringSchemeRecord* activeMeteringScheme(MeteringSchemeCollection& schemes) {
    return findMeteringSchemeById(schemes, schemes.activeSchemeId);
}

const MeteringSchemeRecord* activeMeteringScheme(const MeteringSchemeCollection& schemes) {
    return findMeteringSchemeById(schemes, schemes.activeSchemeId);
}

bool saveCandidateAsNewMeteringScheme(MeteringSchemeCollection& schemes,
                                      MeteringSchemeCandidate& candidate,
                                      const char* name,
                                      std::uint32_t nowSeconds,
                                      std::uint32_t& newSchemeId) {
    newSchemeId = 0;
    if (!candidate.ready || !validMeteringSchemeParameters(candidate.params) || schemes.nextSchemeId == 0) {
        return false;
    }
    MeteringSchemeRecord* slot = findFreeSchemeSlot(schemes);
    if (!slot) {
        return false;
    }
    newSchemeId = schemes.nextSchemeId++;
    initializeCommonScheme(*slot, newSchemeId, name, candidate.params, candidate.sourceType, nowSeconds);
    copyCandidateMetadata(*slot, candidate);
    candidate = MeteringSchemeCandidate{};
    return true;
}

bool createManualMeteringScheme(MeteringSchemeCollection& schemes,
                                const char* name,
                                const MeteringParameters& params,
                                std::uint32_t nowSeconds,
                                std::uint32_t& newSchemeId) {
    newSchemeId = 0;
    if (!validMeteringSchemeParameters(params) || schemes.nextSchemeId == 0) {
        return false;
    }
    MeteringSchemeRecord* slot = findFreeSchemeSlot(schemes);
    if (!slot) {
        return false;
    }
    newSchemeId = schemes.nextSchemeId++;
    initializeManualMeteringScheme(*slot, newSchemeId, name, params, nowSeconds);
    return true;
}

}  // namespace faucet
