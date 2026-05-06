---
# DOC-06 测试规格说明

| 字段 | 内容 |
|------|------|
| 文档编号 | DOC-06 |
| 项目名称 | ESP32 智能定量出水龙头 |
| 版本 | v1.0 |
| 日期 | 2026-04-16 |

---

## 1. 测试策略

**原则**：烧录前测完一切能测的。所有业务逻辑和模块接口在 native 环境验证通过后，再上板验证硬件交互。

| 层次 | 环境 | 覆盖范围 |
|------|------|----------|
| 业务场景测试 | PC native | 验证 DOC-03 §2.2 行为规则、§2.4 边界条件是否正确实现 |
| 单元测试 | PC native | 每个模块接口的四类场景：正常路径/边界值/无效输入/状态转换 |
| 集成测试 | 开发板 + 串口 | 硬件外设通信、模块组合行为、FreeRTOS 任务协作 |
| 系统测试 | 完整硬件 | 端到端功能、长期可靠性、异常恢复 |

**运行命令**：
```bash
pio test -e native                     # 全量 native 测试
pio test -e native -f native/test_bs   # 只运行业务场景测试
pio test -e test_target                # 目标板集成测试
```

---

## 2. 业务场景测试

> 来源：DOC-03 §2.2 行为规则 + §2.4 边界条件与冲突处理。
> 在 native 环境运行，通过桩函数模拟时间推进和外部事件，不依赖硬件。
> 运行命令：`pio test -e native -f native/test_business`

### 2.1 按键操作逻辑（DOC-03 §2.2 规则1）

| 场景 ID | 对应业务规则 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-01 | 红键任意场景立即关阀 | SYS_RUNNING 出水中，触发红键 ISR 信号量 | ControlTask 收到 g_emergency_sem，立即调用 valveDriver.close()，状态变 SYS_IDLE，响应 ≤50ms | 待实现 |
| BS-02 | 红键在 SYS_IDLE 下无副作用 | SYS_IDLE 时触发红键 | 调用 valveDriver.close()（已关，幂等），状态保持 SYS_IDLE | 待实现 |
| BS-03 | 绿键待机短按进入预设选择 | SYS_IDLE 状态，发送 INPUT_GREEN_SHORT 事件 | 状态变 SYS_CONFIRM，OLED 显示确认界面 | 待实现 |
| BS-04 | 绿键出水中短按暂停 | SYS_RUNNING 状态，发送 INPUT_GREEN_SHORT | 状态变 SYS_PAUSED，电磁阀关闭，计时暂停 | 待实现 |
| BS-05 | 绿键暂停中短按继续 | SYS_PAUSED 状态，发送 INPUT_GREEN_SHORT | 状态恢复 SYS_RUNNING，电磁阀重新打开 | 待实现 |
| BS-06 | EC11 中键进入主菜单 | SYS_IDLE 状态，发送 INPUT_ENCODER_SHORT | 状态变 SYS_CONFIG，OLED 显示菜单第一项 | 待实现 |
| BS-42 | EC11 旋转切换菜单项 | SYS_CONFIG 状态，连续发送 3 次 INPUT_ENCODER_CW | 菜单焦点依次向下移动 3 项；到达末尾后再 CW 保持末项（边界不越界）| 待实现 |
| BS-43 | EC11 长按返回上级菜单 | SYS_CONFIG 子菜单中，发送 INPUT_ENCODER_LONG | 退出子菜单返回父菜单；若已在顶层菜单则状态变 SYS_IDLE | 待实现 |
| BS-46 | 组合键恢复出厂设置（正常触发） | 任意状态，模拟红键+绿键同时按住满 5s | ① 按下瞬间阀立即关闭（ISR路径）；② 5s 后 ControlTask 收到 INPUT_COMBO_FACTORY_RESET；③ NVS 全部参数重置为默认值；④ 设备重启，重启后配置与默认值完全一致 | 待实现 |
| BS-47 | 组合键未满 5s 不触发恢复出厂 | 任意状态，红键+绿键同时按住 4.9s 后松开绿键 | 不触发 INPUT_COMBO_FACTORY_RESET，NVS 数据不变；关阀照常执行（红键 ISR 路径） | 待实现 |

### 2.2 出水流程规则（DOC-03 §2.2 规则2）

| 场景 ID | 对应业务规则 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-07 | 二次确认后电磁阀全压吸合 3s 后切换 PWM | SYS_CONFIRM 确认，模拟时间推进 3s | 前 3s valve_state=VALVE_OPENING（100% PWM），3s 后 valve_state=VALVE_RUNNING（配置占空比） | 待实现 |
| BS-08 | 脉冲防抖：≥1ms 脉冲正常计数 | 模拟注入间隔 1ms 的两个连续脉冲 | 两个脉冲均计入总流量 | 待实现 |
| BS-09 | 脉冲防抖：<1ms 脉冲过滤 | 模拟注入间隔 0.5ms 的两个连续脉冲 | 第二个脉冲被气泡过滤器丢弃，总流量只增加一个脉冲量 | 待实现 |
| BS-10 | 定量模式达到预设容量自动关阀 | 预设 7500ml，累积脉冲换算到 7500ml | 电磁阀关闭，日志记录，统计更新，状态变 SYS_IDLE | 待实现 |
| BS-11 | 定时模式达到预设时间自动关阀 | 预设 60s，模拟时间推进 60s | 电磁阀关闭，日志记录类型=定时，状态变 SYS_IDLE | 待实现 |
| BS-12 | 全局最大时间兜底关阀（不可关闭） | max_out_time_sec=1800，模拟推进 1800s | 强制关阀，日志类型=异常停止，不受任何配置开关影响 | 待实现 |
| BS-13 | 全局最大出水量兜底关阀（不可关闭） | max_out_volume_ml=30000，累积出水 30001ml | 强制关阀，日志类型=异常停止 | 待实现 |
| BS-14 | 超量百分比兜底：超过预设值 10% 关阀 | 预设 7500ml，overflow_percent=10，累积出水 8251ml | 强制关阀（8251 > 7500×1.1=8250），日志类型=异常停止 | 待实现 |
| BS-15 | 暂停超时自动关阀 | SYS_PAUSED，pause_timeout_sec=300，模拟推进 301s | 自动关阀，日志类型=暂停超时停止（type=4），状态变 SYS_IDLE | 待实现 |
| BS-16 | 出水完成蜂鸣器提示 + 日志记录 | 定量出水到达预设量 | beepDriver.play(LONG_BEEP)，logManager.write()，statisticsManager.update() 均被调用 | 待实现 |

