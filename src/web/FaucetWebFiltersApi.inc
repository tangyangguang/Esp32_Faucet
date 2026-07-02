void handleFiltersApi() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("faucet_filters") || !requireContext()) {
            return;
        }
        if (!g_context.app->canApplyConfig()) {
            Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=busy");
            return;
        }
        char text[24]{};
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
            Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=invalid_index");
            return;
        }

        FilterRecord record = g_context.filters->record(index);
        bool enabled = false;
        if (!explicitCheckboxParam("enabled", "enabledSet", enabled)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=invalid_enabled");
            return;
        }
        record.enabled = enabled;
        Esp32BaseWeb::getParam("name", record.name, sizeof(record.name));
        std::uint32_t months = 0;
        if (getParam("recommendMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.recommendDays = monthsToDays(months);
        }
        if (getParam("maxMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.maxDays = monthsToDays(months);
        }
        applyU32Param("lifeMl", record.lifeMl);
        if (getParam("startDate", text, sizeof(text))) {
            if (!parseDate(text, record.startTime)) {
                char location[80]{};
                std::snprintf(location,
                              sizeof(location),
                              "/faucet/filters/edit?index=%lu&error=invalid_date",
                              static_cast<unsigned long>(index));
                Esp32BaseWeb::redirectSeeOther(location);
                return;
            }
            record.startBootId = 0;
            record.usedMl = sumRealRecordVolumeSince(record.startTime);
        }
        record.name[kFilterNameLength - 1] = '\0';

        const bool ok = persistFilterConfig(record, index);
        Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?saved=1" : "/faucet/filters?error=save_failed");
        return;
    }
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writeFiltersJson(g_context.filters->records(), json, sizeof(json)), json);
}

void handleFiltersResetApi() {
    if (!Esp32BaseWeb::checkPostAllowed("faucet_filter_reset") || !requireContext()) {
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=busy");
        return;
    }
    char text[24]{};
    std::uint32_t index = 0;
    if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
        Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
        return;
    }

    const std::uint32_t now = g_context.nowSeconds();
    FilterRecord record = g_context.filters->record(index);
    record.startTime = now;
    record.usedMl = 0;
    record.startBootId = now >= kMinRealDateSeconds ? 0 : (g_context.bootId ? g_context.bootId() : 0);
    if (!g_context.filters->updateFilter(index, record)) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"reset_failed\"}");
        return;
    }
    FilterRuntime runtime[kFilterCount]{};
    g_context.filters->copyRuntime(runtime);
    const bool ok = g_context.configStore->saveFilterRuntime(runtime);
    Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?reset=1" : "/faucet/filters?error=save_failed");
}

Esp32BaseWeb::Handler handlerFor(const FaucetWebRoute& route) {
    switch (route.handler) {
        case FaucetWebHandler::HomePage:
            return handleFaucetPage;
        case FaucetWebHandler::RecordsPage:
            return handleRecordsPage;
        case FaucetWebHandler::StatsPage:
            return handleStatsPage;
        case FaucetWebHandler::PresetsPage:
            return handlePresetsPage;
        case FaucetWebHandler::FiltersPage:
            return handleFiltersPage;
        case FaucetWebHandler::CalibrationPage:
            return handleCalibrationPage;
        case FaucetWebHandler::AppCss:
            return handleAppCss;
        case FaucetWebHandler::CalibrationPost:
            return handleCalibrationPost;
        case FaucetWebHandler::FlowCalibrationPage:
            return handleFlowCalibrationPage;
        case FaucetWebHandler::FlowCalibrationPost:
            return handleFlowCalibrationPost;
        case FaucetWebHandler::FilterEditPage:
            return handleFilterEditPage;
        case FaucetWebHandler::RecordDetailPage:
            return handleRecordInfoPage;
        case FaucetWebHandler::CalibrationDetailPage:
            return handleCalibrationDetailPage;
        case FaucetWebHandler::StatusApi:
            return handleStatusApi;
        case FaucetWebHandler::TodayOverviewApi:
            return handleTodayOverviewApi;
        case FaucetWebHandler::PresetsApi:
            return handlePresetsApi;
        case FaucetWebHandler::RecordsApi:
            return handleRecordsApi;
        case FaucetWebHandler::StatsApi:
            return handleStatsApi;
        case FaucetWebHandler::FiltersApi:
            return handleFiltersApi;
        case FaucetWebHandler::FiltersResetApi:
            return handleFiltersResetApi;
    }
    return nullptr;
}

}  // namespace
