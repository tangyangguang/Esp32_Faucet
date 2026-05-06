# 软件架构草案

## 当前已确认原则

- 项目基于 Arduino + PlatformIO。
- ESP32 基础能力统一来自 `/Users/tyg/dir/claude_dir/Esp32Base`。
- 默认启用 `ESP32BASE_PROFILE_FULL`。
- 本项目只实现业务层和业务相关硬件驱动，不复制基础库已有能力。
- 架构优先简单可靠，不主动拆成复杂多任务系统。

## 分层方向

- Application：`src/main.cpp`，负责固件信息、hostname 策略占位、Web Auth 占位、Esp32Base 生命周期调用。
- Business：承载出水状态机、配置模型、日志、统计、滤芯、Web 业务 API。
- Drivers：承载电磁阀、流量计、三键、OLED、蜂鸣器、RTC。
- Base：Esp32Base 提供 Log、Config、System、Bus、Watchdog、Sleep、Fs、FileLog、Health、WiFi、DNS、NTP、mDNS、Web、OTA。

## 调度模型

- 主循环中先执行业务控制 tick，再执行 `Esp32Base::handle()`。
- 业务控制 tick 必须短小、非阻塞，目标单次耗时小于 1ms。
- 流量计和 `STOP` 使用 ISR 采集最小事件，ISR 内不做复杂计算和日志。
- Web 请求只允许查询状态、读取日志、保存配置或保存校准参数，不投递任何出水控制命令。
- 不主动创建多个 FreeRTOS 业务任务；只有验证发现 loop tick 无法满足实时性时，再提出明确设计变更。

## 核心模块草案

| 模块 | 职责 |
| --- | --- |
| `AppController` | 业务初始化、tick 调度、模块组合 |
| `WaterController` | 出水状态机、安全兜底、命令处理 |
| `ConfigStore` | 应用配置默认值、钳位、读写 |
| `FlowMeter` | 脉冲计数、流量计算、校准系数 |
| `ValveDriver` | 电磁阀开关、全压吸合、PWM 保持 |
| `ButtonInput` | 三键消抖、短按/长按/组合键 |
| `DisplayPresenter` | OLED 页面模型和刷新节流 |
| `BeepDriver` | 操作、完成、异常提示 |
| `WaterLogStore` | 出水日志写入、滚动、分页 |
| `StatisticsStore` | 今日、本周、本月、总累计 |
| `FilterStore` | 最多 6 个滤芯的配置、已用天数、已用流量和重置 |
| `CalibrationController` | 本地流量校准向导、采样一致性检查、系数保存 |
| `FaucetWeb` | 业务页面和 `/api/faucet/...` API |

## 状态机草案

- `INIT`：加载配置并初始化业务驱动。
- `IDLE`：待机，可切换预设和进入确认。
- `CONFIRM`：等待二次确认，超时取消。
- `RUNNING`：出水中，持续检查容量、时间和异常。
- `PAUSED`：暂停关阀，等待继续或超时停止。
- `ERROR`：异常提示，阀门保持关闭。
- `SLEEP`：OLED 熄屏或低功耗状态。

优先级从高到低：

- 外部电源开关硬断电，不在软件状态机内建模。
- `STOP` 软件停止关阀。
- 安全兜底和异常关阀。
- 本地按键命令。
- Web 配置和查询请求。
- 显示、日志、统计。

### 事件

