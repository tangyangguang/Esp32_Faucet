#include "app/WaterPulseTraceStore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace faucet {
namespace {

constexpr std::uint32_t kSavedTraceFileMagic = 0x46575046UL;  // FWPF
constexpr std::uint16_t kSavedTraceFileVersion = 2;

struct SavedTraceFileHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t headerSize;
    std::uint16_t indexEntrySize;
    std::uint16_t sampleSize;
    std::uint16_t slotCount;
    std::uint16_t sampleCapacity;
    std::uint16_t reserved0;
    std::uint32_t fileSize;
    std::uint32_t headerChecksum;
};

bool sameRecordIdentity(const WaterRecord& a, const WaterRecord& b) {
    return a.startTime == b.startTime && a.volumeMl == b.volumeMl && a.targetValue == b.targetValue &&
           a.pulseCount == b.pulseCount && a.durationSec == b.durationSec && a.selectedPreset == b.selectedPreset &&
           a.result == b.result;
}

WaterPulseTraceState mergeState(WaterPulseTraceState current, WaterPulseTraceState next) {
    if (current == WaterPulseTraceState::Error || next == WaterPulseTraceState::Error) {
        return WaterPulseTraceState::Error;
    }
    if (current == WaterPulseTraceState::SafetyStopped || next == WaterPulseTraceState::SafetyStopped) {
        return WaterPulseTraceState::SafetyStopped;
    }
    if (current == WaterPulseTraceState::PauseTimeout || next == WaterPulseTraceState::PauseTimeout) {
        return WaterPulseTraceState::PauseTimeout;
    }
    if (current == WaterPulseTraceState::Stopped || next == WaterPulseTraceState::Stopped) {
        return WaterPulseTraceState::Stopped;
    }
    if (current == WaterPulseTraceState::Completed || next == WaterPulseTraceState::Completed) {
        return WaterPulseTraceState::Completed;
    }
    if (current == WaterPulseTraceState::Paused || next == WaterPulseTraceState::Paused) {
        return WaterPulseTraceState::Paused;
    }
    return WaterPulseTraceState::Running;
}

std::uint32_t roundU32(float value) {
    return value <= 0.0f ? 0 : static_cast<std::uint32_t>(std::lround(value));
}

void hashRecordField(std::uint32_t& hash, std::uint32_t value) {
    hash ^= value;
    hash *= 16777619UL;
}

void checksumBytes(std::uint32_t& hash, const void* data, std::size_t len) {
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
}

std::uint32_t checksumBytes(const void* data, std::size_t len) {
    std::uint32_t hash = 2166136261UL;
    checksumBytes(hash, data, len);
    if (hash == 0) {
        hash = 1;
    }
    return hash;
}

std::uint32_t checksumHeader(SavedTraceFileHeader header) {
    header.headerChecksum = 0;
    return checksumBytes(&header, sizeof(header));
}

bool allZeroBytes(const void* data, std::size_t len) {
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

WaterPulseTraceStore::WaterPulseTraceStore(WaterPulseTrace* traces,
                                           std::size_t traceCapacity,
                                           WaterPulseTraceSample* samples,
                                           std::size_t sampleCapacity,
                                           std::size_t budgetBytes)
    : traces_(traces),
      traceCapacity_(traceCapacity),
      traceCount_(0),
      samples_(samples),
      sampleCapacity_(sampleCapacity),
      sampleCount_(0),
      budgetBytes_(budgetBytes),
      nextTraceId_(1) {}

void WaterPulseTraceStore::setBudgetBytes(std::size_t budgetBytes) {
    budgetBytes_ = budgetBytes;
    enforceBudget();
}

std::uint32_t WaterPulseTraceStore::beginTrace(std::uint32_t startTime) {
    if (!traces_ || !samples_ || traceCapacity_ == 0 || sampleCapacity_ == 0) {
        return 0;
    }
    while (traceCount_ >= traceCapacity_) {
        dropOldest();
    }
    WaterPulseTrace& trace = traces_[traceCount_++];
    trace = WaterPulseTrace{};
    trace.traceId = nextTraceId_++;
    if (nextTraceId_ == 0) {
        nextTraceId_ = 1;
    }
    trace.startTime = startTime;
    trace.sampleStart = sampleCount_;
    enforceBudget();
    return trace.traceId;
}

bool WaterPulseTraceStore::appendSecond(std::uint32_t traceId, std::uint32_t pulseDelta, WaterPulseTraceState state) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || trace->finished || sampleCount_ >= sampleCapacity_) {
        return false;
    }
    while (sampleCount_ >= sampleCapacity_) {
        dropOldest();
        trace = findById(traceId);
        if (!trace || trace->finished) {
            return false;
        }
    }
    const std::uint16_t clipped =
        pulseDelta > UINT16_MAX ? UINT16_MAX : static_cast<std::uint16_t>(pulseDelta);
    samples_[sampleCount_++] = WaterPulseTraceSample{clipped, state, 0};
    ++trace->sampleCount;
    trace->totalPulses += clipped;
    enforceBudget();
    return findById(traceId) != nullptr;
}

