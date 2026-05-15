# 软件架构草案

## 当前已确认原则

- 项目基于 Arduino + PlatformIO。
- ESP32 基础能力统一来自同级目录 `../Esp32Base`，PlatformIO 通过 `symlink://../Esp32Base` 引用。
- 默认启用 `ESP32BASE_PROFILE_FULL`。
- 本项目只实现业务层和业务相关硬件驱动，不复制基础库已有能力。
- 架构优先简单可靠，不主动拆成复杂多任务系统。

## 分层方向

- Application：`src/main.cpp`，负责固件信息、hostname 策略占位、Web Auth 占位、Esp32Base 生命周期调用。
- Business：承载出水状态机、配置模型、日志、统计、滤芯、Web 业务 API。
- Drivers：承载电磁阀、流量计、四键、LCD1602、蜂鸣器、RTC。
- Base：Esp32Base 提供 Log、Config、System、Bus、Watchdog、Sleep、Fs、FileLog、Health、WiFi、DNS、NTP、mDNS、Web、OTA。

## 调度模型

- 主循环中先执行业务控制 tick，再执行 `Esp32Base::handle()`。
- 业务控制 tick 必须短小、非阻塞，目标单次耗时小于 1ms。
- 流量计和 `CANCEL` 使用 ISR 采集最小事件，ISR 内不做复杂计算和日志。
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
| `ButtonInput` | 四键消抖、短按/长按 |
| `DisplayPresenter` | LCD1602 页面模型和刷新节流 |
| `BeepDriver` | 操作、完成、异常提示 |
| `WaterRecordStore` | 出水记录写入、滚动、分页 |
| `StatisticsStore` | 今日、本周、本月、总累计 |
| `FilterStore` | 最多 6 个滤芯的配置、已用天数、已用流量和重置 |
| `FaucetWeb` | 业务页面和 `/api/faucet/...` API |

## 状态机草案

- `INIT`：加载配置并初始化业务驱动。
- `IDLE`：待机，可切换预设和进入确认。
- `CONFIRM`：等待二次确认，超时取消。
- `RUNNING`：出水中，持续检查容量、时间和异常。
- `PAUSED`：暂停关阀，等待继续或超时停止。
- `ERROR`：异常提示，阀门保持关闭。
- `SLEEP`：LCD 熄屏或低功耗状态。

优先级从高到低：

- 外部电源开关硬断电，不在软件状态机内建模。
- `CANCEL` 软件停止关阀。
- 安全兜底和异常关阀。
- 本地按键命令。
- Web 配置和查询请求。
- 显示、记录、统计。

### 事件

| 事件 | 来源 | 说明 |
| --- | --- | --- |
| `KeyCancelDown` | `CANCEL` ISR/tick | 出水中最高优先级软件停止 |
| `KeyOkShort` | 四键输入 | 确认、启动、暂停、继续 |
| `KeyOkLong` | 四键输入 | 在确认/暂停页切换 0.10L 与 0.50L 调整步进；结果页按住 OK 超过 5 秒进入现场校准 |
| `KeyPlusShort` | 四键输入 | 待机选择下一个预设；确认/暂停页增加本次目标水量 |
| `KeyMinusShort` | 四键输入 | 待机选择上一个预设；确认/暂停页减少本次目标水量 |
| `ConfirmTimeout` | timer | 二次确认超时 |
| `PauseTimeout` | timer | 暂停超时停止 |
| `TargetReached` | `WaterController` | 容量或时间达到目标 |
| `SafetyLimitReached` | `WaterController` | 最大时间、最大量、超预设百分比 |
| `NoFlowTimeout` | `FlowMeter` | 开阀后无流量 |
| `HighFlowTimeout` | `FlowMeter` | 异常大流量持续超时 |
| `ResultDisplayTimeout` | timer | 本次出水结果显示结束 |
| `DisplaySleepTimeout` | timer | LCD 熄屏 |
| `WebConfigChanged` | Web | 配置保存后生效 |

### 出水结果

| 结果 | 记录日志 | 更新统计/滤芯 | 说明 |
| --- | --- | --- | --- |
| `Completed` | yes | yes | 正常达到预设 |
| `StoppedByUser` | yes | yes | 本地 `CANCEL` 停止，记录已出水量 |
| `PauseTimeout` | yes | yes | 暂停超时停止 |
| `SafetyStopped` | yes | yes | 安全兜底触发 |
| `FlowError` | yes | yes | 无流量或异常流量 |
| `CanceledBeforeStart` | no | no | 确认前取消 |