### 2.3 存储规则（DOC-03 §2.2 规则3）

| 场景 ID | 对应业务规则 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-17 | 日志满 20000 条自动滚动覆盖最早记录 | 写入第 20001 条日志 | 最早的 1000 条日志被删除，总条数仍为 20000，新日志写入成功 | 待实现 |
| BS-18 | 配置修改实时写入 NVS | 修改 pause_timeout_sec=600 | nvs_storage.write() 在同一帧内被调用，断电恢复后读取值为 600 | 待实现 |
| BS-19 | 统计数据出水完成后立即写入 | 出水完成事件触发 | statisticsManager 立即向 g_config_save_queue 发送写入触发，不依赖 1 小时定时 | 待实现 |

### 2.4 网络规则（DOC-03 §2.2 规则4）

| 场景 ID | 对应业务规则 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-20 | WiFi 连接失败指数退避重试 | networkTask 收到连接失败事件，重试间隔序列：1→2→4→8→...→30min | 每次重试间隔翻倍，超过 30min 后保持最大间隔，重试期间本地功能完全正常 | 待实现 |
| BS-21 | 大请求不阻塞本地出水控制 | Web 端发起日志全量查询（20000条），同时本地出水任务运行 | ControlTask 10ms tick 延迟不受影响，日志查询异步完成，出水精度不降低 | 待实现 |

### 2.5 低功耗规则（DOC-03 §2.2 规则5）

| 场景 ID | 对应业务规则 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-22 | 30s 无操作进入 Modem Sleep | SYS_IDLE，模拟时间推进 sleep_delay_sec=30s，无任何输入事件 | 状态变 SYS_SLEEP，OLED 关屏，WiFi 进入 Modem Sleep 模式 | 待实现 |
| BS-23 | WiFi 重连期间可正常进入休眠 | WiFi 断开，NetworkTask 正在重连等待，同时满足休眠条件 | ControlTask 正常进入 SYS_SLEEP，不因 WiFi 重连而阻塞休眠 | 待实现 |
| BS-44 | SYS_SLEEP 按键唤醒 | SYS_SLEEP 状态，发送任意输入事件（INPUT_GREEN_SHORT）| 状态变 SYS_IDLE，OLED 恢复亮起，按键事件被正常处理，唤醒响应 ≤100ms | 待实现 |

### 2.6 优先级规则（DOC-03 §2.2 优先级规则）

| 场景 ID | 对应业务规则 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-24 | 红键优先于其他所有输入 | SYS_RUNNING 出水中，同一帧内收到红键信号量和绿键暂停事件 | 红键关阀先执行，绿键事件丢弃，状态变 SYS_IDLE | 待实现 |
| BS-25 | 安全兜底优先于普通关阀 | 同一时刻定量到达预设值且超过 max_out_volume_ml | 记录日志类型=安全兜底关阀（不记录为正常完成），屏幕显示故障提示 | 待实现 |

### 2.7 异常处理规则（DOC-03 §2.2 异常处理）

| 场景 ID | 对应业务规则 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-26 | 开阀后 3s 无流量判定故障 | 电磁阀打开后，模拟 no_flow_timeout_sec=3s 内无脉冲输入 | 自动关阀，OLED 显示 E01，屏幕三短嘀报警，状态变 SYS_ERROR | 待实现 |
| BS-27 | 流量超 30L/min 持续 5s 关阀 | 模拟连续流量超过 flow_anomaly_lpm10×0.1=30L/min 持续 5s | 自动关阀，OLED 显示 E02，日志类型=异常停止 | 待实现 |
| BS-28 | 电磁阀故障重试 3 次进入 ERROR | open() 后反复无流量，触发故障重试逻辑 | 最多重试 3 次后进入 SYS_ERROR，所有执行器断电 | 待实现 |
| BS-29 | NVS 读写失败使用临时配置 | 模拟 NvsStorage.read() 连续返回失败 3 次 | 使用内存中默认配置继续运行，打印 WARN 日志，不中断出水功能 | 待实现 |
| BS-30 | 无 DS3231 且 NTP 失败时使用相对时间 | rtc_mode=0（禁用），WiFi 断开，发起日志记录 | 日志 start_time 使用 millis()/1000 相对时间，联网后自动回填正确时间戳 | 待实现 |
| BS-45 | LittleFS 写入失败时核心出水功能不受影响 | 模拟 LittleFS.write() 连续返回失败 3 次，同时有出水任务运行 | 出水任务正常完成，日志缓存在内存中，打印 WARN 日志，不进入 SYS_ERROR，不中断阀门控制 | 待实现 |

