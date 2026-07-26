# ESP32 上板验证记录

## 当前连接状态

- 新版 PCB 已打样并完成焊接，软件硬件基线为 2026-07-11 网表/BOM。
- 新板尚未由代理烧录或执行带电验证；实际串口、电源、外设连接状态以上板当次记录为准。
- 蜂鸣器固定使用 GPIO13，但当前 PCB 未连接；后续需通过外部三极管或 MOS 驱动电路飞线接入。
- 旧核心板验证记录保留在本文后部，只作为串口、分区和基础固件历史依据，不代表新版 PCB 已验证。

## 裸板验证目标

新版 PCB 首次上电阶段先验证固件在缺少外接负载和传感器时可以安全启动和降级：

- Esp32Base 正常启动。
- LittleFS 正常挂载。
- Web、WiFi、OTA 基础能力正常。
- Esp32Base 启动后 WiFi power save 为开启状态；首次 Web 请求允许有短暂额外延迟。
- 当前 PCB 不使用 RTC；NTP 未同步时使用 uptime 相对时间和 boot id。
- TFT 未连接或初始化失败时不影响主循环和本地控水。
- 流量计无脉冲时 GPIO33 不误计数。
- 未接流量计时不应出现持续的 `flow pulse buffer dropped pulses`；若出现，检查 GPIO33 前级 SN74LVC2G17、上拉、滤波和输入插座。
- 四个按键未连接时不应触发出水、暂停、停止或调整。
- GPIO32 `DRV_SD` 从启动起保持高电平，GPIO26 PWM 保持低电平，电磁阀输出默认关闭。
- ADS1115 缺失时传感器降级，不影响控水核心；焊接正常时地址 `0x48` 应可探测。

## 串口识别检查

在 macOS 上优先检查：

```sh
pio device list
ls -1 /dev/cu.* /dev/tty.* | sort
```

常见可上传端口形态：

- `/dev/cu.usbserial-*`
- `/dev/cu.SLAB_USBtoUART`
- `/dev/cu.wchusbserial*`
- `/dev/cu.usbmodem*`

如果没有出现：

- 换一根确定支持数据传输的 USB 线。
- 尽量直连电脑或换一个 USB Hub/Type-C 转接器端口。
- 确认核心板电源灯是否亮。
- 确认核心板 USB 转串口芯片型号，必要时安装对应 macOS 驱动。
- 某些板子上传时需要按住 `BOOT`，开始上传后松开；复位可按 `EN/RST`。

## 裸板上传命令

首次烧录、分区调整或设备无法联网时，串口出现后执行：

```sh
pio run -e esp32dev_smoke -t upload --upload-port <端口>
pio run -e esp32dev -t upload --upload-port <端口>
```

当前主环境和 smoke 环境串口上传速度使用 `460800`，避免 `921600` 在部分 CH340 板子上切换波特率失败。

注意：普通 `pio run -e esp32dev -t upload` 只按 `board_upload.offset_address = 0x20000` 写入 `ota_0`。如果设备刚通过 Web OTA 启动到 `ota_1`，普通 upload 不能保证覆盖当前正在启动的旧固件，也不会清除 bootloader 选择 OTA 分区用的 `otadata`。本项目自定义分区表中真实 `otadata` 位于 `0x19000`，不是 Arduino 默认布局。

厨房设备 Web OTA 后无法启动、串口显示仍在运行 `ota_1` 时，推荐使用 Esp32Base 串口恢复脚本。脚本会读取本项目分区表，同时写入 `ota_0` 和 `ota_1`，并在同一次写 flash 流程中清除真实 `otadata`：

```sh
python3 ../Esp32Base/scripts/esp32base_serial_recover_ota.py -d . -e esp32dev --port <端口> --baud 115200
```

执行前关闭 `pio device monitor` 或其他串口监视器；如设备无法自动进入下载模式，按住 `BOOT` 开始命令后松开，必要时再按一次 `EN/RST`。

用户日常调试可使用 Esp32Base 快速 Web OTA：

```sh
pio run -e esp32dev -t webota
```

`webota` 使用本地 `platformio.local.ini` 中的 `custom_esp32base_webota_host` IP 地址，不依赖 mDNS。首次使用前复制 `platformio.example.ini` 为 `platformio.local.ini` 并填写设备地址和认证信息。代理做烧录、验证或恢复实验时仍按 `AGENTS.md` 使用本机串口上传；只有用户自己日常调试时把 WebOTA 作为便利入口。

