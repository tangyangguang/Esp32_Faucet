#include "app/MeteringSchemeStore.h"

#include "app/ConfigStore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace faucet {
namespace {

constexpr std::uint32_t kMeteringSchemeStoreMagic = 0x314D5346UL;  // FSM1
constexpr std::uint16_t kMeteringSchemeStoreVersion = 3;
constexpr std::uint16_t kLegacyMeteringSchemeStoreVersion = 1;
constexpr std::size_t kMigrationCopyChunkSize = 256;

struct LegacyMeteringParametersV1 {
    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t stablePulsePerLiter;
};

struct LegacyMeteringSchemeRecordV1 {
    std::uint32_t id = 0;
    bool valid = false;
    bool enabled = false;
    char name[kMeteringSchemeNameLength]{};
    char meterLabel[kMeteringSchemeMeterLabelLength]{};
    char installationLabel[kMeteringSchemeInstallationLabelLength]{};
    char conditionLabel[kMeteringSchemeConditionLabelLength]{};
    char userNote[kMeteringSchemeUserNoteLength]{};
    LegacyMeteringParametersV1 params{};
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

struct LegacyMeteringSchemeCandidateV1 {
    bool ready = false;
    LegacyMeteringParametersV1 params{};
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

std::uint32_t headerChecksum(MeteringSchemeStoreHeader header) {
    header.checksum = 0;
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&header);
    std::uint32_t sum = 2166136261UL;
    for (std::size_t i = 0; i < sizeof(MeteringSchemeStoreHeader); ++i) {
        sum ^= bytes[i];
        sum *= 16777619UL;
    }
    return sum;
}

MeteringSchemeStoreHeader makeHeader(std::uint32_t activeSchemeId,
                                     std::uint32_t nextSchemeId,
                                     std::uint32_t slotCount) {
    MeteringSchemeStoreHeader header{
        kMeteringSchemeStoreMagic,
        kMeteringSchemeStoreVersion,
        static_cast<std::uint16_t>(sizeof(MeteringSchemeStoreHeader)),
        static_cast<std::uint16_t>(sizeof(MeteringSchemeRecord)),
        static_cast<std::uint16_t>(sizeof(MeteringSchemeCandidate)),
        activeSchemeId,
        nextSchemeId,
        slotCount,
        0,
        0,
    };
    header.checksum = headerChecksum(header);
    return header;
}

void meteringSlotKey(char* out, std::size_t len, std::size_t index, const char* suffix) {
    std::snprintf(out, len, "ms%u_%s", static_cast<unsigned>(index), suffix);
}

bool tempPathFor(const char* path, char* out, std::size_t len) {
    if (!path || !out || len == 0) {
        return false;
    }
    const int written = std::snprintf(out, len, "%s.tmp", path);
    return written > 0 && static_cast<std::size_t>(written) < len;
}

bool copyFileBytes(WaterRecordFileBackend& backend, const char* from, const char* to, std::size_t size) {
    if (!from || !to || !backend.createSized(to, size)) {
        return false;
    }
    std::uint8_t buffer[kMigrationCopyChunkSize]{};
    for (std::size_t offset = 0; offset < size; offset += sizeof(buffer)) {
        const std::size_t chunk = std::min<std::size_t>(sizeof(buffer), size - offset);
        if (!backend.readAt(from, offset, buffer, chunk) || !backend.writeAt(to, offset, buffer, chunk)) {
            return false;
        }
    }
    return true;
}

std::size_t expectedFileSizeForHeader(const MeteringSchemeStoreHeader& header) {
    return sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate) +
           static_cast<std::size_t>(header.slotCount) * sizeof(MeteringSchemeRecord);
}

bool validV2HeaderForFile(const MeteringSchemeStoreHeader& header, std::int64_t fileSize) {
    if (header.magic != kMeteringSchemeStoreMagic ||
        header.version != kMeteringSchemeStoreVersion ||
        header.headerSize != sizeof(MeteringSchemeStoreHeader) ||
        header.recordSize != sizeof(MeteringSchemeRecord) ||
        header.candidateSize != sizeof(MeteringSchemeCandidate) ||
        header.nextSchemeId == 0 ||
        header.activeSchemeId == 0 ||
        header.checksum != headerChecksum(header)) {
        return false;
    }
    return fileSize >= static_cast<std::int64_t>(expectedFileSizeForHeader(header));
}

template <std::size_t N>
void readConfigText(ConfigBackend& config, const char* key, char (&out)[N], const char* def = "") {
    config.getStr("faucet_cfg", key, out, N, def);
}

bool builtInDefaultParams(const MeteringParameters& params) {
    return params.startupPulseCount == kDefaultStartupPulseCount && params.startupVolumeMl == kDefaultStartupVolumeMl &&
           params.stablePulsePerLiter == kDefaultStablePulsePerLiter;
}

bool legacyEmptyDefaultParams(const MeteringParameters& params) {
    return params.startupPulseCount == 0 && params.startupVolumeMl == 0 &&
           (params.stablePulsePerLiter == kDefaultStablePulsePerLiter || params.stablePulsePerLiter == 450);
}

bool legacyDefaultText(const MeteringSchemeRecord& scheme) {
    return (scheme.name[0] == '\0' || std::strcmp(scheme.name, "默认计量方案") == 0 ||
            std::strcmp(scheme.name, kDefaultMeteringSchemeName) == 0) &&
           (scheme.meterLabel[0] == '\0' || std::strcmp(scheme.meterLabel, kDefaultMeteringSchemeMeterLabel) == 0);
}

MeteringParameters expandLegacyParams(LegacyMeteringParametersV1 params) {
    return MeteringParameters{
        params.startupPulseCount,
        params.startupVolumeMl,
        params.stablePulsePerLiter,
        kDefaultStartupDurationMs,
        kDefaultStableFlowMlPerMin,
    };
}

MeteringSchemeRecord expandLegacyRecord(const LegacyMeteringSchemeRecordV1& legacy) {
    MeteringSchemeRecord record{};
    record.id = legacy.id;
    record.valid = legacy.valid;
    record.enabled = legacy.enabled;
    std::memcpy(record.name, legacy.name, sizeof(record.name));
    std::memcpy(record.meterLabel, legacy.meterLabel, sizeof(record.meterLabel));
    std::memcpy(record.installationLabel, legacy.installationLabel, sizeof(record.installationLabel));
    std::memcpy(record.conditionLabel, legacy.conditionLabel, sizeof(record.conditionLabel));
    std::memcpy(record.userNote, legacy.userNote, sizeof(record.userNote));
    record.params = expandLegacyParams(legacy.params);
    record.sourceType = legacy.sourceType;
    record.revision = legacy.revision;
    record.createdAt = legacy.createdAt;
    record.updatedAt = legacy.updatedAt;
    record.lastActivatedAt = legacy.lastActivatedAt;
    record.useCount = legacy.useCount;
    record.lastUsedAt = legacy.lastUsedAt;
    record.usageStatsDirty = legacy.usageStatsDirty;
    record.sampleCount = legacy.sampleCount;
    std::memcpy(record.sampleTraceIds, legacy.sampleTraceIds, sizeof(record.sampleTraceIds));
    record.minActualMl = legacy.minActualMl;
    record.maxActualMl = legacy.maxActualMl;
    record.maxErrorMl = legacy.maxErrorMl;
    record.maxErrorPercent = legacy.maxErrorPercent;
    record.startupDurationMinSec = legacy.startupDurationMinSec;
    record.startupDurationMaxSec = legacy.startupDurationMaxSec;
    record.startupDurationMedianSec = legacy.startupDurationMedianSec;
    record.startupDurationAvgSec = legacy.startupDurationAvgSec;
    std::memcpy(record.creationSummary, legacy.creationSummary, sizeof(record.creationSummary));
    std::memcpy(record.lastModifiedSummary, legacy.lastModifiedSummary, sizeof(record.lastModifiedSummary));
    return record;
}

MeteringSchemeCandidate expandLegacyCandidate(const LegacyMeteringSchemeCandidateV1& legacy) {
    MeteringSchemeCandidate candidate{};
    candidate.ready = legacy.ready;
    candidate.params = expandLegacyParams(legacy.params);
    candidate.generatedAt = legacy.generatedAt;
    candidate.sampleCount = legacy.sampleCount;
    std::memcpy(candidate.sampleTraceIds, legacy.sampleTraceIds, sizeof(candidate.sampleTraceIds));
    candidate.minActualMl = legacy.minActualMl;
    candidate.maxActualMl = legacy.maxActualMl;
    candidate.maxErrorMl = legacy.maxErrorMl;
    candidate.maxErrorPercent = legacy.maxErrorPercent;
    candidate.startupDurationMinSec = legacy.startupDurationMinSec;
    candidate.startupDurationMaxSec = legacy.startupDurationMaxSec;
    candidate.startupDurationMedianSec = legacy.startupDurationMedianSec;
    candidate.startupDurationAvgSec = legacy.startupDurationAvgSec;
    std::memcpy(candidate.creationSummary, legacy.creationSummary, sizeof(candidate.creationSummary));
    if (candidate.ready && !validMeteringSchemeParameters(candidate.params)) {
        candidate = MeteringSchemeCandidate{};
    }
    return candidate;
}

bool writeV2SchemeFile(WaterRecordFileBackend& backend,
                       const char* path,
                       const MeteringSchemeStoreHeader& header,
                       const MeteringSchemeCandidate& candidate,
                       const MeteringSchemeRecord* records) {
    if (!path || !records || !backend.createSized(path, expectedFileSizeForHeader(header))) {
        return false;
    }
    if (!backend.writeAt(path, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)) ||
        !backend.writeAt(path,
                         sizeof(MeteringSchemeStoreHeader),
                         reinterpret_cast<const std::uint8_t*>(&candidate),
                         sizeof(candidate))) {
        return false;
    }
    const std::size_t recordBase = sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate);
    for (std::size_t i = 0; i < header.slotCount; ++i) {
        if (!backend.writeAt(path,
                             recordBase + i * sizeof(MeteringSchemeRecord),
                             reinterpret_cast<const std::uint8_t*>(&records[i]),
                             sizeof(MeteringSchemeRecord))) {
            return false;
        }
    }
    return true;
}

}  // namespace

