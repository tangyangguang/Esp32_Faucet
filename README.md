# Esp32 Faucet

基于 ESP32 的智能定量出水龙头固件。

## 当前状态

- 使用同级目录 `../Esp32Base` 作为 ESP32 基础库。
- 主固件已接入 Esp32Base FULL profile、四键业务核心、流量计、电磁阀 PWM、LCD1602、蜂鸣器、RTC、LittleFS 记录/日志、统计、滤芯和 Web 查看/配置页面；真实水路仍需按上板文档逐项闭环验证。
- Web 不提供任何远程出水控制能力。
- 裸板验证已通过：未接外设时 `rtc=absent`、`lcd=absent`、`records=file`，WiFi/Web/NTP 正常，设备端 mDNS 服务已启动；当前客户端 `.local` 解析仍需继续确认。
- 2026-06-02 代码侧复测：`pio test -e native` 通过 259 个 native 用例；`pio run -e esp32dev` 通过，Flash 约 88.3%，已进入体积预警区间。

## 环境

- PlatformIO
- ESP32 Arduino framework
- 本地基础库：`../Esp32Base`
- WebOTA 本地配置：复制 `platformio.example.ini` 为 `platformio.local.ini` 后填写设备地址和认证信息。

## 常用命令

```sh
pio test -e native
pio run -e esp32dev
pio run -e esp32dev_smoke
pio run -e esp32dev -t webota
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-130
pio run -e esp32dev -t uploadfs --upload-port /dev/cu.usbserial-130
```

## 文档

- 产品需求：[docs/01-product-requirements.md](docs/01-product-requirements.md)
- 硬件设计：[docs/02-hardware-design.md](docs/02-hardware-design.md)
- 软件架构：[docs/03-software-architecture.md](docs/03-software-architecture.md)
- 交互设计：[docs/04-ui-interaction.md](docs/04-ui-interaction.md)
- 测试计划：[docs/05-test-plan.md](docs/05-test-plan.md)
- 实现任务书：[docs/06-implementation-plan.md](docs/06-implementation-plan.md)
- 上板记录：[docs/07-board-bringup.md](docs/07-board-bringup.md)

## 上板顺序

1. LCD1602
2. 四个按键：`CANCEL`、`OK`、`PLUS`、`MINUS`
3. 蜂鸣器
4. DS3231
5. 流量计
6. 电磁阀
7. 完整水路验证
