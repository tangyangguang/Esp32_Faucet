# 硬件设计

## 当前硬件基线

- 当前 PCB 基线为 2026-07-11 Board1/Schematic1 网表与对应 BOM。
- 主控为 ESP32-WROOM-32E-N4（4MB）。
- 输入设备使用 `CANCEL`、`OK`、`PLUS`、`MINUS` 四个独立按键。
- 电磁阀为 12V 常闭型，通过 EG27324 + IRLR7843 驱动，支持全压吸合和 PWM 保持。
- 所有软件初始化、异常和停止路径都必须优先关闭阀门。
- 外部电源开关作为硬件停止手段，不进入软件控制。

## 硬件范围

| 模块 | 当前 PCB 策略 |
| --- | --- |
| 主控 | ESP32-WROOM-32E-N4 |
| 电磁阀 | GPIO26 PWM；GPIO32 接 EG27324 `SD`，高电平强制关闭 |
| 流量计 | 主脉冲经 SN74LVC2G17 整形后进入 GPIO33；GPIO25 为第二路预留输入 |
| 传感器 ADC | ADS1115，I2C 地址 `0x48`，ALERT/RDY 接 GPIO27 |
| 水温 | ADS1115 AIN1；MH-01 50K B3950 NTC，板上 51K 1% 上拉 |
| TDS | TDS Board V1.0 使用 PCB 5V 接口；AO 经 10K/15K 分压和滤波进入 ADS1115 AIN2 |
| 输入电压 | ADS1115 AIN0；使用 100K/10K 标称分压，首页次要信息显示并支持多点校准，仅作诊断 |
| 预留模拟量 | ADS1115 AIN3，网络名 `T1`，不进入当前业务逻辑 |
| 本地屏 | 240x240 ST7789，SPI 接入，独立 CS、RST、DC 和背光控制 |
| 按键 | 四键低电平有效，使用板上 10K 外部上拉 |
| 蜂鸣器 | GPIO13 PWM；当前 PCB 未连接，后续通过外部驱动电路飞线接 5V 无源蜂鸣器 |
| 漏水检测 | 不做 |

## GPIO

| GPIO | 功能 | 说明 |
| --- | --- | --- |
| GPIO13 | 蜂鸣器 PWM | 当前 PCB 未连接；不得直接驱动 5V 蜂鸣器，需外接三极管或 MOS 驱动 |
| GPIO14 | TFT CS | ST7789 片选 |
| GPIO16 | TFT RST | ST7789 复位 |
| GPIO17 | TFT DC | ST7789 数据/命令 |
| GPIO18 | TFT SCLK | SPI 时钟 |
| GPIO19 | TFT BL | 背光，高电平点亮为当前软件默认，需实板确认 |
| GPIO21 | I2C SDA | ADS1115 |
| GPIO22 | I2C SCL | ADS1115 |
| GPIO23 | TFT MOSI | SPI 数据 |
| GPIO25 | 第二脉冲输入 | 经 SN74LVC2G17 整形，当前只用于上板诊断，不参与计量 |
| GPIO26 | 电磁阀 PWM | EG27324 INA，经驱动后控制 MOS 栅极 |
| GPIO27 | ADS1115 ALERT/RDY | 当前驱动使用 I2C 轮询，保留该引脚 |
| GPIO32 | 电磁阀强制关断 | EG27324 `SD`，高电平关闭、低电平允许 PWM |
| GPIO33 | 主流量脉冲 | 经 SN74LVC2G17 整形后输入中断 |
| GPIO34 | `PLUS` | 输入专用，板上外部 10K 上拉，低电平有效 |
| GPIO35 | `MINUS` | 输入专用，板上外部 10K 上拉，低电平有效 |
| GPIO36 | `OK` | 输入专用，板上外部 10K 上拉，低电平有效 |
| GPIO39 | `CANCEL` | 输入专用，板上外部 10K 上拉；下降沿中断触发最高优先级软件停止 |
| GPIO1 | UART0 TX | 调试串口 |
| GPIO3 | UART0 RX | 调试串口 |

GPIO34、GPIO35、GPIO36、GPIO39 是输入专用且没有内部上下拉。按键驱动必须使用普通输入模式并依赖板上外部上拉，不得配置 `INPUT_PULLUP`。

## ADS1115 通道

| 通道 | PCB 网络 | 当前用途 |
| --- | --- | --- |
| AIN0 | VIN 分压 | 输入电压诊断，默认不采样 |
| AIN1 | NTC50K_IN | 水温 |
| AIN2 | TDS_ADC_VAL | TDS Board V1.0 AO 经 10K/15K 分压后的测量值 |
| AIN3 | T1 | 预留，不进入当前业务逻辑 |

ADS1115 使用单次转换和 860SPS 数据率。量程由 `WaterSensorManager` 按传感器用途配置；TDS 保留自动升降量程和切换后丢弃首个样本的现有规则。TDS 计算和页面显示使用软件按 `(10K+15K)/15K` 还原后的模块 AO 电压，PGA 量程判断仍使用 ADS1115 引脚上的实际电压。

## 关键芯片与软件关系

