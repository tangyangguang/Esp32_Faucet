#include "app/WaterRecordFileStore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kRecordMagic = 0x46575244UL;  // FWRD
constexpr std::uint16_t kRecordVersion = 5;
constexpr std::size_t kRecordsPerSegment = 200;
constexpr std::size_t kBackupSpareSlots = 2;
constexpr std::size_t kSegmentPathSize = 96;
struct RecordHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t recordSize;
    std::uint32_t capacity;
    std::uint32_t count;
    std::uint32_t oldestIndex;
    std::uint32_t commitSeq;
    std::uint32_t checksum;
};

static_assert(sizeof(RecordHeader) == 28, "RecordHeader must stay fixed-size");

bool validPath(const char* path) {
    return path && path[0] == '/';
}

bool segmentPath(const char* path, std::size_t index, char (&output)[kSegmentPathSize]) {
    const int length = std::snprintf(output, sizeof(output), "%s.%lu", path,
                                     static_cast<unsigned long>(index / kRecordsPerSegment));
    return length > 0 && static_cast<std::size_t>(length) < sizeof(output);
}

std::size_t segmentOffset(std::size_t index) {
    return index % kRecordsPerSegment * sizeof(WaterRecord);
}

std::uint32_t headerChecksum(RecordHeader header) {
    header.checksum = 0;
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&header);
    std::uint32_t hash = 2166136261UL;
    for (std::size_t i = 0; i < sizeof(RecordHeader); ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash == 0 ? 1 : hash;
}

RecordHeader makeHeader(std::size_t capacity, std::size_t count, std::size_t oldestIndex, std::uint32_t commitSeq) {
    RecordHeader header{
        kRecordMagic,
        kRecordVersion,
        static_cast<std::uint16_t>(sizeof(WaterRecord)),
        static_cast<std::uint32_t>(capacity),
        static_cast<std::uint32_t>(count),
        static_cast<std::uint32_t>(oldestIndex),
        commitSeq,
        0,
    };
    header.checksum = headerChecksum(header);
    return header;
}

bool validHeaderFields(const RecordHeader& header, std::size_t expectedCapacity) {
    return header.magic == kRecordMagic && header.version == kRecordVersion &&
           header.recordSize == sizeof(WaterRecord) && header.capacity != 0 &&
           header.capacity == expectedCapacity && header.count <= header.capacity &&
           header.oldestIndex < expectedCapacity + kBackupSpareSlots;
}

bool validHeader(const RecordHeader& header, std::size_t expectedCapacity) {
    if (!validHeaderFields(header, expectedCapacity)) {
        return false;
    }
    return header.checksum == headerChecksum(header);
}

bool newerHeader(const RecordHeader& candidate, const RecordHeader& previous) {
    const std::uint32_t delta = candidate.commitSeq - previous.commitSeq;
    return delta != 0 && delta < 0x80000000UL;
}

}  // namespace

WaterRecordFileStore::WaterRecordFileStore(WaterRecordFileBackend& backend, const char* path, std::size_t capacity)
    : backend_(backend),
      path_(path),
      capacity_(capacity),
      oldestIndex_(0),
      count_(0),
      commitSeq_(0),
      ready_(false),
      status_(WaterRecordFileStatus::Unavailable) {}

bool WaterRecordFileStore::begin() {
    ready_ = false;
    status_ = WaterRecordFileStatus::Unavailable;
    if (!validPath(path_) || capacity_ == 0 || capacity_ > UINT32_MAX / sizeof(WaterRecord)) {
        status_ = !validPath(path_) ? WaterRecordFileStatus::InvalidPath : WaterRecordFileStatus::InvalidCapacity;
        return false;
    }
    char lastSegment[kSegmentPathSize];
    if (!segmentPath(path_, slotCount() - 1, lastSegment)) {
        status_ = WaterRecordFileStatus::InvalidPath;
        return false;
    }

    const std::int64_t metadataSize = backend_.fileSize(path_);
    if (metadataSize < 0 && backend_.exists(path_)) {
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }
    if (metadataSize < static_cast<std::int64_t>(2 * sizeof(RecordHeader))) {
        return initializeNewFile(static_cast<std::size_t>(std::max<std::int64_t>(0, metadataSize)));
    }

    if (!loadHeader()) {
        return false;
    }

    ready_ = true;
    status_ = WaterRecordFileStatus::Ready;
    return true;
}

