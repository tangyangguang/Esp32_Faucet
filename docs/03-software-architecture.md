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
| `FlowMeter` | 脉冲计数、容量换算、实时流速窗口估算和诊断瞬时流速 |
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
| `KeyOkLong` | 四键输入 | 确认/暂停页不切换步进；结果页按住 OK 超过 5 秒进入现场校准 |
| `KeyPlusShort` | 四键输入 | 待机选择下一个预设；确认页按配置步进增加本次目标值 |
| `KeyMinusShort` | 四键输入 | 待机选择上一个预设；确认页按配置步进减少本次目标值 |
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
- 配置使用版本化结构。已知旧版本必须迁移到当前结构，保护用户已保存的预设、滤芯名称、启用状态、寿命和计量参数等配置；新增字段使用当前默认值补齐。
- 只有没有配置版本、未知未来版本或无法识别的数据，才加载当前默认配置。固件升级不得静默清空可识别的用户配置。
- 出水记录放 LittleFS，分页读取，单次最多 200 条；记录目标值、原始脉冲数、过滤脉冲数、当时启用的计量方案 ID、方案修订号和 5 个计量参数快照，包括 3 个容量估算计量参数和 2 个时间估算计量参数。记录本体保持不可变，Web 校准后的实测量和重校次数写入独立 LittleFS 校准元数据文件。
- 出水记录时间回溯不得依赖 32 位 `uptimeMs`。未同步时间时只保存启动内相对秒和 boot id；NTP 同步后通过 Esp32Base boot event 解析真实时间并重写记录，避免 `millis()`/uptime 回绕影响历史记录。
- 统计放 NVS，出水完成后立即更新，周期性数据按日期变化重置。
- 运行快照默认只用于安全恢复判断，重启后默认不继续出水。
- 滤芯数据放 NVS，记录每个滤芯的启用状态、名称、建议更换天数、最长使用天数、寿命流量、开始时间和累计流量。
- 流量计计量方案使用独立持久化域：保存当前启用方案 ID、计量方案生成结果和动态方案记录。系统内置 `YF-S201 默认计量方案` 作为首次启动兜底参数，启动脉冲数 8P、启动水量 36ml、稳态 225P/L。运行时只读取当前方案的启动脉冲数、启动水量、稳态 P/L；专业计量方案页从长期样本库生成的结果只能保存为新方案，保存后不自动启用，启用必须是单独动作。普通校准会话的“确认应用”会创建并启用一套新方案。已经被出水记录使用过的方案不能删除，只能停用；从未使用过、非当前且不是最后一套有效方案的方案可以删除。
- 校准会话使用独立存储：RAM 只保留当前/最近一次出水的完整脉冲明细；会话样本区为 Flash 固定 5 槽位，新会话开始时清空并覆盖；长期样本库为 Flash 固定 10 槽位，只有用户确认保存/应用后进入，不自动覆盖。校准出水本身仍是正常出水，必须进入出水记录、统计和滤芯累计。

## Web 草案

- 业务 API 前缀为 `/api/faucet/...`。
- Web 页面通过 Esp32Base 注册，内置 OTA/WiFi/基础状态入口不重复实现。
- Web 采用业务优先首页模型：`/` 进入 `/index`，`/esp32base` 保留为系统工具入口；系统工具放在页面底部小字区域。
- 配置写入、恢复出厂、重启等操作使用 POST，并需要 Basic Auth。
- 记录、统计和参数页面/API 必须分页或小响应，避免大内存拼接。
- Web 端不得注册启动出水、暂停出水、继续出水、停止出水 API 或按钮；首页允许切换“下次预设”，但只修改后续任务使用的 selected preset，不改变已确认或正在运行的 active preset、目标值或阀门状态。
- Web records 页支持最近记录分页，不提供按时间范围筛选；按时间范围查询只保留在 `/api/faucet/records`。列表保持紧凑，显示目标、出水量、持续时间、模式、结束原因、脉冲/P-L、计量方案、校准状态和操作。过滤脉冲只在有值时作为脉冲单元格内的辅助诊断信息展示。有 RAM 明细的记录显示“明细”，已写入设备样本库的记录优先显示“已存明细”；不允许远程打开电磁阀。
- Web 默认认证通过 Esp32Base `setDefaultAuth()` 设置为 `admin/admin`；用户可通过 `/esp32base/auth` 修改认证，已保存认证优先于应用默认值。
- WebOTA 目标地址和凭据不写入仓库；本地复制 `platformio.example.ini` 为 `platformio.local.ini` 后填写 `custom_esp32base_webota_*`。
- 当前构建将 Esp32Base 串口日志等级设为 DEBUG，文件日志等级设为 INFO，用于保留更完整的启动和现场诊断信息。文件日志等级在启动后显式应用为 INFO，避免设备 NVS 中旧的 `eb_log.level` 覆盖当前项目策略。注意：基础库 INFO 日志会输出 WiFi/Web 认证明文凭据，调试日志和文件日志需要按敏感信息管理。

