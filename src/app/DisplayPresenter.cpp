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

void formatLitersNumber(char* out, std::size_t len, std::uint32_t ml) {
    formatLitersValue(out, len, ml, false);
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

}  // namespace

DisplayPresenter::DisplayPresenter(std::uint32_t sleepTimeoutSec)
    : sleepTimeoutMs_(msFromSeconds(sleepTimeoutSec)), lastWakeMs_(0) {}

void DisplayPresenter::configure(std::uint32_t sleepTimeoutSec) {
    sleepTimeoutMs_ = msFromSeconds(sleepTimeoutSec);
}

void DisplayPresenter::wake(std::uint32_t nowMs) {
    lastWakeMs_ = nowMs;
}

DisplayFrame DisplayPresenter::render(const AppSnapshot& snapshot, std::uint32_t nowMs) {
    if (snapshot.localMode == LocalUiMode::Result) {
        char line1[kDisplayLineLength]{};
        char line2[kDisplayLineLength]{};
        char volume[8]{};
        formatLiters(volume, sizeof(volume), snapshot.water.volumeMl);
        char left[kDisplayLineLength]{};
        std::snprintf(left, sizeof(left), "%s %s", stateText(snapshot.water.lastResult), volume);
        composeTopLine(line1, left, snapshot.pulsePerLiter);
        std::snprintf(line2, sizeof(line2), snapshot.calibrationReady ? "Hold OK Cal" : "OK Back");
        return makeFrame(DisplayPage::Result, true, line1, line2);
    }

    if (snapshot.localMode == LocalUiMode::Calibration) {
        char actual[8]{};
        char step[8]{};
        formatLiters(actual, sizeof(actual), snapshot.calibrationActualMl);
        formatLitersNumber(step, sizeof(step), snapshot.calibrationStepMl);
        char line1[kDisplayLineLength]{};
        char line2[kDisplayLineLength]{};
        std::snprintf(line1, sizeof(line1), "A%s P%lu/L", actual, static_cast<unsigned long>(snapshot.pulsePerLiter));
        std::snprintf(line2, sizeof(line2), "S%s +/- OK", step);
        return makeFrame(DisplayPage::Calibration, true, line1, line2);
    }

    if (sleepTimeoutMs_ > 0 && elapsedAtLeast(nowMs, lastWakeMs_, sleepTimeoutMs_) &&
        snapshot.water.state == WaterState::Idle) {
        return makeFrame(DisplayPage::Sleep, false, "", "");
    }

    char line1[kDisplayLineLength]{};
    char line2[kDisplayLineLength]{};
    switch (snapshot.water.state) {
        case WaterState::Idle:
            if (snapshot.water.mode == WaterMode::Volume) {
                char target[8]{};
                formatLiters(target, sizeof(target), snapshot.water.targetValue);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "P%u %s", static_cast<unsigned>(snapshot.water.selectedPreset + 1), target);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
            } else {
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "P%u %lus", static_cast<unsigned>(snapshot.water.selectedPreset + 1),
                              static_cast<unsigned long>(snapshot.water.targetValue));
                composeTopLine(line1, left, snapshot.pulsePerLiter);
            }
            std::snprintf(line2, sizeof(line2), "+/- Sel OK");
            return makeFrame(DisplayPage::Idle, true, line1, line2);
        case WaterState::Confirm:
            if (snapshot.water.mode == WaterMode::Volume) {
                char target[8]{};
                formatLiters(target, sizeof(target), snapshot.water.targetValue);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "Set %s", target);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "+/- Adj OK Go");
            } else {
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "Set %lus", static_cast<unsigned long>(snapshot.water.targetValue));
                composeTopLine(line1, left, snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "OK Go CAN Back");
            }
            return makeFrame(DisplayPage::Confirm, true, line1, line2);
        case WaterState::Running:
            if (snapshot.water.mode == WaterMode::Volume) {
                const std::uint32_t remain =
                    snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0;
                char remainText[8]{};
                formatLiters(remainText, sizeof(remainText), remain);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "Lft %s", remainText);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
            } else {
                const std::uint32_t remain =
                    snapshot.water.targetValue > snapshot.water.elapsedSec ? snapshot.water.targetValue - snapshot.water.elapsedSec : 0;
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "Lft %lus", static_cast<unsigned long>(remain));
                composeTopLine(line1, left, snapshot.pulsePerLiter);
            }
            if (snapshot.water.mode == WaterMode::Volume) {
                char out[8]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                std::snprintf(line2, sizeof(line2), "Out %s OK", out);
            } else {
                std::snprintf(line2, sizeof(line2), "OK Pause");
            }
            return makeFrame(DisplayPage::Running, true, line1, line2);
        case WaterState::Paused:
            if (snapshot.water.mode == WaterMode::Volume) {
                char out[8]{};
                char target[8]{};
                char step[8]{};
                formatLitersNumber(out, sizeof(out), snapshot.water.volumeMl);
                formatLitersNumber(target, sizeof(target), snapshot.water.targetValue);
                formatLitersNumber(step, sizeof(step), snapshot.adjustmentStepMl);
                char left[kDisplayLineLength]{};
                std::snprintf(left, sizeof(left), "P %s/%s", out, target);
                composeTopLine(line1, left, snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "Step%s +/-OK", step);
            } else {
                composeTopLine(line1, "Paused", snapshot.pulsePerLiter);
                std::snprintf(line2, sizeof(line2), "OK Resume");
            }
            return makeFrame(DisplayPage::Paused, true, line1, line2);
        case WaterState::Error:
        default:
            return makeFrame(DisplayPage::Error, true, "Error", stateText(snapshot.water.lastResult));
    }
}

DisplayFrame DisplayPresenter::makeFrame(DisplayPage page, bool on, const char* line1, const char* line2) {
    DisplayFrame frame{page, on, {}, {}};
    copyLine(frame.line1, line1);
    copyLine(frame.line2, line2);
    return frame;
}

}  // namespace faucet
