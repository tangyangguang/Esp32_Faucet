# ESP32 上板验证记录

## 当前连接状态

- 当前只连接 ESP32 核心板，未连接按键、电磁阀、流量计、LCD1602、蜂鸣器、DS3231 等外设。
- 当前电脑已枚举出 `/dev/cu.usbserial-130`，USB VID/PID 为 `1A86:7523`。
- `esp32dev_smoke` 裸板自检固件已验证核心板、串口、Flash 和 Arduino 启动链路正常。
- 已定位并修复分区表 `ota_0=0x20000` 与 PlatformIO 默认上传偏移 `0x10000` 不一致的问题。
- 工程已通过 `board_upload.offset_address = 0x20000` 让串口上传位置与双 OTA 分区表一致。
- 主固件已在裸板上启动成功：`rtc=absent`、`lcd=absent`、`records=file`，WiFi/Web/NTP 正常，设备端 mDNS 服务已启动，18 秒串口观察未出现 watchdog 或反复重启。

## 裸板验证目标

裸板阶段只验证固件在缺少所有业务外设时可以安全启动和降级：

- Esp32Base 正常启动。
- LittleFS 正常挂载。
- Web、WiFi、OTA 基础能力正常。
- DS3231 不存在时降级为 uptime 时间。
- LCD1602 不存在时不影响主循环。
- 流量计无脉冲时不误计数。
- 裸板未接流量计时不应出现持续的 `flow pulse buffer dropped pulses`；若出现，优先检查 GPIO32 是否悬空、流量计信号是否有稳定上拉和 3.3V 电平转换。
- 四个按键未连接时不应触发出水、暂停、停止或调整。
- 电磁阀 GPIO 无负载时不影响启动。

## 串口识别检查

在 macOS 上优先检查：

```sh
pio device list
ls -1 /dev/cu.* /dev/tty.* | sort
```

常见可上传端口形态：

- `/dev/cu.usbserial-*`
- `/dev/cu.SLAB_USBtoUART`
- `/dev/cu.wchusbserial*`
- `/dev/cu.usbmodem*`

如果没有出现：

- 换一根确定支持数据传输的 USB 线。
- 尽量直连电脑或换一个 USB Hub/Type-C 转接器端口。
- 确认核心板电源灯是否亮。
- 确认核心板 USB 转串口芯片型号，必要时安装对应 macOS 驱动。
- 某些板子上传时需要按住 `BOOT`，开始上传后松开；复位可按 `EN/RST`。

## 裸板上传命令

首次烧录、分区调整或设备无法联网时，串口出现后执行：

```sh
pio run -e esp32dev_smoke -t upload --upload-port <端口>
pio run -e esp32dev -t upload --upload-port <端口>
```

当前主环境和 smoke 环境串口上传速度使用 `460800`，避免 `921600` 在部分 CH340 板子上切换波特率失败。

日常需要烧录测试主固件时，优先显示并使用 Esp32Base 快速 Web OTA：

```sh
pio run -e esp32dev -t webota
```

`webota` 使用本地 `platformio.local.ini` 中的 `custom_esp32base_webota_host` IP 地址，不依赖 mDNS。首次使用前复制 `platformio.example.ini` 为 `platformio.local.ini` 并填写设备地址和认证信息。只有首次烧录、文件系统/分区变化、网络不可达或 OTA 失败恢复时，再回到串口上传。

本项目分区表首个应用分区为 `ota_0`，偏移是 `0x20000`。`platformio.ini` 必须保留：

```ini
board_upload.offset_address = 0x20000
```

LittleFS 使用 Arduino ESP32 默认分区名 `spiffs`，分区内容仍由 LittleFS 格式化和挂载。

首次烧录或分区调整后需要上传 LittleFS 镜像：

```sh
pio run -e esp32dev -t uploadfs --upload-port <端口>
```

主固件串口监视：

```sh
pio device monitor -e esp32dev --port <端口> --baud 115200
```

当前构建使用 `ESP32BASE_DEFAULT_HOSTNAME="water-faucet"`、`ESP32BASE_ENABLE_APP_CONFIG=1`、`ESP32BASE_APP_CONFIG_MAX_GROUPS=5`、`ESP32BASE_APP_CONFIG_MAX_FIELDS=32`、`ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_DEBUG`，文件日志使用 `ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_INFO`，并在启动后显式应用 `Esp32BaseFileLog::setMode(INFO)`，避免旧 NVS 配置覆盖。系统级参数配置入口迁移到 Esp32Base 内置 `/esp32base/app-config`，字段直接绑定现有 `faucet_cfg` namespace/key；预设和滤芯配置仍只能在本项目业务页面中修改。实际运行 hostname 可由 Esp32Base 内置 `/esp32base/tools` 写入 `eb_sys.hostname`，重启后生效。Web 默认认证通过 Esp32Base `setDefaultAuth()` 提供，用户可在 `/esp32base/auth` 修改。注意：Esp32Base INFO 日志会输出 WiFi 和 Web Auth 明文凭据，串口日志和 `/esp32base/logs` 文件日志需要按敏感信息管理。

