#pragma once

#ifndef NATIVE_BUILD

#include "app/WaterRecordFileStore.h"

namespace faucet {

class Esp32BaseWaterRecordBackend : public WaterRecordFileBackend {
public:
    bool exists(const char* path) override;
    std::int64_t fileSize(const char* path) override;
    bool createSized(const char* path, std::size_t size) override;
    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override;
    bool readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) override;
    bool writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) override;
    bool removeFile(const char* path) override;
};

}  // namespace faucet

#endif