### 2.8 边界条件与冲突处理（DOC-03 §2.4）

| 场景 ID | 对应边界场景 | 输入场景描述 | 预期行为 | 状态 |
|---------|------------|------------|---------|------|
| BS-31 | 重启时正在执行出水任务（默认不继续） | 模拟 power_resume_enable=0，重启，NVS 中记录有未完成任务 | 重启后不继续出水，电磁阀保持关闭，日志记录中断事件，进入 SYS_IDLE | 待实现 |
| BS-32 | 重启时正在执行出水任务（配置继续） | power_resume_enable=1，重启，NVS 中记录有未完成任务且剩余量 > 0 | 重启后自动继续出水，从剩余量开始执行 | 待实现 |
| BS-33 | 多键同时按下按优先级处理 | 同一帧内 g_input_queue 中同时存在 INPUT_RED_PRESS 和 INPUT_GREEN_SHORT | 红键事件先处理，绿键事件丢弃 | 待实现 |
| BS-34 | 脉冲间隔恰好等于 bubble_filter_us（边界） | 注入间隔恰好等于 bubble_filter_us=500µs 的脉冲 | 脉冲通过过滤器（条件是 >=，边界值应计入） | 待实现 |
| BS-35 | 日志满 20000 条滚动删除最早 1000 条 | 连续写入 21000 条日志 | 第 20001 条写入时删除最早 1000 条，总量 ≤20000，第 21000 条可正确写入 | 待实现 |
| BS-36 | 无 DS3231 断网超 7 天后联网时间回填 | 模拟相对时间日志 + 联网后获取 NTP 时间 | 日志时间戳按相对时序自动回填为绝对时间戳，连续性不中断 | 待实现 |
| BS-37 | 连续多次按停止键只响应第一次 | SYS_IDLE 状态下（阀已关），在 100ms 内发送 5 个 INPUT_RED_PRESS | 只执行一次关阀（幂等），不产生重复日志或状态切换 | 待实现 |
| BS-38 | 滤芯寿命到期后仍可正常出水 | FilterManager 标记滤芯1已到期 | 出水功能不受限制，OLED 显示提醒图标，不进入错误状态 | 待实现 |
| BS-39 | OTA 过程断电后固件自动回滚 | 模拟 OTA 下载到 50% 时系统重置 | 重启后 otadata 指向原 ota_0 分区，系统以旧固件正常启动 | 待实现 |
| BS-40 | 配置参数超出范围自动钳位 | 通过 API 设置 pause_timeout_sec=9999（超过最大 3600） | ConfigManager 自动钳位到 3600，NVS 存储值为 3600，返回 {"ok":true} | 待实现 |
| BS-41 | 出水暂停超时返回 SYS_IDLE | SYS_PAUSED，pause_timeout_sec=300，推进 300s | 日志 type=4（暂停超时），电磁阀关闭，状态变 SYS_IDLE | 待实现 |

---

## 3. 单元测试（Native）

> 全部在 `test/native/` 目录下，不依赖硬件，HAL 层用桩函数替代寄存器操作。
> 每个模块须覆盖：正常路径 / 边界值 / 无效输入 / 状态转换 四类场景。

### 3.1 FlowSensorDriver

测试文件：`test/native/test_flow_sensor/test_flow_sensor.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-FLOW-01 | 正常路径 | 设置中流量系数 450 | 注入 450 个脉冲 | getTotalVolume() == 1000ml（1L）| 待实现 |
| UT-FLOW-02 | 正常路径 | 累积 5000ml | resetTotal() | getTotalVolume() == 0，getCurrentFlow() == 0 | 待实现 |
| UT-FLOW-03 | 正常路径 | 低/中/高系数各异 | 在低流量区间注入脉冲 | 使用 coeff_low 计算，非 coeff_mid | 待实现 |
| UT-FLOW-04 | 边界值 | bubble_filter_us=500 | 注入间隔恰好 500µs 的脉冲对 | 两个脉冲均计入（>=阈值，边界通过）| 待实现 |
| UT-FLOW-05 | 边界值 | bubble_filter_us=500 | 注入间隔 499µs 的脉冲对 | 第二个脉冲被过滤，只计一个 | 待实现 |
| UT-FLOW-06 | 无效输入 | 对象已初始化 | setCoefficients(0, 450, 440, 1000, 5000) | 返回 false，系数不变 | 待实现 |
| UT-FLOW-07 | 状态转换 | getState()==FLOW_IDLE | 注入第一个有效脉冲 | getState()==FLOW_ACTIVE | 待实现 |
| UT-FLOW-08 | 状态转换 | getState()==FLOW_ACTIVE | 模拟推进 3001ms 无脉冲 | getState()==FLOW_IDLE | 待实现 |

### 3.2 ValveDriver

测试文件：`test/native/test_valve/test_valve.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-VALV-01 | 正常路径 | VALVE_IDLE | open()，推进 3000ms | 前 3s PWM=100%（VALVE_OPENING），3s 后 PWM=hold_duty（VALVE_RUNNING）| 待实现 |
| UT-VALV-02 | 正常路径 | VALVE_RUNNING | close() | getState()==VALVE_CLOSING，PWM 停止，最终 VALVE_IDLE | 待实现 |
| UT-VALV-03 | 边界值 | VALVE_IDLE | setHoldDuty(100)，open() | 全压驱动，保持阶段 PWM 仍 100%（全压兼容模式）| 待实现 |
| UT-VALV-04 | 边界值 | VALVE_IDLE | setHoldDuty(0) | 返回 false（0 占空比无法保持吸合，拒绝设置）| 待实现 |
| UT-VALV-05 | 无效输入 | VALVE_IDLE | setHoldDuty(101) | 返回 false，hold_duty 不变 | 待实现 |
| UT-VALV-06 | 状态转换 | VALVE_IDLE | open() → 全压超时 → close() | 完整路径：IDLE→OPENING→RUNNING→CLOSING→IDLE | 待实现 |

