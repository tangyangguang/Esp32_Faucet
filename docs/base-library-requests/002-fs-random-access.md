# Esp32Base 请求：为 Fs 增加随机访问读写能力

状态：已由 Esp32Base 完成，并已在本项目接入验证。

验证记录：

- 本项目已通过 `Esp32BaseFs::readBytesAt()` / `writeBytesAt()` 接入 `WaterLogFileStore`，用于二进制定长出水日志的分页读取和环形覆盖写入。
- 本项目未直接 include `LittleFS.h` 或 Arduino `File`。
- `pio test -e native` 通过。
- `pio run -e esp32dev` 通过。

## 背景

本项目需要在 LittleFS 中保存出水日志。日志目标是尽量接近 20000 条，每条为二进制定长记录，并支持 Web 分页查询。为了避免一次性读取大文件或在日志满后整文件重写，本项目已实现平台无关的 `WaterLogFileStore`，其底层需要按偏移读取和覆盖写入固定长度记录。

当前本项目通过：

```ini
lib_deps =
    symlink:///Users/tyg/dir/claude_dir/Esp32Base
```

引用 Esp32Base，并启用 `ESP32BASE_PROFILE_FULL`。

## 当前 Esp32Base 能力缺口

`Esp32BaseFs` 当前提供：

- `writeBytes(path, data, len)`
- `readBytes(path, out, maxLen, readLen)`
- `appendBytes(path, data, len)`
- `fileSize(path)`
- `exists/removeFile/rename/listDir/mkdir/rmdir`

这些能力适合整文件写入、从文件头读取、追加写入，但不适合以下场景：

- 从二进制日志文件中按偏移读取某一页记录。
- 在环形日志文件中覆盖指定 slot。
- 日志满后继续写入时避免整文件读出、移动、重写。

本项目不应直接包含 `<LittleFS.h>` 或绕过 Esp32Base 打开 `File`，否则会把基础文件系统能力补丁写进业务项目。

## 期望 API

请在 Esp32Base 中为 `Esp32BaseFs` 增加随机访问 API，例如：

```cpp
class Esp32BaseFs {
public:
    static bool readBytesAt(const char* path, size_t offset, uint8_t* out, size_t len, size_t* readLen = nullptr);
    static bool writeBytesAt(const char* path, size_t offset, const uint8_t* data, size_t len);
};
```

建议行为：

- `readBytesAt`：
  - 文件不存在、路径非法、FS 未就绪时返回 `false`。
  - `offset` 超出文件大小时返回 `false`，`readLen` 置 0。
  - 读取到文件尾不足 `len` 时可返回 `true`，并通过 `readLen` 返回实际读取长度；或者严格要求读满并返回 `false`，但需要在文档明确。
- `writeBytesAt`：
  - 文件不存在时可创建文件。
  - `offset` 位于文件尾后时，按 LittleFS/Arduino 能力决定是否允许扩展；若允许，空洞部分应填 0 或在文档说明。
  - `len == 0` 时可用于创建空文件，并返回成功。
- 两个 API 都应遵守现有 `validPath`、`isReady` 语义。

## 验收测试

在 Esp32Base 自身测试或示例中验证：

1. `writeBytesAt("/t.bin", 0, data, 4)` 创建文件并写入 4 字节。
2. `writeBytesAt("/t.bin", 2, data2, 2)` 覆盖中间两字节。
3. `readBytesAt("/t.bin", 1, out, 2, &readLen)` 只读取偏移 1 开始的 2 字节。
4. `readBytesAt` 对非法路径、未挂载 FS、超出范围 offset 返回明确失败。
5. 在启用 `ESP32BASE_PROFILE_FULL` 的外部 PlatformIO 项目中，仅引用 Esp32Base 即可编译通过。

## 本项目约束

在基础库提供上述 API 前，本项目只保留平台无关的 `WaterLogFileStore` 和 native 测试，不在本项目直接使用 LittleFS 或 Arduino `File` 作为临时补丁。
