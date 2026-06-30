bool persistFilterConfig(const FilterRecord& record, std::size_t index) {
    if (!g_context.app->canApplyConfig() || index >= kFilterCount) {
        return false;
    }

    std::unique_ptr<SystemConfig> safe(new (std::nothrow) SystemConfig(g_context.configStore->loadSystemConfig()));
    if (!safe) {
        return false;
    }
    safe->filters[index] = filterConfigFromRecord(record);
    sanitizeConfig(*safe);

    FilterRuntime runtime[kFilterCount]{};
    g_context.filters->copyRuntime(runtime);
    runtime[index] = filterRuntimeFromRecord(record);

    if (!g_context.configStore->saveSystemConfig(*safe) || !g_context.configStore->saveFilterRuntime(runtime)) {
        return false;
    }
    FilterRecord liveRecord = mergeFilterRecord(safe->filters[index], runtime[index]);
    if (!g_context.app->applyConfig(*safe) || !g_context.filters->updateFilter(index, liveRecord)) {
        return false;
    }
    *g_context.config = *safe;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    return true;
}