| 位号 | 型号 | 作用 | 软件关系 |
| --- | --- | --- | --- |
| U1 | ESP32-WROOM-32E-N4 | 主控 | 当前 `BoardPins.h` 按模块焊盘网络映射 |
| U2 | AP63205WU-7 | 12V 转 5V 降压 | 纯硬件电源，不注册软件驱动 |
| U4 | ADS1115IDGSR | 四路 16 位 ADC | 地址 `0x48`；AIN0 VIN、AIN1 水温、AIN2 TDS、AIN3 预留 |
| U5 | ME6211A33PG-N | 5V 转 3.3V LDO | 纯硬件电源，不注册软件驱动 |
| U7 | EG27324 | MOS 栅极驱动 | GPIO26 PWM；GPIO32 `SD` 高电平强制关闭 |
| U8 | SN74LVC2G17DBVR | 双路施密特整形 | 输出到 GPIO33/GPIO25；软件只计量 GPIO33 |
| Q1 | IRLR7843TRPBF | 阀门低侧 MOS | 由 U7 驱动，不由业务层直接操作 |

PCB BOM 和网表中没有 RTC/DS3231，主固件不包含 RTC 驱动或地址扫描。GPIO13 蜂鸣器是后续飞线功能，不在当前 PCB 网表内。

## 接口针序

| 接口 | 针序 |
| --- | --- |
| U3 四键 | 1 GND、2 OK、3 CANCEL/ESC、4 PLUS/UP、5 MINUS/DOWN |
| U9 MH-01 六线 | 1 TDS_BLUE、2 TDS_RED、3 NTC50K_IN、4 FLOW_PULSE_IN、5 GND、6 5V |
| CN3 TFT | 1 GND、2 3.3V、3 SCLK、4 MOSI/SDA、5 RST、6 DC、7 CS、8 BLK |
| CN4 阀门 | 1 VIN/12V、2 VALVE1- |
| CN5 第二脉冲 | 1 PUL_SIG2、2 GND、3 5V |
| CN6 主流量 | 1 FLOW_PULSE_IN、2 GND、3 5V |
| H1 UART | 1 GND、2 TX、3 RX、4 3.3V |
| H2 TDS 模块方向 A | 1 TDS_ADC_VAL、2 T1、3 GND、4 5V |
| H3 TDS 模块方向 B | 1 5V、2 GND、3 T1、4 TDS_ADC_VAL |
| H4 TDS 电极 | 1 TDS_RED、2 TDS_BLUE |

## 安全与可靠性要求

- EG27324 `SD` 高电平强制 OUTA/OUTB 为低电平。PCB 使用上拉保证 ESP32 复位期间默认关阀。
- 软件初始化时必须先配置 `SD=HIGH`，再配置 PWM 引脚和 LEDC。
- 任何关阀动作必须先写 `SD=HIGH` 立即强制关闭输出，再写 PWM 为 0；重复关阀必须幂等。
- 开阀只允许业务状态机下发有效输出后执行：保持 `SD=HIGH` 时先确保 PWM 为 0，再将 `SD` 拉低；随后输出两个 50% PWM 启动周期，保证 EG27324 在解除逐周关断后看到新的输入边沿，最后才进入全功率吸合。启动脉冲只允许在关闭到开启的状态转换中执行，不得因主循环重复下发相同输出而重复触发。
- `CANCEL` 使用 GPIO39 下降沿中断；ISR 只记录事件，主循环优先执行紧急停止。
- 主流量脉冲使用 GPIO33，不启用内部上拉；电平和整形由 PCB 上拉、滤波和 SN74LVC2G17 负责。
- 第二脉冲 GPIO25 未经明确需求确认前不得加入水量累计。
- 阀门 PWM 默认 20kHz，可在系统设置中配置 100-30000Hz；修改只允许在待机状态执行，重新配置 LEDC 时必须保持 `SD=HIGH` 和 PWM=0。
- CN4 是 12V 正端加 MOS 低边开关输出。未接电磁阀时 `VALVE1-` 悬空，万用表跨 CN4 测得的平均电压不能用于判断 PWM 占空比；应使用示波器测 GPIO26、U7 INA/OUTA 或接入实际电磁阀后测电流。
- ADS1115 不存在或 I2C 读取失败时，水温/TDS 降级为不可用，不得阻塞本地出水、停止或 Web 轻量查询。
- GPIO13 蜂鸣器飞线必须经过外部驱动级并与 PCB 共地；不得从 ESP32 GPIO 直接为 5V 蜂鸣器供电。
- 强电、12V 执行器和 3.3V 控制信号保持隔离并可靠共地。

## 已确认实现决策

- 新版 PCB 引脚以本文件和 `include/drivers/BoardPins.h` 为准。
- `CANCEL=GPIO39`、`OK=GPIO36`、`PLUS=GPIO34`、`MINUS=GPIO35`。
- 阀门使用 `PWM=GPIO26` 和 `SD=GPIO32` 双信号控制。
- 主流量输入使用 GPIO33；GPIO25 只保留诊断。
- 水温、TDS 和输入电压统一通过 ADS1115，不再使用 ESP32 内部 ADC。
- 蜂鸣器固定 GPIO13；当前 PCB 未连接，后续飞线。
- 当前 PCB 不使用 RTC，主固件不初始化或扫描 DS3231。
- 当前测试设备允许按新版存储结构和硬件配置直接重建，不保留旧板 GPIO 兼容层。
