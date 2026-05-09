---
> 历史背景文档，仅作需求来源参考；最新项目规则和设计以 `AGENTS.md` 与 `docs/` 为准。

# DOC-05 实现与环境配置

| 字段 | 内容 |
|------|------|
| 文档编号 | DOC-05 |
| 项目名称 | ESP32 智能定量出水龙头 |
| 版本 | v1.0 |
| 日期 | 2026-04-16 |

---

## 1. 平台信息

| 项目 | 选择 |
|------|------|
| 芯片 | ESP32-WROOM-32（4 MB Flash） |
| 框架 | Arduino（PlatformIO 方案 A，见 CLAUDE.md） |
| RTOS | FreeRTOS（显式，xTaskCreatePinnedToCore 多任务，见 DOC-03 §8） |
| 构建工具 | PlatformIO CLI / VS Code PlatformIO 插件 |
| 开发机 | macOS（主），Windows（备用） |

---

## 2. ESP 系列配置（PlatformIO）

### 2.1 Flash 分区表（partitions_4mb.csv）

在项目根目录创建 `partitions_4mb.csv`，`platformio.ini` 通过 `board_build.partitions` 引用。

```csv
# ESP32 4 MB 自定义分区表
# Name,    Type, SubType, Offset,   Size,
# ── 系统区域 ──────────────────────────────────────────────────────
nvs,       data, nvs,     0x9000,   0x10000,   # 64 KB   系统配置 NVS
otadata,   data, ota,     0x19000,  0x2000,    # 8 KB    OTA 状态寄存器
# ── 应用分区（双 OTA，各 1.375 MB） ──────────────────────────────
ota_0,     app,  ota_0,   0x20000,  0x160000,  # 1.375 MB  主固件
ota_1,     app,  ota_1,   0x180000, 0x160000,  # 1.375 MB  OTA 备份
# ── 文件系统（LittleFS） ─────────────────────────────────────────
littlefs,  data, spiffs,  0x2E0000, 0x120000,  # 1.125 MB  日志+Web静态资源
# 总计 0x400000（4 MB）✓
```

> **与 DOC-01 的细微差异**：DOC-01 描述每个 OTA 分区为 1.4 MB，
> 实际受 4 MB 总量约束，精确值为 1.375 MB（0x160000），
> 比描述值少 25 KB，不影响固件大小（当前固件预计 400~700 KB，远低于上限）。

### 2.2 platformio.ini

```ini
[platformio]
default_envs = esp32dev

; ── 目标板（ESP32 Arduino + FreeRTOS）──────────────────────────────
[env:esp32dev]
platform                    = espressif32 @ 6.7.0
board                       = esp32dev
framework                   = arduino
monitor_speed               = 115200
upload_speed                = 921600
board_build.partitions      = partitions_4mb.csv
board_build.filesystem      = littlefs

lib_deps =
    me-no-dev/AsyncTCP            @ 1.1.1
    me-no-dev/ESP Async WebServer @ 1.2.3
    bblanchon/ArduinoJson         @ 7.2.0
    olikraus/U8g2                 @ 2.35.19

; ── PC 端 native 单元测试环境 ────────────────────────────────────
[env:native]
platform    = native
build_flags = -std=c++14 -DNATIVE_BUILD
test_filter = native/*

; ── 目标板集成测试环境 ───────────────────────────────────────────
[env:test_target]
extends     = env:esp32dev
test_filter = target/*
```

### 2.3 条件编译（src/config.h）

开关须与 **DOC-03 第 3 节可选模块表**保持严格一致；修改前先同步 DOC-03。