| 事件 | 来源 | 说明 |
| --- | --- | --- |
| `KeyStopDown` | `STOP` ISR/tick | 最高优先级软件停止 |
| `KeyOkShort` | 三键输入 | 确认、启动、暂停、继续 |
| `KeyNextShort` | 三键输入 | 切换预设或菜单项 |
| `KeyNextLong` | 三键输入 | 进入本地菜单 |
| `ComboFactoryReset` | `STOP + OK` | 进入恢复出厂确认页 |
| `ConfirmTimeout` | timer | 二次确认超时 |
| `PauseTimeout` | timer | 暂停超时停止 |
| `TargetReached` | `WaterController` | 容量或时间达到目标 |
| `SafetyLimitReached` | `WaterController` | 最大时间、最大量、超预设百分比 |
| `NoFlowTimeout` | `FlowMeter` | 开阀后无流量 |
| `HighFlowTimeout` | `FlowMeter` | 异常大流量持续超时 |
| `DisplaySleepTimeout` | timer | OLED 熄屏 |
| `WebConfigChanged` | Web | 配置保存后生效 |

### 出水结果

| 结果 | 记录日志 | 更新统计/滤芯 | 说明 |
| --- | --- | --- | --- |
| `Completed` | yes | yes | 正常达到预设 |
| `StoppedByUser` | yes | yes | 本地 `STOP` 停止，记录已出水量 |
| `PauseTimeout` | yes | yes | 暂停超时停止 |
| `SafetyStopped` | yes | yes | 安全兜底触发 |
| `FlowError` | yes | yes | 无流量或异常流量 |
| `CalibrationSample` | no | no | 校准采样不进入出水日志和统计 |
| `CanceledBeforeStart` | no | no | 确认前取消 |

## 存储草案

- 应用 NVS namespace 使用 `faucet_cfg`、`faucet_stat`、`faucet_run`，不得使用 `eb_` 前缀。
- 配置不做历史迁移；配置版本不匹配时加载默认值。
- 日志放 LittleFS，分页读取，单次最多 200 条。
- 统计放 NVS，出水完成后立即更新，周期性数据按日期变化重置。
- 运行快照默认只用于安全恢复判断，重启后默认不继续出水。
- 滤芯数据放 NVS，记录每个滤芯的启用状态、名称、建议更换天数、最长使用天数、寿命流量、开始时间和累计流量。
- 校准系数和最多 4 个本地校准候选容量放 NVS；容量为 0 表示停用该候选项，校准采样过程仅在本地操作期间存在，保存前需要用户确认。

## Web 草案

- 业务 API 前缀为 `/api/faucet/...`。
- Web 页面通过 Esp32Base 注册，内置 OTA/WiFi/基础状态入口不重复实现。
- 配置写入、恢复出厂、重启等操作使用 POST，并需要 Basic Auth。
- 日志、统计、配置接口必须分页或小响应，避免大内存拼接。
- Web 端不得注册启动出水、暂停出水、继续出水、停止出水 API 或按钮。
- Web 端校准页面只允许查看/保存每升信号数和本地候选容量，不允许远程打开电磁阀。

### Web 页面

| 页面 | 路径 | 功能 |
| --- | --- | --- |
| 首页 | `/faucet` | 状态、当前预设、基础统计、启用滤芯寿命概览 |
| 配置 | `/faucet/config` | 预设、安全阈值、显示、蜂鸣器、电磁阀参数 |
| 日志 | `/faucet/logs` | 分页查看出水记录 |
| 统计 | `/faucet/stats` | 今日、本周、本月、总累计 |
| 滤芯 | `/faucet/filters` | 最多 6 个滤芯的已用天数、已用流量、寿命范围、状态、设置入口和重置 |
| 滤芯设置 | `/faucet/filters/edit?index=N` | 单个滤芯的启用状态、名称、建议更换周期、最长使用周期、寿命流量和上次更换日期配置；隐藏路由，不进入导航 |
| 校准 | `/faucet/calibration` | 查看/保存每升信号数和 4 个本地候选容量；不出水 |

### Web API