### Web 页面

| 页面 | 路径 | 功能 |
| --- | --- | --- |
| 首页 | `/index` | 状态、下次预设展示和切换、基础统计、启用滤芯寿命概览 |
| 系统参数 | `/esp32base/app-config` | 安全阈值、流量保护、显示、蜂鸣器和电磁阀参数；由 Esp32Base App Config 提供 |
| 预设 | `/faucet/presets` | 9 组预设的启用、名称、类型和值 |
| 记录 | `/faucet/records` | 分页查看最近出水记录，显示校准状态、实测量和脉冲明细入口 |
| 记录详情 | `/faucet/records/detail` | 单条记录详情、RAM 或已保存脉冲明细；隐藏路由，不进入导航 |
| 校准 | `/faucet/calibration` | 校准会话、实测容量录入、放弃本次样本、生成并确认应用本次校准方案、样本库查看 |
| 校准详情 | `/faucet/calibration/detail` | 校准样本详情、RAM 或已保存脉冲明细；隐藏路由，不进入导航 |
| 计量方案 | `/faucet/metering` | 当前方案、从长期样本库生成方案、手工方案管理、测算、显式启用 |
| 统计 | `/faucet/stats` | 今日、本周、本月、总累计 |
| 滤芯 | `/faucet/filters` | 最多 6 个滤芯的已用天数、已用流量、寿命范围、状态、设置入口和重置 |
| 滤芯设置 | `/faucet/filters/edit?index=N` | 单个滤芯的启用状态、名称、建议更换周期、最长使用周期、寿命流量和上次更换日期配置；隐藏路由，不进入导航 |

### Web API

| 方法 | 路径 | 功能 |
| --- | --- | --- |
| GET | `/api/faucet/status` | 查询状态 |
| GET | `/api/faucet/today` | 查询首页今日概览 HTML 片段 |
| GET | `/api/faucet/presets` | 查询预设 |
| POST | `/api/faucet/presets` | 保存预设配置；`select_previous`、`select_next`、`select` 只切换“下次预设”并返回最新状态，不启动或改变当前出水任务 |
| GET | `/api/faucet/records` | 按时间范围筛选并分页查询出水记录；出水确认、运行和暂停期间返回 busy |
| POST | `/api/faucet/records` | 通过 `action` 执行记录校准、脉冲明细保存/删除；危险 POST 必须通过同源校验；出水确认、运行和暂停期间返回 busy |
| POST | `/faucet/calibration` | 校准会话动作：进入/退出校准模式、保存本次实测容量、放弃本次样本、生成会话方案、确认应用；不提供远程出水/停水 |
| GET | `/faucet/calibration/samples` | 校准样本库片段；隐藏路由，不进入导航 |
| POST | `/faucet/metering` | 计量方案动作：从长期样本库生成、保存/放弃生成结果、create/edit/enable/delete 方案；不提供远程出水/停水 |
| GET | `/api/faucet/stats` | 查询统计 |
| GET | `/api/faucet/filters` | 查询滤芯 |
| POST | `/api/faucet/filters` | 保存指定滤芯的启用状态、名称、寿命和上次更换日期 |
| POST | `/api/faucet/filters/reset` | 重置指定滤芯更换时间和累计流量；出水确认、运行和暂停期间返回 busy |