本项目分区表首个应用分区为 `ota_0`，偏移是 `0x20000`。`platformio.ini` 必须保留：

```ini
board_upload.offset_address = 0x20000
```

LittleFS 使用 Arduino ESP32 默认分区名 `spiffs`，分区内容仍由 LittleFS 格式化和挂载。

首次烧录或分区调整后需要上传 LittleFS 镜像：

```sh
pio run -e esp32dev -t uploadfs --upload-port <端口>
```

主固件串口监视：

```sh
pio device monitor -e esp32dev --port <端口> --baud 115200
```

当前构建使用 `ESP32BASE_DEFAULT_HOSTNAME="water-faucet"`、`ESP32BASE_ENABLE_APP_CONFIG=1`、`ESP32BASE_APP_CONFIG_MAX_GROUPS=6`、`ESP32BASE_APP_CONFIG_MAX_FIELDS=64`、`ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_DEBUG`，文件日志默认使用 `ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_WARN`。系统级参数配置入口使用 Esp32Base 内置 `/esp32base/app-config`，字段直接绑定现有 `faucet_cfg` namespace/key；预设和滤芯配置仍只能在本项目业务页面中修改。实际运行 hostname 可由 Esp32Base 内置 `/esp32base/tools` 写入 `eb_sys.hostname`，重启后生效。Web 默认认证通过 Esp32Base `setDefaultAuth()` 提供，用户可在 `/esp32base/auth` 修改。注意：已有设备若 NVS 中已保存 FileLog 模式，会继续使用用户持久化配置；需要统一切回 WARN 时应通过 System Logs 页面修改或清除对应配置，不能在固件启动时静默覆盖。

期望日志：

- 固件名为 `esp32-faucet`。
- 应用初始化日志出现。
- `water_sensor_adc=ready type=ads1115 address=0x48 temp_channel=1 tds_channel=2` 表示外部 ADC 初始化完成；水温/TDS 未接线或未启用时传感器值无效是合理状态。
- `records=file` 优先，若 LittleFS 初始化异常则需要排查分区或文件系统。
- 不应反复重启，不应出现 panic/backtrace。
- LittleFS 使用 Arduino ESP32 默认 `spiffs` 分区 label 承载，这是当前 PlatformIO/Arduino 文件系统命名兼容约定；维护时以 `board_build.filesystem = littlefs` 为实际文件系统口径。

## 逐步接线验证顺序

1. 电源与安全静态检查：不接电磁阀和水路，测量 12V、5V、3.3V；确认 GPIO32/EG27324 `SD` 为高电平、MOS 栅极关闭。
2. 串口与 smoke：仅通过本机串口烧录 `esp32dev_smoke`，确认无复位循环，并探测 ADS1115 地址 `0x48`。
3. TFT 本地屏：验证 `CS=14`、`RST=16`、`DC=17`、`SCLK=18`、`BL=19`、`MOSI=23`，确认背光极性、方向和色序。
4. 四个按键：验证 `CANCEL=GPIO39`、`OK=GPIO36`、`PLUS=GPIO34`、`MINUS=GPIO35` 外部上拉、低电平有效、消抖和长按。
5. ADS1115：读取 AIN0-AIN3 原始值和电压，确认通道没有互换、短路或超量程；AIN0 应在首页显示输入电压参考值。
6. AIN1 水温：接 MH-01 前测绿色温度线到黑色 GND 室温约 40K-70K；系统设置使用 R25=50K、B=3950、上拉=51K 后水温应接近环境水温。拔掉探头应显示 `--`。
7. AIN2 TDS：使用 PCB 5V 接口，确认插座针序；测量模块 AO 和 ADS1115 AIN2，后者应约为前者的 0.6 倍，页面显示电压应还原为模块 AO 电压；未接时的 0 仍按 0 显示，不推断断线。
8. AIN0 输入电压：确认系统设置分压上/下臂为板上标称 100K/10K；用万用表测输入端，保存校准点并核对列表中的 ADC 原始值、ADC mV、理论值和实际值，重启后再次确认。
9. 流量计：先用手动脉冲或低频信号验证 GPIO33 计数；GPIO25 第二通道只观察，不加入计量。
10. 电磁阀：先不接水，用示波器验证 GPIO26 PWM、GPIO32 SD 和 MOS 栅极；开始或暂停后继续时，GPIO26 应在 GPIO32 `SD` 拉低后先输出两个 50% 启动周期，再立即进入全功率，运行期间不得重复出现启动脉冲。分别设置 20kHz、1kHz 验证启动周期长度和后台频率立即生效，再在出水中保存频率并确认波形切换时阀门输出不被主动关闭；全功率时间和保持占空比应从下一次出水生效。CN4 未接线圈时低边输出悬空，禁止用万用表跨 CN4 的平均读数判断占空比。重点验证启动、`CANCEL`、异常和重启均优先关断。
11. 蜂鸣器：完成 GPIO13 外部驱动飞线后，再验证短提示、异常提示和 Web 开关；未飞线时跳过。
12. 完整水路：验证定量出水、暂停/继续、本次目标调整、记录、统计、滤芯累计、水温/TDS 记录和统计趋势。