MeteringSchemeStore::MeteringSchemeStore(WaterRecordFileBackend& backend, const char* path)
    : backend_(backend), path_(path), header_(makeHeader(0, 0, 0)), ready_(false) {}

bool MeteringSchemeStore::begin() {
    ready_ = false;
    if (!validPath()) {
        return false;
    }
    if (!backend_.exists(path_)) {
        return initializeNewFile();
    }
    if (!loadHeader()) {
        return false;
    }
    ready_ = true;
    if (!upgradeLegacyDefaultSchemeIfNeeded()) {
        ready_ = false;
        return false;
    }
    MeteringSchemeRecord active{};
    if (!activeScheme(active) || !active.enabled) {
        ready_ = false;
        return false;
    }
    return true;
}

bool MeteringSchemeStore::ready() const {
    return ready_ && backend_.exists(path_);
}

std::uint32_t MeteringSchemeStore::activeSchemeId() const {
    return ready() ? header_.activeSchemeId : 0;
}

bool MeteringSchemeStore::activeScheme(MeteringSchemeRecord& output) const {
    return findById(header_.activeSchemeId, output);
}

bool MeteringSchemeStore::findById(std::uint32_t id, MeteringSchemeRecord& output) const {
    std::size_t slot = 0;
    return findSlotById(id, output, slot);
}

