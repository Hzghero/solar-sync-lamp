# STM32C011F6P6 低功耗 ADC 采集功能说明

## 1. 目标与适用范围
本说明描述基于 `STM32C011F6P6` 的低功耗数据采集测试功能。功能重点是：在低功耗待机（`STOP`）下周期性唤醒，完成 `ADC1` 对指定模拟通道的采样、计算平均与电压换算，并在调试模式下通过 `USART1` 输出结果。  
采集逻辑完成后，后续将该功能模块移植到其他项目复用。

## 2. 总体架构（外设与职责）
系统包含以下关键模块：

1. 低功耗管理模块
   - 系统默认进入 `STOP` 模式以降低功耗
   - 使用 `RTC` 闹钟每隔 `3s` 唤醒 MCU
   - 唤醒后恢复时钟配置并执行采集流程

2. ADC 采样模块（`ADC1`）
   - 采集 `PA0`（ADC1_IN0）与 `PA1`（ADC1_IN1）两路通道
   - 扫描模式依次转换两路通道
   - 每个通道采样 `16` 次并取平均，降低噪声

3. 时钟配置模块
   - 系统主时钟使用外部 `HSE = 24MHz`
   - `RTC` 使用内部 `LSI` 作为低速时钟源
   - `ADC` 时钟配置为 `12MHz`（确保不超过平台允许上限）

4. 串口调试输出模块（`USART1`）
   - 使用 `USART1`（`PA9/TX`、`PA10/RX`）输出调试信息
   - 通过宏开关 `ENABLE_UART_DEBUG` 控制是否输出

## 3. 外设与引脚定义

1. ADC 输入
   - `PA0` -> `ADC1_IN0`（采集通道 0）
   - `PA1` -> `ADC1_IN1`（采集通道 1）

2. UART 调试（仅在调试模式启用）
   - `PA9` -> `USART1_TX`
   - `PA10` -> `USART1_RX`

## 4. 配置要点

### 4.1 低功耗策略
- 默认进入 `STOP` 模式，目标达到 μA 级静态功耗。
- 进入 `STOP` 前进行功耗优化：
  - 将不使用的 GPIO 配置为 `Analog`（模拟模式），减少浮空漏电与数字缓冲损耗
  - 关闭 UART 相关时钟（或让 UART 不参与低功耗）
- `RTC` 每 `3 秒`产生闹钟中断唤醒 MCU。
- 唤醒后重新初始化/恢复系统时钟配置，保证后续外设时钟正常。

### 4.2 ADC 采样策略
- 外设：`ADC1`
- 模式：扫描模式，依次转换 `PA0`、`PA1`
- 转换次数：每个通道采样 `16` 次
- 采样时间：`160.5` 个 ADC 时钟周期  
  - 在 `ADC 时钟 = 12MHz` 的情况下，采样时间约为：`160.5 / 12MHz ≈ 13.4us`
- 结果处理：
  - 对每个通道的 16 次采样求平均
  - 将平均 ADC 值转换为电压值（具体换算依赖参考电压/校准设置）

### 4.3 时钟配置
- `HSE`：24MHz 外部晶振 -> 作为系统主时钟源
- `LSI`：内部低速时钟 -> 作为 `RTC` 时钟源
- `ADC 时钟`：目标 12MHz（配置时确保满足芯片允许上限，例如 ≤14MHz）

### 4.4 调试输出宏开关
宏：`ENABLE_UART_DEBUG`

- `ENABLE_UART_DEBUG = 1`
  - 开启串口打印
  - 输出两路通道对应的电压值（例如 `PA0/PA1`）
- `ENABLE_UART_DEBUG = 0`
  - 不进行串口输出
  - 进入最低功耗策略（进一步减少 UART 相关能耗）

## 5. 工作流程（时序描述）

1. 上电复位后进行初始化
   - 初始化 HAL、系统时钟、GPIO、ADC、UART（按需）
   - 初始化 `RTC` 并配置 `3s` 闹钟

2. 进入低功耗待机
   - 将系统进入 `STOP` 模式，CPU 停止运行

3. RTC 闹钟唤醒
   - `RTC` 闹钟中断触发，MCU 从 `STOP` 唤醒
   - 唤醒后恢复系统时钟配置

4. ADC 采样与计算
   - 启动 `ADC1` 扫描转换（PA0 -> PA1）
   - 对每个通道累计采样 `16` 次并求平均
   - 进行电压换算得到最终结果

5. 调试输出（按宏开关）
   - 若 `ENABLE_UART_DEBUG=1`：通过 `USART1` 输出结果
   - 若 `ENABLE_UART_DEBUG=0`：跳过输出并保持/加强低功耗配置

