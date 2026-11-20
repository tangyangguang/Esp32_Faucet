#ifndef NATIVE_BUILD

#include "app/Esp32BaseWaterRecordBackend.h"

#include <Esp32Base.h>
#include <FS.h>
#include <LittleFS.h>

#include <algorithm>
#include <limits>

namespace faucet {
namespace {

constexpr std::size_t kCreateSizedDirectChunk = 512;

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

}  // namespace faucet

#endif
