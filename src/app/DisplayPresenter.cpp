#include "app/DisplayPresenter.h"

#include "app/TimeUtils.h"

#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

std::uint32_t msFromSeconds(std::uint32_t seconds) {
    return seconds * 1000UL;
}

const char* stateText(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return "Done";
        case WaterResult::StoppedByUser:
            return "Stopped";
        case WaterResult::PauseTimeout:
            return "Pause Timeout";
        case WaterResult::FlowError:
            return "Flow Error";
        case WaterResult::SafetyStopped:
        default:
            return "Safety Stop";
    }
}

void formatLitersValue(char* out, std::size_t len, std::uint32_t ml, bool withUnit) {
    const std::uint32_t centiliters = (ml + 5UL) / 10UL;
    std::snprintf(out, len, withUnit ? "%lu.%02luL" : "%lu.%02lu", static_cast<unsigned long>(centiliters / 100UL),
                  static_cast<unsigned long>(centiliters % 100UL));
}

void formatLiters(char* out, std::size_t len, std::uint32_t ml) {
    formatLitersValue(out, len, ml, true);
}

void formatLitersCompact(char* out, std::size_t len, std::uint32_t ml) {
    const std::uint32_t deciliters = (ml + 50UL) / 100UL;
    std::snprintf(out, len, "%lu.%luL", static_cast<unsigned long>(deciliters / 10UL),
                  static_cast<unsigned long>(deciliters % 10UL));
}

void formatElapsed(char* out, std::size_t len, std::uint32_t seconds) {
    std::snprintf(out, len, "%02lu:%02lu", static_cast<unsigned long>((seconds / 60UL) % 100UL),
                  static_cast<unsigned long>(seconds % 60UL));
}

void copyLine(char (&dest)[kDisplayLineLength], const char* src) {
    std::strncpy(dest, src ? src : "", kDisplayLineLength - 1);
    dest[kDisplayLineLength - 1] = '\0';
}

void formatPulseLabel(char* out, std::size_t len, std::uint32_t pulsePerLiter) {
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (pulsePerLiter == 0) {
        return;
    }
    std::snprintf(out, len, "%luP", static_cast<unsigned long>(pulsePerLiter));
}

void composeTopLine(char (&out)[kDisplayLineLength], const char* left, std::uint32_t pulsePerLiter) {
    constexpr std::size_t kVisibleWidth = kDisplayLineLength - 1;
    char label[8]{};
    formatPulseLabel(label, sizeof(label), pulsePerLiter);
    if (label[0] == '\0') {
        copyLine(out, left);
        return;
    }

    const std::size_t labelLen = std::strlen(label);
    if (labelLen >= kVisibleWidth) {
        copyLine(out, label);
        return;
    }

    const std::size_t leftLimit = kVisibleWidth - labelLen - 1;
    std::size_t leftLen = std::strlen(left ? left : "");
    if (leftLen > leftLimit) {
        leftLen = leftLimit;
    }

    std::memset(out, ' ', kVisibleWidth);
    if (leftLen > 0) {
        std::memcpy(out, left, leftLen);
    }
    std::memcpy(out + kVisibleWidth - labelLen, label, labelLen);
    out[kVisibleWidth] = '\0';
}

bool idleSensorPageAvailable(const AppSnapshot& snapshot) {
    return snapshot.lcdSensorPageEnabled &&
           (snapshot.temperatureSensorEnabled || snapshot.tdsSensorEnabled);
}

void formatTemperature(char* out, std::size_t len, const SensorValue& value) {
    if (!value.valid) {
        std::snprintf(out, len, "--.-");
        return;
    }
    const std::int32_t tenths = value.value >= 0 ? (value.value + 5) / 10 : (value.value - 5) / 10;
    const std::int32_t decimal = tenths % 10 < 0 ? -(tenths % 10) : tenths % 10;
    std::snprintf(out, len, "%ld.%ld", static_cast<long>(tenths / 10), static_cast<long>(decimal));
}

void formatTds(char* out, std::size_t len, const SensorValue& value) {
    if (!value.valid) {
        std::snprintf(out, len, "---");
        return;
    }
    const std::int32_t ppm = value.value < 0 ? 0 : value.value;
    if (ppm < 1000) {
        std::snprintf(out, len, "%03ld", static_cast<long>(ppm));
    } else {
        std::snprintf(out, len, "%ld", static_cast<long>(ppm));
    }
}

void formatVoltage(char* out, std::size_t len, const SensorValue& value) {
    if (!value.valid) {
        std::snprintf(out, len, "--.-");
        return;
    }
    const std::int32_t tenths = value.value >= 0 ? (value.value + 50) / 100 : 0;
    std::snprintf(out, len, "%ld.%ld", static_cast<long>(tenths / 10), static_cast<long>(tenths % 10));
}

