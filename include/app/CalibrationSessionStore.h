#pragma once

#include "app/AppStorageStatus.h"
#include "app/CalibrationSession.h"
#include "app/WaterRecordFileStore.h"

namespace faucet {

class CalibrationSessionFileStore {
public:
    CalibrationSessionFileStore(WaterRecordFileBackend& backend, const char* path);

    bool begin();
    bool ready() const;
    AppStorageStatus status() const;
    bool load(CalibrationSessionRecord& output) const;
    bool save(const CalibrationSessionRecord& session);
    bool clear();
    const char* storageName() const;

private:
    WaterRecordFileBackend& backend_;
    const char* path_;
    bool ready_;
    AppStorageStatus status_;
};

}  // namespace faucet