```cpp
/**
 * @file    config.h
 * @brief   编译期功能开关与全局常量
 */
#pragma once

// ── 可选功能开关（1=启用 0=禁用，与 DOC-03 §3 一致）─────────────
#define FEATURE_WEB_SERVER    1   // Web 页面（web_server.cpp）
#define FEATURE_WEB_API       1   // Web Service API（与 web_server 合并实现）
#define FEATURE_OTA           1   // OTA 空中升级（ota_manager.cpp）
#define FEATURE_HA            0   // Home Assistant 对接（暂不实现，预留接口）
#define FEATURE_LEAK_DETECT   0   // 漏水检测（暂不实现，预留 GPIO）

// ── 日志级别（0=关闭 1=ERROR 2=WARN 3=INFO 4=DEBUG）──────────────
#ifdef NATIVE_BUILD
  #define LOG_LEVEL           4   // native 测试环境输出全部日志
#else
  #define LOG_LEVEL           3   // 设备端默认 INFO 级别
#endif

// ── 配置版本（NVS 迁移用，见 DOC-03 §2.5）────────────────────────
#define CURRENT_CONFIG_VERSION  1

// ── 固件版本 ─────────────────────────────────────────────────────
#define FW_VERSION            "v1.0.0"
#define FW_BUILD_DATE         __DATE__

// ── native 编译桩：屏蔽 Arduino/ESP32 专属符号 ───────────────────
// HAL 层单元测试时，硬件操作由 test/native/stubs/ 下的桩函数替代
#ifdef NATIVE_BUILD
  #define IRAM_ATTR
  #define portYIELD_FROM_ISR(x)   ((void)(x))
  #include <cstdint>
  #include <cstring>
  #include <cstdbool>
#endif
```

---

## 3. 依赖库版本记录

| 库名 | 版本 | 引入原因 |
|------|------|---------|
| espressif32（PlatformIO platform） | 6.7.0 | ESP32 Arduino 核心，含 FreeRTOS、NVS、LittleFS、WiFi 栈 |
| AsyncTCP | 1.1.1 | ESPAsyncWebServer 底层依赖，ESP32 异步 TCP 驱动 |
| ESP Async WebServer | 1.2.3 | 异步 HTTP 服务，Web 页面 + REST API，不阻塞主控任务 |
| ArduinoJson | 7.2.0 | REST API JSON 序列化/反序列化，Web 配置下发解析 |
| U8g2 | 2.35.19 | 0.91 寸 SSD1306 128×32 OLED 驱动，支持大字体和自定义布局 |

**U8g2 OLED 构造函数**（OledDisplay.cpp 中使用，SSD1306 128×32，硬件 I2C）：

```cpp
// 全缓冲模式（_F_）= 512 字节 RAM，IIC 地址 0x3C（典型值）
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
```

**LittleFS 使用**（ESP32 Arduino core 2.x 已内置，无需额外安装）：

```cpp
#include <LittleFS.h>
LittleFS.begin(true);  // true = 首次启动自动格式化
```

---

## 4. 烧录与开发命令

```bash
# ── 固件烧录 ───────────────────────────────────────────────────
pio run --target upload                            # USB 首次烧录固件
pio run -e esp32dev --target upload                # 指定环境烧录

# ── OTA 烧录 ───────────────────────────────────────────────────
pio run --target upload --upload-port 192.168.x.x  # 替换为设备实际 IP

# ── 文件系统（LittleFS）────────────────────────────────────────
pio run --target buildfs                           # 将 data/ 目录构建为 LittleFS 镜像
pio run --target uploadfs                          # 上传文件系统镜像（USB，会覆盖设备日志）
# 注意：生产设备慎用 uploadfs，会清空日志

# ── 串口监视 ───────────────────────────────────────────────────
pio device monitor --baud 115200
pio device monitor --baud 115200 --filter esp32_exception_decoder   # 崩溃堆栈解析

# ── 单元测试 ───────────────────────────────────────────────────
pio test -e native                                 # PC 端 native 全量单元测试
pio test -e native -f native/test_flow_sensor      # 只运行指定模块测试
pio test -e test_target                            # 目标板集成测试（需连接设备）

# ── 常用辅助 ───────────────────────────────────────────────────
pio run --target clean                             # 清理编译缓存
pio run -v                                         # 详细输出（排查库依赖冲突）
```