业务 API 路由必须按 GET/POST 明确注册，非预期方法返回 405；不得把业务配置或危险 action 只挂在 `METHOD_ANY` 上。

禁止注册 `/api/faucet/water/*`、`/api/faucet/start`、`/api/faucet/stop`、`/api/faucet/config`、`/faucet/config` 等旧式或远程出水控制接口。

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

系统配置保存在 `faucet_cfg` namespace，配置版本由 `ConfigStore` 定义。运行统计和滤芯运行量分别保存在 `faucet_stat`、`faucet_run`，不与系统配置版本耦合。流量计计量方案使用独立持久化域保存，避免每次调整实验参数都整块重写系统配置。详细规则见 `docs/config-persistence-migration.md`。

| 版本 | 主要字段 | 加载策略 |
|---|---|---|
| 无版本但有可识别字段 | 早期固件或 AppConfig 曾写字段但没有写 `ver`；可能包含旧流量字段、预设、滤芯 `life_d` 等 | 不按空配置处理；按可识别旧字段迁移为当前支持结构，迁移失败保留原数据且不写当前版本 |
| v1-v10 | 旧配置结构；可能包含旧流量字段、预设、阀控、显示、蜂鸣器、滤芯等参数 | 加载后迁移为当前结构：保留可识别用户配置；旧计量字段迁移为当前流量计计量方案；旧生成结果迁移为计量方案生成结果；旧滤芯 `life_d` 同时迁移为建议周期和最长周期；旧滤芯运行量迁入 `faucet_run` |
| v11-v12 | 旧多参数配置结构；包含固定编号计量配置和旧生成结果配置 | 加载后迁移为流量计计量方案模型：当前启用配置迁移为当前方案，非默认有效配置迁移为独立方案，旧生成结果迁移为计量方案生成结果；只有旧空默认方案时修复为系统内置 YF-S201 默认方案 |
| 未来版本 | 版本号大于当前固件支持版本 | 按当前已知字段只读加载，不自动写回；Web/本地保存会失败，避免降级固件覆盖用户配置 |
| 未知/损坏版本 | 版本号小于 0 或无法识别 | 使用默认配置并记录警告；用户可通过 Web 系统工具重新生成配置 |

所有保存路径都必须先通过 `ConfigStore` 加载和迁移完整当前配置，再合并本次提交字段，通过钳位后提交给 `ConfigStore` 和 `AppController`。AppConfig 和 Web 不得直接写 `faucet_cfg/ver`。出水、确认和暂停期间拒绝热更新配置。

## 校准与计量方案

- 校准主流程基于样本列表：所有有脉冲明细且结束状态可校准的记录都可输入量杯实测容量，不再限制为最后一次出水记录。
- 结果页 `OK` 按住超过 5 秒进入本地校准，不等松开；校准实际水量默认本次显示量，默认步进 0.10L，长按 `OK` 切换 0.10L / 0.01L。
- Web 校准页样本列表同时展示 RAM 临时明细和已保存设备明细；校准 RAM 明细时必须先写入设备样本库，入库失败则本次校准失败。
- Web 校准成功后保存记录级校准元数据；记录页用“已校准/未校准”和出水量单元格内的“实测 x.xx L”展示状态，重校会覆盖该记录的实测量并递增重校次数。
- 分段计量公式和流量计计量方案规则见 `docs/脉冲分段计量参数说明.md`、`docs/segmented-metering.md` 与 `docs/10-flow-meter-metering-schemes.md`。
- 实时流速显示、窗口估算、显示平滑和平均流速区分规则见 `docs/11-realtime-flow-display.md`。
- Web 校准页生成方案后不直接启用；用户确认后只能保存为新方案。保存新方案不会影响当前计量，用户必须显式启用方案后设备才切换运行参数。

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
