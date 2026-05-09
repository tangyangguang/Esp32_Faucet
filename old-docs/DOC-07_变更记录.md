---
> 历史背景文档，仅作需求来源参考；最新项目规则和设计以 `AGENTS.md` 与 `docs/` 为准。

# DOC-07 变更记录

| 字段 | 内容 |
|------|------|
| 文档编号 | DOC-07 |
| 项目名称 | ESP32 智能定量出水龙头 |
| 版本 | v1.0 |
| 日期 | 2026-04-17 |

---

## 变更记录

| 编号 | 日期 | 类型 | 影响文档 | 变更描述 | 决策理由 | 受影响固件版本 |
|------|------|------|---------|---------|---------|--------------|
| CHG-001 | 2026-04-16 | 需求变更 | DOC-01 | 修正重复需求编号：第二组 F-20~F-23 重编为 F-28~F-29、F-34~F-35；扩展追踪矩阵至全部35条需求 | 文档编写过程中发现编号重复，统一编号体系，保证追踪矩阵完整性 | 不适用 |
| CHG-002 | 2026-04-16 | 设计变更 | DOC-02 | 修正电阻清单：R1–R2 数量标注错误（qty:3 → 独立三个电阻 R1/R2/R3），明确每个电阻功能（R1=流量计上拉、R2=红键上拉、R3=绿键上拉）；R3→R4（MOS门极限流）、R4→R5（蜂鸣器驱动限流）重新编号避免冲突 | BOM 中数量标注与实际器件一一对应关系不清晰，修正以避免采购错误 | 不适用 |
| CHG-003 | 2026-04-16 | 设计变更 | DOC-03 | 新增 §8 FreeRTOS 任务架构：5 个任务（ControlTask/InputTask/DisplayTask/StorageTask/NetworkTask）的核心绑定、优先级、栈大小、Queue/Semaphore 定义、红键 ISR→ControlTask 紧急停止路径；补充栈水位监控代码 | DOC-03 声明显式使用 FreeRTOS 但无任务设计，补全后才能指导编码实现 | v1.0 |
| CHG-004 | 2026-04-16 | 设计变更 | DOC-03 | 扩展 SystemConfig_t 结构体：新增 flow_anomaly_lpm10、bubble_filter_us、overflow_percent、power_resume_enable、leak_sensitivity、leak_protect_enable、valve_hold_duty、valve_full_power_sec、oled_scroll_interval_sec、encoder_sensitivity_ml、beep_enable、filter_remind_percent、ntp_server、timezone_offset、ntp_sync_interval_h、rtc_mode、wifi_reconnect_max_min、web_password、ota_password、web_port 共 20 个字段；reserved 从 6 字节增至 16 字节 | DOC-01 §7 可配置参数列表中的配置项必须在数据模型中有对应字段，补充完整 | v1.0 |
| CHG-005 | 2026-04-16 | Bug 修复 | DOC-03 | 修正 FlowSensorDriver ISR 设计：将类成员函数 `handleInterrupt()` 改为静态包装函数 `_isrWrapper()` + 模块级全局单例指针 `s_instance`；气泡过滤逻辑移入 ISR 内直接执行 | Arduino `attachInterrupt()` 不接受类成员函数指针，原设计无法编译 | v1.0 |
| CHG-006 | 2026-04-16 | 设计变更 | DOC-04 | 优化 OLED 像素布局规格：明确第一行最多 12 字符（10px 字宽，120px）、第二行最多 16 字符（8px 字宽，128px）；修正所有超出像素预算的显示示例；新增休眠状态（SYS_SLEEP）显示行为描述（OLED 立即关屏，唤醒 ≤100ms 恢复）；OTA 菜单改为引导用户在浏览器操作而非设备端执行 | 原文档示例字符数超过物理像素容量，直接导致显示溢出截断 | v1.0 |
| CHG-007 | 2026-04-16 | 设计变更 | DOC-04 | 移除错误码 E05（网络断开），改为在待机屏第二行追加 `W?` 图标；移除 CORS 声明并补充说明：设备 Web 服务为同源架构，开启 CORS 会导致 Basic Auth 预检请求失败；Web 密码存储方式从"加密存储"更正为"明文存储（NVS blob）" | E05 将网络断开误归类为可恢复硬件故障；CORS 在 Basic Auth 场景下反而破坏认证；密码存储描述与实现不符 | v1.0 |
| CHG-008 | 2026-04-16 | 设计变更 | DOC-05 | 新建文档：确定自定义 4MB 分区表（nvs 64KB + otadata 8KB + ota_0/ota_1 各 1.375MB + littlefs 1.125MB）；锁定库版本（espressif32@6.7.0、AsyncTCP@1.1.1、ESP Async WebServer@1.2.3、ArduinoJson@7.2.0、U8g2@2.35.19）；NATIVE_BUILD 条件编译桩定义；完整烧录/测试命令参考 | 初始创建，为编码提供唯一确定的环境配置 | v1.0 |
| CHG-009 | 2026-04-16 | 设计变更 | DOC-06 | 新建文档：41 条业务场景测试（BS-01~BS-41）、15 个模块单元测试（UT-FLOW/VALV/OLED/EC11/BTN/BEEP/RTC/NVS/LFS/SYS/WTM/LOG/STAT/FILT/CFG）、11 条集成测试（IT-01~IT-11）、8 项异常恢复测试、9 项验收标准清单 | 初始创建，对应 DOC-03 §2.2 全量业务规则和 §4 全量模块 | v1.0 |
| CHG-010 | 2026-04-17 | 设计变更 | DOC-06 | 补充覆盖空白：新增 BS-42~BS-45（EC11旋转菜单切换、EC11长按返回、SLEEP唤醒路径、LittleFS失败不中断出水）；新增 IT-12（本地按键优先于网络请求）；新增 §3.16 WebServer 单元测试（UT-WEB-01~07）；UT-RTC 从 4 条扩至 6 条（补正常路径和NTP写入）；UT-SYS 从 7 条扩至 9 条（补 SYS_CONFIG/SYS_OTA 状态转换）；验收标准更新为 BS-01~BS-45、IT-01~IT-12、16 个模块 | 基于覆盖空白分析：EC11 旋转和唤醒路径是 DOC-03 §2.2 明确规则但无对应测试；WebServer 为已启用模块但完全缺失单元测试 | v1.0 |
| CHG-011 | 2026-04-17 | 设计变更 | DOC-01 | 填写需求追踪矩阵（§8）：为全部 35 条功能需求（F-01~F-35）填写对应的 DOC-03 业务规则、负责模块、DOC-06 业务场景测试编号和单元测试编号，状态全部更新为"已设计" | 追踪矩阵空白无法支撑评审和变更影响分析 | 不适用 |
| CHG-012 | 2026-04-17 | 设计变更 | DOC-01, DOC-03, DOC-04, DOC-06 | 恢复出厂设置触发方式变更：由"红键超长按 >3s"改为"红键+绿键同时按住 >5s"。具体影响：①DOC-01 F-28 更新验收标准；②DOC-03 §2.2 规则1 新增组合键描述，InputEventType_t 删除原 INPUT_RED_SUPER_LONG（原未正式入 enum），新增 INPUT_RED_LONG 和 INPUT_COMBO_FACTORY_RESET；③DOC-04 §1.1 按键时长表去除"超长按"行，新增"组合长按"行，§1.2 红键功能表删除超长按列，新增独立"组合键操作"节；④DOC-06 UT-BTN-06 重写为组合键测试，新增 UT-BTN-07（防抖边界），新增 BS-46、BS-47 业务场景 | 单键超长按（红键>3s）与正常操作区分度不足，意外触发风险高；双键组合5s提供更强的防误触保护，且不增加硬件成本 | v1.0 |

---

## 变更类型说明

| 类型 | 说明 |
|------|------|
| 需求变更 | DOC-01 变化，影响验收标准 |
| 设计变更 | DOC-02/03/04/05/06 变化，影响实现方案 |
| Bug 修复 | 设计或文档中的错误修正 |
| 配置变更 | DOC-05 环境配置变化 |
| 硬件变更 | PCB 改版，必须填写受影响固件版本 |
