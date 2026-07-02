# 软件架构

## 当前已确认原则

- 项目基于 Arduino + PlatformIO。
- ESP32 基础能力统一来自同级目录 `../Esp32Base`，PlatformIO 通过 `symlink://../Esp32Base` 引用。
- 默认启用 `ESP32BASE_PROFILE_FULL`。
- 本项目只实现业务层和业务相关硬件驱动，不复制基础库已有能力。
- 架构优先简单可靠，不主动拆成复杂多任务系统。

## 分层方向

- Application：`src/main.cpp`，负责固件信息、Esp32Base 生命周期、Web 默认认证、业务模块初始化和主循环调度。
- Business：承载出水状态机、配置模型、日志、统计、滤芯、Web 业务 API。
- Drivers：承载电磁阀、流量计、四键、ST7789 TFT、本地蜂鸣器、RTC。
- Base：Esp32Base 提供 Log、Config、System、Bus、Watchdog、Sleep、Fs、FileLog、Health、WiFi、DNS、NTP、mDNS、Web、OTA。

## 调度模型

- 主循环中先执行业务控制 tick，再执行 `Esp32Base::handle()`。
- 业务控制 tick 必须短小、非阻塞，目标单次耗时小于 1ms。
- 流量计和 `CANCEL` 使用 ISR 采集最小事件，ISR 内不做复杂计算和日志。
- Web 请求只允许查询状态、读取日志、保存配置或保存校准参数，不投递任何出水控制命令。
- 不主动创建多个 FreeRTOS 业务任务；只有验证发现 loop tick 无法满足实时性时，再提出明确设计变更。

## 核心模块

| 模块 | 职责 |
| --- | --- |
| `AppController` | 业务初始化、tick 调度、模块组合 |
| `WaterController` | 出水状态机、安全兜底、命令处理 |
| `ConfigStore` | 应用配置默认值、钳位、读写 |
| `FlowMeter` | 脉冲计数、容量换算、实时流速窗口估算和诊断瞬时流速 |
| `WaterPulseTraceStore` / `WaterPulseTraceAnalysis` | 校准脉冲轨迹暂存、趋势聚合、稳定段识别和分段校准拟合 |
| `AdcReader` / `Esp32AnalogAdcReader` | ADC 抽象与 ESP32 原生 ADC1 读取，native 测试使用 fake ADC |
| `WaterSensors` | 输入电压、50K B3950 NTC 和 TDS AO 的纯算法换算 |
| `WaterSensorManager` | 1s 采样、ADC 故障降级、TDS 自动量程、实时快照和出水摘要聚合 |
| `ValveDriver` | 电磁阀开关、全压吸合、PWM 保持 |
| `ButtonInput` | 四键消抖、短按/长按 |
| `ColorDisplayPresenter` | 240x240 TFT 页面模型、状态摘要和刷新节流 |
| `BeepDriver` | 操作、完成、异常提示 |
| `WaterRecordFileStore` / `WaterRecordStore` | 文件记录写入、滚动、分页；记录查询和统计聚合 |
| `StatisticsStore` | 今日、本周、本月、总累计 |
| `FilterStore` | 最多 6 个滤芯的配置、已用天数、已用流量和重置 |
| `FaucetWeb` | 业务页面和 `/api/faucet/...` API |

水温、TDS 和输入电压只进入实时状态、出水记录、统计和诊断页面，不进入 `FlowMeter`、`WaterController` 或安全关阀判断。ADC 读取失败只影响传感器状态，不得阻塞按键、关阀、流量计量或 Web 轻量状态查询。

## 状态机

