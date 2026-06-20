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
    : backend_(backend), path_(path), ready_(false), status_(AppStorageStatus::Unavailable) {}

bool CalibrationSessionFileStore::begin() {
    ready_ = false;
    status_ = AppStorageStatus::Unavailable;
    if (!validPath(path_)) {
        status_ = AppStorageStatus::InvalidPath;
        return false;
    }
    const auto initializeEmpty = [this]() {
        if (!backend_.createSized(path_, sizeof(SessionFile))) {
            return false;
        }
        SessionFile empty = makeFile(CalibrationSessionRecord{});
        return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&empty), sizeof(empty));
    };
    const auto writeEmpty = [this]() {
        SessionFile empty = makeFile(CalibrationSessionRecord{});
        return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&empty), sizeof(empty));
    };
    const auto rebuildEmpty = [this, &initializeEmpty]() {
        if (backend_.exists(path_) && !backend_.removeFile(path_)) {
            return false;
        }
        return initializeEmpty();
    };
    if (!backend_.exists(path_)) {
        ready_ = initializeEmpty();
        status_ = ready_ ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return ready_;
    }
    SessionFile file{};
    if (backend_.fileSize(path_) < static_cast<std::int64_t>(sizeof(SessionFile))) {
        ready_ = rebuildEmpty();
        status_ = ready_ ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return ready_;
    }
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&file), sizeof(file))) {
        status_ = AppStorageStatus::BackendFailure;
        return false;
    }
    if (!validFile(file)) {
        ready_ = writeEmpty();
        status_ = ready_ ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return ready_;
    }
    ready_ = true;
    status_ = AppStorageStatus::Ready;
    return true;
}

bool CalibrationSessionFileStore::ready() const {
    return ready_ && backend_.exists(path_);
}

AppStorageStatus CalibrationSessionFileStore::status() const {
    if (ready_ && !backend_.exists(path_)) {
        return AppStorageStatus::Missing;
    }
    return status_;
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
