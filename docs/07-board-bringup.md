# ESP32 上板验证记录

## 当前连接状态

- 当前只连接 ESP32 核心板，未连接按键、电磁阀、流量计、OLED、蜂鸣器、DS3231 等外设。
- 当前电脑已枚举出 `/dev/cu.usbserial-130`，USB VID/PID 为 `1A86:7523`。
- `esp32dev_smoke` 裸板自检固件已验证核心板、串口、Flash 和 Arduino 启动链路正常。
- 已定位并修复分区表 `ota_0=0x20000` 与 PlatformIO 默认上传偏移 `0x10000` 不一致的问题。
- 工程已通过 `board_upload.offset_address = 0x20000` 让串口上传位置与双 OTA 分区表一致。
- 主固件已在裸板上启动成功：`rtc=absent`、`oled=absent`、`log=file`，WiFi/Web/mDNS/NTP 正常，18 秒串口观察未出现 watchdog 或反复重启。

## 裸板验证目标

裸板阶段只验证固件在缺少所有业务外设时可以安全启动和降级：

- Esp32Base 正常启动。
- LittleFS 正常挂载。
- Web、WiFi、OTA 基础能力正常。
- DS3231 不存在时降级为 uptime 时间。
- OLED 不存在时不影响主循环。
- 流量计无脉冲时不误计数。
- 三个按键未连接时不应触发出水、校准或恢复出厂。
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

串口出现后执行：

```sh
pio run -e esp32dev_smoke -t upload --upload-port <端口>
pio run -e esp32dev -t upload --upload-port <端口>
```

当前 CH340 串口在 `921600` 上传后切换波特率阶段可能失败，主环境和 smoke 环境都使用 `115200` 低速上传，优先保证上板可靠性。

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

期望日志：

- 固件名为 `esp32-faucet`。
- 应用初始化日志出现。
- `rtc=absent` 合理，因为当前未接 DS3231。
- `oled=absent` 合理，因为当前未接 OLED。
- `log=file` 优先，若 LittleFS 初始化异常则需要排查分区或文件系统。
- 不应反复重启，不应出现 panic/backtrace。

## 逐步接线验证顺序

1. OLED：接 I2C SDA/SCL，验证地址、方向、双行显示、空闲熄屏和按键唤醒。
2. 三个按键：验证 `STOP`、`OK`、`NEXT` 低电平有效、消抖、长按和组合键。
3. 蜂鸣器：验证短提示、异常提示和 Web 关闭蜂鸣器配置。
4. DS3231：验证自动检测、时间读取、断开后降级。
5. 流量计：先用手动脉冲或低频信号验证计数，再接水路校准。
6. 电磁阀：先不接水验证 PWM 输出和 STOP 关断，再接水做安全验证。
7. 完整水路：验证定量出水、本地校准、日志、统计、滤芯累计。

## 暂不执行的验证

以下必须等对应硬件连接后执行：

- `STOP` 软件停止响应时间。
- 电磁阀关断时间。
- 流量校准精度。
- OLED 页面肉眼确认。
- 蜂鸣器提示音。
- 72 小时连续运行。

## 2026-05-06 裸板复测

连接状态：只连接 ESP32 核心板，串口为 `/dev/cu.usbserial-130`，未接业务外设。

本次已验证：

- `pio device list` 可识别 CH340 串口。
- `pio test -e native` 通过，94 个 native 用例全部成功。
- `pio run -e esp32dev` 通过，主固件 RAM 约 28.6%，Flash 约 68.1%。
- `pio run -e esp32dev_smoke` 通过。
- 主固件串口启动正常：进入 `setup()`，`rtc=absent`、`oled=absent`、`log=file`，WiFi 已连接，Web 服务就绪，NTP 已同步。
- Web 首页 `http://192.168.2.112/faucet` 返回 200。
- 未授权访问 `/faucet` 返回 401。
- 状态 API 返回 `idle`、`valveOpen=false`、`waterControl=false`。
- 远程出水控制路径 `/api/faucet/start` 返回 404。
- `/api/faucet/stats`、`/api/faucet/presets`、`/api/faucet/filters`、`/api/faucet/calibration`、`/api/faucet/logs?page=0&pageSize=10` 均可访问。
- 日志 API 返回 `storage=file`。
- 校准 API 返回 `webCanStartCalibration=false`。

观察到但不影响本次裸板验证：

- 启动早期仍有 ESP-IDF core dump 分区提示，随后应用正常启动。
- 本机访问 `water-65e4.local` 解析超时，IP 访问正常；后续可在网络环境或 mDNS 客户端侧继续确认。
