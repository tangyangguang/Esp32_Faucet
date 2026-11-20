#ifndef NATIVE_BUILD

#include "app/Esp32BaseWaterRecordBackend.h"

#include <Esp32Base.h>
#include <FS.h>
#include <LittleFS.h>

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace faucet {
namespace {

constexpr std::size_t kCreateSizedDirectChunk = 512;

bool littleFsVfsPath(const char* path, char (&mountedPath)[112]) {
    if (!path || path[0] != '/') {
        return false;
    }
    const int length = std::snprintf(mountedPath, sizeof(mountedPath), "/littlefs%s", path);
    return length > 0 && static_cast<std::size_t>(length) < sizeof(mountedPath);
}

bool createSizedDirectly(const char* path, std::size_t size) {
    if (!path) {
        return false;
    }
    if (Esp32BaseFs::exists(path) && Esp32BaseFs::fileSize(path) == static_cast<std::int64_t>(size)) {
        return true;
    }
    if (Esp32BaseFs::exists(path) && !Esp32BaseFs::removeFile(path)) {
        return false;
    }
    File file = LittleFS.open(path, FILE_WRITE, true);
    if (!file) {
        return false;
    }
    std::uint8_t zeros[kCreateSizedDirectChunk]{};
    std::size_t remaining = size;
    bool ok = true;
    while (remaining > 0) {
        const std::size_t chunk = std::min<std::size_t>(remaining, sizeof(zeros));
        if (file.write(zeros, chunk) != chunk) {
            ok = false;
            break;
        }
        remaining -= chunk;
    }
    file.flush();
    file.close();
    if (!ok) {
        Esp32BaseFs::removeFile(path);
        return false;
    }
    if (Esp32BaseFs::fileSize(path) != static_cast<std::int64_t>(size)) {
        Esp32BaseFs::removeFile(path);
        return false;
    }
    return true;
}

}  // namespace

bool Esp32BaseWaterRecordBackend::exists(const char* path) {
    return Esp32BaseFs::exists(path);
}

std::int64_t Esp32BaseWaterRecordBackend::fileSize(const char* path) {
    return Esp32BaseFs::fileSize(path);
}

bool Esp32BaseWaterRecordBackend::createSized(const char* path, std::size_t size) {
    if (!path || size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    return createSizedDirectly(path, size);
}

bool Esp32BaseWaterRecordBackend::appendBytes(const char* path, const std::uint8_t* data, std::size_t len) {
    return Esp32BaseFs::appendBytes(path, data, len);
}

bool Esp32BaseWaterRecordBackend::readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) {
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    std::size_t readLen = 0;
    return Esp32BaseFs::readBytesAt(path, static_cast<std::uint32_t>(offset), out, len, &readLen) && readLen == len;
}

bool Esp32BaseWaterRecordBackend::writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) {
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    return Esp32BaseFs::writeBytesAt(path, static_cast<std::uint32_t>(offset), data, len);
}

bool Esp32BaseWaterRecordBackend::removeFile(const char* path) {
    return Esp32BaseFs::removeFile(path);
}

bool Esp32BaseWaterRecordBackend::replaceBytes(const char* path, const std::uint8_t* data, std::size_t len) {
    char mountedPath[112];
    if (!littleFsVfsPath(path, mountedPath)) {
        return false;
    }
    const int fd = ::open(mountedPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return false;
    }
    const bool synced = ::write(fd, data, len) == static_cast<ssize_t>(len) && ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    return synced && closed;
}

bool Esp32BaseWaterRecordBackend::rename(const char* from, const char* to) {
    return Esp32BaseFs::rename(from, to);
}

bool Esp32BaseWaterRecordBackend::truncate(const char* path, std::size_t size) {
    // Esp32Base mounts the Arduino LittleFS instance at its default VFS path.
    char mountedPath[112];
    if (!littleFsVfsPath(path, mountedPath)) {
        return false;
    }
    const int fd = ::open(mountedPath, O_WRONLY);
    if (fd < 0) {
        return false;
    }
    const bool synced = ::ftruncate(fd, size) == 0 && ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    delay(1);
    return synced && closed && Esp32BaseFs::fileSize(path) == static_cast<std::int64_t>(size);
}

}  // namespace faucet

#endif
