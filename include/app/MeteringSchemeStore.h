#pragma once

#include "app/MeteringScheme.h"
#include "app/WaterRecordFileStore.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class ConfigBackend;

struct MeteringSchemeStoreHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t headerSize;
    std::uint16_t recordSize;
    std::uint16_t candidateSize;
    std::uint32_t activeSchemeId;
    std::uint32_t nextSchemeId;
    std::uint32_t slotCount;
    std::uint32_t reserved;
    std::uint32_t checksum;
};

class MeteringSchemeStore {
public:
    MeteringSchemeStore(WaterRecordFileBackend& backend, const char* path);

    bool begin();
    bool ready() const;
    std::uint32_t activeSchemeId() const;
    bool activeScheme(MeteringSchemeRecord& output) const;
    bool findById(std::uint32_t id, MeteringSchemeRecord& output) const;
    std::size_t list(MeteringSchemeRecord* output, std::size_t outputCapacity, bool includeDisabled) const;

    bool loadCandidate(MeteringSchemeCandidate& output) const;
    bool saveCandidate(const MeteringSchemeCandidate& candidate);
    bool discardCandidate();
    bool saveCandidateAsNew(const MeteringSchemeCandidate& candidate,
                            const char* name,
                            std::uint32_t nowSeconds,
                            std::uint32_t& newId);
    bool createManual(const char* name,
                      const MeteringParameters& params,
                      std::uint32_t nowSeconds,
                      std::uint32_t& newId);
    bool updateScheme(const MeteringSchemeRecord& edited, std::uint32_t nowSeconds);
    bool enableScheme(std::uint32_t schemeId, std::uint32_t nowSeconds);
    bool disableScheme(std::uint32_t schemeId, std::uint32_t nowSeconds);
    bool restoreScheme(std::uint32_t schemeId, std::uint32_t nowSeconds);
    bool deleteScheme(std::uint32_t schemeId);
    bool markUsedAfterRecordWrite(std::uint32_t schemeId);
    bool migrateLegacyFromConfig(ConfigBackend& config, std::uint32_t nowSeconds);

private:
    bool validPath() const;
    bool initializeNewFile();
    bool migrateV1File(const MeteringSchemeStoreHeader& loaded);
    bool upgradeLegacyDefaultSchemeIfNeeded();
    bool loadHeader();
    bool saveHeader() const;
    bool readRecord(std::size_t slot, MeteringSchemeRecord& output) const;
    bool writeRecord(std::size_t slot, const MeteringSchemeRecord& record);
    bool findSlotById(std::uint32_t id, MeteringSchemeRecord& output, std::size_t& slot) const;
    bool findFreeSlot(std::size_t& slot) const;
    bool writeOrAppendAt(std::size_t offset, const std::uint8_t* data, std::size_t len);
    std::size_t candidateOffset() const;
    std::size_t recordOffset(std::size_t slot) const;
    std::size_t expectedFileSize() const;

    WaterRecordFileBackend& backend_;
    const char* path_;
    MeteringSchemeStoreHeader header_;
    bool ready_;
};

}  // namespace faucet