bool WaterPulseTraceStore::finishTrace(std::uint32_t traceId, const WaterRecord& record, WaterPulseTraceState finalState) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace) {
        return false;
    }
    trace->record = record;
    trace->finished = true;
    if (trace->sampleCount > 0) {
        samples_[trace->sampleStart + trace->sampleCount - 1].state =
            mergeState(samples_[trace->sampleStart + trace->sampleCount - 1].state, finalState);
    }
    enforceBudget();
    return findById(traceId) != nullptr;
}

bool WaterPulseTraceStore::setActualMl(std::uint32_t traceId, std::uint32_t actualMl) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || actualMl == 0) {
        return false;
    }
    trace->actualMl = actualMl;
    return true;
}

const WaterPulseTrace* WaterPulseTraceStore::findById(std::uint32_t traceId) const {
    const std::size_t index = indexOf(traceId);
    return index < traceCount_ ? &traces_[index] : nullptr;
}

WaterPulseTrace* WaterPulseTraceStore::findById(std::uint32_t traceId) {
    const std::size_t index = indexOf(traceId);
    return index < traceCount_ ? &traces_[index] : nullptr;
}

const WaterPulseTrace* WaterPulseTraceStore::findByRecord(const WaterRecord& record) const {
    for (std::size_t i = 0; i < traceCount_; ++i) {
        if (traces_[i].finished && sameRecordIdentity(traces_[i].record, record)) {
            return &traces_[i];
        }
    }
    return nullptr;
}

const WaterPulseTrace* WaterPulseTraceStore::traceAt(std::size_t index) const {
    return index < traceCount_ ? &traces_[index] : nullptr;
}

const WaterPulseTraceSample* WaterPulseTraceStore::sampleAt(const WaterPulseTrace& trace, std::size_t index) const {
    if (index >= trace.sampleCount || trace.sampleStart + index >= sampleCount_) {
        return nullptr;
    }
    return &samples_[trace.sampleStart + index];
}

WaterPulseTraceStats WaterPulseTraceStore::stats() const {
    WaterPulseTraceStats out{};
    out.traceCount = traceCount_;
    out.traceCapacity = traceCapacity_;
    out.sampleCount = sampleCount_;
    out.sampleCapacity = sampleCapacity_;
    out.usedBytes = usedBytes();
    out.budgetBytes = budgetBytes_;
    out.usagePercent = budgetBytes_ == 0
                           ? 0
                           : static_cast<std::uint8_t>(std::min<std::size_t>(100, (out.usedBytes * 100U) / budgetBytes_));
    if (traceCount_ > 0) {
        out.oldestStartTime = traces_[0].startTime;
        out.latestStartTime = traces_[traceCount_ - 1].startTime;
    }
    return out;
}

std::size_t WaterPulseTraceStore::count() const {
    return traceCount_;
}

std::size_t WaterPulseTraceStore::usedBytes() const {
    return traceCount_ * sizeof(WaterPulseTrace) + sampleCount_ * sizeof(WaterPulseTraceSample);
}

void WaterPulseTraceStore::enforceBudget() {
    while (traceCount_ > 1 && budgetBytes_ > 0 && usedBytes() > budgetBytes_) {
        dropOldest();
    }
}

void WaterPulseTraceStore::dropOldest() {
    if (traceCount_ == 0) {
        sampleCount_ = 0;
        return;
    }
    const std::size_t removedSamples = traces_[0].sampleCount;
    if (removedSamples > 0 && removedSamples <= sampleCount_) {
        std::memmove(samples_, samples_ + removedSamples, (sampleCount_ - removedSamples) * sizeof(WaterPulseTraceSample));
        sampleCount_ -= removedSamples;
    }
    for (std::size_t i = 1; i < traceCount_; ++i) {
        traces_[i].sampleStart -= removedSamples;
        traces_[i - 1] = traces_[i];
    }
    --traceCount_;
}

