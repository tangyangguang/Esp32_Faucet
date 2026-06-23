#include "app/MeteringSchemeStore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace faucet {
namespace {

constexpr std::uint32_t kMeteringSchemeStoreMagic = 0x314D5346UL;  // FSM1
constexpr std::uint16_t kMeteringSchemeStoreVersion = 7;
constexpr std::size_t kCopyChunkSize = 256;

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
        0,
        activeSchemeId,
        nextSchemeId,
        slotCount,
        0,
        0,
    };
    header.checksum = headerChecksum(header);
    return header;
}

std::size_t expectedFileSizeForHeader(const MeteringSchemeStoreHeader& header) {
    return sizeof(MeteringSchemeStoreHeader) +
           static_cast<std::size_t>(header.slotCount) * sizeof(MeteringSchemeRecord);
}

bool validCurrentHeaderForFile(const MeteringSchemeStoreHeader& header, std::int64_t fileSize) {
    if (header.magic != kMeteringSchemeStoreMagic ||
        header.version != kMeteringSchemeStoreVersion ||
        header.headerSize != sizeof(MeteringSchemeStoreHeader) ||
        header.recordSize != sizeof(MeteringSchemeRecord) ||
        header.candidateSize != 0 ||
        header.nextSchemeId == 0 ||
        header.activeSchemeId == 0 ||
        header.slotCount != kMeteringSchemeStoreSlotCount ||
        header.checksum != headerChecksum(header)) {
        return false;
    }
    return fileSize == static_cast<std::int64_t>(expectedFileSizeForHeader(header));
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
    std::uint8_t buffer[kCopyChunkSize]{};
    for (std::size_t offset = 0; offset < size; offset += sizeof(buffer)) {
        const std::size_t chunk = std::min<std::size_t>(sizeof(buffer), size - offset);
        if (!backend.readAt(from, offset, buffer, chunk) || !backend.writeAt(to, offset, buffer, chunk)) {
            return false;
        }
    }
    return true;
}

bool writeCurrentSchemeFile(WaterRecordFileBackend& backend,
                            const char* path,
                            const MeteringSchemeStoreHeader& header,
                            const MeteringSchemeRecord* records) {
    if (!path || !records || !backend.createSized(path, expectedFileSizeForHeader(header))) {
        return false;
    }
    if (!backend.writeAt(path, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header))) {
        return false;
    }
    const std::size_t recordBase = sizeof(MeteringSchemeStoreHeader);
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
    : backend_(backend),
      path_(path),
      header_(makeHeader(0, 0, 0)),
      ready_(false),
      status_(AppStorageStatus::Unavailable) {}

bool MeteringSchemeStore::begin() {
    ready_ = false;
    status_ = AppStorageStatus::Unavailable;
    if (!validPath()) {
        status_ = AppStorageStatus::InvalidPath;
        return false;
    }
    if (!backend_.exists(path_)) {
        const bool ok = initializeNewFile();
        status_ = ok ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return ok;
    }
    if (!loadHeader()) {
        return false;
    }
    ready_ = true;
    if (!repairNextSchemeId()) {
        ready_ = false;
        status_ = AppStorageStatus::BackendFailure;
        return false;
    }
    MeteringSchemeRecord active{};
    if (!activeScheme(active) || !validMeteringSchemeParameters(active.params)) {
        ready_ = false;
        status_ = AppStorageStatus::Corrupt;
        return false;
    }
    status_ = AppStorageStatus::Ready;
    return true;
}

bool MeteringSchemeStore::ready() const {
    return ready_ && status_ == AppStorageStatus::Ready && backend_.exists(path_);
}

