# 实现任务书

## 目标

按 `docs/01-product-requirements.md` 到 `docs/05-test-plan.md` 实现固件。实现顺序必须先保证本地控水安全闭环，再补 Web 查看和配置能力。Web 端不得出现任何出水控制入口。

## 前置状态

- `ESP32BASE_PROFILE_FULL` 下 LittleFS 依赖问题已在 Esp32Base 修复，并在本项目验证通过。
- 本项目通过 `symlink://../Esp32Base` 引用同级基础库，后续基础库修改可直接在本项目验证。

## 目录结构

```text
include/
  app/
    AppConfig.h
    AppTypes.h
    AppController.h
    WaterController.h
    
    ConfigStore.h
    WaterRecordStore.h
    StatisticsStore.h
    FilterStore.h
  drivers/
    ButtonInput.h
    ValveDriver.h
    FlowMeter.h
    DisplayPresenter.h
    BeepDriver.h
    RtcClock.h
  web/
    FaucetWeb.h
src/
  app/
  drivers/
  web/
  main.cpp
test/native/
```

模块命名可以在实现中微调，但分层边界必须保持：业务不直接操作 GPIO，Web 不直接操作硬件。

## 实现顺序

1. 纯 C++ 类型和默认配置
   - 定义预设、滤芯、记录、统计、运行状态、校准结果等定长数据结构。
   - 写默认配置和钳位函数。
   - Native 测试默认值、范围钳位、9 组预设、最多 6 个滤芯。

2. 硬件抽象桩和业务核心
   - 先定义 `ValveDriver`、`FlowMeter`、`ButtonInput` 的接口和 native fake。
   - 实现 `WaterController` 状态机。
   - 覆盖待机、确认、运行、暂停、错误、`CANCEL` 软件停止、安全兜底。

3. 校准参数和算法
   - 删除独立校准控制器，校准直接基于最新可校准出水记录。
   - 删除 4 个校准候选容量配置。
   - 校准基于最新本地出水记录的原始脉冲数；本地结果页长按 OK 进入，Web 记录页保存量杯实际水量，不允许远程打开电磁阀。

4. 存储与统计
   - `ConfigStore` 使用应用 namespace：`faucet_cfg`、`faucet_stat`、`faucet_run`。
   - `StatisticsStore` 支持今日、本周、本月、总累计。
   - `FilterStore` 支持最多 6 个滤芯的配置、已用天数、已用流量和重置。
   - `WaterRecordStore` 使用 LittleFS 二进制定长记录，分页最大 200 条。

5. 设备驱动
   - `ValveDriver`：GPIO16，全压吸合后 PWM 保持，默认全压吸合 10s、保持占空比 70%。业务逻辑、GPIO16 LEDC 适配和主循环接入已完成，待上板验证。
   - `FlowMeter`：GPIO32 中断计数，软件过滤，单一流量系数。业务逻辑、GPIO32 ISR 缓冲适配和主循环接入已完成，待上板验证。
   - `ButtonInput`：GPIO25/33/26/27，内部上拉，低电平有效。业务逻辑、GPIO 低电平读取和主循环接入已完成，待上板验证。
   - `Lcd1602Display` / `DisplayPresenter`：LCD1602 I2C 双行页面和 PCF8574T 驱动已完成，按键/运行唤醒、空闲熄屏和结果显示已接入，待上板验证地址和背包引脚映射。
   - `BeepDriver`：短提示、完成提示、异常提示，可关闭。业务逻辑、GPIO17 LEDC 适配和主循环接入已完成，待上板验证。
   - `RtcClock`：DS3231 自动检测，有则使用，无则降级。I2C 探测、读取和时间策略已接入，待上板验证。

6. Web 页面和 API
   - 注册 `/faucet`、`/faucet/presets`、`/faucet/records`、`/faucet/stats`、`/faucet/filters`、`/faucet/filters/edit`。
   - 注册只读和配置 API：状态、预设、记录、统计、滤芯，以及 records 校准动作。
   - 禁止注册 `/api/faucet/water/*`、`/api/faucet/start`、`/api/faucet/stop` 或同义出水控制接口。
   - Web 记录页允许基于最新记录输入量杯实际水量校准，不提供独立校准页，不允许打开电磁阀。

7. 上板验证
   - 先验证 `CANCEL` 软件停止、电磁阀 PWM、流量计计数。
   - 再验证 LCD1602、本地暂停/继续/本次目标调整、记录、统计、滤芯、Web 页面。
   - 最后做 72 小时连续运行测试。

## 关键实现规则