std::size_t WaterPulseTraceStore::indexOf(std::uint32_t traceId) const {
    for (std::size_t i = 0; i < traceCount_; ++i) {
        if (traces_[i].traceId == traceId) {
            return i;
        }
    }
    return traceCount_;
}

WaterPulseTraceFileStore::WaterPulseTraceFileStore(WaterRecordFileBackend& backend,
                                                   const char* path,
                                                   std::size_t sampleCapacityPerTrace,
                                                   std::size_t maxTraceCount)
    : backend_(backend),
      path_(path),
      sampleCapacityPerTrace_(sampleCapacityPerTrace),
      maxTraceCount_(maxTraceCount),
      ready_(false),
      indexLoaded_(false),
      indexValid_(false),
      filePresent_(false),
      corrupt_(false),
      index_{} {}

bool WaterPulseTraceFileStore::begin() {
    ready_ = false;
    clearIndexCache();
    if (!path_ || path_[0] != '/' || sampleCapacityPerTrace_ == 0 || sampleCapacityPerTrace_ > UINT16_MAX ||
        maxTraceCount_ == 0 || maxTraceCount_ > kSavedPulseTraceMaxCountLimit) {
        return false;
    }
    ready_ = true;
    return true;
}

bool WaterPulseTraceFileStore::save(const WaterPulseTrace& trace,
                                    const WaterPulseTraceSample* samples,
                                    std::size_t sampleCount,
                                    std::uint32_t* savedTraceId,
                                    WaterPulseTraceSaveStatus* status) {
    if (savedTraceId) {
        *savedTraceId = 0;
    }
    if (status) {
        *status = WaterPulseTraceSaveStatus::Ok;
    }
    if (!ready() || !samples || sampleCount == 0 || sampleCount > sampleCapacityPerTrace_ ||
        sampleCount != trace.sampleCount) {
        if (status) {
            *status = ready() ? WaterPulseTraceSaveStatus::InvalidInput : WaterPulseTraceSaveStatus::NotReady;
        }
        return false;
    }
    if (!loadIndex()) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::CorruptStore;
        }
        return false;
    }
    const std::size_t existing = findSlotByRecord(trace.record);
    if (existing < maxTraceCount_) {
        if (savedTraceId) {
            *savedTraceId = index_[existing].key;
        }
        if (status) {
            *status = WaterPulseTraceSaveStatus::AlreadyExists;
        }
        return true;
    }
    const std::size_t slot = findFreeSlot();
    if (slot >= maxTraceCount_) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::LimitReached;
        }
        return false;
    }
    const std::uint32_t key = keyForNewRecord(trace.record);
    if (key == 0) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::InvalidInput;
        }
        return false;
    }
    if (!ensureFileForWrite()) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::WriteFailed;
        }
        return false;
    }
    const IndexEntry blank{};
    if (!writeIndexEntry(slot, blank)) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::WriteFailed;
        }
        return false;
    }

    const std::size_t sampleBytes = sampleCount * sizeof(WaterPulseTraceSample);
    if (!writeOrAppendAt(sampleSlotOffset(slot),
                         reinterpret_cast<const std::uint8_t*>(samples),
                         sampleBytes)) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::WriteFailed;
        }
        return false;
    }

    IndexEntry entry{};
    entry.key = key;
    entry.sequence = nextSequence();
    entry.record = trace.record;
    entry.sampleCount = static_cast<std::uint32_t>(sampleCount);
    entry.totalPulses = trace.totalPulses;
    entry.actualMl = trace.actualMl;
    entry.sampleChecksum = checksumBytes(samples, sampleBytes);
    entry.finished = trace.finished ? 1 : 0;
    entry.entryChecksum = indexEntryChecksum(entry);
    if (!writeIndexEntry(slot, entry)) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::WriteFailed;
        }
        return false;
    }
    index_[slot] = entry;
    filePresent_ = true;
    if (savedTraceId) {
        *savedTraceId = key;
    }
    return true;
}

bool WaterPulseTraceFileStore::remove(std::uint32_t traceId) {
    if (!ready() || traceId == 0) {
        return false;
    }
    if (loadIndex()) {
        const std::size_t slot = findSlotByKey(traceId);
        if (slot < maxTraceCount_) {
            const IndexEntry blank{};
            if (!writeIndexEntry(slot, blank)) {
                return false;
            }
            index_[slot] = blank;
            return true;
        }
    }
    return false;
}

