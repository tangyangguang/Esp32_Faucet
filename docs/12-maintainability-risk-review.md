# 维护风险复盘

## 结论

本轮复盘后，项目最大维护风险仍集中在 Web 大文件和 AppController 核心组合层。已经先处理两类低风险膨胀：Web 静态资源输出从主 handler 文件剥离，AppController 测试夹具从场景测试文件剥离。未触碰出水状态机、阀门控制、流量计算和显示驱动行为。

## 本轮已更新

| 位置 | 更新前 | 更新后 | 说明 |
| --- | ---: | ---: | --- |
| `src/web/FaucetWeb.cpp` | 4,639 行 | 4,509 行 | 抽出 CSS 和页面刷新脚本到 `FaucetWebAssets`，主文件保留页面和 API handler |
| `test/native/test_app_controller/test_app_controller.cpp` | 1,734 行 | 1,331 行 | 抽出共享夹具、按键动作和校准样本构造到 `AppControllerTestSupport` |

同步更新：

- `include/web/FaucetWebAssets.h`
- `src/web/FaucetWebAssets.cpp`
- `test/native/test_app_controller/AppControllerTestSupport.h`
- `test/native/test_app_controller/AppControllerTestSupport.cpp`
- `platformio.ini` 的 native 源码过滤清单
- `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp` 的直接 include 清单

## 当前高风险点

### 1. `src/web/FaucetWeb.cpp`

剩余 4,509 行，仍是最高维护风险。当前混合了页面渲染、表单解析、busy 策略、校准工作台、记录详情、滤芯页面和 API handler。

建议下一轮拆分顺序：

1. `FaucetWebCalibration.cpp`：流量校准、水温校准、TDS 校准页面和 POST handler。
2. `FaucetWebRecords.cpp`：记录列表、记录详情、记录 API 和记录时间/传感器展示格式化。
3. `FaucetWebFilters.cpp`：滤芯页面、编辑页、重置 handler 和累计量辅助函数。
4. 保留 `FaucetWeb.cpp` 承担 context、通用页面框架、路由 handler 映射和首页。

拆分原则：先按业务页面边界移动函数，不改 URL、不改 HTTP method、不改 busy 策略。每次拆分后必须跑 Web route 和 handler native 测试。

### 2. `src/app/AppController.cpp`

仍为 1,328 行。它同时承担模块组合、tick、按键状态机、校准会话、TDS/水温校准、结果记录、统计和滤芯更新。风险不只来自行数，而是它是业务闭环入口，错误会直接影响关阀、记录和校准状态。

建议下一轮拆分顺序：

1. `AppControllerCalibration.cpp`：移动 Web 流量校准会话、候选参数生成、会话恢复、RAM trace 转持久样本逻辑。
2. `AppControllerSensors.cpp`：移动 TDS/水温校准 Web 方法和传感器配置应用逻辑。
3. 主 `AppController.cpp` 保留构造、tick、按键分发、阀门同步、流量同步和结果处理。

拆分原则：只移动成员函数定义，不改 `AppController` 对外接口；避免新增继承、回调层或异步任务。

### 3. `test/native/test_app_controller`

主场景文件已降到 1,331 行，但仍偏重。它现在更适合继续按场景拆测试文件，而不是再把更多逻辑塞到 support。

建议下一轮：

- `test_app_controller_calibration.cpp`
- `test_app_controller_sensors.cpp`
- `test_app_controller_runtime.cpp`

保留一个 `main` 注册全部测试，或按 PlatformIO 测试目录拆成多个目录。优先选择不会引入重复 fixture 的方式。

### 4. `src/drivers/St7789Display.cpp`

1,207 行，但当前属于显示实现集中区，不在本轮优先拆分。理由：显示驱动稳定后，拆分收益主要是可读性；风险是引入渲染差异且 native 测试覆盖有限。只有在继续改显示页面、字体或局部刷新时再拆。

建议边界：

- `St7789DisplayPrimitives.cpp`：绘图基础函数。
- `St7789DisplayLayout.cpp`：页面布局和动态区域渲染。

### 5. Store 和校准复杂区

当前行数：

- `WaterPulseTraceStore.cpp`：742 行
- `WaterSensorManager.cpp`：514 行
- `WaterRecordCalibrationStore.cpp`：422 行
- `CalibrationSessionTraceStore.cpp`：417 行
- `WaterRecordStore.cpp`：400 行
- `WaterRecordFileStore.cpp`：348 行

判断：这些文件复杂但边界相对清晰，暂不按行数强拆。优先补充格式、容量、坏文件恢复和边界行为测试；如果继续增长，再按“存储格式/查询/校验/聚合算法”拆分。

## 其他观察

- `src/main.cpp` 765 行，包含大量初始化、硬件绑定和运行时诊断。短期可接受，但后续如果继续增长，应把硬件构造和 Web context 组装提取到独立初始化模块。
- `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp` 883 行，是 Web 边界的重要保护网。暂不拆，除非 `FaucetWeb.cpp` 页面拆分后测试也自然分组。
- Native 测试不应并行跑多个 `pio test` 进程；PlatformIO 会争用 `.pio/build/native/unity_config`。

## 验证要求

每次触碰以上拆分边界至少执行：

```sh
pio test -e native --filter 'native/test_faucet_web_routes'
pio test -e native --filter 'native/test_faucet_web_handler'
pio test -e native --filter 'native/test_app_controller'
```

如果修改 `AppController`、store 或 Web handler 行为，再执行：

```sh
pio test -e native
```

如果修改 ESP32 构建清单、驱动、显示或主固件集成，再执行：

```sh
pio run -e esp32dev
```
