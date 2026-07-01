#include "app/CalibrationSessionStore.h"

#include <cstring>
#include <memory>
#include <new>

namespace faucet {
namespace {

constexpr std::uint32_t kSessionMagic = 0x46435353UL;  // FCSS
constexpr std::uint16_t kSessionVersion = 2;

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

std::unique_ptr<SessionFile> allocateSessionFile() {
    return std::unique_ptr<SessionFile>(new (std::nothrow) SessionFile);
}

void fillFile(SessionFile& file, const CalibrationSessionRecord* session) {
    std::memset(&file, 0, sizeof(file));
    file.magic = kSessionMagic;
    file.version = kSessionVersion;
    file.recordSize = static_cast<std::uint16_t>(sizeof(CalibrationSessionRecord));
    if (session) {
        file.session = *session;
    }
    file.checksum = checksumBytes(reinterpret_cast<const std::uint8_t*>(&file.session), sizeof(file.session));
}

bool validFile(const SessionFile& file) {
    return file.magic == kSessionMagic && file.version == kSessionVersion &&
           file.recordSize == sizeof(CalibrationSessionRecord) &&
           file.checksum == checksumBytes(reinterpret_cast<const std::uint8_t*>(&file.session), sizeof(file.session));
}

bool writeSessionFile(WaterRecordFileBackend& backend, const char* path, const CalibrationSessionRecord* session) {
    const auto encoded = allocateSessionFile();
    if (!encoded) {
        return false;
    }
    fillFile(*encoded, session);
    return backend.writeAt(path, 0, reinterpret_cast<const std::uint8_t*>(encoded.get()), sizeof(*encoded));
}

bool createEmptySessionFile(WaterRecordFileBackend& backend, const char* path) {
    return backend.createSized(path, sizeof(SessionFile)) && writeSessionFile(backend, path, nullptr);
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
    if (!backend_.exists(path_)) {
        ready_ = createEmptySessionFile(backend_, path_);
        status_ = ready_ ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return ready_;
    }
    if (backend_.fileSize(path_) < static_cast<std::int64_t>(sizeof(SessionFile))) {
        ready_ = backend_.removeFile(path_) && createEmptySessionFile(backend_, path_);
        status_ = ready_ ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return ready_;
    }
    const auto decoded = allocateSessionFile();
    if (!decoded) {
        status_ = AppStorageStatus::BackendFailure;
        return false;
    }
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(decoded.get()), sizeof(*decoded))) {
        status_ = AppStorageStatus::BackendFailure;
        return false;
    }
    if (!validFile(*decoded)) {
        ready_ = writeSessionFile(backend_, path_, nullptr);
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
    const auto decoded = allocateSessionFile();
    if (!decoded ||
        !backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(decoded.get()), sizeof(*decoded)) ||
        !validFile(*decoded)) {
        return false;
    }
    output = decoded->session;
    return true;
}

bool CalibrationSessionFileStore::save(const CalibrationSessionRecord& session) {
    if (!ready()) {
        return false;
    }
    return writeSessionFile(backend_, path_, &session);
}

bool CalibrationSessionFileStore::clear() {
    return ready() && writeSessionFile(backend_, path_, nullptr);
}

const char* CalibrationSessionFileStore::storageName() const {
    return "calibration-session-file";
}

}  // namespace faucet