std::size_t MeteringSchemeStore::list(MeteringSchemeRecord* output,
                                      std::size_t outputCapacity,
                                      bool includeDisabled) const {
    if (!ready() || !output || outputCapacity == 0) {
        return 0;
    }
    std::size_t copied = 0;
    for (std::size_t slot = 0; slot < header_.slotCount && copied < outputCapacity; ++slot) {
        MeteringSchemeRecord record{};
        if (!readRecord(slot, record)) {
            return copied;
        }
        if (record.valid && (includeDisabled || record.enabled)) {
            output[copied++] = record;
        }
    }
    return copied;
}

bool MeteringSchemeStore::loadCandidate(MeteringSchemeCandidate& output) const {
    if (!ready()) {
        return false;
    }
    return backend_.readAt(path_,
                           candidateOffset(),
                           reinterpret_cast<std::uint8_t*>(&output),
                           sizeof(output));
}

bool MeteringSchemeStore::saveCandidate(const MeteringSchemeCandidate& candidate) {
    if (!ready()) {
        return false;
    }
    return backend_.writeAt(path_,
                            candidateOffset(),
                            reinterpret_cast<const std::uint8_t*>(&candidate),
                            sizeof(candidate));
}

bool MeteringSchemeStore::discardCandidate() {
    MeteringSchemeCandidate empty{};
    return saveCandidate(empty);
}