6. 再次进入 STOP 模式
   - 再次设置 GPIO 为模拟模式、处理 UART 低功耗策略
   - 回到步骤 2 循环执行

## 6. 移植到其他项目的注意点
为便于复用，移植时建议按以下依赖关系检查：

1. 引脚与通道映射
   - 确保目标项目中 `PA0/PA1` 仍接入模拟信号（ADC IN0/IN1）
   - 如 UART 调试复用，确认 `USART1` 的 `TX/RX` 引脚一致

2. 时钟与功耗接口
   - `HSE=24MHz` 与 `LSI` 配置方式要与目标工程一致
   - `STOP` 模式进入/唤醒时钟恢复逻辑需要保持完整（包括唤醒后的重新配置）

3. ADC 时序参数
   - `采样时间（160.5 cycles）`、`ADC 时钟（12MHz）`、`每通道采样次数（16次）`保持一致
   - 电压换算依赖参考电压配置（如 `VREF+` / `内部参考` / `外部基准`），移植时务必核对

4. RTC 闹钟与中断处理
   - RTC 闹钟回调/中断服务必须正确唤醒 MCU，并触发后续采样流程

## 7. 验证要点（测试建议）
- 通过串口（调试模式）观察每次唤醒输出的 `PA0/PA1` 电压是否稳定，且在输入变化时响应正确。
- 在调试模式关闭后验证低功耗：待机功耗随 STOP 是否显著降低。
- 检查平均采样是否有效：噪声应较单次采样有所下降。

## 8. 常见坑与经验（移植/调试）
本节总结这次 `STM32C011F6P6` 测试中遇到的典型问题与解决经验，后续移植到其它项目时优先对照检查。

### 8.1 RTC 周期异常（例如从 3s 变成约 60s）
现象：
- 前几次唤醒周期接近预期，随后 ADC 采样与串口输出间隔变得很长（接近 1 分钟）。

根因：
- RTC Alarm A 中断触发“更快/更频繁”时，若使用单比特唤醒标志位并在主循环中清零，可能会在 `ADC` 采样、`UART` 输出等耗时操作期间发生“中断触发但主循环没来得及消费，导致事件被覆盖/丢失”。

修复经验：
- 将唤醒标志从“bit flag”改为“计数器”（例如 `g_rtc_wakeup_cnt`）：中断里只做 `++`，主循环用 `while (g_rtc_wakeup_cnt > 0)` 逐个消费并在每次消费后重设下一次 Alarm A。
- 维持“动态重设 Alarm A”：每次唤醒都根据当前 RTC seconds 计算 `nextSec`，不要完全依赖 CubeMX/静态一次性设定长期稳定。

### 8.2 RTC seconds 读数卡住（`currSec/nextSec` 不跟着走）
现象：
- 调试输出中 `currSec/nextSec` 重复不变化，进而导致下一次 Alarm A 写入逻辑异常或周期变长。

根因：
- STM32C0 的 RTC 寄存器存在影子/同步机制（RSF）。HAL 的 `HAL_RTC_GetTime()` 读取时间时会锁住影子寄存器，若后续没有正确同步并解锁影子寄存器（例如未按时调用 `HAL_RTC_WaitForSynchro()` 和在读取 Time 后配套 `HAL_RTC_GetDate()`），后续 `GetTime()` 可能读到旧值。

修复经验：
- 在设置下一次 Alarm A 前，按固定顺序处理 RTC 同步与影子寄存器：
  - `HAL_RTC_WaitForSynchro()`
  - `HAL_RTC_GetTime()`
  - 随后调用 `(void)HAL_RTC_GetDate()` 解锁 RSF
- 写入 Alarm 前加入短暂的 seconds 刷新等待（例如等待 200ms 内 seconds 发生跳变），避免“刚好处在秒边界窗口”导致下一次设定落在意外的时间点。

### 8.3 STOP 模式下 SWD 锁死/下载失败
现象：
- 进入 `STOP` 后调试器难以再次连接，表现为下载/连接卡死或无法进入调试会话。

根因：
- 不同低功耗模式与调试模块行为存在耦合；在 STOP 下电源/时钟/调试相关的时序处理不当时，容易触发调试链路不稳定。

修复经验：
- 调试阶段优先用更友好的低功耗路径：通过宏开关先走 `SLEEP/WFI`（例如 `LOWPOWER_USE_STOP=0`）。
- 若确需在 STOP 下调试，启用调试保持功能：在初始化后调用 `HAL_DBGMCU_EnableDBGStopMode()`，提高 STOP 下调试稳定性。