bool WaterPulseTraceFileStore::findById(std::uint32_t traceId, WaterPulseTrace& output) const {
    if (traceId == 0) {
        return false;
    }
    if (loadIndex()) {
        const std::size_t slot = findSlotByKey(traceId);
        if (slot < maxTraceCount_) {
            return populateTraceFromEntry(index_[slot], output);
        }
    }
    return false;
}

bool WaterPulseTraceFileStore::findByRecord(const WaterRecord& record, WaterPulseTrace& output) const {
    if (loadIndex()) {
        const std::size_t slot = findSlotByRecord(record);
        if (slot < maxTraceCount_) {
            return populateTraceFromEntry(index_[slot], output);
        }
    }
    return false;
}

std::size_t WaterPulseTraceFileStore::findByRecords(const WaterRecord* records,
                                                    std::size_t recordCount,
                                                    WaterPulseTrace* output,
                                                    bool* found) const {
    if (found) {
        for (std::size_t i = 0; i < recordCount; ++i) {
            found[i] = false;
        }
    }
    if (!records || !output || !found || recordCount == 0 || !ready()) {
        return 0;
    }
    std::size_t matched = 0;
    if (loadIndex()) {
        for (std::size_t slot = 0; slot < maxTraceCount_ && matched < recordCount; ++slot) {
            const IndexEntry& entry = index_[slot];
            if (entry.key == 0 || entry.entryChecksum != indexEntryChecksum(entry)) {
                continue;
            }
            for (std::size_t i = 0; i < recordCount; ++i) {
                if (found[i] || !sameRecordIdentity(entry.record, records[i])) {
                    continue;
                }
                if (populateTraceFromEntry(entry, output[i])) {
                    found[i] = true;
                    ++matched;
                }
            }
        }
        if (filePresent_) {
            return matched;
        }
    }
    return matched;
}

std::size_t WaterPulseTraceFileStore::readSamples(std::uint32_t traceId,
                                                  WaterPulseTraceSample* output,
                                                  std::size_t outputCapacity) const {
    if (!ready() || !output || outputCapacity == 0 || traceId == 0) {
        return 0;
    }
    if (loadIndex()) {
        const std::size_t slot = findSlotByKey(traceId);
        if (slot < maxTraceCount_) {
            const IndexEntry& entry = index_[slot];
            if (entry.sampleCount == 0 || entry.sampleCount > outputCapacity ||
                entry.sampleCount > sampleCapacityPerTrace_) {
                return 0;
            }
            const std::size_t sampleBytes = static_cast<std::size_t>(entry.sampleCount) * sizeof(WaterPulseTraceSample);
            if (!backend_.readAt(path_, sampleSlotOffset(slot), reinterpret_cast<std::uint8_t*>(output), sampleBytes) ||
                checksumBytes(output, sampleBytes) != entry.sampleChecksum) {
                return 0;
            }
            return entry.sampleCount;
        }
    }
    return 0;
}

bool WaterPulseTraceFileStore::containsRecord(const WaterRecord& record) const {
    WaterPulseTrace trace{};
    return findByRecord(record, trace);
}

WaterPulseTraceFileStats WaterPulseTraceFileStore::stats() const {
    WaterPulseTraceFileStats out{};
    out.maxCount = maxTraceCount_;
    out.maxBytes = expectedFileSize();
    out.sampleCapacityPerTrace = sampleCapacityPerTrace_;
    out.ready = ready();
    if (ready()) {
        loadIndex();
    }
    out.corrupt = corrupt_;
    if (filePresent_) {
        const std::int64_t fileSize = backend_.fileSize(path_);
        out.usedBytes = fileSize > 0 ? static_cast<std::size_t>(fileSize) : 0;
    }
    for (std::size_t i = 0; i < maxTraceCount_; ++i) {
        if (index_[i].key != 0 && index_[i].entryChecksum == indexEntryChecksum(index_[i])) {
            ++out.savedCount;
        }
    }
    return out;
}

std::size_t WaterPulseTraceFileStore::sampleCapacityPerTrace() const {
    return sampleCapacityPerTrace_;
}

bool WaterPulseTraceFileStore::ready() const {
    return ready_ && path_ && path_[0] == '/';
}

std::uint32_t WaterPulseTraceFileStore::keyForRecord(const WaterRecord& record) const {
    std::uint32_t hash = 2166136261UL;
    hashRecordField(hash, record.startTime);
    hashRecordField(hash, record.volumeMl);
    hashRecordField(hash, record.targetValue);
    hashRecordField(hash, record.pulseCount);
    hashRecordField(hash, record.rejectedPulseCount);
    hashRecordField(hash, record.durationSec);
    hashRecordField(hash, static_cast<std::uint32_t>(record.mode));
    hashRecordField(hash, static_cast<std::uint32_t>(record.result));
    hashRecordField(hash, record.selectedPreset);
    if (hash == 0) {
        hash = 1;
    }
    return hash;
}

