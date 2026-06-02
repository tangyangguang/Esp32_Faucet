# 实时流速显示优化设计

## 背景

首页“机器状态”的流速主值当前直接显示 `currentFlowMlPerMin`。该值由最近两个有效流量计脉冲的时间间隔换算得到，属于单周期瞬时流速。单周期算法响应快，但对脉冲间隔抖动、水压波动、启动段波动、气泡和边沿噪声非常敏感，出水时容易出现大幅跳动。

本项目已保存原始脉冲明细，实际累计出水量由有效脉冲总数和计量方案换算得到。此次优化不改变累计出水量、滤芯累计、统计累计、校准样本和历史记录的计量口径，只优化运行中“实时流速”的估算和展示语义。

## 目标

- 首页主值显示专业意义上的实时流速：基于最近短时间窗内有效脉冲数量估算，而不是单个脉冲间隔。
- 保留单周期瞬时流速作为诊断值，便于排查传感器抖动、毛刺和水路波动。
- 将“实时流速”“原始瞬时流速”“本次平均流速”“历史近期平均流速”“预计稳态流速”明确区分，避免混用。
- 高流量安全判断同步使用短窗口流速，避免单个异常脉冲导致误判。

## 非目标

- 不改变总出水量计算方式。
- 不改变流量计计量方案参数含义。
- 不新增远程出水控制能力。
- 不把显示平滑结果写入历史记录或校准数据。
- 不在本项目内修改 Esp32Base。

## 流速定义

### 原始瞬时流速

`instantFlowMlPerMin` 使用最近两个有效脉冲间隔计算：

```text
instant = 60,000,000 / intervalUs * 1000 / stablePulsePerLiter
```

用途：诊断和调试。该值允许跳动，不作为首页主值。

### 窗口实时流速

`windowFlowMlPerMin` 使用最近固定时间窗内有效脉冲数计算：

```text
windowFlow = pulseCountInWindow / windowSeconds * 60 * 1000 / stablePulsePerLiter
```

推荐窗口：

- 默认窗口：2 秒。
- 低流速或窗口内脉冲过少时，可扩展到 3 秒。
- 无新脉冲超过窗口有效期后归零。

用途：运行中首页主显示、LCD 运行流速显示、API 面向用户的实时流速字段。

### 显示平滑流速

`displayFlowMlPerMin` 在窗口实时流速基础上做轻微指数平滑：

```text
display = previousDisplay * 0.7 + windowFlow * 0.3
```

约束：

- 只用于显示，不参与累计出水量。
- 阀门关闭、暂停超时或无新脉冲超过有效期时快速衰减到 0，避免页面残留虚假流速。
- 出水刚开始时允许较快跟随，避免长时间显示 `-`。

### 本次平均流速

`runAverageFlowMlPerMin` 使用本次已出水量和本次运行时长计算：

```text
runAverage = currentRunVolumeMl * 60 / elapsedSec
```

用途：运行中辅助信息或记录页展示，不替代实时流速。

## 架构设计

### FlowMeter

`FlowMeter` 继续负责有效脉冲计数、容量换算和流速估算。新增内部短窗口状态：

- 保存最近有效脉冲时间戳的环形缓冲，容量按最大有效频率和 3 秒窗口估算。
- `onPulse()` 接受有效脉冲时写入环形缓冲。
- `reset()` 清空脉冲计数、过滤计数、窗口状态和平滑状态。
- `snapshot(nowUs)` 输出：
  - `instantFlowMlPerMin`
  - `windowFlowMlPerMin`
  - `displayFlowMlPerMin`
  - 原有 `pulseCount`
  - 原有 `volumeMl`
  - 原有 `rejectedPulses`

为保持代码语义清晰，原 `currentFlowMlPerMin` 应改为或映射为 `displayFlowMlPerMin`。诊断用原始瞬时值使用新字段，不再复用 `currentFlowMlPerMin`。

### AppController

`AppController` 接收 `FlowSnapshot` 后：

- 面向用户状态快照使用 `displayFlowMlPerMin`。
- 安全高流量判断使用 `windowFlowMlPerMin`，不使用单周期瞬时值。
- 本次平均流速可在 `AppSnapshot` 中派生，或在 Web 层用 `volumeMl` 和 `elapsedSec` 派生。

### Web API

`/api/faucet/status` 建议输出字段：

- `currentFlowMlPerMin`：兼容页面主值语义，返回显示用实时流速。
- `windowFlowMlPerMin`：短窗口实时流速。
- `instantFlowMlPerMin`：单周期诊断值。
- `runAverageFlowMlPerMin`：本次运行平均流速。
- `recentAverageFlowMlPerMin`：历史近期平均流速，保留但文案必须明确是历史记录平均。

由于项目不保留旧 API 历史包袱，若实现时发现命名会造成长期混淆，可以直接调整字段命名和页面使用方，但必须同步 native Web JSON 测试。

### 首页显示

首页“机器状态”流速卡片：

- 主值显示 `currentFlowMlPerMin`，其语义为显示用实时流速。
- 小字显示“本次平均 x.xx”，用于辅助理解当前任务整体流速。
- 历史近期平均不再放在流速主卡片中，避免误解为本次实时平滑值。

诊断区域或状态条可显示：

- 原始瞬时流速。
- 窗口实时流速。
- 丢弃脉冲。

## 参数建议

首版固定参数：

- 窗口默认 2 秒。
- 低脉冲时最多扩展到 3 秒。
- EMA 系数：新值权重 0.3，旧值权重 0.7。
- 无新脉冲超过 2 秒时窗口流速归零；显示值随后快速归零。

这些参数不进入持久化配置。若上板验证证明需要现场调整，再评估是否加入业务配置；避免为一次优化增加长期配置负担。

## 边界条件

- `micros()` 回绕必须正确处理。
- 暂停、停止、重置后流速必须清零。
- 启动阶段脉冲少，允许前 1-2 个脉冲显示不稳定；首页主值应尽快进入窗口估算。
- `stablePulsePerLiter` 无效时流速返回 0。
- 脉冲过滤窗口内的拒绝脉冲不得进入窗口流速。
- 高流速饱和时不能整数溢出。

## 测试计划

Native 单元测试：

- 最近两个脉冲间隔仍能得到诊断瞬时流速。
- 2 秒窗口内多个脉冲能得到窗口实时流速。
- 窗口滑动后旧脉冲被剔除。
- 低流速脉冲稀疏时 3 秒窗口可稳定显示。
- 无新脉冲超过有效期后流速归零。
- `micros()` 回绕场景窗口计算正确。
- 被 `pulseMinIntervalUs` 过滤的脉冲不进入窗口。
- `reset()` 清空所有流速状态。

Web JSON 测试：

- `/api/faucet/status` 输出新增字段。
- `currentFlowMlPerMin` 使用显示用实时流速。
- `instantFlowMlPerMin` 和 `windowFlowMlPerMin` 不混淆。

页面结构测试：

- 首页流速主值使用显示用实时流速。
- 页面文案区分本次平均、历史近期平均和原始瞬时流速。

上板验证：

- 使用 `pio run -e esp32dev -t webota` 烧录测试主固件。
- 连续出水观察首页流速主值是否稳定跟随。
- 对比原始瞬时值、窗口值和丢弃脉冲，确认没有隐藏明显硬件异常。
- 验证总出水量、记录、校准入口不受影响。