## 暂不执行的验证

以下必须等对应硬件连接后执行：

- `CANCEL` 软件停止是否无明显迟滞并优先关阀。
- 电磁阀关断时间。
- 定量出水精度。
- TFT 页面肉眼确认。
- 蜂鸣器提示音。
- 72 小时连续运行。

## 2026-07-26 WiFi modem sleep 与 CANCEL 瞬态修复

主固件曾在 `Esp32Base::begin()` 成功后固定调用 `Esp32BaseWiFi::setPowerSave(true)`。实板接水回归中，按正常流程启动后电磁阀和水路只动作一瞬间便立即停止，出水记录为 `0s`、`StoppedByUser`，用户没有按下 `CANCEL`。

代码链路确认 `CANCEL=GPIO39` 的下降沿 ISR 会设置紧急停止标志，下一轮主循环在按键消抖之前直接调用 `AppController::emergencyStop()`；该结果与记录现象一致。当前没有 GPIO39、电源轨、WiFi 射频电流脉冲和阀门启动瞬态的同步波形，不能断言具体耦合路径，但 modem sleep 是故障出现前唯一运行逻辑变化，具有明确时间关联。

为同时保留节能和安全停止，本轮继续启用 WiFi modem sleep，并在 ISR 与紧急停止之间增加非阻塞的 1ms 连续低电平确认。不足 1ms 的瞬态不会进入业务；真实 `CANCEL` 比普通 10ms 按键消抖更早触发，仍满足最高优先级和自动关阀小于 100ms 的目标。必须通过连续阀门启动、真实 `CANCEL`、Web、NTP 和 OTA 联合回归；若仍复现误停止，再测量 GPIO39 和电源轨波形，不继续增加软件过滤时间。

## 2026-06-02 裸板复测

连接状态：只连接 ESP32 核心板，串口为 `/dev/cu.usbserial-130`，未接业务外设。

本次已验证：

- `pio device list` 可识别 CH340 串口。
- 固件体积预算：当前双 OTA app 分区为 `0x160000`，Flash 使用率已超过 85% 预警线；继续增加 Web 页面、诊断或日志前，优先评估静态 HTML/CSS 字符串体积、可静态化资源迁移到 LittleFS，或重新评估分区表。
- `pio run -e esp32dev_smoke` 通过。
- 旧主固件串口启动正常：进入 `setup()`，当时记录为 `rtc=absent`、`records=file`，WiFi 已连接，Web 服务就绪，NTP 已同步；新版固件已移除 RTC 探测。
- Web 首页 `http://192.168.2.112/index` 返回 200。
- 未授权访问 `/index` 返回 401。
- 状态 API 返回 `idle`、`valveOpen=false`、`waterControl=false`。
- 远程出水控制路径 `/api/faucet/start` 返回 404，`/api/faucet/presets` 对 `action=select_previous/select_next/select` 只切换“下次预设”并返回最新状态，不打开阀门、不启动出水、不改变当前出水任务。
- `/api/faucet/status`、`/api/faucet/today`、`/api/faucet/stats`、`/api/faucet/presets`、`/api/faucet/filters`、`/api/faucet/records?page=0&pageSize=10` 均可访问。
- 记录 API 返回 `storage=file`。
- 流量计校准页使用 `/faucet/calibration/flow` 进入会话；本地 `OK` 开始校准出水、本地 `CANCEL` 停止，停止后网页显示实测容量录入并保存样本。

观察到但不影响本次裸板验证：

- 启动早期仍有 ESP-IDF core dump 分区提示，随后应用正常启动。
- 本机访问 `water-65e4.local` 解析超时，IP 访问正常；当前只能确认设备端 mDNS 服务启动，macOS 客户端解析链路仍未闭环，后续需在网络环境或 mDNS 客户端侧继续确认。
