# Agent 项目规则

## 项目定位

本项目是基于 ESP32 的智能定量出水龙头固件。当前阶段已进入首版实现和逐步上板验证。

## 基本原则

- 本项目所有 ESP32 基础能力使用 `/Users/tyg/dir/claude_dir/Esp32Base`。
- 如果实现过程中发现基础库能力缺口、基础库 bug，或需要新增基础库设计，不在本项目内打补丁。
- 遇到上述基础库问题时，在 `docs/base-library-requests/` 下创建独立文档，写出可直接交给 Esp32Base 项目的完整提示词。
- 本项目不保留历史兼容性包袱，不迁移旧 key、旧 API 或旧行为，只按当前确认后的最佳方案实现。
- 不管是新需求、bug 修复还是优化，都坚决不做临时方案、局部补丁或带历史包袱的兼容迁就；即使影响较多代码和设计，也只按最优方案、最佳实践重新整理和实现。
- `old-docs/` 只作为需求背景和讨论材料，不作为新架构约束。
- 所有新项目文档放入 `docs/`，根目录只保留必要工程文件和本规则文件。
- 每次回复用户时，都要说明当前剩余工作。

## 当前实施边界

- 当前需求和设计文档已进入首版实现阶段，按 `docs/06-implementation-plan.md` 顺序推进。
- 优先实现可 native 测试的业务核心和平台无关逻辑，再接入真实硬件驱动和 Web。
- Web 业务页面或业务 API 不得提供任何远程出水控制能力。
- 三个按键命名为 `STOP`、`OK`、`NEXT`。

## 基础库引用

- 使用本地库路径：`/Users/tyg/dir/claude_dir/Esp32Base`。
- 默认启用 `ESP32BASE_PROFILE_FULL`，覆盖 Log、Config、System、Bus、Watchdog、Sleep、Fs、FileLog、Health、WiFi、DNS、NTP、mDNS、Web、OTA/Web OTA。
- 本项目不得重复实现 Esp32Base 已经提供的基础能力。
