# 实时流速显示

## 背景

流量计输出的是离散脉冲。运行中的“实时流速”不能直接理解为某个无穷小瞬间的真值，只能是最近短时间内有效脉冲对应的流量估算。

当前首页“机器状态”的流速曾直接使用最近两个有效脉冲间隔换算。该单周期瞬时流速响应快，但对水压波动、启动段不稳定、气泡、传感器边沿抖动和脉冲间隔离散性非常敏感，出水时容易大幅跳动。

## 显示口径

项目必须区分以下流速：

- `instantFlowMlPerMin`：原始瞬时流速。使用最近两个有效脉冲间隔计算，只用于诊断。
- `windowFlowMlPerMin`：窗口实时流速。使用最近短时间窗内有效脉冲数计算，是专业意义上的实时流速。
- `currentFlowMlPerMin`：面向用户显示的实时流速。基于窗口实时流速做轻微显示平滑，首页主值使用该字段。
- `runAverageFlowMlPerMin`：本次任务平均流速。使用本次已出水量和本次运行时长计算。
- `recentAverageFlowMlPerMin`：历史近期平均流速。来自最近历史记录，不得作为本次实时流速使用。
- `stableFlowMlPerMin`：计量方案中的预计稳态流速，仅用于时间目标估算展示，不参与实际容量累计。

## 算法

原始瞬时流速：

```text
instant = 60,000,000 / intervalUs * 1000 / stablePulsePerLiter
```

窗口实时流速：

```text
windowFlow = pulseCountInWindow / windowSeconds * 60 * 1000 / stablePulsePerLiter
```

显示平滑流速：

```text
display = previousDisplay * 0.7 + windowFlow * 0.3
```

首版固定参数：

- 默认窗口为 2 秒。
- 低流速或窗口内脉冲过少时，最长可扩展到 3 秒。
- EMA 新值权重为 0.3，旧值权重为 0.7。
- 无新脉冲超过有效期后，窗口实时流速和显示流速归零。

这些参数暂不进入持久化配置。只有上板验证证明不同水路必须现场调整时，才评估是否加入业务配置。

## 计量与安全边界

- 总出水量、滤芯累计、统计累计、出水记录和校准样本仍使用有效脉冲总数换算，不使用显示平滑流速。
- 被 `pulseMinIntervalUs` 过滤的脉冲不得进入任何流速窗口。
- 高流量安全判断使用 `windowFlowMlPerMin`，不使用单周期瞬时流速，避免单个异常脉冲导致误判。
- 暂停、停止、重置后流速状态必须清零。
- `micros()` 回绕必须正确处理。

## Web 展示

首页“机器状态”流速卡片：

- 主值显示 `currentFlowMlPerMin`。
- Web 和本地屏统一保留两位小数，避免 1.95-2.04L/min 等正常变化在本地全部显示为 2.0L/min。
- 小字显示“本次平均 x.xx”，对应 `runAverageFlowMlPerMin`。
- 不再把历史近期平均放在本次流速卡片里。

诊断信息可显示：

- 原始瞬时流速 `instantFlowMlPerMin`。
- 窗口实时流速 `windowFlowMlPerMin`。
- 丢弃脉冲 `flowDroppedPulses`。

## 验证

Native 测试必须覆盖：

- 单周期瞬时流速。
- 2 秒窗口实时流速。
- 低流速 3 秒窗口。
- 窗口滑动剔除旧脉冲。
- 无新脉冲后归零。
- `micros()` 回绕。
- 过滤脉冲不进入窗口。
- `reset()` 清空所有流速状态。

上板验证入口和上传方式以 `docs/07-board-bringup.md` 与 `AGENTS.md` 为准；本文不重复维护烧录流程。
