# Agent 项目规则

## 项目定位

本项目是基于 ESP32 的智能定量出水龙头固件，当前处于首版实现完成后的逐步上板验证阶段。核心目标是保证本地定量出水安全闭环，再提供 Web 查看和配置能力；Web 不得提供任何远程出水控制能力。

## 权威上下文

- 当前需求、架构、测试和上板依据优先看 `docs/`：
  - `docs/01-product-requirements.md`
  - `docs/03-software-architecture.md`
  - `docs/05-test-plan.md`
  - `docs/07-board-bringup.md`
- 正式项目文档只放在 `docs/`，过程性设计稿和历史文档不作为当前实现入口。
- 所有新项目文档放入 `docs/`；根目录只保留必要工程文件和本规则文件。

## 基础库边界

- ESP32 基础能力统一使用同级目录 `../Esp32Base`，PlatformIO 通过 `symlink://../Esp32Base` 引用。
- 默认启用 `ESP32BASE_PROFILE_FULL`，基础能力包括 Log、Config、System、Bus、Watchdog、Sleep、Fs、FileLog、Health、WiFi、DNS、NTP、mDNS、Web、OTA/Web OTA。
- 本项目不得重复实现 Esp32Base 已提供的基础能力。
- 如果发现基础库能力缺口、基础库 bug，或需要新增基础库设计，不在本项目内打补丁；应在回复中给出可交给 Esp32Base 项目的完整提示词，不在本项目内创建基础库请求文档。

## 架构和实现边界

- 优先实现和维护可 native 测试的业务核心和平台无关逻辑，再接入真实硬件驱动和 Web。
- 分层边界必须保持：业务逻辑不直接操作 GPIO，Web 不直接操作硬件。
- 出水过程中的异常必须优先关阀，再记录日志、统计或更新状态。
- `CANCEL` 软件停止响应目标小于 50ms；自动关阀动作目标小于 100ms。
- Web 请求不得阻塞控制 tick；记录、统计、校准等接口必须分页、小响应或 busy 返回。
- 重启后默认不继续未完成出水任务。
- 四个按键命名为 `CANCEL`、`OK`、`PLUS`、`MINUS`，对应取消、确认、加、减。

## Web 安全边界

- Web 业务页面和业务 API 不得提供启动、暂停、继续、停止出水能力。
- 禁止注册 `/api/faucet/water/*`、`/api/faucet/start`、`/api/faucet/stop` 或同义远程控水接口。
- Web 首页允许切换“下次预设”，但不得打开阀门、启动出水、改变已确认或正在运行任务的 active preset 或本次目标值。
- Web 写配置类 API 必须遵守当前 busy 策略：出水确认、运行、暂停期间不得执行可能阻塞或改变状态的文件扫描、样本聚合、大 JSON 输出或文件写入。

## 配置和持久化

- 本项目不保留旧代码、旧 API 或旧行为的历史包袱。
- 当前测试设备的数据允许格式化重建；配置结构变化按当前结构直接重建，不为旧字段、旧枚举值、旧页面或旧存储键增加长期兼容适配。
- `platformio.local.ini` 用于用户本地 WebOTA 设备地址和认证信息，不得提交真实设备凭据。

## 常用命令

```sh
pio test -e native
pio run -e esp32dev
pio run -e esp32dev_smoke
```

代理需要向测试设备烧录主固件做实验时，必须使用本机串口上传，不使用外部 OTA 或 WebOTA：

```sh
pio run -e esp32dev -t upload --upload-port <端口>
```

首次烧录、分区变化或文件系统变化后，需要通过本机串口上传 LittleFS 镜像：

```sh
pio run -e esp32dev -t uploadfs --upload-port <端口>
```

`pio run -e esp32dev -t webota` 只作为用户自己测试方便的本地方式；代理不要把 WebOTA 当作默认烧录、验证或部署手段。

## 验证要求

- 修改业务核心、配置、Web handler、存储或安全边界后，优先运行 `pio test -e native`。
- 修改主固件集成、驱动接入、构建参数或分区相关内容后，运行 `pio run -e esp32dev`。
- 修改 smoke 固件或裸板验证链路后，运行 `pio run -e esp32dev_smoke`。
- Web 远程控水边界变更必须有 native 测试或明确的路由/API 检查。
- 当前 Flash 使用率已进入预警区间；新增 Web 页面、静态 HTML/CSS、诊断输出或日志字符串前，要评估固件体积影响。

## 协作约定

- 做重要判断前先阅读相关文档和现有实现，不凭记忆改动。
- 不做临时方案、局部补丁或带历史包袱的兼容迁就；如果正确方案影响较大，先说明影响范围和验证方式。
- 每次回复用户时，说明当前是否还有剩余工作；如果有，说明下一步、风险或需要用户决策的点。
