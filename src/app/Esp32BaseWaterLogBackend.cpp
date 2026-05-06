#ifndef NATIVE_BUILD

#include "app/Esp32BaseWaterLogBackend.h"

#include <Esp32Base.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace faucet {

bool Esp32BaseWaterLogBackend::exists(const char* path) {
    return Esp32BaseFs::exists(path);
}

std::int64_t Esp32BaseWaterLogBackend::fileSize(const char* path) {
    return Esp32BaseFs::fileSize(path);
}

bool Esp32BaseWaterLogBackend::createSized(const char* path, std::size_t size) {
    if (!path || size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    if (!Esp32BaseFs::writeBytes(path, nullptr, 0)) {
        return false;
    }

    std::uint8_t zeros[256]{};
    std::size_t remaining = size;
    while (remaining > 0) {
        const std::size_t chunk = std::min<std::size_t>(remaining, sizeof(zeros));
        if (!Esp32BaseFs::appendBytes(path, zeros, chunk)) {
            return false;
        }
        remaining -= chunk;
    }
    return Esp32BaseFs::fileSize(path) == static_cast<std::int64_t>(size);
}

bool Esp32BaseWaterLogBackend::appendBytes(const char* path, const std::uint8_t* data, std::size_t len) {
    return Esp32BaseFs::appendBytes(path, data, len);
}

bool Esp32BaseWaterLogBackend::readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) {
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    std::size_t readLen = 0;
    return Esp32BaseFs::readBytesAt(path, static_cast<std::uint32_t>(offset), out, len, &readLen) && readLen == len;
}

bool Esp32BaseWaterLogBackend::writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) {
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    return Esp32BaseFs::writeBytesAt(path, static_cast<std::uint32_t>(offset), data, len);
}

bool Esp32BaseWaterLogBackend::removeFile(const char* path) {
    return Esp32BaseFs::removeFile(path);
}

}  // namespace faucet

#endif
