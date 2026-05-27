# 变更记录

## 2026-05-27 本地步进、LCD 与小容量补偿

- 修复手动暂停后取消停止导致上一轮出水量污染下一轮确认页调整下限的问题。
- 本地确认页容量和时间目标改为按 Web 配置步进调整，默认容量 0.10L、时间 10s；长按 `OK` 不再切换 0.10L/0.50L，暂停页不再允许调整目标。
- LCD1602 页面重排，所有醒屏页面第一行右侧固定显示每升脉冲数；本地 LCD 只显示预设编号与容量/时间，不显示预设名称。
- 系统配置版本从 v5 升到 v6，新增本地容量/时间调整步进和启动补偿水量；迁移保留旧预设、滤芯、统计、记录和流量系数。
- 小容量记录校准可更新启动补偿水量，大容量记录继续用于稳态流量系数校准。
- Web 记录页精简为高频字段，取消固定“诊断”列；脉冲列显示原始脉冲和 P/L，过滤脉冲只在有值时作为辅助信息。新增独立记录校准元数据文件，保存实测量、校准类型、参数变化和重校次数；校准后记录页显示“已校准”和实测量，重校默认上次实测量。

## 2026-05-16 原型评审收口

- P2-15：正式文档补充出水记录时间回溯不依赖 32 位 `uptimeMs`；未同步时间只保存启动内相对秒和 boot id，NTP 同步后通过 Esp32Base boot event 回写真实时间。
- P2-18：26/27/28 原型评审结论并入正式文档，不再把独立评审稿作为实现依据；records 首版必须支持时间范围筛选，并在 `docs/03-software-architecture.md` 与 `docs/06-implementation-plan.md` 中作为正式范围维护。

## 2026-05-15 Records 重整与校准入口收敛

- 统一产品概念为出水记录 `records`，业务页面从 `/faucet/logs` 改为 `/faucet/records`。
- 业务 API 从 `/api/faucet/logs` 改为 `/api/faucet/records`，JSON 顶层数组字段从 `logs` 改为 `records`。
- 删除独立校准页 `/faucet/calibration` 和旧校准 API `/api/faucet/calibration`。
- 新增 `POST /api/faucet/records/calibration`，固定基于最新可校准出水记录保存流量系数，请求参数为 `actualMl`，单位 ml。
- 删除 4 个校准候选容量配置，配置保存后不再写入 `cal_ml`、`cal0_ml`、`cal1_ml`、`cal2_ml`、`cal3_ml`。
- 删除旧校准采样控制器，校准公式统一为 `newPulsePerMl = pulseCount / actualMl`。
- 出水记录 LittleFS 文件改为 `/faucet_records_v1.bin`，文件 header 使用 records 语义 magic/version。
- 手动流量系数保留为 Esp32Base 系统参数页的“流量计校准系数”，页面单位为脉冲/L；内部仍按 pulse/ml 计算，不再在业务校准页中维护。
- 系统配置版本从 v4 升到 v5；v5 加载保留安全、阀门、显示、预设、滤芯、流量系数等有效字段，忽略旧校准候选容量字段。