bool MeteringSchemeStore::saveCandidateAsNew(const char* name,
                                             std::uint32_t nowSeconds,
                                             std::uint32_t& newId) {
    newId = 0;
    if (!ready()) {
        return false;
    }
    MeteringSchemeCandidate candidate{};
    if (!loadCandidate(candidate)) {
        return false;
    }
    MeteringSchemeRecord records[1]{};
    MeteringSchemeCollection collection{records, 1, header_.activeSchemeId, header_.nextSchemeId};
    if (!saveCandidateAsNewMeteringScheme(collection, candidate, name, nowSeconds, newId)) {
        return false;
    }

    std::size_t slot = 0;
    const bool reuseSlot = findFreeSlot(slot);
    if (!reuseSlot) {
        slot = header_.slotCount;
    }
    if (!writeRecord(slot, records[0])) {
        return false;
    }
    MeteringSchemeStoreHeader previous = header_;
    header_.nextSchemeId = collection.nextSchemeId;
    if (!reuseSlot) {
        ++header_.slotCount;
    }
    header_.checksum = headerChecksum(header_);
    if (!saveHeader()) {
        header_ = previous;
        return false;
    }
    return saveCandidate(candidate);
}

bool MeteringSchemeStore::createManual(const char* name,
                                       const MeteringParameters& params,
                                       std::uint32_t nowSeconds,
                                       std::uint32_t& newId) {
    newId = 0;
    if (!ready()) {
        return false;
    }
    MeteringSchemeRecord records[1]{};
    MeteringSchemeCollection collection{records, 1, header_.activeSchemeId, header_.nextSchemeId};
    if (!createManualMeteringScheme(collection, name, params, nowSeconds, newId)) {
        return false;
    }

    std::size_t slot = 0;
    const bool reuseSlot = findFreeSlot(slot);
    if (!reuseSlot) {
        slot = header_.slotCount;
    }
    if (!writeRecord(slot, records[0])) {
        return false;
    }
    MeteringSchemeStoreHeader previous = header_;
    header_.nextSchemeId = collection.nextSchemeId;
    if (!reuseSlot) {
        ++header_.slotCount;
    }
    header_.checksum = headerChecksum(header_);
    if (!saveHeader()) {
        header_ = previous;
        return false;
    }
    return true;
}

bool MeteringSchemeStore::updateScheme(const MeteringSchemeRecord& edited, std::uint32_t nowSeconds) {
    if (!ready() || edited.id == 0) {
        return false;
    }
    MeteringSchemeRecord current{};
    std::size_t slot = 0;
    if (!findSlotById(edited.id, current, slot)) {
        return false;
    }
    MeteringSchemeEdit edit = makeMeteringSchemeEdit(edited);
    if (!updateMeteringSchemeRecord(current, edit, nowSeconds)) {
        return false;
    }
    return writeRecord(slot, current);
}

bool MeteringSchemeStore::enableScheme(std::uint32_t schemeId, std::uint32_t nowSeconds) {
    if (!ready()) {
        return false;
    }
    MeteringSchemeRecord record{};
    std::size_t slot = 0;
    if (!findSlotById(schemeId, record, slot) || !record.valid) {
        return false;
    }
    record.enabled = true;
    record.updatedAt = nowSeconds;
    record.lastActivatedAt = nowSeconds;
    MeteringSchemeStoreHeader previous = header_;
    header_.activeSchemeId = schemeId;
    header_.checksum = headerChecksum(header_);
    if (!writeRecord(slot, record)) {
        header_ = previous;
        return false;
    }
    if (!saveHeader()) {
        header_ = previous;
        return false;
    }
    return true;
}

