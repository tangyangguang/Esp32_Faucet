bool persistFilterConfig(const FilterRecord& record, std::size_t index) {
    if (!g_context.app->canApplyConfig() || index >= kFilterCount) {
        return false;
    }

    std::unique_ptr<SystemConfig> safe(new (std::nothrow) SystemConfig(g_context.configStore->loadSystemConfig()));
    if (!safe) {
        return false;
    }
    safe->filters[index] = record;
    sanitizeConfig(*safe);

    FilterRecord runtime[kFilterCount]{};
    const FilterRecord* current = g_context.filters->records();
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        runtime[i] = current[i];
    }
    runtime[index].startTime = record.startTime;
    runtime[index].usedMl = record.usedMl;
    runtime[index].startBootId = record.startBootId;

    if (!g_context.configStore->saveSystemConfig(*safe) || !g_context.configStore->saveFilterRuntime(runtime)) {
        return false;
    }
    FilterRecord liveRecord = safe->filters[index];
    liveRecord.startTime = runtime[index].startTime;
    liveRecord.usedMl = runtime[index].usedMl;
    liveRecord.startBootId = runtime[index].startBootId;
    if (!g_context.app->applyConfig(*safe) || !g_context.filters->updateFilter(index, liveRecord)) {
        return false;
    }
    *g_context.config = *safe;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    return true;
}