AppStorageStatus MeteringSchemeStore::status() const {
    if (ready_ && !backend_.exists(path_)) {
        return AppStorageStatus::Missing;
    }
    return status_;
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

std::size_t MeteringSchemeStore::list(MeteringSchemeRecord* output, std::size_t outputCapacity) const {
    if (!ready() || !output || outputCapacity == 0) {
        return 0;
    }
    const std::size_t slotCount = static_cast<std::size_t>(header_.slotCount);
    MeteringSchemeRecord* records = outputCapacity >= slotCount
                                        ? output
                                        : new (std::nothrow) MeteringSchemeRecord[slotCount]{};
    if (records &&
        backend_.readAt(path_,
                        recordOffset(0),
                        reinterpret_cast<std::uint8_t*>(records),
                        slotCount * sizeof(MeteringSchemeRecord))) {
        std::size_t copied = 0;
        for (std::size_t slot = 0; slot < slotCount && copied < outputCapacity; ++slot) {
            const MeteringSchemeRecord& record = records[slot];
            if (record.recordUsed) {
                if (&output[copied] != &record) {
                    output[copied] = record;
                }
                ++copied;
            }
        }
        if (records != output) {
            delete[] records;
        }
        return copied;
    }
    if (records && records != output) {
        delete[] records;
    }
    std::size_t copied = 0;
    for (std::size_t slot = 0; slot < slotCount && copied < outputCapacity; ++slot) {
        MeteringSchemeRecord record{};
        if (!readRecord(slot, record)) {
            return copied;
        }
        if (record.recordUsed) {
            output[copied++] = record;
        }
    }
    return copied;
}

bool MeteringSchemeStore::saveCandidateAsNew(const MeteringSchemeCandidate& candidate,
                                             const char* name,
                                             std::uint32_t nowSeconds,
                                             std::uint32_t& newId) {
    newId = 0;
    if (!ready()) {
        return false;
    }
    MeteringSchemeCandidate working = candidate;
    MeteringSchemeRecord records[1]{};
    MeteringSchemeCollection collection{records, 1, header_.activeSchemeId, header_.nextSchemeId};
    if (!saveCandidateAsNewMeteringScheme(collection, working, name, nowSeconds, newId)) {
        return false;
    }

    std::size_t slot = 0;
    if (!findFreeSlot(slot) && !findOldestNonCurrentSlot(slot)) {
        newId = 0;
        return false;
    }
    MeteringSchemeRecord previousRecord{};
    if (!readRecord(slot, previousRecord)) {
        newId = 0;
        return false;
    }
    if (!writeRecord(slot, records[0])) {
        newId = 0;
        return false;
    }
    MeteringSchemeStoreHeader previous = header_;
    header_.nextSchemeId = collection.nextSchemeId;
    header_.checksum = headerChecksum(header_);
    if (!saveHeader()) {
        header_ = previous;
        writeRecord(slot, previousRecord);
        return false;
    }
    return true;
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
    if (!findFreeSlot(slot) && !findOldestNonCurrentSlot(slot)) {
        newId = 0;
        return false;
    }
    MeteringSchemeRecord previousRecord{};
    if (!readRecord(slot, previousRecord)) {
        newId = 0;
        return false;
    }
    if (!writeRecord(slot, records[0])) {
        newId = 0;
        return false;
    }
    MeteringSchemeStoreHeader previous = header_;
    header_.nextSchemeId = collection.nextSchemeId;
    header_.checksum = headerChecksum(header_);
    if (!saveHeader()) {
        header_ = previous;
        writeRecord(slot, previousRecord);
        return false;
    }
    return true;
}

bool MeteringSchemeStore::setActiveScheme(std::uint32_t schemeId, std::uint32_t nowSeconds) {
    (void)nowSeconds;
    if (!ready()) {
        return false;
    }
    MeteringSchemeRecord record{};
    std::size_t slot = 0;
    if (!findSlotById(schemeId, record, slot) || !validMeteringSchemeParameters(record.params)) {
        return false;
    }
    MeteringSchemeStoreHeader previous = header_;
    header_.activeSchemeId = schemeId;
    header_.checksum = headerChecksum(header_);
    if (!saveHeader()) {
        header_ = previous;
        return false;
    }
    return true;
}

bool MeteringSchemeStore::validPath() const {
    return path_ && path_[0] == '/';
}

bool MeteringSchemeStore::initializeNewFile() {
    std::unique_ptr<MeteringSchemeRecord[]> records(
        new (std::nothrow) MeteringSchemeRecord[kMeteringSchemeStoreSlotCount]{});
    if (!records) {
        return false;
    }
    MeteringSchemeCollection collection{records.get(), kMeteringSchemeStoreSlotCount, 0, 0};
    if (!initializeDefaultMeteringSchemes(collection, 0)) {
        return false;
    }
    header_ = makeHeader(collection.activeSchemeId,
                         collection.nextSchemeId,
                         static_cast<std::uint32_t>(kMeteringSchemeStoreSlotCount));
    ready_ = writeCurrentSchemeFile(backend_, path_, header_, records.get());
    return ready_;
}

bool MeteringSchemeStore::repairNextSchemeId() {
    if (!ready()) {
        return false;
    }
    std::uint32_t maxId = 0;
    bool activeFound = false;
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord record{};
        if (!readRecord(i, record)) {
            return false;
        }
        if (!record.recordUsed) {
            continue;
        }
        maxId = std::max(maxId, record.id);
        if (record.id == header_.activeSchemeId) {
            activeFound = true;
        }
    }
    if (!activeFound || maxId == UINT32_MAX) {
        return false;
    }
    const std::uint32_t repairedNextId = std::max<std::uint32_t>(header_.nextSchemeId, maxId + 1U);
    if (repairedNextId == header_.nextSchemeId) {
        return true;
    }
    header_.nextSchemeId = repairedNextId;
    header_.checksum = headerChecksum(header_);
    if (!saveHeader()) {
        return false;
    }
    return true;
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
            validCurrentHeaderForFile(tempHeader, tempSize) &&
            copyFileBytes(backend_, tempPath, path_, expectedFileSizeForHeader(tempHeader))) {
            backend_.removeFile(tempPath);
            header_ = tempHeader;
            return true;
        }
        return false;
    };

    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(sizeof(MeteringSchemeStoreHeader))) {
        if (recoverFromTemp()) {
            status_ = AppStorageStatus::Ready;
            return true;
        }
        status_ = AppStorageStatus::Corrupt;
        return false;
    }
    MeteringSchemeStoreHeader loaded{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&loaded), sizeof(loaded))) {
        if (recoverFromTemp()) {
            status_ = AppStorageStatus::Ready;
            return true;
        }
        status_ = AppStorageStatus::BackendFailure;
        return false;
    }
    if (validCurrentHeaderForFile(loaded, fileSize)) {
        if (hasTempPath && backend_.exists(tempPath)) {
            backend_.removeFile(tempPath);
        }
        header_ = loaded;
        status_ = AppStorageStatus::Ready;
        return true;
    }
    if (recoverFromTemp()) {
        status_ = AppStorageStatus::Ready;
        return true;
    }
    status_ = (loaded.magic != kMeteringSchemeStoreMagic ||
               loaded.headerSize != sizeof(MeteringSchemeStoreHeader) ||
               loaded.version != kMeteringSchemeStoreVersion ||
               loaded.recordSize != sizeof(MeteringSchemeRecord) ||
               loaded.candidateSize != 0)
                  ? AppStorageStatus::IncompatibleFormat
                  : AppStorageStatus::Corrupt;
    return false;
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
    if (!ready_ || slot >= header_.slotCount) {
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
        if (record.recordUsed && record.id == id) {
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
        if (!record.recordUsed) {
            slot = i;
            return true;
        }
    }
    return false;
}

bool MeteringSchemeStore::findOldestNonCurrentSlot(std::size_t& slot) const {
    if (!ready()) {
        return false;
    }
    bool found = false;
    std::uint32_t bestCreatedAt = UINT32_MAX;
    std::uint32_t bestId = UINT32_MAX;
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord record{};
        if (!readRecord(i, record)) {
            return false;
        }
        if (!record.recordUsed || record.id == header_.activeSchemeId) {
            continue;
        }
        const bool older = record.createdAt < bestCreatedAt ||
                           (record.createdAt == bestCreatedAt && record.id < bestId);
        if (older) {
            bestCreatedAt = record.createdAt;
            bestId = record.id;
            slot = i;
            found = true;
        }
    }
    return found;
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

std::size_t MeteringSchemeStore::recordOffset(std::size_t slot) const {
    return sizeof(MeteringSchemeStoreHeader) + slot * sizeof(MeteringSchemeRecord);
}

std::size_t MeteringSchemeStore::expectedFileSize() const {
    return expectedFileSizeForHeader(header_);
}

}  // namespace faucet