bool WaterRecordFileStore::append(const WaterRecord& record) {
    if (!ready() && !begin()) {
        return false;
    }

    // The two spare slots keep both committed headers readable before the next commit.
    const std::size_t writeIndex = (oldestIndex_ + count_) % slotCount();
    const std::size_t nextCount = std::min(count_ + 1, capacity_);
    const std::size_t nextOldestIndex = count_ == capacity_ ? (oldestIndex_ + 1) % slotCount() : oldestIndex_;

    if (!appendRecord(writeIndex, record)) {
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }
    const std::size_t oldCount = count_;
    const std::size_t oldOldestIndex = oldestIndex_;
    count_ = nextCount;
    oldestIndex_ = nextOldestIndex;
    if (saveHeader()) {
        status_ = WaterRecordFileStatus::Ready;
        return true;
    }
    count_ = oldCount;
    oldestIndex_ = oldOldestIndex;
    status_ = WaterRecordFileStatus::BackendFailure;
    return false;
}

std::size_t WaterRecordFileStore::rewriteBootRelativeTimes(std::uint32_t bootId, std::uint32_t bootStartRealSec,
                                                         std::size_t newestRecordLimit) {
    if (newestRecordLimit == 0 || bootId == 0 || !ready()) {
        return 0;
    }
    const std::size_t limit = std::min(count_, newestRecordLimit);
    std::size_t changed = 0;
    constexpr std::size_t kRewriteBatchSize = 24;
    WaterRecord records[kRewriteBatchSize];
    for (std::size_t offset = 0; offset < limit;) {
        const std::size_t newest = physicalIndexFromNewestOffset(offset);
        const std::size_t batch = std::min({limit - offset, newest % kRecordsPerSegment + 1, kRewriteBatchSize});
        const std::size_t first = newest + 1 - batch;
        char path[kSegmentPathSize];
        if (!segmentPath(path_, first, path) || !readRecordSpan(first, records, batch)) {
            return changed;
        }
        for (std::size_t i = batch; i > 0; --i) {
            WaterRecord& record = records[i - 1];
            if (!waterRecordHasBootRelativeTime(record) || waterRecordBootId(record) != bootId) {
                continue;
            }
            record.startTime = bootStartRealSec + record.startTime;
            clearWaterRecordBootId(record);
            if (!backend_.writeAt(path, segmentOffset(first + i - 1),
                                  reinterpret_cast<const std::uint8_t*>(&record), sizeof(record))) {
                return changed;
            }
            ++changed;
        }
        offset += batch;
    }
    return changed;
}

std::size_t WaterRecordFileStore::readPage(std::size_t pageIndex,
                                        std::uint16_t pageSize,
                                        WaterRecord* output,
                                        std::size_t outputCapacity) const {
    if (!ready_ || !output || outputCapacity == 0 || count_ == 0) {
        return 0;
    }
    if (!ready()) {
        return 0;
    }

    const std::uint16_t sanitizedPageSize = sanitizeRecordPageSize(pageSize);
    if (pageIndex > (count_ - 1) / sanitizedPageSize) {
        return 0;
    }
    const std::size_t startOffset = pageIndex * static_cast<std::size_t>(sanitizedPageSize);

    const std::size_t available = count_ - startOffset;
    const std::size_t limit = std::min<std::size_t>({available, sanitizedPageSize, outputCapacity});
    std::size_t copied = 0;
    while (copied < limit) {
        const std::size_t newestOffset = startOffset + copied;
        const std::size_t newestIndex = physicalIndexFromNewestOffset(newestOffset);
        const std::size_t batch = std::min(limit - copied, newestIndex % kRecordsPerSegment + 1);
        const std::size_t firstIndex = newestIndex + 1 - batch;
        if (!readRecordSpan(firstIndex, output + copied, batch)) {
            return copied;
        }
        std::reverse(output + copied, output + copied + batch);
        copied += batch;
    }
    return limit;
}

