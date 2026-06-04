#include "app/CalibrationSessionStore.h"

#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kSessionMagic = 0x46435353UL;  // FCSS
constexpr std::uint16_t kSessionVersion = 1;

struct SessionFile {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t recordSize;
    CalibrationSessionRecord session;
    std::uint32_t checksum;
};

std::uint32_t checksumBytes(const std::uint8_t* data, std::size_t len) {
    std::uint32_t hash = 2166136261UL;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619UL;
    }
    return hash == 0 ? 1 : hash;
}

bool validPath(const char* path) {
    return path && path[0] == '/';
}

SessionFile makeFile(const CalibrationSessionRecord& session) {
    SessionFile file{};
    file.magic = kSessionMagic;
    file.version = kSessionVersion;
    file.recordSize = static_cast<std::uint16_t>(sizeof(CalibrationSessionRecord));
    file.session = session;
    file.checksum = checksumBytes(reinterpret_cast<const std::uint8_t*>(&file.session), sizeof(file.session));
    return file;
}

bool validFile(const SessionFile& file) {
    return file.magic == kSessionMagic && file.version == kSessionVersion &&
           file.recordSize == sizeof(CalibrationSessionRecord) &&
           file.checksum == checksumBytes(reinterpret_cast<const std::uint8_t*>(&file.session), sizeof(file.session));
}

}  // namespace

CalibrationSessionFileStore::CalibrationSessionFileStore(WaterRecordFileBackend& backend, const char* path)
    : backend_(backend), path_(path), ready_(false) {}

bool CalibrationSessionFileStore::begin() {
    ready_ = false;
    if (!validPath(path_)) {
        return false;
    }
    if (!backend_.exists(path_)) {
        SessionFile empty = makeFile(CalibrationSessionRecord{});
        if (!backend_.createSized(path_, sizeof(SessionFile)) ||
            !backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&empty), sizeof(empty))) {
            return false;
        }
        ready_ = true;
        return true;
    }
    if (backend_.fileSize(path_) != static_cast<std::int64_t>(sizeof(SessionFile))) {
        return false;
    }
    SessionFile file{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&file), sizeof(file)) || !validFile(file)) {
        return false;
    }
    ready_ = true;
    return true;
}

bool CalibrationSessionFileStore::ready() const {
    return ready_ && backend_.exists(path_);
}

bool CalibrationSessionFileStore::load(CalibrationSessionRecord& output) const {
    if (!ready()) {
        return false;
    }
    SessionFile file{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&file), sizeof(file)) || !validFile(file)) {
        return false;
    }
    output = file.session;
    return true;
}

bool CalibrationSessionFileStore::save(const CalibrationSessionRecord& session) {
    if (!ready()) {
        return false;
    }
    const SessionFile file = makeFile(session);
    return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&file), sizeof(file));
}

bool CalibrationSessionFileStore::clear() {
    return save(CalibrationSessionRecord{});
}

const char* CalibrationSessionFileStore::storageName() const {
    return "calibration-session-file";
}

}  // namespace faucet