## 存储草案

- 应用 NVS namespace 使用 `faucet_cfg`、`faucet_stat`、`faucet_run`，不得使用 `eb_` 前缀。
- 配置使用版本化结构。已知旧版本必须迁移到当前结构，保护用户已保存的预设、滤芯名称、启用状态、寿命和流量系数等配置；新增字段使用当前默认值补齐。
- 只有没有配置版本、未知未来版本或无法识别的数据，才加载当前默认配置。固件升级不得静默清空可识别的用户配置。
- 出水记录放 LittleFS，分页读取，单次最多 200 条；记录目标值、原始脉冲数、过滤脉冲数和当时流量系数。
- 统计放 NVS，出水完成后立即更新，周期性数据按日期变化重置。
- 运行快照默认只用于安全恢复判断，重启后默认不继续出水。
- 滤芯数据放 NVS，记录每个滤芯的启用状态、名称、建议更换天数、最长使用天数、寿命流量、开始时间和累计流量。
- 流量系数放 NVS；可由本地结果页或 Web records 页基于最新可校准出水记录保存，也可在 Esp32Base 系统参数页以“流量计校准系数”手动修改，页面单位为脉冲/L。

## Web 草案

- 业务 API 前缀为 `/api/faucet/...`。
- Web 页面通过 Esp32Base 注册，内置 OTA/WiFi/基础状态入口不重复实现。
- Web 采用业务优先首页模型：`/` 进入 `/faucet`，`/esp32base` 保留为系统工具入口；系统工具放在页面底部小字区域。
- 配置写入、恢复出厂、重启等操作使用 POST，并需要 Basic Auth。
- 记录、统计、配置接口必须分页或小响应，避免大内存拼接。
- Web 端不得注册启动出水、暂停出水、继续出水、停止出水 API 或按钮。
- Web records 页允许基于最新一条可校准出水记录输入量杯实际水量并保存校准，不提供独立校准页，不允许远程打开电磁阀。
- Web 默认认证通过 Esp32Base `setDefaultAuth()` 设置为 `admin/admin`；用户可通过 `/esp32base/auth` 修改认证，已保存认证优先于应用默认值。
- WebOTA 目标地址和凭据不写入仓库；本地复制 `platformio.ini.example` 为 `platformio.ini.local` 后填写 `custom_esp32base_webota_*`。
- 当前构建将 Esp32Base 串口日志等级设为 DEBUG，文件日志等级设为 INFO，用于保留更完整的启动和现场诊断信息。文件日志等级在启动后显式应用为 INFO，避免设备 NVS 中旧的 `eb_log.level` 覆盖当前项目策略。注意：基础库 INFO 日志会输出 WiFi/Web 认证明文凭据，调试日志和文件日志需要按敏感信息管理。

### Web 页面

| 页面 | 路径 | 功能 |
| --- | --- | --- |
| 首页 | `/faucet` | 状态、当前预设、基础统计、启用滤芯寿命概览 |
| 配置 | `/faucet/config` | 安全阈值、显示、蜂鸣器、电磁阀参数 |
| 预设 | `/faucet/presets` | 9 组预设的启用、名称、类型和值 |
| 记录 | `/faucet/records` | 分页查看出水记录，最新可校准记录可输入量杯实际水量保存校准 |
| 统计 | `/faucet/stats` | 今日、本周、本月、总累计 |
| 滤芯 | `/faucet/filters` | 最多 6 个滤芯的已用天数、已用流量、寿命范围、状态、设置入口和重置 |
| 滤芯设置 | `/faucet/filters/edit?index=N` | 单个滤芯的启用状态、名称、建议更换周期、最长使用周期、寿命流量和上次更换日期配置；隐藏路由，不进入导航 |

### Web API

| 方法 | 路径 | 功能 |
| --- | --- | --- |
| GET | `/api/faucet/status` | 查询状态 |
| GET | `/api/faucet/config` | 查询配置 |
| POST | `/api/faucet/config` | 保存配置 |
| GET | `/api/faucet/presets` | 查询预设 |
| POST | `/api/faucet/presets` | 保存预设 |
| GET | `/api/faucet/records` | 分页查询出水记录 |
| POST | `/api/faucet/records/calibration` | 基于最新可校准出水记录保存流量系数 |
| GET | `/api/faucet/stats` | 查询统计 |
| GET | `/api/faucet/filters` | 查询滤芯 |
| POST | `/api/faucet/filters` | 保存指定滤芯的启用状态、名称、寿命和上次更换日期 |
| POST | `/api/faucet/filters/reset` | 重置指定滤芯更换时间和累计流量 |

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

