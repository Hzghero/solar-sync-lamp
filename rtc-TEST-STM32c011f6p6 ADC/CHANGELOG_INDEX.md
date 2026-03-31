# CHANGELOG_INDEX.md

| 版本号 | 日期 | 修改要点 | 关联文件 |
| --- | --- | --- | --- |
| `v0.0.1-DOC-ADC-LowPower` | 2026-03-25 | 新增 `STM32C011F6P6` 低功耗 ADC 采集功能说明文档（STOP + RTC 唤醒、ADC1 扫描双通道、16 次平均、电压换算、USART1 调试宏） | `STM32C011F6P6_低功耗_ADC采集_功能说明.md` |
| `v0.1.0-StopRTC-ADCAvg16-3s` | 2026-03-25 | 在 `STOP` 模式下由 RTC Alarm A 周期唤醒（当前秒 + 3s 方式动态重设），唤醒后采集 PA0/PA1（ADC1 扫描双通道，16 次平均）并按 Vref=3.3V 换算电压，调试信息经 USART1 输出（`ENABLE_UART_DEBUG` 控制）。针对周期异常：由单 bit 唤醒标志可能丢中断事件，改为 `g_rtc_wakeup_cnt` 计数器并在主循环逐个消费，避免遗漏导致“约 60s 再次采样”。 | `Core/Src/main.c`、`Core/Inc/main.h`、`STM32C011F6P6_低功耗_ADC采集_功能说明.md` |
| `v0.1.1-StopRTC-ADCAvg16-4s` | 2026-03-25 | 在 SLEEP 调试阶段稳定 RTC Alarm A 周期：动态重设偏移由 `+3s` 改为 `+4s`，并在写入下一次闹钟前等待 seconds 刷新对齐，同时打印 currSec/nextSec 便于定位周期抖动。该阶段解决了“写闹钟点落在秒边界窗口导致时序偏差/间隔变长”的问题。 | `Core/Src/main.c`、`Core/Inc/main.h` |
| `v0.1.2-SleepRTC-ADCAvg16-4s-FixRSF` | 2026-03-25 | SLEEP 调试阶段修复 RTC seconds 读取偶发“卡住不随时间变化”的问题：在读取 RTC Time 后调用 `HAL_RTC_WaitForSynchro()` 并读取 Date 解锁影子寄存器；从而使 currSec/nextSec 每轮能正确更新 | `Core/Src/main.c`、`Core/Inc/main.h` |
| `v0.1.3-SleepRTC-ADCAvg16-3s` | 2026-03-25 | 将动态重设偏移由 `+4s` 改回 `+3s`，并同步首次 Alarm A seconds 配置；保持 seconds 同步/影子寄存器解锁逻辑不变。同时补充文档 `STM32C011F6P6_低功耗_ADC采集_功能说明.md` 的“常见坑与经验”章节（RTC 事件丢失/RSF 卡住/SWD 锁死/串口观测偏差）。 | `Core/Src/main.c`、`Core/Inc/main.h`、`STM32C011F6P6_低功耗_ADC采集_功能说明.md` |
