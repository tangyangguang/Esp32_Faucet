#ifndef NATIVE_BUILD

#include "app/Esp32BaseWaterRecordBackend.h"

#include <Esp32Base.h>

#include <limits>

namespace faucet {

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
    return Esp32BaseFs::createFixedFile(path, static_cast<std::uint32_t>(size), 0);
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
