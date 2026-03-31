# 新旧 MCU IO 映射对照表

> 版本：v1.0 | 创建：2026-03-23
> STM32C011F6P6 (TSSOP20) 引脚少于 F103，部分 IO 需重映射

---

## 1. 对照表

| 功能 | 旧 MCU (F103) | 新 MCU (C011) | 说明 |
|------|---------------|---------------|------|
| LED 状态指示 | **PB5** | **PB6** | 板载/调试 LED，C011 无 PB5 或已它用 |
| 分压控制 DIV_MOS | **PA11** | **PA6** | C011 TSSOP20 无 PA11，改至 PA6 |
| LED 驱动 PWM | PA2 | PA2 | 一致 |
| 升压使能 BOOST_EN | PA3 | PA3 | 一致 |
| RF 片选 CSN | PA4 | PA4 | 一致 |
| RF 时钟 SCK | PA5 | PA5 | 一致 |
| RF 数据 DATA | PA7 | PA7 | 一致，**必须 PA7 不能 PA6**（参考 CHANGELOG 问题 #1） |
| 充电控制 CHG_MOS | PA8 | PA8 | 一致 |
| 太阳能 ADC | PA0 | PA0 | 一致 |
| 电池 ADC | PA1 | PA1 | 一致 |
| 调试串口 | PA9/PA10 | PA9/PA10 | 一致 |
| 按键输入 KEY_IN | - | PB7 | 新 MCU 新增 |

---

## 2. 映射差异要点

### PB5 → PB6（LED）
- 旧项目：`LED_Pin GPIO_PIN_5, LED_GPIO_Port GPIOB`（PB5）
- 新项目：`LED_Pin GPIO_PIN_6, LED_GPIO_Port GPIOB`（PB6）
- 移植时所有 `LED_GPIO_Port/LED_Pin` 宏会自动适配，无需改逻辑

### PA11 → PA6（DIV_MOS）
- 旧项目：`DIV_MOS_Pin GPIO_PIN_11, DIV_MOS_GPIO_Port GPIOA`（PA11）
- 新项目：`DIV_MOS_Pin GPIO_PIN_6, DIV_MOS_GPIO_Port GPIOA`（PA6）
- **注意**：PA6 在旧项目中可能用于其他用途，新项目 PA6 专用于 DIV_MOS；RF_DATA 必须在 PA7，不可与 PA6 混淆

### RF_DATA 必须 PA7
- 参考 `CHANGELOG_INDEX.md` 常见问题 #1：CubeMX 曾误配为 PA6 导致通讯失败
- 新项目原理图若不同，需以实际硬件为准

---

## 3. 新 MCU 引脚汇总（STM32C011F6P6 TSSOP20）

| 引脚 | 功能 |
|------|------|
| PA0 | 太阳能 ADC |
| PA1 | 电池 ADC |
| PA2 | LED_DRV (PWM) |
| PA3 | BOOST_EN |
| PA4 | RF_CSN |
| PA5 | RF_SCK |
| PA6 | DIV_MOS |
| PA7 | RF_DATA |
| PA8 | CHG_MOS |
| PA9 | USART1_TX |
| PA10 | USART1_RX |
| PB6 | LED 状态 |
| PB7 | KEY_IN |