bool MeteringSchemeStore::disableScheme(std::uint32_t schemeId, std::uint32_t nowSeconds) {
    if (!ready()) {
        return false;
    }
    MeteringSchemeRecord record{};
    std::size_t slot = 0;
    std::size_t enabledCount = 0;
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord listed{};
        if (!readRecord(i, listed)) {
            return false;
        }
        if (listed.valid && listed.enabled) {
            ++enabledCount;
        }
    }
    if (!findSlotById(schemeId, record, slot) ||
        !canDisableMeteringScheme(record, header_.activeSchemeId, enabledCount)) {
        return false;
    }
    record.enabled = false;
    record.updatedAt = nowSeconds;
    return writeRecord(slot, record);
}

bool MeteringSchemeStore::restoreScheme(std::uint32_t schemeId, std::uint32_t nowSeconds) {
    if (!ready()) {
        return false;
    }
    MeteringSchemeRecord record{};
    std::size_t slot = 0;
    if (!findSlotById(schemeId, record, slot) || record.enabled) {
        return false;
    }
    record.enabled = true;
    record.updatedAt = nowSeconds;
    return writeRecord(slot, record);
}

bool MeteringSchemeStore::deleteScheme(std::uint32_t schemeId) {
    if (!ready()) {
        return false;
    }
    std::size_t validCount = 0;
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord listed{};
        if (!readRecord(i, listed)) {
            return false;
        }
        if (listed.valid) {
            ++validCount;
        }
    }
    MeteringSchemeRecord record{};
    std::size_t slot = 0;
    if (!findSlotById(schemeId, record, slot) ||
        !canPhysicallyDeleteMeteringScheme(record, header_.activeSchemeId, validCount)) {
        return false;
    }
    record.valid = false;
    record.enabled = false;
    return writeRecord(slot, record);
}

bool MeteringSchemeStore::incrementUsageAfterRecordWrite(std::uint32_t schemeId, std::uint32_t nowSeconds) {
    if (!ready()) {
        return false;
    }
    MeteringSchemeRecord record{};
    std::size_t slot = 0;
    if (!findSlotById(schemeId, record, slot)) {
        return false;
    }
    record.useCount += 1;
    record.lastUsedAt = nowSeconds;
    if (writeRecord(slot, record)) {
        return true;
    }
    markUsageStatsDirty(schemeId);
    return false;
}

bool MeteringSchemeStore::markUsageStatsDirty(std::uint32_t schemeId) {
    if (!ready()) {
        return false;
    }
    MeteringSchemeRecord record{};
    std::size_t slot = 0;
    if (!findSlotById(schemeId, record, slot)) {
        return false;
    }
    record.usageStatsDirty = true;
    return writeRecord(slot, record);
}