std::uint32_t WaterPulseTraceFileStore::keyForRecordProbe(const WaterRecord& record, std::size_t probe) const {
    std::uint32_t key = keyForRecord(record);
    for (std::size_t i = 0; i < probe; ++i) {
        hashRecordField(key, static_cast<std::uint32_t>(i + 1));
    }
    if (key == 0) {
        key = 1;
    }
    return key;
}

std::uint32_t WaterPulseTraceFileStore::keyForNewRecord(const WaterRecord& record) const {
    for (std::size_t probe = 0; probe < maxTraceCount_; ++probe) {
        const std::uint32_t key = keyForRecordProbe(record, probe);
        const std::size_t slot = findSlotByKey(key);
        if (slot >= maxTraceCount_ || sameRecordIdentity(index_[slot].record, record)) {
            return key;
        }
    }
    return 0;
}

bool WaterPulseTraceFileStore::loadIndex() const {
    if (indexLoaded_) {
        return indexValid_;
    }
    clearIndexCache();
    indexLoaded_ = true;
    if (!ready()) {
        return false;
    }
    if (!backend_.exists(path_)) {
        indexValid_ = true;
        return true;
    }
    filePresent_ = true;
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < 0 || fileSize > static_cast<std::int64_t>(expectedFileSize())) {
        corrupt_ = true;
        return false;
    }
    if (fileSize < static_cast<std::int64_t>(sizeof(SavedTraceFileHeader))) {
        if (filePrefixAllZero(static_cast<std::size_t>(fileSize))) {
            filePresent_ = false;
            indexValid_ = true;
            return true;
        }
        corrupt_ = true;
        return false;
    }
    SavedTraceFileHeader header{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
        corrupt_ = true;
        return false;
    }
    if (header.magic != kSavedTraceFileMagic || header.version != kSavedTraceFileVersion ||
        header.headerSize != sizeof(SavedTraceFileHeader) || header.indexEntrySize != sizeof(IndexEntry) ||
        header.sampleSize != sizeof(WaterPulseTraceSample) || header.slotCount != maxTraceCount_ ||
        header.sampleCapacity != sampleCapacityPerTrace_ || header.fileSize != expectedFileSize() ||
        header.headerChecksum != checksumHeader(header)) {
        if (allZeroBytes(&header, sizeof(header))) {
            filePresent_ = false;
            indexValid_ = true;
            return true;
        }
        corrupt_ = true;
        return false;
    }
    if (fileSize < static_cast<std::int64_t>(minimumFileSize())) {
        corrupt_ = true;
        return false;
    }
    indexValid_ = true;
    const std::size_t indexBytes = maxTraceCount_ * sizeof(IndexEntry);
    if (!backend_.readAt(path_, indexOffset(0), reinterpret_cast<std::uint8_t*>(index_), indexBytes)) {
        corrupt_ = true;
        indexValid_ = false;
        return false;
    }
    for (std::size_t i = 0; i < maxTraceCount_; ++i) {
        const IndexEntry entry = index_[i];
        if (entry.key == 0) {
            continue;
        }
        const std::size_t sampleBytes = static_cast<std::size_t>(entry.sampleCount) * sizeof(WaterPulseTraceSample);
        if (entry.sampleCount == 0 || entry.sampleCount > sampleCapacityPerTrace_ ||
            sampleSlotOffset(i) + sampleBytes > static_cast<std::size_t>(fileSize) ||
            entry.entryChecksum != indexEntryChecksum(entry)) {
            corrupt_ = true;
            index_[i] = IndexEntry{};
            continue;
        }
    }
    return true;
}

void WaterPulseTraceFileStore::clearIndexCache() const {
    indexLoaded_ = false;
    indexValid_ = false;
    filePresent_ = false;
    corrupt_ = false;
    for (std::size_t i = 0; i < kSavedPulseTraceMaxCountLimit; ++i) {
        index_[i] = IndexEntry{};
    }
}

bool WaterPulseTraceFileStore::ensureFileForWrite() {
    if (!ready() || !indexValid_) {
        return false;
    }
    if (filePresent_) {
        return true;
    }
    if (!backend_.createSized(path_, minimumFileSize())) {
        return false;
    }
    if (!writeHeader()) {
        backend_.removeFile(path_);
        return false;
    }
    filePresent_ = true;
    return true;
}