bool WaterRecordFileStore::clear() {
    if (!ready()) {
        return false;
    }
    const std::size_t oldOldestIndex = oldestIndex_;
    const std::size_t oldCount = count_;
    oldestIndex_ = (oldestIndex_ + count_) % slotCount();
    count_ = 0;
    if (saveHeader()) {
        return true;
    }
    oldestIndex_ = oldOldestIndex;
    count_ = oldCount;
    status_ = WaterRecordFileStatus::BackendFailure;
    return false;
}

std::size_t WaterRecordFileStore::count() const {
    return ready() ? count_ : 0;
}

bool WaterRecordFileStore::ready() const {
    return ready_ && status_ == WaterRecordFileStatus::Ready && backend_.exists(path_);
}

const char* WaterRecordFileStore::storageName() const {
    return ready() ? "file" : "unavailable";
}

WaterRecordFileStatus WaterRecordFileStore::status() const {
    if (validPath(path_) && !backend_.exists(path_)) {
        return WaterRecordFileStatus::Missing;
    }
    return status_;
}

bool WaterRecordFileStore::initializeNewFile(std::size_t existingBytes) {
    oldestIndex_ = 0;
    count_ = 0;
    commitSeq_ = 2;
    const RecordHeader headers[] = {makeHeader(capacity_, count_, oldestIndex_, 1),
                                    makeHeader(capacity_, count_, oldestIndex_, commitSeq_)};
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(headers);
    std::uint8_t stored[sizeof(headers)]{};
    if (existingBytes > 0 && !backend_.readAt(path_, 0, stored, existingBytes)) {
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }
    if (std::memcmp(stored, bytes, existingBytes) != 0) {
        status_ = WaterRecordFileStatus::Corrupt;
        return false;
    }
    ready_ = backend_.appendBytes(path_, bytes + existingBytes, sizeof(headers) - existingBytes) &&
             backend_.readAt(path_, 0, stored, sizeof(stored)) &&
             std::memcmp(stored, bytes, sizeof(stored)) == 0;
    status_ = ready_ ? WaterRecordFileStatus::Ready : WaterRecordFileStatus::BackendFailure;
    return ready_;
}

bool WaterRecordFileStore::loadHeader() {
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(2 * sizeof(RecordHeader))) {
        status_ = WaterRecordFileStatus::Corrupt;
        return false;
    }
    if (fileSize > static_cast<std::int64_t>(2 * sizeof(RecordHeader))) {
        status_ = WaterRecordFileStatus::IncompatibleFormat;
        return false;
    }

    RecordHeader header{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }

    const bool primaryValid = header.commitSeq % 2U == 1 && validHeader(header, capacity_);
    RecordHeader backup{};
    if (!backend_.readAt(path_, sizeof(RecordHeader), reinterpret_cast<std::uint8_t*>(&backup), sizeof(backup))) {
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }
    const bool backupValid = backup.commitSeq % 2U == 0 && validHeader(backup, capacity_);

    if (!primaryValid && !backupValid) {
        status_ = (header.magic != kRecordMagic || header.version != kRecordVersion ||
                   header.recordSize != sizeof(WaterRecord) || header.capacity != capacity_)
                      ? WaterRecordFileStatus::IncompatibleFormat
                      : WaterRecordFileStatus::Corrupt;
        return false;
    }

    const bool preferPrimary = primaryValid && (!backupValid || newerHeader(header, backup));
    const RecordHeader* selected = preferPrimary ? &header : &backup;
    const RecordHeader* fallback = preferPrimary ? &backup : &header;
    if (!hasRecords(selected->oldestIndex, selected->count)) {
        const bool fallbackValid = preferPrimary ? backupValid : primaryValid;
        if (fallbackValid && hasRecords(fallback->oldestIndex, fallback->count)) {
            selected = fallback;
        } else {
            status_ = WaterRecordFileStatus::Corrupt;
            return false;
        }
    }

    count_ = selected->count;
    oldestIndex_ = selected->oldestIndex;
    commitSeq_ = selected->commitSeq;
    status_ = WaterRecordFileStatus::Ready;
    return true;
}