bool MeteringSchemeStore::migrateLegacyFromConfig(ConfigBackend& config, std::uint32_t nowSeconds) {
    if (!ready()) {
        return false;
    }

    std::unique_ptr<MeteringSchemeRecord[]> existing(
        new (std::nothrow) MeteringSchemeRecord[2]{});
    std::unique_ptr<MeteringSchemeRecord[]> migrated(
        new (std::nothrow) MeteringSchemeRecord[kLegacyMeteringSlotCount]{});
    if (!existing || !migrated) {
        return false;
    }

    if (list(existing.get(), 2, true) != 1 || existing[0].sourceType != MeteringSchemeSource::Default ||
        existing[0].id != 1 || !builtInDefaultParams(existing[0].params)) {
        return true;
    }

    const std::uint8_t legacyActive =
        static_cast<std::uint8_t>(config.getInt("faucet_cfg", "active_ms", 0));
    std::size_t migratedCount = 0;

    auto appendLegacySlot = [&](std::size_t index) -> bool {
        if (index >= kLegacyMeteringSlotCount || migratedCount >= kLegacyMeteringSlotCount) {
            return false;
        }
        char key[16]{};
        meteringSlotKey(key, sizeof(key), index, "valid");
        if (!config.getBool("faucet_cfg", key, false)) {
            return true;
        }

        char name[kMeteringSchemeNameLength]{};
        meteringSlotKey(key, sizeof(key), index, "name");
        readConfigText(config, key, name);
        if (name[0] == '\0') {
            std::snprintf(name, sizeof(name), "迁移方案 %u", static_cast<unsigned>(index + 1));
        }

        meteringSlotKey(key, sizeof(key), index, "sp");
        const std::uint32_t startupPulse =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", key, 0));
        meteringSlotKey(key, sizeof(key), index, "sv");
        const std::uint32_t startupVolume =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", key, 0));
        meteringSlotKey(key, sizeof(key), index, "pl");
        const std::uint32_t stable =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", key, kDefaultStablePulsePerLiter));
        const MeteringParameters params{startupPulse, startupVolume, stable};
        if (!validMeteringSchemeParameters(params)) {
            return true;
        }
        const bool active = index == legacyActive;
        if (!active && (builtInDefaultParams(params) || legacyEmptyDefaultParams(params))) {
            return true;
        }

        meteringSlotKey(key, sizeof(key), index, "mod_at");
        const std::uint32_t modifiedAt =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", key, nowSeconds));

        MeteringSchemeRecord& scheme = migrated[migratedCount];
        initializeManualMeteringScheme(scheme,
                                       static_cast<std::uint32_t>(migratedCount + 1),
                                       name,
                                       params,
                                       modifiedAt == 0 ? nowSeconds : modifiedAt);
        scheme.sourceType = MeteringSchemeSource::Migrated;
        scheme.lastActivatedAt = active ? nowSeconds : 0;
        meteringSlotKey(key, sizeof(key), index, "create");
        readConfigText(config, key, scheme.creationSummary);
        if (scheme.creationSummary[0] == '\0') {
            std::snprintf(scheme.creationSummary,
                          sizeof(scheme.creationSummary),
                          "由旧参数槽 %u 迁移。",
                          static_cast<unsigned>(index + 1));
        }
        ++migratedCount;
        return true;
    };

    if (legacyActive < kLegacyMeteringSlotCount && !appendLegacySlot(legacyActive)) {
        return false;
    }
    for (std::size_t i = 0; i < kLegacyMeteringSlotCount; ++i) {
        if (i == legacyActive) {
            continue;
        }
        if (!appendLegacySlot(i)) {
            return false;
        }
    }
    if (migratedCount == 0) {
        return true;
    }

    MeteringSchemeCandidate candidate{};
    candidate.ready = config.getBool("faucet_cfg", "mc_ready", false);
    if (candidate.ready) {
        candidate.params.startupPulseCount =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", "mc_sp", 0));
        candidate.params.startupVolumeMl =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", "mc_sv", 0));
        candidate.params.stablePulsePerLiter =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", "mc_pl", kDefaultStablePulsePerLiter));
        candidate.generatedAt =
            static_cast<std::uint32_t>(config.getInt("faucet_cfg", "mc_at", 0));
        readConfigText(config, "mc_note", candidate.creationSummary);
        if (!validMeteringSchemeParameters(candidate.params)) {
            candidate = MeteringSchemeCandidate{};
        }
    }

    header_ = makeHeader(1, static_cast<std::uint32_t>(migratedCount + 1), static_cast<std::uint32_t>(migratedCount));
    const std::size_t size = expectedFileSize();
    if (!backend_.createSized(path_, size)) {
        return false;
    }
    if (!saveHeader()) {
        return false;
    }
    if (!backend_.writeAt(path_,
                          candidateOffset(),
                          reinterpret_cast<const std::uint8_t*>(&candidate),
                          sizeof(candidate))) {
        return false;
    }
    for (std::size_t i = 0; i < migratedCount; ++i) {
        if (!writeRecord(i, migrated[i])) {
            return false;
        }
    }
    return true;
}

bool MeteringSchemeStore::validPath() const {
    return path_ && path_[0] == '/';
}

bool MeteringSchemeStore::initializeNewFile() {
    MeteringSchemeRecord record{};
    MeteringSchemeCollection collection{&record, 1, 0, 0};
    if (!initializeDefaultMeteringSchemes(collection, 0)) {
        return false;
    }
    header_ = makeHeader(collection.activeSchemeId, collection.nextSchemeId, 1);
    const std::size_t size = expectedFileSize();
    if (!backend_.createSized(path_, size)) {
        return false;
    }
    MeteringSchemeCandidate candidate{};
    ready_ = backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&header_), sizeof(header_)) &&
             backend_.writeAt(path_,
                              candidateOffset(),
                              reinterpret_cast<const std::uint8_t*>(&candidate),
                              sizeof(candidate)) &&
             backend_.writeAt(path_,
                              recordOffset(0),
                              reinterpret_cast<const std::uint8_t*>(&record),
                              sizeof(record));
    return ready_;
}