bool WaterPulseTraceFileStore::writeHeader() {
    SavedTraceFileHeader header{};
    header.magic = kSavedTraceFileMagic;
    header.version = kSavedTraceFileVersion;
    header.headerSize = sizeof(SavedTraceFileHeader);
    header.indexEntrySize = sizeof(IndexEntry);
    header.sampleSize = sizeof(WaterPulseTraceSample);
    header.slotCount = static_cast<std::uint16_t>(maxTraceCount_);
    header.sampleCapacity = static_cast<std::uint16_t>(sampleCapacityPerTrace_);
    header.fileSize = static_cast<std::uint32_t>(expectedFileSize());
    header.headerChecksum = checksumHeader(header);
    return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header));
}

bool WaterPulseTraceFileStore::writeIndexEntry(std::size_t slot, const IndexEntry& entry) {
    return slot < maxTraceCount_ &&
           backend_.writeAt(path_, indexOffset(slot), reinterpret_cast<const std::uint8_t*>(&entry), sizeof(entry));
}

std::uint32_t WaterPulseTraceFileStore::indexEntryChecksum(IndexEntry entry) const {
    entry.entryChecksum = 0;
    return checksumBytes(&entry, sizeof(entry));
}

bool WaterPulseTraceFileStore::populateTraceFromEntry(const IndexEntry& entry, WaterPulseTrace& output) const {
    if (entry.key == 0 || entry.sampleCount == 0 || entry.sampleCount > sampleCapacityPerTrace_ ||
        entry.entryChecksum != indexEntryChecksum(entry)) {
        return false;
    }
    output = WaterPulseTrace{};
    output.traceId = entry.key;
    output.startTime = entry.record.startTime;
    output.record = entry.record;
    output.sampleStart = 0;
    output.sampleCount = entry.sampleCount;
    output.totalPulses = entry.totalPulses;
    output.actualMl = entry.actualMl;
    output.finished = entry.finished != 0;
    return true;
}

std::size_t WaterPulseTraceFileStore::findSlotByKey(std::uint32_t key) const {
    if (key == 0) {
        return maxTraceCount_;
    }
    for (std::size_t i = 0; i < maxTraceCount_; ++i) {
        if (index_[i].key == key && index_[i].entryChecksum == indexEntryChecksum(index_[i])) {
            return i;
        }
    }
    return maxTraceCount_;
}

std::size_t WaterPulseTraceFileStore::findSlotByRecord(const WaterRecord& record) const {
    for (std::size_t probe = 0; probe < maxTraceCount_; ++probe) {
        const std::uint32_t key = keyForRecordProbe(record, probe);
        const std::size_t slot = findSlotByKey(key);
        if (slot < maxTraceCount_ && sameRecordIdentity(index_[slot].record, record)) {
            return slot;
        }
    }
    return maxTraceCount_;
}

std::size_t WaterPulseTraceFileStore::findFreeSlot() const {
    for (std::size_t i = 0; i < maxTraceCount_; ++i) {
        if (index_[i].key == 0) {
            return i;
        }
    }
    return maxTraceCount_;
}

std::uint32_t WaterPulseTraceFileStore::nextSequence() const {
    std::uint32_t sequence = 0;
    for (std::size_t i = 0; i < maxTraceCount_; ++i) {
        if (index_[i].key != 0 && index_[i].entryChecksum == indexEntryChecksum(index_[i])) {
            sequence = std::max(sequence, index_[i].sequence);
        }
    }
    ++sequence;
    return sequence == 0 ? 1 : sequence;
}

std::size_t WaterPulseTraceFileStore::minimumFileSize() const {
    return sizeof(SavedTraceFileHeader) + maxTraceCount_ * sizeof(IndexEntry);
}

std::size_t WaterPulseTraceFileStore::expectedFileSize() const {
    return minimumFileSize() + maxTraceCount_ * sampleCapacityPerTrace_ * sizeof(WaterPulseTraceSample);
}

std::size_t WaterPulseTraceFileStore::indexOffset(std::size_t slot) const {
    return sizeof(SavedTraceFileHeader) + slot * sizeof(IndexEntry);
}

std::size_t WaterPulseTraceFileStore::sampleSlotOffset(std::size_t slot) const {
    return minimumFileSize() + slot * sampleCapacityPerTrace_ * sizeof(WaterPulseTraceSample);
}