struct WaterRecord {
    uint32_t startTime;    // UTC seconds or relative seconds when time is unknown
    uint32_t volumeMl;
    uint32_t targetValue;  // ml for Volume, seconds for Time
    uint32_t pulseCount;
    uint32_t rejectedPulseCount;
    uint16_t durationSec;
    uint8_t mode;          // volume/time/calibration
    uint8_t result;        // completed/stopped/safety/error/pause_timeout
    uint8_t selectedPreset;
    float pulsePerMlAtRun;
    uint8_t reserved[4];   // boot id when time is boot-relative
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

## 持久化配置版本化

系统配置保存在 `faucet_cfg` namespace，当前版本为 v5。运行统计和滤芯运行量分别保存在 `faucet_stat`、`faucet_run`，不与系统配置版本耦合。

| 版本 | 主要字段 | 加载策略 |
|---|---|---|
| v1 | 单个校准目标 `cal_ml`；滤芯只保存单个寿命天数 `life_d` | 加载后迁移为 v5：忽略 `cal_ml`，滤芯建议/最大天数都继承 `life_d` |
| v2 | 单个校准目标 `cal_ml`；滤芯已区分建议/最大天数 | 加载后迁移为 v5：忽略 `cal_ml`，保留其他有效字段 |
| v3 | 多个校准候选容量；滤芯建议/最大天数；预设、阀控、OLED 休眠、蜂鸣器等完整参数 | 加载后迁移为 v5：忽略 `cal*_ml`，旧 `oled_s` 迁移为 `displaySleepSec` |
| v4 | LCD1602 地址、显示休眠、结果显示时间；预设、阀控、滤芯、流量系数等完整参数 | 加载后迁移为 v5：忽略旧校准候选容量，保留其他有效字段 |
| v5 | 删除校准候选容量；records 作为出水记录与校准入口；流量系数保留为高级参数 | 直接加载并做范围钳位 |
| 未来版本 | 版本号大于当前固件支持版本 | 按当前已知字段只读加载，不自动写回；Web/本地保存会失败，避免降级固件覆盖用户配置 |
| 未知/损坏版本 | 版本号小于 0 或无法识别 | 使用默认配置并记录警告；用户可通过 Web 系统工具重新生成配置 |

所有保存路径都必须先复制当前配置、完整修改并通过钳位后再一次性提交给 `ConfigStore` 和 `AppController`。出水、确认和暂停期间拒绝热更新配置。

## 校准参数方案

- 校准主流程基于最后一次真实本地出水记录：目标停止或手工停止均可，记录原始脉冲后由用户输入量杯实际水量。
- 结果页 `OK` 按住超过 5 秒进入本地校准，不等松开；校准实际水量默认本次显示量，默认步进 0.10L，长按 `OK` 切换 0.10L / 0.01L。
- Web 记录页只允许最新一条带原始脉冲且结果为完成或手动停止的记录执行校准；容量目标和时间目标都可校准。
- 校准公式为 `newPulsePerMl = pulseCount / actualMl`；不使用显示水量反推。
- 新系数必须在允许范围内，且相对旧系数偏差超过 30% 时拒绝保存，提示重新测量。

## 已确认实现决策

- Esp32Base 当前 Web 能力按轻量页面、分页、小响应方式使用；若能力不足，按项目规则生成基础库请求。
- `CANCEL` ISR 只记录高优先级停止事件，不在 ISR 内做复杂日志、文件、Web 或状态机操作。
- 控制 tick 必须足够频繁，保证 `CANCEL` 软件停止响应不超过 50ms。
- 日志文件采用二进制定长记录，Web 输出时转换为 JSON。
- DS3231 按自动检测实现；未焊接时 RTC 驱动自动降级。

## 旧文档迁移说明

- `old-docs/DOC-03_软件架构设计.md` 是业务规则参考材料。
- 旧文档中的模块名、FreeRTOS 任务拆分、AsyncWebServer 方案不直接迁移。
- 旧文档中的 EC11 驱动和菜单逻辑不迁移。
- 旧业务规则需要进入“需要实现”范围后，才进入新架构文档。