- `INIT`：加载配置并初始化业务驱动。
- `IDLE`：待机，可切换预设和进入确认。
- `CONFIRM`：等待二次确认，超时取消。
- `RUNNING`：出水中，持续检查容量、时间和异常。
- `PAUSED`：暂停关阀，等待继续或超时停止。
- `ERROR`：异常提示，阀门保持关闭。
- `SLEEP`：本地屏背光熄灭或低功耗状态。

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
| `KeyOkLong` | 四键输入 | 确认/暂停/结果页不触发额外功能 |
| `KeyPlusShort` | 四键输入 | 待机选择下一个预设；确认页按配置步进增加本次目标值 |
| `KeyMinusShort` | 四键输入 | 待机选择上一个预设；确认页按配置步进减少本次目标值 |
| `ConfirmTimeout` | timer | 二次确认超时 |
| `PauseTimeout` | timer | 暂停超时停止 |
| `TargetReached` | `WaterController` | 容量或时间达到目标 |
| `SafetyLimitReached` | `WaterController` | 最大时间、最大量、超预设百分比 |
| `NoFlowTimeout` | `FlowMeter` | 开阀后无流量 |
| `HighFlowTimeout` | `FlowMeter` | 异常大流量持续超时 |
| `ResultDisplayTimeout` | timer | 本次出水结果显示结束 |
| `DisplaySleepTimeout` | timer | 本地屏背光熄灭 |
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

## 存储

- 应用 NVS namespace 使用 `faucet_cfg`、`faucet_stat`、`faucet_run`，不得使用 `eb_` 前缀。
- 配置使用版本化结构。当前测试阶段只读取版本号等于当前固件支持版本的配置。
- 无版本、非当前版本或不可识别版本配置一律按当前默认配置加载；加载过程不探测历史字段、不合并历史字段、不自动写回。用户显式保存配置时，系统按当前结构重写完整配置。
- 出水记录放 LittleFS 环形文件，分页读取，底层单次最多 200 条，记录 JSON API 为保证 32KB 响应缓冲单次最多 80 条；当前 4MB Flash 分区预算下工程容量固定为 15,000 条，记录结构 40 字节时记录文件约 586 KiB，达到容量边界后覆盖最旧记录。记录保存目标值、有效脉冲数、最小间隔过滤数、当时启用的计量方案 ID、结束前最后滑动窗口的水温/TDS 值和传感器状态标志，不保存 5 个计量参数快照，也不保存记录级实测量或长期校准样本。历史追溯通过方案 ID 查找对应计量方案；计量参数历史固定 6 条，槽位满后覆盖最早的非当前参数，早期历史记录查不到参数时显示已覆盖。校准实测容量只保存在当前/最近一次校准会话样本中。
- 出水记录时间回溯不得依赖 32 位 `uptimeMs`。未同步时间时只保存启动内相对秒和 boot id；NTP 同步后通过 Esp32Base boot event 解析真实时间并重写记录，避免 `millis()`/uptime 回绕影响历史记录。
- 统计放 NVS，出水完成后立即更新，周期性数据按日期变化重置。
- 运行快照默认只用于安全恢复判断，重启后默认不继续出水。
- 滤芯数据放 NVS，记录每个滤芯的启用状态、名称、建议更换天数、最长使用天数、寿命流量、开始时间和累计流量。
- 水温/TDS 实时采样不写 Flash。TDS 校准只有在用户保存校准结果时写一次系统配置；出水记录只在出水结束时写一次。统计仍按出水结束事件更新，不按 1s 采样周期写 Flash。
- 出水记录文件格式随传感器字段升级为当前版本。当前项目仍处于测试阶段，测试记录不做跨结构兼容读取；文件头版本不匹配时应明确返回不兼容，由上板测试阶段手动格式化或重建记录文件。
- 流量计计量参数使用独立持久化域：保存当前参数 ID 和最多 6 条历史参数记录。系统内置 `默认计量参数` 作为首次启动兜底参数，启动脉冲数 8P、启动水量 130ml、稳态 248P/L、启动时间 5s、预计稳态流速 1.95L/min。运行时只读取当前参数的启动脉冲数、启动水量、稳态 P/L；校准推荐参数确认应用或 Web 手工设置参数保存后，创建新的历史参数记录并立即成为当前参数。历史参数不提供原地编辑、删除或恢复入口；复制历史参数只用于预填手工设置表单，保存后创建新参数 ID。参数文件固定 6 槽；槽位满后覆盖最早的非当前参数，早期历史记录查不到参数时显示已覆盖。
- 校准会话使用独立存储：RAM 只保留当前/最近一次出水的完整脉冲明细；会话样本区为 Flash 固定 6 槽位，新会话复用槽位并保留上次会话明细供查看。每条有效样本在会话记录中保存轻量摘要，后续生成计量参数直接使用摘要，不反复扫描完整脉冲明细。校准出水本身仍是正常出水，必须进入出水记录、统计和滤芯累计。