### 3.3 OledDisplay

测试文件：`test/native/test_oled/test_oled.cpp`（桩替代 U8g2 I2C 调用）

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-OLED-01 | 正常路径 | 对象新建 | begin()（桩替代 I2C）| 返回 true，内部状态初始化为 OLED_IDLE | 待实现 |
| UT-OLED-02 | 正常路径 | 已初始化 | showRunning(7500, 5200, 1200, 30) | 缓冲区内容包含 "5.2L"，无越界写入（≤12字符第一行）| 待实现 |
| UT-OLED-03 | 边界值 | 已初始化 | setBrightness(0) 和 setBrightness(100) | 两次均返回 true，值被正确存储 | 待实现 |
| UT-OLED-04 | 无效输入 | 已初始化 | setBrightness(101) | 返回 false，亮度值不变 | 待实现 |
| UT-OLED-05 | 状态转换 | OLED_ACTIVE | sleep()（SYS_SLEEP 触发）| 状态变 OLED_SLEEP，模拟 PWM 输出为 0 | 待实现 |
| UT-OLED-06 | 状态转换 | OLED_SLEEP | wakeUp() | 状态恢复 OLED_ACTIVE，亮度恢复保存值 | 待实现 |

### 3.4 Ec11Driver

测试文件：`test/native/test_ec11/test_ec11.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-EC11-01 | 正常路径 | 编码器处于中位 | 模拟顺时针一格（A 超前 B）| getPosition() 增加 1，读取后自动清零 | 待实现 |
| UT-EC11-02 | 正常路径 | 编码器处于中位 | 模拟逆时针一格（B 超前 A）| getPosition() 减少 1 | 待实现 |
| UT-EC11-03 | 边界值 | 快速旋转 | 在 10ms 内模拟 50 格顺时针 | 所有 50 格均被记录，无丢失 | 待实现 |
| UT-EC11-04 | 无效输入 | AB 相同时为高 | 模拟 AB 同时变化（非法格雷码）| 忽略，不计入位移 | 待实现 |
| UT-EC11-05 | 状态转换 | 按键未按下 | 模拟按下 500ms 后松开 | 生成 INPUT_ENCODER_SHORT 事件 | 待实现 |
| UT-EC11-06 | 状态转换 | 按键未按下 | 模拟按下 1500ms 后松开 | 生成 INPUT_ENCODER_LONG 事件 | 待实现 |

### 3.5 ButtonDriver

测试文件：`test/native/test_button/test_button.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-BTN-01 | 正常路径 | 绿键未按 | 模拟按下 200ms 松开 | InputTask 生成 INPUT_GREEN_SHORT 事件 | 待实现 |
| UT-BTN-02 | 正常路径 | 绿键未按 | 模拟按下 1500ms 松开 | InputTask 生成 INPUT_GREEN_LONG 事件 | 待实现 |
| UT-BTN-03 | 边界值 | 短按阈值 1000ms | 模拟按下恰好 1000ms 松开 | 生成 INPUT_GREEN_LONG（1000ms 为长按起点）| 待实现 |
| UT-BTN-04 | 无效输入 | 消抖窗口 20ms | 模拟按下 10ms 抖动后松开 | 事件被过滤，不生成任何输入事件 | 待实现 |
| UT-BTN-05 | 状态转换 | 红键 ISR 路径 | 模拟红键下降沿触发 ISR | g_emergency_sem 被 Give，ControlTask 可立即 Take | 待实现 |
| UT-BTN-06 | 状态转换 | 红键+绿键同时按下 5100ms，双键均保持 | 模拟组合键满 5s | InputTask 生成 INPUT_COMBO_FACTORY_RESET 事件；同时 g_emergency_sem 在按下瞬间已被 Give（关阀先于计时触发）| 待实现 |
| UT-BTN-07 | 边界值 | 红键+绿键同时按下，4900ms 后松开其中一键 | 未达到 5s 双键组合 | 不生成 INPUT_COMBO_FACTORY_RESET 事件，计时器重置 | 待实现 |

### 3.6 BeepDriver

测试文件：`test/native/test_beep/test_beep.cpp`（桩替代 PWM 输出）

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-BEEP-01 | 正常路径 | beep_enable=1 | play(SHORT_BEEP) | 桩记录 PWM 输出持续 100ms，频率在合法范围 | 待实现 |
| UT-BEEP-02 | 正常路径 | beep_enable=1 | play(LONG_BEEP) | 桩记录 PWM 输出持续 500ms | 待实现 |
| UT-BEEP-03 | 边界值 | beep_enable=1 | setVolume(0) 和 setVolume(100) | 两次均返回 true，0 时 PWM 占空比为 0 | 待实现 |
| UT-BEEP-04 | 无效输入 | beep_enable=1 | setVolume(101) | 返回 false，音量值不变 | 待实现 |
| UT-BEEP-05 | 状态转换 | beep_enable=0 | play(ALARM_BEEP) | 不输出 PWM（桩记录无调用），静默返回 | 待实现 |