bool MeteringSchemeStore::migrateV1File(const MeteringSchemeStoreHeader& loaded) {
    if (!validPath()) {
        return false;
    }
    std::unique_ptr<LegacyMeteringSchemeRecordV1[]> legacyRecords(
        new (std::nothrow) LegacyMeteringSchemeRecordV1[loaded.slotCount]{});
    std::unique_ptr<MeteringSchemeRecord[]> records(
        new (std::nothrow) MeteringSchemeRecord[loaded.slotCount]{});
    if (!legacyRecords || !records) {
        return false;
    }
    LegacyMeteringSchemeCandidateV1 legacyCandidate{};
    if (!backend_.readAt(path_,
                         sizeof(MeteringSchemeStoreHeader),
                         reinterpret_cast<std::uint8_t*>(&legacyCandidate),
                         sizeof(legacyCandidate))) {
        return false;
    }
    const std::size_t legacyRecordBase = sizeof(MeteringSchemeStoreHeader) + sizeof(LegacyMeteringSchemeCandidateV1);
    for (std::size_t i = 0; i < loaded.slotCount; ++i) {
        if (!backend_.readAt(path_,
                             legacyRecordBase + i * sizeof(LegacyMeteringSchemeRecordV1),
                             reinterpret_cast<std::uint8_t*>(&legacyRecords[i]),
                             sizeof(LegacyMeteringSchemeRecordV1))) {
            return false;
        }
        records[i] = expandLegacyRecord(legacyRecords[i]);
    }
    MeteringSchemeCandidate candidate = expandLegacyCandidate(legacyCandidate);
    header_ = makeHeader(loaded.activeSchemeId, loaded.nextSchemeId, loaded.slotCount);
    char tempPath[96]{};
    if (!tempPathFor(path_, tempPath, sizeof(tempPath))) {
        return false;
    }
    if (!writeV2SchemeFile(backend_, tempPath, header_, candidate, records.get())) {
        return false;
    }
    if (!copyFileBytes(backend_, tempPath, path_, expectedFileSize())) {
        return false;
    }
    backend_.removeFile(tempPath);
    return true;
}

bool MeteringSchemeStore::upgradeLegacyDefaultSchemeIfNeeded() {
    if (!ready()) {
        return false;
    }

    MeteringSchemeRecord legacy{};
    std::size_t legacySlot = 0;
    std::size_t validCount = 0;
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord record{};
        if (!readRecord(i, record)) {
            return false;
        }
        if (!record.valid) {
            continue;
        }
        ++validCount;
        legacy = record;
        legacySlot = i;
    }

    if (validCount != 1 || legacy.id != 1 || header_.activeSchemeId != 1 || !legacy.enabled ||
        legacy.sourceType != MeteringSchemeSource::Default || !legacyEmptyDefaultParams(legacy.params) ||
        !legacyDefaultText(legacy)) {
        return true;
    }

    MeteringSchemeRecord upgraded{};
    MeteringSchemeCollection collection{&upgraded, 1, 0, 0};
    if (!initializeDefaultMeteringSchemes(collection, legacy.createdAt)) {
        return false;
    }
    upgraded.createdAt = legacy.createdAt;
    upgraded.updatedAt = legacy.updatedAt;
    upgraded.lastActivatedAt = legacy.lastActivatedAt;
    return writeRecord(legacySlot, upgraded);
}