bool WaterPulseTraceFileStore::extendFileTo(std::size_t size) {
    if (size > expectedFileSize()) {
        return false;
    }
    std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < 0 || fileSize > static_cast<std::int64_t>(expectedFileSize())) {
        return false;
    }
    std::size_t currentSize = static_cast<std::size_t>(fileSize);
    if (currentSize >= size) {
        return true;
    }

    std::uint8_t zeros[256]{};
    while (currentSize < size) {
        const std::size_t chunk = std::min<std::size_t>(sizeof(zeros), size - currentSize);
        if (!backend_.appendBytes(path_, zeros, chunk)) {
            return false;
        }
        currentSize += chunk;
    }
    return backend_.fileSize(path_) == static_cast<std::int64_t>(size);
}

bool WaterPulseTraceFileStore::writeOrAppendAt(std::size_t offset, const std::uint8_t* data, std::size_t len) {
    if (!data && len > 0) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (offset > expectedFileSize() || len > expectedFileSize() - offset || !extendFileTo(offset)) {
        return false;
    }

    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(offset)) {
        return false;
    }
    const std::size_t currentSize = static_cast<std::size_t>(fileSize);
    if (currentSize >= offset + len) {
        return backend_.writeAt(path_, offset, data, len);
    }

    const std::size_t overwriteBytes = currentSize > offset ? currentSize - offset : 0;
    if (overwriteBytes > 0 && !backend_.writeAt(path_, offset, data, overwriteBytes)) {
        return false;
    }
    return backend_.appendBytes(path_, data + overwriteBytes, len - overwriteBytes);
}