期望日志：

- 固件名为 `esp32-faucet`。
- 应用初始化日志出现。
- `rtc=absent` 合理，因为当前未接 DS3231。
- `lcd=absent` 合理，因为当前未接 LCD1602。
- `records=file` 优先，若 LittleFS 初始化异常则需要排查分区或文件系统。
- 不应反复重启，不应出现 panic/backtrace。
- LittleFS 使用 Arduino ESP32 默认 `spiffs` 分区 label 承载，这是当前 PlatformIO/Arduino 文件系统命名兼容约定；维护时以 `board_build.filesystem = littlefs` 为实际文件系统口径。

## 逐步接线验证顺序

1. LCD1602：接 I2C SDA/SCL，验证 0x27 地址、背光、双行显示、空闲熄屏和按键唤醒。
2. 四个按键：验证 `CANCEL`、`OK`、`PLUS`、`MINUS` 低电平有效、消抖和长按。
3. 蜂鸣器：验证短提示、异常提示和 Web 关闭蜂鸣器配置。
4. DS3231：验证自动检测、时间读取、断开后降级。
5. 流量计：先用手动脉冲或低频信号验证计数，再接水路验证定量误差。
6. 电磁阀：先不接水验证 PWM 输出和 `CANCEL` 关断，再接水做安全验证。
7. 完整水路：验证定量出水、暂停/继续、本次目标调整、记录、统计、滤芯累计。

## 暂不执行的验证

以下必须等对应硬件连接后执行：

- `CANCEL` 软件停止响应时间。
- 电磁阀关断时间。
- 定量出水精度。
- LCD1602 页面肉眼确认。
- 蜂鸣器提示音。
- 72 小时连续运行。

## 2026-05-06 裸板复测

连接状态：只连接 ESP32 核心板，串口为 `/dev/cu.usbserial-130`，未接业务外设。

本次已验证：

- `pio device list` 可识别 CH340 串口。
- 2026-06-02 代码侧复测：`pio test -e native` 通过，273 个 native 用例全部成功。
- 2026-06-02 代码侧复测：`pio run -e esp32dev` 通过，主固件 RAM 约 26.3%，Flash 约 88.4%。
- 固件体积预算：当前双 OTA app 分区为 `0x160000`，Flash 使用率已超过 85% 预警线；继续增加 Web 页面、诊断或日志前，优先评估静态 HTML/CSS 字符串体积、可静态化资源迁移到 LittleFS，或重新评估分区表。
- `pio run -e esp32dev_smoke` 通过。
- 主固件串口启动正常：进入 `setup()`，`rtc=absent`、`lcd=absent`、`records=file`，WiFi 已连接，Web 服务就绪，NTP 已同步。
- Web 首页 `http://192.168.2.112/index` 返回 200。
- 未授权访问 `/index` 返回 401。
- 状态 API 返回 `idle`、`valveOpen=false`、`waterControl=false`。
- 远程出水控制路径 `/api/faucet/start` 返回 404，`/api/faucet/presets` 对 `action=select_previous/select_next/select` 只切换“下次预设”并返回最新状态，不打开阀门、不启动出水、不改变当前出水任务。
- `/api/faucet/status`、`/api/faucet/today`、`/api/faucet/stats`、`/api/faucet/presets`、`/api/faucet/filters`、`/api/faucet/records?page=0&pageSize=10` 均可访问。
- 记录 API 返回 `storage=file`。
- 校准页使用 `POST /faucet/calibration` 携带 `action=calibrate`、`traceSource`、`trace` 和 `actualMl`，基于样本列表中的 RAM 或已保存脉冲明细保存量杯实测容量。

观察到但不影响本次裸板验证：

- 启动早期仍有 ESP-IDF core dump 分区提示，随后应用正常启动。
- 本机访问 `water-65e4.local` 解析超时，IP 访问正常；当前只能确认设备端 mDNS 服务启动，macOS 客户端解析链路仍未闭环，后续需在网络环境或 mDNS 客户端侧继续确认。