### 3.7 RtcDriver

测试文件：`test/native/test_rtc/test_rtc.cpp`（桩替代 I2C DS3231 读写）

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-RTC-01 | 正常路径 | DS3231 桩可用，rtc_mode=1 | begin()，setTime(1714502400)，getTime() | 返回值 == 1714502400，状态为 RTC_READY | 待实现 |
| UT-RTC-02 | 正常路径 | DS3231 桩已初始化 | NTP 同步后调用 syncFromNtp(1714502400) | setTime() 被调用一次，DS3231 桩记录写入值为 1714502400 | 待实现 |
| UT-RTC-03 | 边界值 | DS3231 桩可用 | setTime(0)（Unix epoch）和 setTime(UINT32_MAX）| 均成功存储，读取值一致 | 待实现 |
| UT-RTC-04 | 无效输入 | DS3231 桩可用 | setTimeStruct 月份=13（非法）| 返回 false，时间不变 | 待实现 |
| UT-RTC-05 | 状态转换 | rtc_mode=2（自动检测），桩模拟 I2C 无响应 | begin() | 检测到无 DS3231，fallback 到软件时钟，返回 true（降级成功）| 待实现 |
| UT-RTC-06 | 状态转换 | rtc_mode=1，DS3231 桩在 getTime() 时返回 I2C 错误 | getTime() | 返回上一次缓存的有效时间戳，打印 WARN 日志，不返回 0 | 待实现 |

### 3.8 NvsStorage

测试文件：`test/native/test_nvs/test_nvs.cpp`（桩替代 ESP32 NVS API）

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-NVS-01 | 正常路径 | NVS 已初始化 | 写入 SystemConfig_t，读取对比 | 所有字段字节级一致，crc 校验通过 | 待实现 |
| UT-NVS-02 | 正常路径 | NVS 已初始化 | 写入 Statistics_t，断电模拟（清空内存），重新 begin()，读取 | 读取值与写入值一致（持久化验证）| 待实现 |
| UT-NVS-03 | 边界值 | NVS 已初始化 | 写入最大 blob（SystemConfig_t 约 1200 字节）| 写入成功，读取长度正确 | 待实现 |
| UT-NVS-04 | 无效输入 | NVS 已初始化 | 读取不存在的键 "config/noexist" | 返回 false，输出缓冲区不被修改 | 待实现 |
| UT-NVS-05 | 状态转换 | NVS 已写入数据 | erase()，然后读取 "config/system" | 返回 false（已擦除），NVS 干净 | 待实现 |

### 3.9 LittleFsStorage

测试文件：`test/native/test_lfs/test_lfs.cpp`（桩替代 LittleFS API）

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-LFS-01 | 正常路径 | 文件系统已初始化 | 写入 1000 条 WaterLog_t，读取分页（page=0，page_size=100）| 返回 100 条，顺序与写入一致 | 待实现 |
| UT-LFS-02 | 正常路径 | 已有 1000 条日志文件 | appendLog() 追加第 1001 条 | 新建第二个日志文件（每 1000 条一个文件），写入成功 | 待实现 |
| UT-LFS-03 | 边界值 | 接近 1.125MB 磁盘满 | 写入超出剩余空间的数据 | 返回错误码，已有日志不损坏 | 待实现 |
| UT-LFS-04 | 无效输入 | 文件系统已初始化 | 写入 NULL 日志指针 | 返回 false，不写入 | 待实现 |
| UT-LFS-05 | 状态转换 | 空文件系统 | getLogCount()，readPage(0, 10) | count=0，readPage 返回空数组而非崩溃 | 待实现 |

### 3.10 SystemController

测试文件：`test/native/test_system_controller/test_system_controller.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-SYS-01 | 正常路径 | 对象新建，桩依赖 | begin()，tick() | 状态从 SYS_INIT 变 SYS_IDLE（自检通过）| 待实现 |
| UT-SYS-02 | 正常路径 | SYS_IDLE | startWaterTask(0, 7500)，tick() | 状态变 SYS_CONFIRM（二次确认开启）| 待实现 |
| UT-SYS-03 | 边界值 | SYS_IDLE | _state = SYS_COUNT-1（SYS_ERROR，最大合法值）| tick() 正常执行 SYS_ERROR 处理，不崩溃 | 待实现 |
| UT-SYS-04 | 无效输入 | 任意状态 | 强制设 _state = SYS_COUNT | tick() 第一行检测到越界，自动纠正为 SYS_ERROR | 待实现 |
| UT-SYS-05 | 状态转换 | SYS_IDLE | 推进时间 sleep_delay_sec=30s，无事件输入 | 状态变 SYS_SLEEP | 待实现 |
| UT-SYS-06 | 状态转换 | SYS_ERROR | tick() 中验证 valveDriver.close() 被调用 | 桩记录 close() 至少被调用一次，且在 default 分支无崩溃 | 待实现 |
| UT-SYS-07 | 状态转换 | SYS_CONFIRM | 推进 10s 无绿键确认 | 状态变 SYS_IDLE（确认超时）| 待实现 |
| UT-SYS-08 | 状态转换 | SYS_IDLE | enterConfigMode()，tick()，再调用 exitConfigMode() | 状态依次：SYS_IDLE→SYS_CONFIG→SYS_IDLE，OLED 桩记录配置界面被渲染 | 待实现 |
| UT-SYS-09 | 状态转换 | SYS_IDLE | enterOtaMode()，tick()，模拟升级完成事件 | 状态依次：SYS_IDLE→SYS_OTA→SYS_IDLE，升级完成后不进入 ERROR | 待实现 |

### 3.11 WaterTaskManager

测试文件：`test/native/test_water_task/test_water_task.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-WTM-01 | 正常路径 | 任务类型=容量，预设 7500ml | 累积脉冲到 7500ml | isComplete() == true，日志 type=0（定量）| 待实现 |
| UT-WTM-02 | 正常路径 | 任务类型=时间，预设 60s | 推进 60s | isComplete() == true，日志 type=1（定时）| 待实现 |
| UT-WTM-03 | 边界值 | 预设 7500ml，coeff=450 | 注入恰好 7500×450/1000=3375 个脉冲 | isComplete() == true，最后一个脉冲触发完成 | 待实现 |
| UT-WTM-04 | 边界值 | 预设 7500ml，overflow=10% | 累积到 8250ml（=7500×1.1） | 未触发兜底；累积到 8251ml | 触发安全兜底，type=3 | 待实现 |
| UT-WTM-05 | 无效输入 | 任务未启动 | start(type=5, value=7500)（无效类型）| 返回 false，任务不启动 | 待实现 |
| UT-WTM-06 | 无效输入 | 任务未启动 | start(type=0, value=0)（零容量）| 返回 false | 待实现 |
| UT-WTM-07 | 状态转换 | 任务运行中 | pause() → resume() → stop() | 状态依次：RUNNING→PAUSED→RUNNING→IDLE，每次转换后 isComplete() 返回值正确 | 待实现 |

### 3.12 LogManager

测试文件：`test/native/test_log_manager/test_log_manager.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-LOG-01 | 正常路径 | 日志为空 | 写入 3 条日志，readPage(page=0, size=10) | 返回 3 条，内容与写入一致（start_time/volume_ml/type）| 待实现 |
| UT-LOG-02 | 正常路径 | 空日志 | readPage(page=0, size=10) | 返回 0 条，total=0，不崩溃 | 待实现 |
| UT-LOG-03 | 边界值 | 已有 19999 条 | 再写入 1 条（第 20000 条）| 成功写入，getCount()==20000，不触发滚动 | 待实现 |
| UT-LOG-04 | 边界值 | 已有 20000 条 | 再写入 1 条（第 20001 条，触发滚动）| getCount()==20000（删除最早 1000 条后新写 1 条），最旧日志 ID 更新 | 待实现 |
| UT-LOG-05 | 无效输入 | 日志管理器已初始化 | write(NULL) | 返回 false，不写入 | 待实现 |
| UT-LOG-06 | 状态转换 | page=2（第 3 页，共 3 页）| readPage(page=3, size=100)（超出范围）| 返回空数组，total 值正确，不越界 | 待实现 |

### 3.13 StatisticsManager

测试文件：`test/native/test_statistics/test_statistics.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-STAT-01 | 正常路径 | 统计全为 0 | update(1500ml，timestamp=任意)，读取所有维度 | daily/weekly/monthly/yearly/total 均增加 1500ml，filter used_flow 增加 1500ml | 待实现 |
| UT-STAT-02 | 正常路径 | 统计全为 0 | 连续 update 10 次各 1000ml | total==10000ml，daily==10000ml | 待实现 |
| UT-STAT-03 | 边界值 | last_reset_day=20260416，daily=5000 | update 时传入次日时间戳（20260417）| daily 自动清零后加本次出水量，weekly/monthly/yearly 继续累加 | 待实现 |
| UT-STAT-04 | 无效输入 | 统计有效值 | update(-100ml)（负值）| 返回 false，统计数据不变 | 待实现 |
| UT-STAT-05 | 状态转换 | 首次加载（NVS 为空）| loadFromNvs()（读取失败）| 所有字段初始化为 0，boot_count=1 | 待实现 |

### 3.14 FilterManager

测试文件：`test/native/test_filter_manager/test_filter_manager.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-FILT-01 | 正常路径 | 滤芯1启用，total_flow_ml=3500000 | consumeFlow(1000ml)（消耗 1000ml）| filters[0].used_flow_ml 增加 1000ml | 待实现 |
| UT-FILT-02 | 正常路径 | 滤芯1 total_flow_ml=3500000，used=3150000 | getRemaining(0)（查询剩余）| 剩余 (3500000-3150000)/3500000 = 10%（恰好等于提醒阈值）| 待实现 |
| UT-FILT-03 | 边界值 | filter_remind_percent=10 | 消耗使剩余量降到恰好 10%（边界）| 到达阈值时，isRemindActive(0) == true | 待实现 |
| UT-FILT-04 | 无效输入 | 9个滤芯配置 | resetFilter(9)（ID越界，有效范围0-8）| 返回 false，不执行任何重置 | 待实现 |
| UT-FILT-05 | 状态转换 | 滤芯1已到期 | resetFilter(0) | used_flow_ml=0，start_time更新为当前，isRemindActive(0)==false | 待实现 |

### 3.15 ConfigManager

测试文件：`test/native/test_config_manager/test_config_manager.cpp`

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-CFG-01 | 正常路径 | NVS 空（首次启动）| loadConfig() | 所有字段等于文档定义默认值，返回 true | 待实现 |
| UT-CFG-02 | 正常路径 | 已加载默认配置 | 设置 pause_timeout_sec=600，saveConfig()，重新 loadConfig() | 读取值 == 600（持久化验证）| 待实现 |
| UT-CFG-03 | 边界值 | 已加载默认配置 | 设置 overflow_percent=100（最大合法值）| 返回 true，值被保存 | 待实现 |
| UT-CFG-04 | 边界值 | 已加载默认配置 | 设置 pause_timeout_sec=0（最小值，含义为不超时）| 返回 true，值被保存 | 待实现 |
| UT-CFG-05 | 无效输入 | 已加载默认配置 | 设置 pulse_coeff_low=0（低于最小值 300）| 返回 false，值被自动钳位到 300 | 待实现 |
| UT-CFG-06 | 无效输入 | 已加载默认配置 | 设置 web_port=0（无效端口）| 返回 false，端口值不变 | 待实现 |
| UT-CFG-07 | 状态转换 | NVS 存储 version=0（旧版本）| loadConfig() | 检测版本不一致，执行迁移（填充新增字段默认值），打印 WARN 日志 | 待实现 |
| UT-CFG-08 | 状态转换 | NVS 存储 version=99（跨度超过 2）| loadConfig() | 重置为默认配置，打印 WARN 日志"config version too old, reset" | 待实现 |

### 3.16 WebServer

测试文件：`test/native/test_web_server/test_web_server.cpp`（桩替代 AsyncWebServer 及 SystemController 依赖）

| 用例 ID | 类型 | 前置条件 | 操作 | 预期结果 | 状态 |
|---------|------|----------|------|----------|------|
| UT-WEB-01 | 正常路径 | WebServer 已初始化，系统状态 SYS_IDLE | 模拟 `GET /api/status` 请求 | 响应 JSON 包含 state/current_flow/total_volume/runtime/error_code 五个字段，HTTP 200 | 待实现 |
| UT-WEB-02 | 正常路径 | WebServer 已初始化，SYS_IDLE | 模拟 `POST /api/water/start` body=`{"type":0,"value":7500}` | ok=true，startWaterTask(0, 7500) 桩被调用一次，HTTP 200 | 待实现 |
| UT-WEB-03 | 正常路径 | WebServer 已初始化 | 模拟 `GET /api/statistics` | 响应 JSON 包含 daily/weekly/monthly/yearly/total 五个字段，数值与 StatisticsManager 桩返回值一致 | 待实现 |
| UT-WEB-04 | 无效输入 | WebServer 已初始化 | 模拟 `POST /api/water/start` body=`{"type":5,"value":7500}`（无效类型）| ok=false，startWaterTask 桩未被调用，HTTP 400 | 待实现 |
| UT-WEB-05 | 无效输入 | WebServer 已初始化 | 模拟 `POST /api/config` body=`{"pause_timeout_sec":9999}`（超出最大值 3600）| ok=true，ConfigManager 桩接收到的值为 3600（自动钳位），HTTP 200 | 待实现 |
| UT-WEB-06 | 无效输入 | WebServer 已初始化，密码="admin" | 模拟 `GET /api/status` 携带错误 Basic Auth 凭证 | HTTP 401，不调用任何业务接口 | 待实现 |
| UT-WEB-07 | 状态转换 | WebServer 已初始化，系统状态 SYS_RUNNING | 模拟 `POST /api/water/stop` | ok=true，stopWaterTask() 桩被调用，HTTP 200 | 待实现 |

---

## 4. 集成测试（Target）

> 需要连接实际 ESP32 开发板，通过串口验证模块组合行为。
> 运行命令：`pio test -e test_target`

| 用例 ID | 名称 | 硬件依赖 | 验证方式 | 预期结果 |
|---------|------|---------|---------|---------|
| IT-01 | 电磁阀 GPIO 输出电平 | 万用表测 GPIO16 | 命令 open() 后测量 | 高电平 ≥3.0V；close() 后 ≤0.1V |
| IT-02 | OLED 显示正常 | OLED 屏幕接 I2C | 目视检查 | 开机界面正常显示，无花屏 |
| IT-03 | 流量计脉冲读取 | YF-S201 接水管通水 | 串口打印 currentFlow | 流速 1L/min 时脉冲计数 ≥430/min（系数450） |
| IT-04 | IIC 总线 OLED + DS3231 共存 | 两个 I2C 设备均接 | 串口打印 I2C 扫描结果 | 检测到 0x3C（OLED）和 0x68（DS3231）两个地址 |
| IT-05 | 蜂鸣器 PWM 音频 | 蜂鸣器接 GPIO17 | 听觉验证 | 短鸣 100ms 清晰，报警音三短嘀可区分 |
| IT-06 | NVS 掉电持久化 | 无额外硬件 | 写入配置 → 断电 → 上电后读取 | 配置值与写入值完全一致 |
| IT-07 | LittleFS 日志读写 | 无额外硬件 | 写入 100 条日志，读取分页 | 读取内容与写入完全一致，无乱码 |
| IT-08 | WiFi 连接 + mDNS | 2.4G 路由器 | 浏览器访问 `http://water-xxxx.local/` | 返回 Web 首页，状态 200 |
| IT-09 | FreeRTOS 任务协作 | 无额外硬件 | 串口打印栈水位 + 状态机推进日志 | 5个任务均运行，无栈溢出（水位 > 200 字节） |
| IT-10 | 红键紧急停止响应时间 | 逻辑分析仪 GPIO36 + GPIO16 | 测量红键按下到 GPIO16 变低的时间 | ≤50ms |
| IT-11 | AsyncWebServer 不阻塞出水 | WiFi + 模拟流量计脉冲 | Web 端查询日志同时出水任务运行 | 出水期间 ControlTask tick 延迟 ≤15ms（目标 10ms） |
| IT-12 | 本地按键优先于网络请求 | WiFi + 出水任务运行中 | Web 端发起 `POST /api/water/stop`，同时本地立即按下红键 | 红键关阀先于 API 响应执行，阀关闭时间 ≤50ms；API 请求返回 ok=true（操作已被红键执行）|

---

## 5. 系统测试（手动）

### 5.1 功能验证

| 需求编号 | 验证步骤 | 预期结果 | 实测结果 | 通过 |
|---------|---------|---------|---------|------|
| F-01 | 设置 7500ml 预设，实际接水后称重 | 实际出水量 7500ml ±225ml（±3%）| | [ ] |
| F-02 | 设置 60s 定时，秒表计时 | 实际出水时长 60s ±2s | | [ ] |
| F-03 | 出水中按红键，测量关阀响应 | 阀关闭 ≤50ms | | [ ] |
| F-05 | 切换9组预设，验证各组参数 | 每组参数独立，一键选择生效 | | [ ] |
| F-06 | 完成流量校准后重新接水 | 误差 ≤3% | | [ ] |
| F-07 | 开阀后不通水，等待超时提示 | 3s 后自动关阀，E01 显示 | | [ ] |
| F-10 | 观察 OLED 出水界面 | 剩余量/流速 每秒刷新，格式正确 | | [ ] |
| F-15 | 接水 10 次后查询 Web 日志 | 10 条日志时间/容量/类型正确 | | [ ] |
| F-19 | 断开路由器，继续本地出水 | 出水功能完全正常，不中断 | | [ ] |
| F-20 | 访问 Web 界面所有页面 | 8 个路由均正常返回，响应 ≤1000ms | | [ ] |
| F-21 | 通过 Web 界面上传新固件 | 升级成功，版本号更新 | | [ ] |
| F-22 | 无操作 30s 后观察 OLED | 屏幕关闭，按键后立即亮起 | | [ ] |
| F-24 | 设置最大出水量为 100ml，接水 | 超过 110ml（10%兜底）时强制关阀 | | [ ] |
| F-34 | 完成出水，听蜂鸣器 | 长鸣 500ms 明显可听 | | [ ] |
| F-35 | 观察电磁阀发热情况（30分钟持续接水）| PWM 保持阶段温升 < 20°C | | [ ] |

### 5.2 异常恢复测试（强制，不可删除）

| 场景 | 触发方式 | 预期行为 | 通过 |
|------|---------|---------|------|
| WDT 超时 | 在串口命令行阻塞主循环超过 WDT 超时时间 | 系统在 1s 内自动重启，重启后正常运行 | [ ] |
| 断电恢复 | 出水过程中随机切断电源后重新上电 | 电磁阀断电自动关闭，重启后进入 SYS_IDLE，配置参数完整 | [ ] |
| OTA 升级中断 | 固件上传到 50% 时关闭浏览器/切断网络 | 重启后以原固件运行，不变砖，OTA 状态寄存器回滚 | [ ] |
| WiFi 断线 5 分钟 | 关闭路由器 5 分钟后重新开启 | 路由器恢复后设备自动重连，重连后 Web 界面可正常访问 | [ ] |
| NVS 配置损坏 | 通过串口命令 `factory_reset` 强制清空 NVS | 重启后所有配置恢复为默认值，系统正常运行 | [ ] |
| 流量计断线 | 出水中拔掉流量计连接线 | 3s 后判定无流量故障，自动关阀，显示 E01 | [ ] |
| I2C 总线故障 | 出水中断开 OLED 连接线 | 系统继续出水（I2C 错误不中断核心功能），打印 WARN 日志 | [ ] |
| LittleFS 日志写满 | 写入 20000 条日志后继续出水 | 日志滚动覆盖，系统不崩溃，新日志正常写入 | [ ] |

### 5.3 可靠性测试

| 项目 | 方法 | 标准 |
|------|------|------|
| 短期连续运行 | 正常使用状态（含 WiFi、Web 服务），持续运行 | ≥ 72 小时无意外重启 |
| 长期部署 | 安装到实际净水器后，每天正常使用 1~3 次 | ≥ 180 天无需手动干预（NF-01 要求）|
| 高频接水测试 | 连续接水 100 次，记录日志和流量精度 | 精度误差持续 ≤3%，无日志丢失 |

---

## 6. 验收标准（烧录前必须全部勾选）

- [ ] 所有业务场景测试通过（BS-01 ~ BS-47 全部 pass）
- [ ] 所有 native 单元测试通过（0 fail，覆盖全部 16 个模块含 WebServer）
- [ ] 每个模块四类场景（正常路径/边界值/无效输入/状态转换）均有测试用例
- [ ] 集成测试 IT-01 ~ IT-12 全部通过
- [ ] DOC-01 所有 Must 功能（F-01 ~ F-35）验证通过
- [ ] 异常恢复测试（§5.2，8 项）全部通过
- [ ] 可靠性测试连续运行 ≥ 72 小时（部署前 ≥ 180 天另行记录）
- [ ] 内存：RAM 峰值 ≤ 70%（uxTaskGetStackHighWaterMark 监控确认，见 NF-03）
- [ ] 响应时间：红键关阀 ≤50ms，本地按键响应 ≤200ms（NF-02）

---

## 7. 已知问题

| 编号 | 描述 | 优先级 | 状态 |
|------|------|--------|------|
| （暂无，测试执行后填写） | — | — | — |