bool MeteringSchemeStore::loadHeader() {
    char tempPath[96]{};
    const bool hasTempPath = tempPathFor(path_, tempPath, sizeof(tempPath));
    auto recoverFromTemp = [&]() -> bool {
        if (!hasTempPath || !backend_.exists(tempPath)) {
            return false;
        }
        MeteringSchemeStoreHeader tempHeader{};
        const std::int64_t tempSize = backend_.fileSize(tempPath);
        if (tempSize >= static_cast<std::int64_t>(sizeof(tempHeader)) &&
            backend_.readAt(tempPath, 0, reinterpret_cast<std::uint8_t*>(&tempHeader), sizeof(tempHeader)) &&
            validV2HeaderForFile(tempHeader, tempSize) &&
            copyFileBytes(backend_, tempPath, path_, expectedFileSizeForHeader(tempHeader))) {
            backend_.removeFile(tempPath);
            header_ = tempHeader;
            return true;
        }
        return false;
    };
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(sizeof(MeteringSchemeStoreHeader))) {
        return recoverFromTemp();
    }
    MeteringSchemeStoreHeader loaded{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&loaded), sizeof(loaded))) {
        return recoverFromTemp();
    }
    if (loaded.magic != kMeteringSchemeStoreMagic ||
        loaded.headerSize != sizeof(MeteringSchemeStoreHeader) ||
        loaded.nextSchemeId == 0 ||
        loaded.activeSchemeId == 0 ||
        loaded.checksum != headerChecksum(loaded)) {
        return recoverFromTemp();
    }
    if (validV2HeaderForFile(loaded, fileSize)) {
        if (hasTempPath && backend_.exists(tempPath)) {
            backend_.removeFile(tempPath);
        }
        header_ = loaded;
        return true;
    }
    if (loaded.version == kLegacyMeteringSchemeStoreVersion &&
        loaded.recordSize == sizeof(LegacyMeteringSchemeRecordV1) &&
        loaded.candidateSize == sizeof(LegacyMeteringSchemeCandidateV1)) {
        if (recoverFromTemp()) {
            return true;
        }
        const std::size_t minimumV1Size =
            sizeof(MeteringSchemeStoreHeader) + sizeof(LegacyMeteringSchemeCandidateV1) +
            static_cast<std::size_t>(loaded.slotCount) * sizeof(LegacyMeteringSchemeRecordV1);
        if (fileSize < static_cast<std::int64_t>(minimumV1Size)) {
            return false;
        }
        return migrateV1File(loaded);
    }
    return recoverFromTemp();
}

bool MeteringSchemeStore::saveHeader() const {
    MeteringSchemeStoreHeader saved = header_;
    saved.checksum = headerChecksum(saved);
    return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&saved), sizeof(saved));
}

bool MeteringSchemeStore::readRecord(std::size_t slot, MeteringSchemeRecord& output) const {
    if (!ready() || slot >= header_.slotCount) {
        return false;
    }
    return backend_.readAt(path_,
                           recordOffset(slot),
                           reinterpret_cast<std::uint8_t*>(&output),
                           sizeof(output));
}

bool MeteringSchemeStore::writeRecord(std::size_t slot, const MeteringSchemeRecord& record) {
    if (!ready_ || slot > header_.slotCount) {
        return false;
    }
    return writeOrAppendAt(recordOffset(slot),
                           reinterpret_cast<const std::uint8_t*>(&record),
                           sizeof(record));
}

bool MeteringSchemeStore::findSlotById(std::uint32_t id,
                                       MeteringSchemeRecord& output,
                                       std::size_t& slot) const {
    if (!ready() || id == 0) {
        return false;
    }
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord record{};
        if (!readRecord(i, record)) {
            return false;
        }
        if (record.valid && record.id == id) {
            output = record;
            slot = i;
            return true;
        }
    }
    return false;
}

bool MeteringSchemeStore::findFreeSlot(std::size_t& slot) const {
    if (!ready()) {
        return false;
    }
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord record{};
        if (!readRecord(i, record)) {
            return false;
        }
        if (!record.valid) {
            slot = i;
            return true;
        }
    }
    return false;
}

bool MeteringSchemeStore::writeOrAppendAt(std::size_t offset, const std::uint8_t* data, std::size_t len) {
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize == static_cast<std::int64_t>(offset)) {
        return backend_.appendBytes(path_, data, len);
    }
    if (fileSize > static_cast<std::int64_t>(offset)) {
        return backend_.writeAt(path_, offset, data, len);
    }
    return false;
}

std::size_t MeteringSchemeStore::candidateOffset() const {
    return sizeof(MeteringSchemeStoreHeader);
}

std::size_t MeteringSchemeStore::recordOffset(std::size_t slot) const {
    return sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate) + slot * sizeof(MeteringSchemeRecord);
}

std::size_t MeteringSchemeStore::expectedFileSize() const {
    return sizeof(MeteringSchemeStoreHeader) + sizeof(MeteringSchemeCandidate) +
           static_cast<std::size_t>(header_.slotCount) * sizeof(MeteringSchemeRecord);
}

}  // namespace faucet
