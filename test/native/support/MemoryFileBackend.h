#pragma once

#include "app/WaterRecordStore.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace faucet_test {

class MemoryFileBackend : public faucet::WaterRecordFileBackend {
public:
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::size_t createSizedCalls = 0;
    std::size_t readCalls = 0;
    std::size_t writeCalls = 0;
    std::size_t removeCalls = 0;
    bool failAppend = false;
    bool failRead = false;
    bool failWrite = false;
    bool failWriteAt = false;
    bool failHeaderWriteOnce = false;
    bool writeAtExtends = true;

    bool exists(const char* path) override {
        return files.find(key(path)) != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(key(path));
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        ++createSizedCalls;
        if (!path || failWrite) {
            return false;
        }
        files[path] = std::vector<std::uint8_t>(size, 0);
        return true;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        if (!path || (!data && len > 0) || failAppend || failWrite) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        const std::size_t oldSize = file.size();
        file.resize(oldSize + len, 0);
        if (len > 0) {
            std::memcpy(file.data() + oldSize, data, len);
        }
        return true;
    }

    bool readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) override {
        ++readCalls;
        if (!out || failRead) {
            return false;
        }
        const auto it = files.find(key(path));
        if (it == files.end() || offset + len > it->second.size()) {
            return false;
        }
        std::memcpy(out, it->second.data() + offset, len);
        return true;
    }

    bool writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) override {
        ++writeCalls;
        if (failHeaderWriteOnce && offset == 0) {
            failHeaderWriteOnce = false;
            return false;
        }
        if (!path || (!data && len > 0) || failWrite || failWriteAt) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        if (!writeAtExtends && offset + len > file.size()) {
            return false;
        }
        if (offset + len > file.size()) {
            file.resize(offset + len, 0);
        }
        if (len > 0) {
            std::memcpy(file.data() + offset, data, len);
        }
        return true;
    }

    bool removeFile(const char* path) override {
        ++removeCalls;
        files.erase(key(path));
        return true;
    }

    void overwriteByte(const char* path, std::size_t offset, std::uint8_t value) {
        std::vector<std::uint8_t>& file = files[key(path)];
        if (offset >= file.size()) {
            file.resize(offset + 1, 0);
        }
        file[offset] = value;
    }

private:
    static std::string key(const char* path) {
        return path ? path : "";
    }
};

}  // namespace faucet_test