## Web

- 业务 API 前缀为 `/api/faucet/...`。
- Web 页面通过 Esp32Base 注册，内置 OTA/WiFi/基础状态入口不重复实现。
- Web 采用业务优先首页模型：`/` 进入 `/index`，`/esp32base` 保留为系统工具入口；系统工具放在页面底部小字区域。
- 配置写入、恢复出厂、重启等操作使用 POST，并需要 Basic Auth。
- 记录、统计和参数页面/API 必须分页或小响应，避免大内存拼接。
- Web 端不得注册启动出水、暂停出水、继续出水、停止出水 API 或按钮；首页允许切换“下次预设”，但只修改后续任务使用的 selected preset，不改变已确认或正在运行的 active preset、目标值或阀门状态。
- Web records 页支持最近记录分页，不提供按时间范围筛选；按时间范围查询只保留在 `/api/faucet/records`。列表保持紧凑，显示目标、出水量、持续时间、模式、结束原因、脉冲/P-L、计量参数、传感器摘要和操作。过滤脉冲只在有值时作为脉冲单元格内的辅助诊断信息展示。普通记录页不提供独立脉冲明细保存入口，也不显示记录级实测量或“已校准”；脉冲明细只通过 RAM 最近明细和校准会话样本区查看；不允许远程打开电磁阀。
- Web 默认认证通过 Esp32Base `setDefaultAuth()` 设置为 `admin/admin`；用户可通过 `/esp32base/auth` 修改认证，已保存认证优先于应用默认值。
- WebOTA 目标地址和凭据不写入仓库；本地复制 `platformio.example.ini` 为 `platformio.local.ini` 后填写 `custom_esp32base_webota_*`。
- 当前构建将 Esp32Base 串口日志等级设为 DEBUG，文件日志默认等级设为 WARN。生产和常规调试都保持文件日志 WARN，避免长期 INFO 日志反复写 Flash；需要详细过程日志时优先查看串口日志。若设备 NVS 中已有 FileLog 模式配置，应通过 Esp32Base 系统日志页面人工调整，不在固件启动时静默覆盖用户配置。

### Web 页面

| 页面 | 路径 | 功能 |
| --- | --- | --- |
| 首页 | `/index` | 状态、下次预设展示和切换、基础统计、传感器实时状态、启用滤芯寿命概览 |
| 系统参数 | `/esp32base/app-config` | 安全阈值、流量保护、显示、蜂鸣器、电磁阀和传感器开关参数；由 Esp32Base App Config 提供 |
| 预设 | `/faucet/presets` | 9 组预设的启用、名称、类型和值 |
| 记录 | `/faucet/records` | 分页查看最近出水记录，显示计量方案、传感器摘要和普通详情入口 |
| 记录详情 | `/faucet/records/detail` | 单条普通记录详情；隐藏路由，不进入导航 |
| 校准 | `/faucet/calibration` | 校准首页，概览当前计量参数、温度状态和水质状态，并进入各校准详情 |
| 流量计校准 | `/faucet/calibration/flow` | 流量校准流程、本次会话样本、推荐参数和历史参数；隐藏路由，不进入顶层导航 |
| 校准详情 | `/faucet/calibration/detail` | 校准样本详情、RAM 或会话样本脉冲明细；隐藏路由，不进入导航 |
| 统计 | `/faucet/stats` | 今日、本周、本月、总累计、水温趋势和 TDS 趋势 |
| 滤芯 | `/faucet/filters` | 最多 6 个滤芯的启用状态、上次更换日期、已用天数、已用流量、建议/最长/流量寿命详情、状态、设置入口和重置 |
| 滤芯设置 | `/faucet/filters/edit?index=N` | 单个滤芯的启用状态、名称、建议更换周期、最长使用周期、寿命流量和上次更换日期配置；隐藏路由，不进入导航 |

