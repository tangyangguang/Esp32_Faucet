#pragma once

namespace faucet {

bool registerAppCss(const char* path);
void sendAppStylesheetLink();
void sendCalibrationPageScript();
void sendHomeAutoRefreshScript();

}  // namespace faucet