DisplayFrame makeIdleSensorFrame(const AppSnapshot& snapshot) {
    char temp[8]{};
    char tds[6]{};
    char vin[8]{};
    formatTemperature(temp, sizeof(temp), snapshot.sensors.temperatureCentiC);
    formatTds(tds, sizeof(tds), snapshot.sensors.tdsPpm);
    formatVoltage(vin, sizeof(vin), snapshot.sensors.inputVoltageMv);
    char line1[kDisplayLineLength]{};
    char line2[kDisplayLineLength]{};
    std::snprintf(line1, sizeof(line1), "T:%sC TDS:%s", temp, tds);
    std::snprintf(line2, sizeof(line2), "VIN:%sV IDLE", vin);
    DisplayFrame frame{DisplayPage::Idle, true, {}, {}};
    copyLine(frame.line1, line1);
    copyLine(frame.line2, line2);
    return frame;
}

}  // namespace

DisplayPresenter::DisplayPresenter(std::uint32_t sleepTimeoutSec)
    : sleepTimeoutMs_(msFromSeconds(sleepTimeoutSec)),
      lastWakeMs_(0),
      idlePageAnchorMs_(0),
      idleSensorPageVisible_(false),
      lastWaterState_(WaterState::Idle) {}

void DisplayPresenter::configure(std::uint32_t sleepTimeoutSec) {
    sleepTimeoutMs_ = msFromSeconds(sleepTimeoutSec);
    idleSensorPageVisible_ = false;
}

void DisplayPresenter::wake(std::uint32_t nowMs) {
    lastWakeMs_ = nowMs;
    idlePageAnchorMs_ = nowMs;
    idleSensorPageVisible_ = false;
}