| 方法 | 路径 | 功能 |
| --- | --- | --- |
| GET | `/api/faucet/status` | 查询状态 |
| GET | `/api/faucet/config` | 查询配置 |
| POST | `/api/faucet/config` | 保存配置 |
| GET | `/api/faucet/presets` | 查询预设 |
| POST | `/api/faucet/presets` | 保存预设 |
| GET | `/api/faucet/logs` | 分页查询日志 |
| GET | `/api/faucet/stats` | 查询统计 |
| GET | `/api/faucet/filters` | 查询滤芯 |
| POST | `/api/faucet/filters` | 保存指定滤芯的启用状态、名称、寿命和上次更换日期 |
| POST | `/api/faucet/filters/reset` | 重置指定滤芯更换时间和累计流量 |
| GET | `/api/faucet/calibration` | 查询流量系数和候选容量 |
| POST | `/api/faucet/calibration` | 手动保存每升信号数和候选容量 |

禁止注册 `/api/faucet/water/*`、`/api/faucet/start`、`/api/faucet/stop` 等任何远程出水控制接口。

## 数据模型草案

```cpp
enum class PresetType : uint8_t {
    Volume = 0,
    Time = 1,
};

struct PresetConfig {
    bool enabled;
    PresetType type;
    uint32_t value;      // ml for Volume, seconds for Time
    char name[16];
};

struct FilterRecord {
    bool enabled;
    char name[16];
    uint32_t recommendDays; // suggested replacement point, 0 when disabled
    uint32_t maxDays;       // maximum usage point, 0 when disabled
    uint32_t lifeMl;     // 0 when disabled
    uint32_t startTime;  // seconds since 2000-01-01, 0 when unknown
    uint32_t usedMl;
};

struct WaterLogRecord {
    uint32_t startTime;    // UTC seconds or relative seconds when time is unknown
    uint32_t volumeMl;
    uint16_t durationSec;
    uint8_t mode;          // volume/time/calibration
    uint8_t result;        // completed/stopped/safety/error/pause_timeout
    uint8_t reserved[2];
};

struct StatisticsRecord {
    uint32_t todayMl;
    uint32_t weekMl;
    uint32_t monthMl;
    uint32_t totalMl;
    uint32_t lastDayKey;
    uint32_t lastWeekKey;
    uint32_t lastMonthKey;
};
```

结构体字段最终以实现为准，但必须保持小而定长，便于 NVS/LittleFS 存储和 native 测试。

## 本地流量校准方案

- 校准入口在本地菜单中，Web 不可触发出水校准。
- 用户准备有刻度的容器；本地可用 `NEXT` 在 Web 启用的候选容量中选择目标容量，默认启用 1.50L 和 7.50L。
- 按 `OK` 开始校准出水，达到容器刻度时按 `OK` 停止采样，`STOP` 可随时取消并关阀。
- 每次采样记录脉冲数和目标容量，计算 `pulse_per_ml = pulses / target_ml`。
- 至少完成 2 次有效采样；两次结果偏差不超过 5% 时，取平均值作为新系数。
- 若采样偏差超过 5%，OLED 提示重新采样，不保存新系数。
- 保存前 OLED 显示旧系数、新系数和确认提示；按 `OK` 保存，按 `STOP` 放弃。

## 已确认实现决策

- Esp32Base 当前 Web 能力按轻量页面、分页、小响应方式使用；若能力不足，按项目规则生成基础库请求。
- `STOP` ISR 只记录高优先级停止事件，不在 ISR 内做复杂日志、文件、Web 或状态机操作。
- 控制 tick 必须足够频繁，保证 `STOP` 软件停止响应不超过 50ms。
- 日志文件采用二进制定长记录，Web 输出时转换为 JSON。
- DS3231 按自动检测实现；未焊接时 RTC 驱动自动降级。

## 旧文档迁移说明

- `old-docs/DOC-03_软件架构设计.md` 是业务规则参考材料。
- 旧文档中的模块名、FreeRTOS 任务拆分、AsyncWebServer 方案不直接迁移。
- 旧文档中的 EC11 驱动和菜单逻辑不迁移。
- 旧业务规则需要进入“需要实现”范围后，才进入新架构文档。