### 8.4 串口调试信息的“观测偏差”
经验提醒：
- `ENABLE_UART_DEBUG=1` 会显著增加唤醒后处理时间，导致你“肉眼观察的时间间隔”可能与实际 RTC 触发点的相对关系不完全一致。
- 建议先以“RTC currSec/nextSec 打印是否正确并稳定递增”为准，再评估功耗与调试输出对时序观测的影响。

### 8.5 移植到其它项目的最低检查清单
- RTC：时钟源（`LSI`）、Alarm mask（只匹配 `Seconds`）、Alarm A 中断使能、以及 seconds 写入前的 RSF 解锁流程必须保留。
- 事件处理：使用计数器逐个消费中断事件，避免单 bit 标志丢事件。
- 低功耗：调试期间优先使用 `SLEEP/WFI` 路径，确认稳定后再切回 STOP 做功耗验证，并配合 `HAL_DBGMCU_EnableDBGStopMode()`。

## 9. CubeMX / ioc 配置摘录（用于迁移复原）
下列摘录来自 `STM32c011f6p6-xl2400t.ioc` 与当前项目生成代码（`main.c` 里的 `MX_*_Init`、以及 RTC 中断/ MSP 逻辑）。迁移到同芯片、同 HAL 工程结构时，优先保证这些配置一致；否则需要在“常见坑与经验”对应条目里再做修正。

### 9.1 时钟（RCC）
- SYSCLK：`HSE=24MHz`
- RTC 时钟源：`LSI`
- ADC 时钟：`12MHz`（CubeMX/代码中使用 `HSIKER` 作为 ADC 时钟源，并通过时钟树分频得到 12MHz）

### 9.2 RTC（Alarm A：3s 周期唤醒）
- `RTC` 时钟源：`LSI（约 32k）`
- `Alarm A`：`Seconds=3`
- `AlarmMask`：屏蔽（不匹配）`Date/WeekDay`、`Hours`、`Minutes`，仅用 `Seconds` 作为匹配条件
- `NVIC`：使能 `RTC_IRQn`（优先级在项目里为 `0/0`）

### 9.3 ADC1（PA0/PA1 扫描 + 16 次平均）
- 通道映射：
  - `PA0 -> ADC1_IN0 (ADC_CHANNEL_0)`
  - `PA1 -> ADC1_IN1 (ADC_CHANNEL_1)`
- 扫描模式：`ADC_SCAN_SEQ_FIXED`
- 转换次数：`NbrOfConversion = 2`
- 通道采样时间：`ADC_SAMPLETIME_160CYCLES_5`

### 9.4 USART1（串口调试）
- 引脚映射：`PA9=USART1_TX`，`PA10=USART1_RX`
- 串口参数：`115200, 8N1, TX_RX, 无硬件流控`

### 9.5 新项目中必须手工移植/逐项验证的代码点
- RTC 唤醒事件计数：
  - `static volatile uint32_t g_rtc_wakeup_cnt`
  - `HAL_RTC_AlarmAEventCallback()` 里只做 `++`，主循环用计数器消费
- 动态重设下一次闹钟（关键稳定性逻辑）：
  - `RTC_SetNextAlarmA_3s()`：包含 `HAL_RTC_WaitForSynchro()`、读取 Time 后用 `HAL_RTC_GetDate()` 解锁 RSF、并在写 Alarm 前等待 seconds 刷新对齐
- 低功耗进入前的外设去初始化（本项目默认假设引脚为 PA0/PA1、且 UART 可按宏关闭）：
  - `Power_PrepareForStop()`：ADC stop/deinit、PA0/PA1 置为 `GPIO_MODE_ANALOG`；`ENABLE_UART_DEBUG==0` 时额外去 init UART 并将 PA9/PA10 置为 Analog
- ADC 采样/平均函数：
  - `ADC_ReadAvgPa0Pa1_16x()`：每轮 16 次采样并分别对通道 0/1 求平均
- 主循环低功耗节奏：
  - `g_rtc_wakeup_cnt==0` 才进入低功耗
  - `while (g_rtc_wakeup_cnt>0)` 逐个消费唤醒事件，并在每次消费后重设下一次 Alarm + 开始 ADC 采样与输出
- 中断与 MSP：
  - `RTC_IRQHandler()` 里必须调用 `HAL_RTC_AlarmIRQHandler(&hrtc);`
  - `HAL_RTC_MspInit()` 里必须选择 `RTCClockSelection=LSI` 并使能 `RTC_IRQn`