DisplayFrame DisplayPresenter::render(const AppSnapshot& snapshot, std::uint32_t nowMs) {
    if (snapshot.water.state != WaterState::Idle || lastWaterState_ != WaterState::Idle) {
        idlePageAnchorMs_ = nowMs;
        idleSensorPageVisible_ = false;
    }
    lastWaterState_ = snapshot.water.state;

    if (snapshot.localMode == LocalUiMode::RecordCalibration) {
        char line1[kDisplayLineLength]{};
        char line2[kDisplayLineLength]{};
        char actual[8]{};
        char step[8]{};
        formatLiters(actual, sizeof(actual), snapshot.calibrationActualMl);
        formatLiters(step, sizeof(step), snapshot.calibrationStepMl);
        char left[kDisplayLineLength]{};
        std::snprintf(left, sizeof(left), "A%s", actual);
        composeTopLine(line1, left, snapshot.pulsePerLiter);
        std::snprintf(line2, sizeof(line2), "S%s +/- OK", step);
        return makeFrame(DisplayPage::Calibration, true, line1, line2);
    }

    if (snapshot.localMode == LocalUiMode::Result) {
        char line1[kDisplayLineLength]{};
        char line2[kDisplayLineLength]{};
        char volume[8]{};
        formatLiters(volume, sizeof(volume), snapshot.water.volumeMl);
        char left[kDisplayLineLength]{};
        std::snprintf(left, sizeof(left), "%s %s", stateText(snapshot.water.lastResult), volume);
        composeTopLine(line1, left, snapshot.pulsePerLiter);
        if (snapshot.calibrationReady) {
            std::snprintf(line2, sizeof(line2), "Hold OK Cal");
        } else {
            char elapsed[8]{};
            formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
            std::snprintf(line2, sizeof(line2), "OK Back %s", elapsed);
        }
        return makeFrame(DisplayPage::Result, true, line1, line2);
    }

    if (snapshot.localMode == LocalUiMode::Calibration) {
        char line1[kDisplayLineLength]{};
        char line2[kDisplayLineLength]{};
        char left[kDisplayLineLength]{};
        switch (snapshot.calibrationStatus) {
            case CalibrationSessionStatus::Preparing:
                std::snprintf(left, sizeof(left), "Cal Ready");
                std::snprintf(line2, sizeof(line2), "Cup Ready OK");
                break;
            case CalibrationSessionStatus::WaitingLocalRun:
                std::snprintf(left, sizeof(left), "Cal Sample");
                std::snprintf(line2, sizeof(line2), "OK Start");
                break;
            case CalibrationSessionStatus::Running: {
                char volume[8]{};
                formatLiters(volume, sizeof(volume), snapshot.water.volumeMl);
                std::snprintf(left, sizeof(left), "Cal %s", volume);
                std::snprintf(line2, sizeof(line2), "Cancel Stop");
                break;
            }
            case CalibrationSessionStatus::AwaitingActual:
                std::snprintf(left, sizeof(left), "Actual ml");
                std::snprintf(line2, sizeof(line2), "Input/Cancel");
                break;
            case CalibrationSessionStatus::ReadyToGenerate:
                std::snprintf(left, sizeof(left), "%u Samples", static_cast<unsigned>(snapshot.calibrationValidSampleCount));
                std::snprintf(line2, sizeof(line2), "Generate Web");
                break;
            case CalibrationSessionStatus::Generated:
                std::snprintf(left, sizeof(left), "Scheme Ready");
                std::snprintf(line2, sizeof(line2), "Apply Web");
                break;
            case CalibrationSessionStatus::Applied:
                std::snprintf(left, sizeof(left), "Applied");
                std::snprintf(line2, sizeof(line2), "OK Back");
                break;
            case CalibrationSessionStatus::Failed:
                std::snprintf(left, sizeof(left), "Cal Failed");
                std::snprintf(line2, sizeof(line2), "Cancel Back");
                break;
            case CalibrationSessionStatus::Discarded:
            case CalibrationSessionStatus::Idle:
            default:
                std::snprintf(left, sizeof(left), "Cal");
                std::snprintf(line2, sizeof(line2), "OK Back");
                break;
        }
        composeTopLine(line1, left, snapshot.pulsePerLiter);
        return makeFrame(DisplayPage::Calibration, true, line1, line2);
    }

    if (sleepTimeoutMs_ > 0 && elapsedAtLeast(nowMs, lastWakeMs_, sleepTimeoutMs_) &&
        snapshot.water.state == WaterState::Idle) {
        return makeFrame(DisplayPage::Sleep, false, "", "");
    }

    char line1[kDisplayLineLength]{};
    char line2[kDisplayLineLength]{};
    switch (snapshot.water.state) {
        case WaterState::Idle: {
            if (idleSensorPageAvailable(snapshot)) {
                const std::uint32_t elapsed = nowMs - idlePageAnchorMs_;
                idleSensorPageVisible_ = ((elapsed / 3000UL) % 2UL) == 1UL;
                if (idleSensorPageVisible_) {
                    return makeIdleSensorFrame(snapshot);
                }
            } else {
                idlePageAnchorMs_ = nowMs;
                idleSensorPageVisible_ = false;
            }
            if (snapshot.water.mode == WaterMode::Volume) {
                char target[8]{};
                formatLitersCompact(target, sizeof(target), snapshot.water.targetValue);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "SEL P%u %s", static_cast<unsigned>(snapshot.water.selectedPreset + 1), target);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
            } else {
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "SEL P%u %lus", static_cast<unsigned>(snapshot.water.selectedPreset + 1),
                              static_cast<unsigned long>(snapshot.water.targetValue));
                composeTopLine(line1, left, snapshot.pulsePerLiter);
            }
            char today[8]{};
            formatLiters(today, sizeof(today), snapshot.statistics.todayMl);
            std::snprintf(line2, sizeof(line2), "TODAY %s", today);
            return makeFrame(DisplayPage::Idle, true, line1, line2);
        }
        case WaterState::Confirm:
            if (snapshot.water.mode == WaterMode::Volume) {
                char target[8]{};
                char step[8]{};
                formatLitersCompact(target, sizeof(target), snapshot.water.targetValue);
                formatLiters(step, sizeof(step), snapshot.adjustmentStepMl);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "GO P%u %s", static_cast<unsigned>(snapshot.water.activePreset + 1), target);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "STEP %s", step);
            } else {
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "GO P%u %lus", static_cast<unsigned>(snapshot.water.activePreset + 1),
                              static_cast<unsigned long>(snapshot.water.targetValue));
                composeTopLine(line1, left, snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "STEP %lus", static_cast<unsigned long>(snapshot.timeAdjustmentStepSec));
            }
            return makeFrame(DisplayPage::Confirm, true, line1, line2);
        case WaterState::Running:
            {
                char elapsed[8]{};
                formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "RUN %s", elapsed);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
            }
            if (snapshot.water.mode == WaterMode::Volume) {
                const std::uint32_t remain =
                    snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0;
                char remainText[8]{};
                formatLiters(remainText, sizeof(remainText), remain);
                char out[8]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                std::snprintf(line2, sizeof(line2), "%s LFT %s", out, remainText);
            } else {
                const std::uint32_t remain =
                    snapshot.water.targetValue > snapshot.water.elapsedSec ? snapshot.water.targetValue - snapshot.water.elapsedSec : 0;
                char out[8]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                std::snprintf(line2, sizeof(line2), "%s LFT %lus", out, static_cast<unsigned long>(remain));
            }
            return makeFrame(DisplayPage::Running, true, line1, line2);
        case WaterState::Paused:
            if (snapshot.water.mode == WaterMode::Volume) {
                char out[8]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "PAU %s", out);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "CAN=STOP OK=RUN");
            } else {
                char out[8]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "PAU %s", out);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "CAN=STOP OK=RUN");
            }
            return makeFrame(DisplayPage::Paused, true, line1, line2);
        case WaterState::Error:
        default:
            composeTopLine(line1, "Error", snapshot.pulsePerLiter);
            return makeFrame(DisplayPage::Error, true, line1, stateText(snapshot.water.lastResult));
    }
}

DisplayFrame DisplayPresenter::makeFrame(DisplayPage page, bool on, const char* line1, const char* line2) {
    DisplayFrame frame{page, on, {}, {}};
    copyLine(frame.line1, line1);
    copyLine(frame.line2, line2);
    return frame;
}

}  // namespace faucet
