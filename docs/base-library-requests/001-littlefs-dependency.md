# Esp32Base 请求：修复 FULL profile 下 LittleFS 依赖声明

状态：已验证解决。

验证日期：2026-05-05。

验证结果：Esp32Base 已将 `Esp32BaseFs.inc` 中的 LittleFS include 调整为直接 `#include <LittleFS.h>`，本项目执行 `pio run -e esp32dev` 已通过。

## 背景

本项目 `/Users/tyg/dir/claude_dir/Esp32_Faucet` 使用 PlatformIO + Arduino ESP32，并通过本地库路径引用：

```ini
lib_deps =
    symlink:///Users/tyg/dir/claude_dir/Esp32Base
```

项目按规划启用：

```ini
build_flags =
    -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
```

FULL profile 会启用 `ESP32BASE_ENABLE_FS` 和 `ESP32BASE_ENABLE_FILELOG`，因此会编译 `Esp32BaseFs`。

## 当前问题

执行：

```bash
pio run -e esp32dev
```

失败：

```text
.pio/libdeps/esp32dev/Esp32Base/src/runtime/Esp32BaseFs.inc:8:36: fatal error: LittleFS.h: No such file or directory
```

本机 Arduino ESP32 framework 包内存在该文件：

```text
~/.platformio/packages/framework-arduinoespressif32@3.20016.0/libraries/LittleFS/src/LittleFS.h
```

推测原因是 Esp32Base 的 `library.json` 没有声明或暴露对 Arduino ESP32 内置 LittleFS 库的依赖，且 `Esp32BaseFs.inc` 通过宏间接 include：

```cpp
#define ESP32BASE_INCLUDE_LITTLEFS <LittleFS.h>
#include ESP32BASE_INCLUDE_LITTLEFS
```

PlatformIO LDF 没有把内置 LittleFS 库加入 include path，导致 FULL profile 在作为第三方库被应用引用时无法编译。

## 期望行为

- 应用只需要引用 Esp32Base 并启用 `ESP32BASE_PROFILE_FULL`，即可编译通过。
- 应用不需要在自己的 `main.cpp` 中额外 `#include <LittleFS.h>` 来强迫 LDF 发现依赖。
- 应用不需要手工添加 framework 内部 include path。
- Esp32Base 的库声明、文档或包含方式应保证 FS/FileLog profile 在 PlatformIO 下可独立使用。

## 建议方向

请在 Esp32Base 中评估并选择最优修复方式：

- 在 `library.json` 中声明对 framework 内置 LittleFS 的依赖或调整 LDF 所需信息。
- 将 `#include <LittleFS.h>` 改为 PlatformIO LDF 能稳定识别的直接 include 方式。
- 如不同 Arduino ESP32 Core 版本的 LittleFS 暴露方式不同，请在 Esp32Base 文档中明确支持矩阵和必要配置。

## 验收测试

在一个空 PlatformIO 应用中仅配置：

```ini
[env:esp32dev]
platform = espressif32 @ 6.7.0
board = esp32dev
framework = arduino
board_build.filesystem = littlefs
build_flags =
    -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
lib_deps =
    symlink:///Users/tyg/dir/claude_dir/Esp32Base
```

并在 `src/main.cpp` 中只包含：

```cpp
#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Esp32Base::begin();
}

void loop() {
    Esp32Base::handle();
}
```

执行 `pio run -e esp32dev` 必须成功。

## 本项目约束

本项目不在业务代码中添加临时 include、不复制 LittleFS 封装、不修改 `.pio` 下安装后的 Esp32Base，也不在本项目内实现 Esp32Base FS/FileLog 的替代能力。
