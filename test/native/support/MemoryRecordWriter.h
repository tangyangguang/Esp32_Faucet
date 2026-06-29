#pragma once

#include "app/WaterRecordStore.h"

#include <vector>

namespace faucet_test {

class MemoryRecordWriter : public faucet::WaterRecordWriter {
public:
    bool ok = true;
    std::vector<faucet::WaterRecord> records;

    bool append(const faucet::WaterRecord& record) override {
        if (!ok) {
            return false;
        }
        records.push_back(record);
        return true;
    }
};

}  // namespace faucet_test