- `CANCEL` 响应必须小于 50ms；自动关阀动作必须小于 100ms。
- 出水过程中的异常优先关阀，后记录日志和更新统计。
- Web 请求不得阻塞控制 tick；记录和统计响应必须分页或小响应。
- 重启后默认不继续未完成出水任务。
- Web 默认账号密码为 `admin/admin`，配置页提示修改但不强制。
- 硬件电源开关不在软件中检测、控制或建模。

## 当前接入状态

同步日期：2026-05-10。

- `main.cpp` 已接入本地控水主循环：配置加载、四键读取、`CANCEL` ISR 快速停止、流量 ISR 缓冲、业务 tick、电磁阀 PWM 输出、蜂鸣器事件输出、RTC 自动检测。
- 本地常用出水已接入：待机用 `PLUS`/`MINUS` 选择预设，`OK` 进入确认，确认/暂停页用 `PLUS`/`MINUS` 调整本次目标水量，默认 0.10L，长按 `OK` 切换 0.10L/0.50L 步进，运行中 `OK` 暂停，暂停中 `OK` 继续，`CANCEL` 停止；结果页 `OK` 按住超过 5 秒进入现场校准。
- 出水结果显示已接入：完成、用户停止、暂停超时、安全停止或流量异常后显示本次结果，默认 15 秒，可在 Web 配置。
- 统计和滤芯运行数据已接入 NVS 持久化：出水任务完成后保存 `faucet_stat` 和 `faucet_run`，避免每个 tick 写入。
- 出水记录已接入 LittleFS 文件环形存储：通过 Esp32Base Fs 按偏移读写 API 实现 20000 条目标容量的二进制定长记录，文件不可用时保留 RAM 环形降级写入。记录包含目标值、原始脉冲数、过滤脉冲数和当时流量系数。文件启动时只创建头部，记录按需追加，满容量后再环形覆盖，避免裸板首次启动预分配大文件触发 watchdog。
- LCD1602 页面模型、PCF8574T I2C 驱动、刷新节流、按键/运行唤醒和空闲熄屏已接入，默认地址 0x27。
- Web 路由壳已接入 Esp32Base：13 条页面/API 路由；滤芯编辑页作为隐藏 HTML 路由注册，不进入导航；已用 native 测试禁止远程出水控制路径。
- Web API 已接入真实状态、预设、记录分页、统计、滤芯和 records 校准动作；滤芯配置 POST 与重置 POST 已接入，重置后写入当前时间、清零累计流量并返回滤芯列表。
- Web 写配置类 API 已通过 Esp32BaseWeb 当前请求方法能力接入 POST 保存：支持配置、单个预设保存；空闲状态下保存后立即热更新到运行中控制器、流量计、电磁阀、LCD 和蜂鸣器，出水/确认/暂停期间拒绝修改并返回 busy。
- Web 写配置类 API 采用“复制当前配置 -> 修改候选配置 -> 钳位 -> 保存 -> 热更新”的提交路径；checkbox 未提交会按 false 处理，避免用户关闭蜂鸣器或预设时旧值被保留。
- 配置加载已覆盖 v1/v2/v3/v4 到 v5 的迁移；v5 删除旧校准候选容量，旧 `cal_ml` 和 `cal*_ml` 加载时忽略，旧 `oled_s` 会迁移为 `displaySleepSec`；检测到未来配置版本时只读加载当前字段并拒绝保存，避免降级固件静默覆盖用户配置。
- 主循环已将流量 ISR 缓冲丢脉冲计数写入状态快照并记录告警日志；开机后检查 LittleFS 剩余空间并在低于 100KB 时告警。
- Web 页面已从安全占位升级为轻量可用页面：状态、预设、记录、统计、滤芯页面已接入；首页用卡片展示启用滤芯寿命进度；滤芯列表页只显示查看、状态、设置入口和重置，单个滤芯配置放在独立编辑页；记录页支持分页按钮和每页条数下拉，选项为 20、30、50、100、200，默认 20，并显示原始脉冲及最新记录校准入口；页面仍不提供任何远程出水控制入口。

## Native 测试优先级

1. `ConfigStore` 默认值和钳位。
2. `WaterController` 状态机和安全兜底。
3. `ButtonInput` 四键事件。
4. records 校准 API 和本地结果页校准。
5. `StatisticsStore` 周期累计。
6. `FilterStore` 已用天数和流量。
7. `WaterRecordStore` 分页和滚动。
8. `FaucetWeb` 路由黑名单：不存在远程出水控制路径。
9. `AppController` 本次目标调整、暂停/继续和结果显示。

## 完成定义

- `pio test -e native` 通过。
- `pio run -e esp32dev` 通过。
- 上板验证满足 `docs/05-test-plan.md` 验收标准。
- 裸板和逐步接线验证记录见 `docs/07-board-bringup.md`。
- 文档、代码和默认参数保持一致。
