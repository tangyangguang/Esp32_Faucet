# 变更记录

## 2026-05-15 Records 重整与校准入口收敛

- 统一产品概念为出水记录 `records`，业务页面从 `/faucet/logs` 改为 `/faucet/records`。
- 业务 API 从 `/api/faucet/logs` 改为 `/api/faucet/records`，JSON 顶层数组字段从 `logs` 改为 `records`。
- 删除独立校准页 `/faucet/calibration` 和旧校准 API `/api/faucet/calibration`。
- 新增 `POST /api/faucet/records/calibration`，固定基于最新可校准出水记录保存流量系数，请求参数为 `actualMl`，单位 ml。
- 删除 4 个校准候选容量配置，配置保存后不再写入 `cal_ml`、`cal0_ml`、`cal1_ml`、`cal2_ml`、`cal3_ml`。
- 删除旧校准采样控制器，校准公式统一为 `newPulsePerMl = pulseCount / actualMl`。
- 出水记录 LittleFS 文件改为 `/faucet_records_v1.bin`，文件 header 使用 records 语义 magic/version。
- 手动流量系数保留为 Esp32Base 系统参数页的高级救援参数，不再在业务校准页中维护。
- 系统配置版本从 v4 升到 v5；v5 加载保留安全、阀门、显示、预设、滤芯、流量系数等有效字段，忽略旧校准候选容量字段。
