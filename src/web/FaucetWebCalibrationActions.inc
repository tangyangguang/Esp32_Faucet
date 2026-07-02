bool readTdsCalibrationInput(std::uint16_t& referencePpm) {
    char text[32]{};
    std::uint32_t value = 0;
    if (!getParam("referencePpm", text, sizeof(text)) || !parseU32(text, value) || value > 2000) {
        return false;
    }
    referencePpm = static_cast<std::uint16_t>(value);
    return true;
}

bool readTemperatureCalibrationInput(std::int16_t& referenceCentiC) {
    char text[32]{};
    float referenceC = 0.0f;
    if (!getParam("referenceC", text, sizeof(text)) || !parseFloat(text, referenceC) || referenceC < 0.0f ||
        referenceC > 90.0f) {
        return false;
    }
    referenceCentiC = static_cast<std::int16_t>(referenceC * 100.0f + 0.5f);
    return true;
}

void redirectTemperatureCalibrationResult(const char* saved) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=temperature&saved=%s", saved ? saved : "temperature");
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTemperatureCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=temperature&error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTdsCalibrationResult(bool ok, const char* success, const char* failure) {
    char url[96]{};
    if (ok) {
        std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&saved=%s", success ? success : "tds_saved");
    } else {
        std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&error=%s", failure ? failure : "save_failed");
    }
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTdsCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void persistTdsCalibrationResult(bool ok, const char* success, const char* failure) {
    if (!ok || !g_context.app || !g_context.config || !g_context.configStore) {
        redirectTdsCalibrationFailure(failure);
        return;
    }
    const SystemConfig updated = g_context.app->config();
    if (!g_context.configStore->saveSystemConfig(updated)) {
        redirectTdsCalibrationFailure("save_failed");
        return;
    }
    *g_context.config = updated;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    redirectTdsCalibrationResult(true, success, failure);
}

bool readManualMeteringParametersInput(char* name, std::size_t nameLen, MeteringParameters& params) {
    char text[32]{};
    if (!getParam("name", name, nameLen) || !name[0]) {
        return false;
    }
    if (!getParam("startupPulseCount", text, sizeof(text)) || !parseU32(text, params.startupPulseCount) ||
        !getParam("startupVolumeMl", text, sizeof(text)) || !parseU32(text, params.startupVolumeMl) ||
        !getParam("stablePulsePerLiter", text, sizeof(text)) || !parseU32(text, params.stablePulsePerLiter) ||
        !getParam("stableFlowMlPerMin", text, sizeof(text)) || !parseU32(text, params.stableFlowMlPerMin)) {
        return false;
    }
    float startupDurationSec = 0.0f;
    if (!getParam("startupDurationSec", text, sizeof(text)) || !parseFloat(text, startupDurationSec) ||
        startupDurationSec < 0.0f ||
        startupDurationSec > static_cast<float>(kMaxSegmentedStartupDurationMs) / 1000.0f) {
        return false;
    }
    params.startupDurationMs = static_cast<std::uint32_t>(startupDurationSec * 1000.0f + 0.5f);
    return validMeteringSchemeParameters(params);
}

void handleCreateMeteringSchemeApi(bool ajax, std::uint32_t now) {
    if (waterTaskActive() || !g_context.app || !g_context.app->canApplyConfig()) {
        sendFlowCalibrationPostFailure(ajax, "busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        sendFlowCalibrationPostFailure(ajax, "metering_storage_unavailable");
        return;
    }
    char name[kMeteringSchemeNameLength]{};
    MeteringParameters params{};
    if (!readManualMeteringParametersInput(name, sizeof(name), params)) {
        sendFlowCalibrationPostFailure(ajax, "invalid_value");
        return;
    }
    std::uint32_t newId = 0;
    if (!g_context.meteringSchemes->createManualAsCurrent(name, params, now, newId)) {
        sendFlowCalibrationPostFailure(ajax, "save_failed");
        return;
    }
    MeteringSchemeRecord active{};
    if (!g_context.meteringSchemes->activeScheme(active) || !g_context.app->applyActiveMeteringScheme(active)) {
        sendFlowCalibrationPostFailure(ajax, "save_failed");
        return;
    }
    if (ajax) {
        Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
        return;
    }
    char url[128]{};
    std::snprintf(url,
                  sizeof(url),
                  "/faucet/calibration/flow?saved=metering_manual&createdScheme=%lu",
                  static_cast<unsigned long>(newId));
    Esp32BaseWeb::redirectSeeOther(url);
}

void handleCalibrationPost() {
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (!Esp32BaseWeb::checkPostAllowed("faucet_calibration")) {
        return;
    }
    char text[32]{};
    if (!getParam("action", text, sizeof(text))) {
        redirectCalibrationFailure("invalid_action");
        return;
    }
    if (std::strcmp(text, "temperature_save") == 0) {
        std::int16_t reference = 0;
        if (!readTemperatureCalibrationInput(reference)) {
            redirectTemperatureCalibrationFailure("invalid_value");
            return;
        }
        const bool updated = g_context.app && g_context.app->saveTemperatureCalibrationForWeb(reference);
        if (!updated || !g_context.configStore->saveSystemConfig(g_context.app->config())) {
            redirectTemperatureCalibrationFailure("save_failed");
            return;
        }
        *g_context.config = g_context.app->config();
        if (g_context.applySettings) {
            g_context.applySettings(*g_context.config);
        }
        redirectTemperatureCalibrationResult("temperature");
        return;
    }
    if (std::strcmp(text, "tds_start_session") == 0) {
        if (!g_context.app || waterTaskActive()) {
            redirectTdsCalibrationFailure("busy");
            return;
        }
        redirectTdsCalibrationResult(g_context.app->startTdsCalibrationSessionForWeb(
                                         g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_started",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_start_point") == 0) {
        std::uint16_t referencePpm = 0;
        if (!readTdsCalibrationInput(referencePpm)) {
            redirectTdsCalibrationFailure("invalid_value");
            return;
        }
        redirectTdsCalibrationResult(g_context.app &&
                                         g_context.app->startTdsCalibrationPointForWeb(
                                             referencePpm,
                                             g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_point_started",
                                     "busy");
        return;
    }
    if (std::strcmp(text, "tds_save_point") == 0) {
        redirectTdsCalibrationResult(g_context.app &&
                                         g_context.app->saveTdsCalibrationPointForWeb(
                                             g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_point_saved",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_remove_point") == 0) {
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kTdsCalibrationMaxPoints) {
            redirectTdsCalibrationFailure("invalid_value");
            return;
        }
        redirectTdsCalibrationResult(g_context.app &&
                                         g_context.app->removeTdsCalibrationPointForWeb(
                                             static_cast<std::uint8_t>(index),
                                             g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_point_removed",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_discard_session") == 0) {
        redirectTdsCalibrationResult(g_context.app && g_context.app->discardTdsCalibrationForWeb(),
                                     "tds_discarded",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_apply_session") == 0) {
        persistTdsCalibrationResult(g_context.app && g_context.app->applyTdsCalibrationForWeb(),
                                    "tds_saved",
                                    "invalid_state");
        return;
    }
    redirectCalibrationFailure("invalid_action");
}

void handleFlowCalibrationPost() {
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (!Esp32BaseWeb::checkPostAllowed("faucet_metering")) {
        return;
    }
    char text[32]{};
    const bool ajax = Esp32BaseWeb::hasParam("ajax");
    const std::uint32_t now = g_context.nowSeconds ? g_context.nowSeconds() : 0;
    if (!getParam("action", text, sizeof(text))) {
        sendFlowCalibrationPostFailure(ajax, "invalid_value");
        return;
    }
    if (std::strcmp(text, "start_session") == 0) {
        if (waterTaskActive()) {
            sendFlowCalibrationPostFailure(ajax, "busy");
            return;
        }
        if (!calibrationSessionStorageReady()) {
            sendFlowCalibrationPostFailure(ajax, "calibration_storage_unavailable");
            return;
        }
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->startCalibrationSessionForWeb(now),
                                      "session_started",
                                      "invalid_state");
        return;
    }
    if (std::strcmp(text, "discard_session") == 0) {
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->discardCalibrationSessionForWeb(now),
                                      "session_discarded",
                                      "invalid_state");
        return;
    }
    if (std::strcmp(text, "save_actual") == 0) {
        std::uint32_t actualMl = 0;
        if (!getParam("actualMl", text, sizeof(text)) || !parseU32(text, actualMl) ||
            actualMl < kCalibrationMinActualMl || actualMl > kMaxVolumePresetMl) {
            sendFlowCalibrationPostFailure(ajax, "invalid_value");
            return;
        }
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->submitCalibrationActualForWeb(actualMl, now),
                                      "actual",
                                      "save_failed");
        return;
    }
    if (std::strcmp(text, "skip_attempt") == 0) {
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->skipCalibrationAttemptForWeb(now),
                                      "attempt_skipped",
                                      "invalid_state");
        return;
    }
    if (std::strcmp(text, "apply_session") == 0) {
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->applyGeneratedCalibrationForWeb(now),
                                      "applied",
                                      "no_generated_result");
        return;
    }
    if (std::strcmp(text, "create_metering_scheme") == 0) {
        handleCreateMeteringSchemeApi(ajax, now);
        return;
    }
    sendFlowCalibrationPostFailure(ajax, "invalid_value");
}