bool WaterPulseTraceFileStore::filePrefixAllZero(std::size_t size) const {
    if (size == 0) {
        return true;
    }
    std::uint8_t buffer[64]{};
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t chunk = std::min<std::size_t>(sizeof(buffer), size - offset);
        if (!backend_.readAt(path_, offset, buffer, chunk) || !allZeroBytes(buffer, chunk)) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace&,
                                     const WaterPulseTraceSample* samples,
                                     std::size_t sampleCount,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity) {
    if (!samples || !buckets || bucketCapacity == 0 || sampleCount == 0) {
        return 0;
    }
    if (bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 4 && bucketSeconds != 5) {
        bucketSeconds = 1;
    }
    std::size_t bucketCount = 0;
    std::uint32_t cumulative = 0;
    for (std::size_t i = 0; i < sampleCount && bucketCount < bucketCapacity;) {
        WaterPulseTraceBucket bucket{};
        bucket.startSec = static_cast<std::uint32_t>(i);
        bucket.durationSec = bucketSeconds;
        bucket.state = samples[i].state;
        for (std::uint32_t j = 0; j < bucketSeconds && i < sampleCount; ++j, ++i) {
            bucket.pulseDelta += samples[i].pulseDelta;
            bucket.state = mergeState(bucket.state, samples[i].state);
        }
        cumulative += bucket.pulseDelta;
        bucket.cumulativePulses = cumulative;
        buckets[bucketCount++] = bucket;
    }
    return bucketCount;
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace&,
                                               const WaterPulseTraceSample* samples,
                                               std::size_t sampleCount) {
    WaterPulseTraceAnalysis out{};
    if (!samples || sampleCount < 6) {
        return out;
    }
    std::uint32_t runningCount = 0;
    std::uint32_t runningTotal = 0;
    for (std::size_t i = sampleCount / 2; i < sampleCount; ++i) {
        if (samples[i].state == WaterPulseTraceState::Running && samples[i].pulseDelta > 0) {
            ++runningCount;
            runningTotal += samples[i].pulseDelta;
        }
    }
    if (runningCount < 3) {
        return out;
    }
    const float stableRate = static_cast<float>(runningTotal) / static_cast<float>(runningCount);
    const std::uint32_t stableFloor = roundU32(stableRate);
    if (stableFloor == 0) {
        return out;
    }
    constexpr std::size_t kWindow = 4;
    for (std::size_t i = 0; i + kWindow <= sampleCount; ++i) {
        if (samples[i].state != WaterPulseTraceState::Running || samples[i].pulseDelta < stableFloor) {
            continue;
        }
        std::uint32_t total = 0;
        std::uint32_t minValue = UINT32_MAX;
        std::uint32_t maxValue = 0;
        bool allRunning = true;
        for (std::size_t j = 0; j < kWindow; ++j) {
            if (samples[i + j].state != WaterPulseTraceState::Running) {
                allRunning = false;
                break;
            }
            const std::uint32_t value = samples[i + j].pulseDelta;
            total += value;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (!allRunning || minValue == 0) {
            continue;
        }
        const float avg = static_cast<float>(total) / static_cast<float>(kWindow);
        if (std::fabs(avg - stableRate) <= std::max(1.0f, stableRate * 0.25f) &&
            static_cast<float>(maxValue - minValue) <= std::max(1.0f, stableRate * 0.4f)) {
            out.stable = true;
            out.stableStartSec = static_cast<std::uint32_t>(i);
            for (std::size_t k = 0; k < i; ++k) {
                if (samples[k].state == WaterPulseTraceState::Running) {
                    out.startupPulseCount += samples[k].pulseDelta;
                }
            }
            std::uint32_t stableSeconds = 0;
            for (std::size_t k = i; k < sampleCount; ++k) {
                if (samples[k].state == WaterPulseTraceState::Running) {
                    out.stablePulseCount += samples[k].pulseDelta;
                    ++stableSeconds;
                }
            }
            out.stablePulsePerSec =
                stableSeconds == 0 ? 0.0f : static_cast<float>(out.stablePulseCount) / static_cast<float>(stableSeconds);
            out.confidence = static_cast<std::uint8_t>(std::min<std::size_t>(100, 40 + stableSeconds * 5));
            return out;
        }
    }
    return out;
}

bool computeSegmentedCalibration(const SegmentedCalibrationSample* samples,
                                 std::size_t sampleCount,
                                 SegmentedCalibrationResult& result) {
    result = SegmentedCalibrationResult{};
    if (!samples || sampleCount < 2) {
        return false;
    }
    const SegmentedCalibrationSample* low = nullptr;
    const SegmentedCalibrationSample* high = nullptr;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        if (samples[i].actualMl == 0 || samples[i].stablePulseCount == 0 || samples[i].totalPulses == 0) {
            continue;
        }
        if (!low || samples[i].actualMl < low->actualMl) {
            low = &samples[i];
        }
        if (!high || samples[i].actualMl > high->actualMl) {
            high = &samples[i];
        }
    }
    if (!low || !high || low == high || high->actualMl <= low->actualMl + 500UL ||
        high->stablePulseCount <= low->stablePulseCount) {
        return false;
    }
    const float stablePulsePerMl =
        static_cast<float>(high->stablePulseCount - low->stablePulseCount) /
        static_cast<float>(high->actualMl - low->actualMl);
    if (!(stablePulsePerMl > 0.0f)) {
        return false;
    }
    const float startupVolumeMl =
        static_cast<float>(low->actualMl) - static_cast<float>(low->stablePulseCount) / stablePulsePerMl;
    if (!(startupVolumeMl > 0.0f)) {
        return false;
    }
    result.valid = true;
    result.startupDurationSec = low->startupDurationSec;
    result.startupPulseCount = low->startupPulseCount;
    result.startupVolumeMl = roundU32(startupVolumeMl);
    result.startupPulsePerLiter =
        result.startupVolumeMl == 0
            ? 0
            : roundU32(static_cast<float>(result.startupPulseCount) * 1000.0f /
                       static_cast<float>(result.startupVolumeMl));
    result.stablePulsePerLiter = roundU32(stablePulsePerMl * 1000.0f);
    result.overallPulsePerLiter =
        roundU32(static_cast<float>(high->totalPulses) * 1000.0f / static_cast<float>(high->actualMl));
    return true;
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace, const WaterPulseTraceStore& store) {
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!samples) {
        return WaterPulseTraceAnalysis{};
    }
    for (std::size_t i = 0; i < trace.sampleCount; ++i) {
        const WaterPulseTraceSample* sample = store.sampleAt(trace, i);
        samples[i] = sample ? *sample : WaterPulseTraceSample{};
    }
    const WaterPulseTraceAnalysis result = analyzeWaterPulseTrace(trace, samples, trace.sampleCount);
    delete[] samples;
    return result;
}

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceStore& store,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity) {
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!samples) {
        return 0;
    }
    for (std::size_t i = 0; i < trace.sampleCount; ++i) {
        const WaterPulseTraceSample* sample = store.sampleAt(trace, i);
        samples[i] = sample ? *sample : WaterPulseTraceSample{};
    }
    const std::size_t result =
        aggregateWaterPulseTrace(trace, samples, trace.sampleCount, bucketSeconds, buckets, bucketCapacity);
    delete[] samples;
    return result;
}

}  // namespace faucet
