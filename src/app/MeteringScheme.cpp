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

bool textEquals(const char* left, const char* right) {
    return std::strcmp(left ? left : "", right ? right : "") == 0;
}

MeteringSchemeRecord* findFreeSchemeSlot(MeteringSchemeCollection& schemes) {
    if (!schemes.records) {
        return nullptr;
    }
    for (std::size_t i = 0; i < schemes.capacity; ++i) {
        if (!schemes.records[i].valid) {
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
    scheme.valid = true;
    scheme.enabled = true;
    copyText(scheme.name, name && name[0] ? name : "未命名计量方案");
    scheme.params = params;
    scheme.sourceType = source;
    scheme.revision = 1;
    scheme.createdAt = nowSeconds;
    scheme.updatedAt = nowSeconds;
}

void copyCandidateMetadata(MeteringSchemeRecord& scheme, const MeteringSchemeCandidate& candidate) {
    scheme.sampleCount = candidate.sampleCount;
    const std::size_t traceCount =
        std::min<std::size_t>(candidate.sampleCount, kMeteringSchemeTraceIdCapacity);
    for (std::size_t i = 0; i < traceCount; ++i) {
        scheme.sampleTraceIds[i] = candidate.sampleTraceIds[i];
    }
    scheme.minActualMl = candidate.minActualMl;
    scheme.maxActualMl = candidate.maxActualMl;
    scheme.maxErrorMl = candidate.maxErrorMl;
    scheme.maxErrorPercent = candidate.maxErrorPercent;
    scheme.startupDurationMinSec = candidate.startupDurationMinSec;
    scheme.startupDurationMaxSec = candidate.startupDurationMaxSec;
    scheme.startupDurationMedianSec = candidate.startupDurationMedianSec;
    scheme.startupDurationAvgSec = candidate.startupDurationAvgSec;
    copyText(scheme.creationSummary, candidate.creationSummary);
}

void writeManualSummary(MeteringSchemeRecord& scheme) {
    std::snprintf(scheme.creationSummary,
                  sizeof(scheme.creationSummary),
                  "手工创建：启动脉冲数 %lu，启动水量 %luml，稳态 %lu P/L，启动时长 %lums，预计稳态流速 %luml/min。",
                  static_cast<unsigned long>(scheme.params.startupPulseCount),
                  static_cast<unsigned long>(scheme.params.startupVolumeMl),
                  static_cast<unsigned long>(scheme.params.stablePulsePerLiter),
                  static_cast<unsigned long>(scheme.params.startupDurationMs),
                  static_cast<unsigned long>(scheme.params.stableFlowMlPerMin));
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
    copyText(schemes.records[0].meterLabel, kDefaultMeteringSchemeMeterLabel);
    copyText(schemes.records[0].conditionLabel, "系统内置默认参数");
    std::snprintf(schemes.records[0].creationSummary,
                  sizeof(schemes.records[0].creationSummary),
                  "系统内置 YF-S201 默认计量方案：启动约 5 秒，启动阶段约 8P，启动水量按 225P/L 折算为 36ml，稳态 225P/L。");
    schemes.records[0].lastActivatedAt = nowSeconds;
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
    writeManualSummary(scheme);
}

std::size_t meteringSchemeCount(const MeteringSchemeCollection& schemes, bool includeDisabled) {
    if (!schemes.records) {
        return 0;
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < schemes.capacity; ++i) {
        const MeteringSchemeRecord& scheme = schemes.records[i];
        if (scheme.valid && (includeDisabled || scheme.enabled)) {
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
        if (schemes.records[i].valid && schemes.records[i].id == id) {
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
        if (schemes.records[i].valid && schemes.records[i].id == id) {
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
    initializeCommonScheme(*slot, newSchemeId, name, candidate.params, MeteringSchemeSource::Generated, nowSeconds);
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

MeteringSchemeEdit makeMeteringSchemeEdit(const MeteringSchemeRecord& scheme) {
    MeteringSchemeEdit edit{};
    copyText(edit.name, scheme.name);
    copyText(edit.meterLabel, scheme.meterLabel);
    copyText(edit.installationLabel, scheme.installationLabel);
    copyText(edit.conditionLabel, scheme.conditionLabel);
    copyText(edit.userNote, scheme.userNote);
    edit.params = scheme.params;
    return edit;
}

MeteringSchemeEditKind classifyMeteringSchemeEdit(const MeteringSchemeRecord& scheme,
                                                  const MeteringSchemeEdit& edit) {
    const bool nameChanged = !textEquals(scheme.name, edit.name);
    const bool paramsChanged =
        scheme.params.startupPulseCount != edit.params.startupPulseCount ||
        scheme.params.startupVolumeMl != edit.params.startupVolumeMl ||
        scheme.params.stablePulsePerLiter != edit.params.stablePulsePerLiter ||
        scheme.params.startupDurationMs != edit.params.startupDurationMs ||
        scheme.params.stableFlowMlPerMin != edit.params.stableFlowMlPerMin;
    const bool applicabilityChanged = !textEquals(scheme.meterLabel, edit.meterLabel) ||
                                      !textEquals(scheme.installationLabel, edit.installationLabel) ||
                                      !textEquals(scheme.conditionLabel, edit.conditionLabel) ||
                                      !textEquals(scheme.userNote, edit.userNote);
    if (paramsChanged || applicabilityChanged) {
        return MeteringSchemeEditKind::MeteringOrApplicability;
    }
    if (nameChanged) {
        return MeteringSchemeEditKind::NameOnly;
    }
    return MeteringSchemeEditKind::NoChange;
}

bool updateMeteringSchemeRecord(MeteringSchemeRecord& scheme,
                                const MeteringSchemeEdit& edit,
                                std::uint32_t nowSeconds) {
    if (!scheme.valid || !validMeteringSchemeParameters(edit.params)) {
        return false;
    }
    const MeteringSchemeEditKind kind = classifyMeteringSchemeEdit(scheme, edit);
    copyText(scheme.name, edit.name);
    copyText(scheme.meterLabel, edit.meterLabel);
    copyText(scheme.installationLabel, edit.installationLabel);
    copyText(scheme.conditionLabel, edit.conditionLabel);
    copyText(scheme.userNote, edit.userNote);
    scheme.params = edit.params;
    scheme.updatedAt = nowSeconds;
    if (kind == MeteringSchemeEditKind::MeteringOrApplicability) {
        ++scheme.revision;
        std::snprintf(scheme.lastModifiedSummary,
                      sizeof(scheme.lastModifiedSummary),
                      "修改计量参数或适用条件，revision=%lu。",
                      static_cast<unsigned long>(scheme.revision));
    } else if (kind == MeteringSchemeEditKind::NameOnly) {
        std::snprintf(scheme.lastModifiedSummary,
                      sizeof(scheme.lastModifiedSummary),
                      "修改方案名称，revision 不变。");
    }
    return true;
}

bool canDisableMeteringScheme(const MeteringSchemeRecord& scheme,
                              std::uint32_t activeSchemeId,
                              std::size_t enabledSchemeCount) {
    return scheme.valid && scheme.enabled && scheme.id != activeSchemeId && enabledSchemeCount > 1;
}

bool canPhysicallyDeleteMeteringScheme(const MeteringSchemeRecord& scheme,
                                       std::uint32_t activeSchemeId,
                                       std::size_t validSchemeCount) {
    return scheme.valid && validSchemeCount > 1 && scheme.id != activeSchemeId && scheme.useCount == 0 &&
           !scheme.usageStatsDirty;
}

}  // namespace faucet
