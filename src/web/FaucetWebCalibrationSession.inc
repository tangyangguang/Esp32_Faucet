const char* calibrationSessionStatusText(CalibrationSessionStatus status) {
    switch (status) {
        case CalibrationSessionStatus::Idle:
            return "未开始";
        case CalibrationSessionStatus::Preparing:
            return "准备中";
        case CalibrationSessionStatus::WaitingLocalRun:
            return "等待本地出水";
        case CalibrationSessionStatus::Running:
            return "本地出水中";
        case CalibrationSessionStatus::AwaitingActual:
            return "等待输入实际容量";
        case CalibrationSessionStatus::ReadyToGenerate:
            return "可生成";
        case CalibrationSessionStatus::Generated:
            return "已生成，待应用";
        case CalibrationSessionStatus::Applied:
            return "已应用";
        case CalibrationSessionStatus::Discarded:
            return "已放弃";
        case CalibrationSessionStatus::Failed:
            return "失败";
    }
    return "未知";
}

bool calibrationSessionInactive(CalibrationSessionStatus status) {
    return status == CalibrationSessionStatus::Idle || status == CalibrationSessionStatus::Applied ||
           status == CalibrationSessionStatus::Discarded || status == CalibrationSessionStatus::Failed;
}

bool calibrationSessionStorageReady() {
    if (!g_context.calibrationSessions || !g_context.calibrationSessionTraces) {
        return false;
    }
    if (!g_context.calibrationSessions->ready() && !g_context.calibrationSessions->begin()) {
        return false;
    }
    if (!g_context.calibrationSessionTraces->ready() && !g_context.calibrationSessionTraces->begin()) {
        return false;
    }
    return g_context.calibrationSessions->ready() && g_context.calibrationSessionTraces->ready();
}

void redirectCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectFlowCalibrationFailure(const char* error) {
    char url[112]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration/flow?error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void sendFlowCalibrationPostFailure(bool ajax, const char* failure) {
    if (!ajax) {
        redirectFlowCalibrationFailure(failure);
        return;
    }
    char json[80]{};
    std::snprintf(json, sizeof(json), "{\"error\":\"%s\"}", failure ? failure : "save_failed");
    Esp32BaseWeb::sendJson(400, json);
}

void sendFlowCalibrationPostResult(bool ajax, bool ok, const char* success, const char* failure) {
    if (!ok) {
        sendFlowCalibrationPostFailure(ajax, failure);
        return;
    }
    if (ajax) {
        Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
        return;
    }
    char url[112]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration/flow?saved=%s", success ? success : "1");
    Esp32BaseWeb::redirectSeeOther(url);
}

