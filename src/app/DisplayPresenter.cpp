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

void formatLiters(char* out, std::size_t len, std::uint32_t ml) {
    std::snprintf(out, len, "%lu.%01luL", static_cast<unsigned long>(ml / 1000UL),
                  static_cast<unsigned long>((ml % 1000UL) / 100UL));
}

void formatClock(char* out, std::size_t len, std::uint32_t seconds) {
    std::snprintf(out, len, "%02lu:%02lu", static_cast<unsigned long>(seconds / 60UL),
                  static_cast<unsigned long>(seconds % 60UL));
}

void copyLine(char (&dest)[kDisplayLineLength], const char* src) {
    std::strncpy(dest, src ? src : "", kDisplayLineLength - 1);
    dest[kDisplayLineLength - 1] = '\0';
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
        char clock[6]{};
        formatLiters(volume, sizeof(volume), snapshot.water.volumeMl);
        formatClock(clock, sizeof(clock), snapshot.water.elapsedSec);
        std::snprintf(line1, sizeof(line1), "%s %s", stateText(snapshot.water.lastResult), volume);
        std::snprintf(line2, sizeof(line2), "%s", clock);
        return makeFrame(DisplayPage::Result, true, line1, line2);
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
                std::snprintf(line1, sizeof(line1), "P%u %s", static_cast<unsigned>(snapshot.water.selectedPreset + 1), target);
            } else {
                std::snprintf(line1, sizeof(line1), "P%u %lus", static_cast<unsigned>(snapshot.water.selectedPreset + 1),
                              static_cast<unsigned long>(snapshot.water.targetValue));
            }
            std::snprintf(line2, sizeof(line2), "+/- Sel OK");
            return makeFrame(DisplayPage::Idle, true, line1, line2);
        case WaterState::Confirm:
            if (snapshot.water.mode == WaterMode::Volume) {
                char target[8]{};
                formatLiters(target, sizeof(target), snapshot.water.targetValue);
                std::snprintf(line1, sizeof(line1), "Set %s S%lu.%lu", target,
                              static_cast<unsigned long>(snapshot.adjustmentStepMl / 1000UL),
                              static_cast<unsigned long>((snapshot.adjustmentStepMl % 1000UL) / 100UL));
                std::snprintf(line2, sizeof(line2), "+/- Adj OK Go");
            } else {
                std::snprintf(line1, sizeof(line1), "Set %lus", static_cast<unsigned long>(snapshot.water.targetValue));
                std::snprintf(line2, sizeof(line2), "OK Go CAN Back");
            }
            return makeFrame(DisplayPage::Confirm, true, line1, line2);
        case WaterState::Running:
            if (snapshot.water.mode == WaterMode::Volume) {
                const std::uint32_t remain =
                    snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0;
                char remainText[8]{};
                char clock[6]{};
                formatLiters(remainText, sizeof(remainText), remain);
                formatClock(clock, sizeof(clock), snapshot.water.elapsedSec);
                std::snprintf(line1, sizeof(line1), "Lft %s %s", remainText, clock);
            } else {
                const std::uint32_t remain =
                    snapshot.water.targetValue > snapshot.water.elapsedSec ? snapshot.water.targetValue - snapshot.water.elapsedSec : 0;
                char clock[6]{};
                formatClock(clock, sizeof(clock), snapshot.water.elapsedSec);
                std::snprintf(line1, sizeof(line1), "Lft %lus %s", static_cast<unsigned long>(remain), clock);
            }
            {
                char out[8]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                std::snprintf(line2, sizeof(line2), "Out %s OK Pause", out);
            }
            return makeFrame(DisplayPage::Running, true, line1, line2);
        case WaterState::Paused:
            if (snapshot.water.mode == WaterMode::Volume) {
                char out[8]{};
                char target[8]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                formatLiters(target, sizeof(target), snapshot.water.targetValue);
                std::snprintf(line1, sizeof(line1), "Pause %s/%s", out, target);
                std::snprintf(line2, sizeof(line2), "S%lu.%lu +/- OK",
                              static_cast<unsigned long>(snapshot.adjustmentStepMl / 1000UL),
                              static_cast<unsigned long>((snapshot.adjustmentStepMl % 1000UL) / 100UL));
            } else {
                std::snprintf(line1, sizeof(line1), "Paused");
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