bool WaterRecordFileStore::hasRecords(std::size_t oldestIndex, std::size_t count) const {
    while (count > 0) {
        const std::size_t span = std::min({count, slotCount() - oldestIndex,
                                          kRecordsPerSegment - oldestIndex % kRecordsPerSegment});
        char path[kSegmentPathSize];
        if (!segmentPath(path_, oldestIndex, path)) {
            return false;
        }
        const std::int64_t size = backend_.fileSize(path);
        if (size < static_cast<std::int64_t>(segmentOffset(oldestIndex) + span * sizeof(WaterRecord)) ||
            size > static_cast<std::int64_t>(kRecordsPerSegment * sizeof(WaterRecord))) {
            return false;
        }
        count -= span;
        oldestIndex = (oldestIndex + span) % slotCount();
    }
    return true;
}

bool WaterRecordFileStore::saveHeader() {
    const std::uint32_t nextSeq = commitSeq_ + 1U;
    const RecordHeader header = makeHeader(capacity_, count_, oldestIndex_, nextSeq);
    const std::size_t offset = (nextSeq % 2U) == 0 ? sizeof(RecordHeader) : 0;
    RecordHeader stored{};
    if (!backend_.writeAt(path_, offset, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)) ||
        !backend_.readAt(path_, offset, reinterpret_cast<std::uint8_t*>(&stored), sizeof(stored)) ||
        std::memcmp(&stored, &header, sizeof(header)) != 0) {
        return false;
    }
    commitSeq_ = nextSeq;
    return true;
}

bool WaterRecordFileStore::appendRecord(std::size_t index, const WaterRecord& record) {
    char path[kSegmentPathSize];
    if (!segmentPath(path_, index, path)) {
        return false;
    }
    const std::size_t offset = segmentOffset(index);
    const std::int64_t size = backend_.fileSize(path);
    const std::int64_t position = static_cast<std::int64_t>(offset);
    const auto* data = reinterpret_cast<const std::uint8_t*>(&record);
    const std::int64_t end = position + static_cast<std::int64_t>(sizeof(record));
    if ((size < position && !(size < 0 && offset == 0)) ||
        size > static_cast<std::int64_t>(kRecordsPerSegment * sizeof(WaterRecord))) {
        return false;
    }
    if (size <= position) {
        if (!backend_.appendBytes(path, data, sizeof(record))) {
            return false;
        }
    } else {
        // A short append is outside both committed ranges. Fill the tail before retrying the slot.
        if (size < end && !backend_.appendBytes(path, data + (size - position), end - size)) {
            return false;
        }
        if (!backend_.writeAt(path, offset, data, sizeof(record))) {
            return false;
        }
    }
    WaterRecord stored{};
    return readRecordSpan(index, &stored, 1) && std::memcmp(&stored, &record, sizeof(record)) == 0;
}

bool WaterRecordFileStore::readRecordSpan(std::size_t firstIndex, WaterRecord* output, std::size_t count) const {
    char path[kSegmentPathSize];
    if (!segmentPath(path_, firstIndex, path)) {
        return false;
    }
    return backend_.readAt(path, segmentOffset(firstIndex), reinterpret_cast<std::uint8_t*>(output),
                           count * sizeof(WaterRecord));
}

std::size_t WaterRecordFileStore::slotCount() const {
    return capacity_ + kBackupSpareSlots;
}

std::size_t WaterRecordFileStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    return (oldestIndex_ + count_ - 1 - offset) % slotCount();
}

}  // namespace faucet