### Web API

| 方法 | 路径 | 功能 |
| --- | --- | --- |
| GET | `/api/faucet/status` | 查询状态，包含传感器实时快照 |
| GET | `/api/faucet/today` | 查询首页今日概览 HTML 片段 |
| GET | `/api/faucet/presets` | 查询预设 |
| POST | `/api/faucet/presets` | 保存预设配置；`select_previous`、`select_next`、`select` 只切换“下次预设”并返回最新状态，不启动或改变当前出水任务 |
| GET | `/api/faucet/records` | 按时间范围筛选并分页查询出水记录；出水确认、运行和暂停期间返回 busy |
| POST | `/faucet/calibration` | 温度校准、TDS 校准点采样、删除、应用和放弃；不提供远程出水/停水 |
| GET | `/faucet/calibration/flow` | 流量计校准中心页面；隐藏路由，不进入顶层导航 |
| POST | `/faucet/calibration/flow` | 流量校准会话动作、录入实测容量、放弃当前待录入样本、应用推荐参数；不提供远程出水/停水 |
| GET | `/api/faucet/stats` | 查询统计，包含每日水温/TDS 摘要 |
| GET | `/api/faucet/filters` | 查询滤芯 |
| POST | `/api/faucet/filters` | 保存指定滤芯的启用状态、名称、寿命和上次更换日期 |
| POST | `/api/faucet/filters/reset` | 重置指定滤芯更换时间和累计流量；出水确认、运行和暂停期间返回 busy |

业务 API 路由必须按 GET/POST 明确注册，非预期方法返回 405；不得把业务配置或危险 action 只挂在 `METHOD_ANY` 上。

禁止注册 `/api/faucet/water/*`、`/api/faucet/start`、`/api/faucet/stop`、`/api/faucet/config`、`/faucet/config` 等远程出水控制或业务配置别名接口。

## 数据模型

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

struct FilterConfig {
    bool enabled;
    char name[46];       // up to 15 Chinese chars plus null terminator
    uint32_t recommendDays; // suggested replacement point, 0 when disabled
    uint32_t maxDays;       // maximum usage point, 0 when disabled
    uint32_t lifeMl;     // 0 when disabled
};

struct FilterRuntime {
    uint32_t startTime;  // seconds since 2000-01-01, 0 when unknown
    uint32_t usedMl;
    uint32_t startBootId;
};

struct FilterRecord {
    bool enabled;
    char name[46];
    uint32_t recommendDays;
    uint32_t maxDays;
    uint32_t lifeMl;
    uint32_t startTime;
    uint32_t usedMl;
    uint32_t startBootId;
};

