# Esp32Base 基础库请求

本目录用于记录需要回到同级基础库项目 `../Esp32Base` 完善的能力、bug 或新设计。

每个请求文档应包含：

- 背景和本项目场景。
- 当前 Esp32Base 能力缺口或问题。
- 期望的公开 API 或行为。
- 验收测试。
- 明确说明本项目不得临时绕过或打补丁。

## 请求状态

| 文档 | 状态 | 备注 |
|---|---|---|
| `001-littlefs-dependency.md` | 已验证 | Esp32Base 已修复 FULL profile 下 LittleFS include/LDF 问题，本项目 `pio run -e esp32dev` 已通过。 |
| `002-fs-random-access.md` | 已验证 | 本项目已使用 `Esp32BaseFs::readBytesAt()` / `writeBytesAt()` 接入 LittleFS 环形日志。 |
| `003-web-method-and-auth.md` | 待跟进 | 当前项目已按 Esp32Base 现有能力规避；后续仍建议基础库增强请求方法和鉴权表达。 |
| `004-web-route-capacity.md` | 待跟进 | 当前项目通过合并路由控制在容量内；基础库若扩容可简化 Web 路由组织。 |
| `005-ssd1306-driver.md` | 待跟进 | 当前项目保留本项目内 SSD1306 业务驱动；如沉淀到基础库需另行设计。 |
