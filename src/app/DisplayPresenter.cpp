#include "app/DisplayPresenter.h"

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

void copyLine(char (&dest)[kDisplayLineLength], const char* src) {
    std::strncpy(dest, src ? src : "", kDisplayLineLength - 1);
    dest[kDisplayLineLength - 1] = '\0';
}

}  // namespace

DisplayPresenter::DisplayPresenter(std::uint32_t sleepTimeoutSec)
    : sleepTimeoutMs_(msFromSeconds(sleepTimeoutSec)), lastWakeMs_(0) {}

void DisplayPresenter::wake(std::uint32_t nowMs) {
    lastWakeMs_ = nowMs;
}

DisplayFrame DisplayPresenter::render(const AppSnapshot& snapshot, std::uint32_t nowMs) {
    if (snapshot.localMode != LocalUiMode::Normal) {
        char line1[kDisplayLineLength]{};
        char line2[kDisplayLineLength]{};
        switch (snapshot.localMode) {
            case LocalUiMode::CalibrationSelect:
                std::snprintf(line1, sizeof(line1), "Cal %lu ml", static_cast<unsigned long>(snapshot.calibration.targetMl));
                std::snprintf(line2, sizeof(line2), "NEXT Size OK");
                return makeFrame(DisplayPage::Idle, true, line1, line2);
            case LocalUiMode::CalibrationConfirm:
                std::snprintf(line1, sizeof(line1), "Put %lu ml cup", static_cast<unsigned long>(snapshot.calibration.targetMl));
                std::snprintf(line2, sizeof(line2), "OK Start STOP Exit");
                return makeFrame(DisplayPage::Confirm, true, line1, line2);
            case LocalUiMode::CalibrationSampling:
                std::snprintf(line1, sizeof(line1), "Cal Running");
                std::snprintf(line2, sizeof(line2), "OK At Mark");
                return makeFrame(DisplayPage::Running, true, line1, line2);
            case LocalUiMode::CalibrationReview:
                std::snprintf(line1, sizeof(line1), "%.3f -> %.3f", static_cast<double>(snapshot.calibration.oldPulsePerMl), static_cast<double>(snapshot.calibration.proposedPulsePerMl));
                std::snprintf(line2, sizeof(line2), "OK Save STOP Exit");
                return makeFrame(DisplayPage::Confirm, true, line1, line2);
            case LocalUiMode::CalibrationRejected:
                std::snprintf(line1, sizeof(line1), "Cal Rejected");
                std::snprintf(line2, sizeof(line2), "NEXT Retry STOP Exit");
                return makeFrame(DisplayPage::Error, true, line1, line2);
            case LocalUiMode::FactoryResetConfirm:
                std::snprintf(line1, sizeof(line1), "Factory Reset?");
                std::snprintf(line2, sizeof(line2), "OK Yes STOP No");
                return makeFrame(DisplayPage::Confirm, true, line1, line2);
            case LocalUiMode::Normal:
                break;
        }
    }

    if (sleepTimeoutMs_ > 0 && nowMs >= lastWakeMs_ && nowMs - lastWakeMs_ >= sleepTimeoutMs_ &&
        snapshot.water.state == WaterState::Idle) {
        return makeFrame(DisplayPage::Sleep, false, "", "");
    }

    char line1[kDisplayLineLength]{};
    char line2[kDisplayLineLength]{};
    switch (snapshot.water.state) {
        case WaterState::Idle:
            std::snprintf(line1, sizeof(line1), "%lu ml", static_cast<unsigned long>(snapshot.water.targetValue));
            std::snprintf(line2,
                          sizeof(line2),
                          "Today %lu",
                          static_cast<unsigned long>(snapshot.statistics.todayMl));
            return makeFrame(DisplayPage::Idle, true, line1, line2);
        case WaterState::Confirm:
            std::snprintf(line1, sizeof(line1), "OK Start?");
            std::snprintf(line2, sizeof(line2), "%lu ml", static_cast<unsigned long>(snapshot.water.targetValue));
            return makeFrame(DisplayPage::Confirm, true, line1, line2);
        case WaterState::Running:
            if (snapshot.water.mode == WaterMode::Volume) {
                const std::uint32_t remain =
                    snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0;
                std::snprintf(line1, sizeof(line1), "Remain %lu", static_cast<unsigned long>(remain));
            } else {
                std::snprintf(line1, sizeof(line1), "Running");
            }
            std::snprintf(line2, sizeof(line2), "Out %lu", static_cast<unsigned long>(snapshot.water.volumeMl));
            return makeFrame(DisplayPage::Running, true, line1, line2);
        case WaterState::Paused:
            std::snprintf(line1, sizeof(line1), "Paused");
            std::snprintf(line2, sizeof(line2), "Out %lu", static_cast<unsigned long>(snapshot.water.volumeMl));
            return makeFrame(DisplayPage::Paused, true, line1, line2);
        case WaterState::Error:
        default:
            return makeFrame(DisplayPage::Error, true, "Error", stateText(WaterResult::SafetyStopped));
    }
}

DisplayFrame DisplayPresenter::makeFrame(DisplayPage page, bool on, const char* line1, const char* line2) {
    DisplayFrame frame{page, on, {}, {}};
    copyLine(frame.line1, line1);
    copyLine(frame.line2, line2);
    return frame;
}

}  // namespace faucet