struct WaterRecord {
    uint32_t startTime;    // UTC seconds or relative seconds when time is unknown
    uint32_t startBootId;  // boot id when time is boot-relative
    uint32_t volumeMl;
    uint32_t targetValue;  // ml for Volume, seconds for Time
    uint32_t pulseCount;
    uint32_t filteredPulseCount;
    uint32_t meteringSchemeId;
    uint16_t durationSec;
    int16_t temperatureCentiC;
    uint16_t tdsPpm;
    uint16_t sensorFlags;
    uint8_t mode;          // volume/time
    uint8_t result;        // completed/stopped/safety/error/pause_timeout
    uint8_t selectedPreset;
    uint8_t sensorSampleCount;
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

`WaterRecord` 当前实现为 40 字节并有 `static_assert` 约束。出水期间传感器每 1s 采样一次，单次记录只保存结束前最后 5 个采样窗口的水温/TDS 均值；样本不足 5 个时按已有样本计算。温度和 TDS 分别通过 `sensorFlags` 标记是否无有效值，避免只启用其中一个传感器时把另一个传感器的默认值当作有效数据。统计聚合按温度/TDS 各自有效性分别计算日均值、最小值和最大值。默认统计排除未校准 TDS 对 TDS 数据的影响，但不丢弃同条记录中有效的温度；API 可按需要包含未校准 TDS 并显示标记。

## 持久化配置版本化

系统配置保存在 `faucet_cfg` namespace，配置版本由 `ConfigStore` 定义。运行统计和滤芯运行量分别保存在 `faucet_stat`、`faucet_run`，不与系统配置版本耦合。流量计计量参数使用独立持久化域保存，避免每次调整实验参数都整块重写系统配置。用户保存系统配置时按当前结构完整写入，并最后写入当前版本号；保存失败不得更新版本号，避免半写入数据被下一次启动当作完整当前配置读取。出水记录、校准会话样本和计量参数文件都有自己的文件头、版本和校验规则；版本或结构不匹配时返回不可用状态，测试阶段通过人工格式化或重建恢复。

| 版本 | 主要字段 | 加载策略 |
|---|---|---|
| 无版本 | 空设备或手动残留字段 | 使用当前默认配置；加载过程不写回 |
| 非当前版本 | 版本号不等于当前固件支持版本 | 使用当前默认配置；用户显式保存时写入当前结构 |
| 当前版本 | 版本号等于当前固件支持版本 | 加载当前结构字段，钳位后供运行使用 |
| 不可识别版本 | 版本号小于 0 或无法识别 | 使用默认配置并记录警告；用户显式保存配置时按当前结构重写 |

所有保存路径都必须先通过 `ConfigStore` 加载配置对象，再合并本次提交字段，通过钳位后提交给 `ConfigStore` 和 `AppController`。AppConfig 和 Web 不得直接写 `faucet_cfg/ver`。出水、确认和暂停期间拒绝热更新配置。

## 校准与计量方案

- 校准首页分为当前计量参数、温度校准、水质校准三块，只做状态概览和入口；温度详情使用 `/faucet/calibration?view=temperature`，水质详情使用 `/faucet/calibration?view=tds`，不新增路由。
- 流量计校准中心通过 `/faucet/calibration/flow` 进入，页面内同时展示校准流程、当前会话样本、推荐参数和历史参数；进入页面不会开始出水。
- 校准会话最少 2 条有效样本可生成，推荐 3 条，有效样本最多 6 条，尝试最多 6 次；实际出水/停水只允许本地按键执行，Web 只录入实测容量、放弃当前待录入样本或应用推荐参数。
- 温度校准详情页输入温度计读数后保存偏移；TDS 校准详情页以 1-5 个校准点作为统一模型，保存稳定采样点后自动生成候选参数，用户确认“使用这组参数”后才写入系统配置。未校准 TDS 仍可显示和记录，但标记为未校准。
- 分段计量公式和流量计计量方案规则见 `docs/10-flow-meter-metering-schemes.md`。
- 实时流速显示、窗口估算、显示平滑和平均流速区分规则见 `docs/11-realtime-flow-display.md`。
- Web 校准页生成候选参数后提供“使用这组参数”；应用后直接成为最新当前参数。页面也提供手工设置当前计量参数入口；历史参数用于查看和复制预填，不做原地修改。

## 已确认实现决策

- Esp32Base 当前 Web 能力按轻量页面、分页、小响应方式使用；若能力不足，按项目规则生成基础库请求。
- `CANCEL` ISR 只记录高优先级停止事件，不在 ISR 内做复杂日志、文件、Web 或状态机操作。
- 控制 tick 必须足够频繁，保证 `CANCEL` 软件停止响应不超过 50ms。
- 日志文件采用二进制定长记录，Web 输出时转换为 JSON。
- DS3231 按自动检测实现；未焊接时 RTC 驱动自动降级。
