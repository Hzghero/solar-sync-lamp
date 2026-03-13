# XL2400T CE 控制修正 (v1.3.2)

## 问题描述

**现象**：
- RX 板的 DATA 引脚持续输出波形，即使 TX 板未发送
- 串口无法收到正确数据（`AA 55 ...`）
- 只有拔掉 PA7 或手摸天线才有乱码数据

**根本原因**：
1. **CE 控制缺失**：XL2400T 模块没有 CE 引脚引出，CE 必须通过 `CFG_TOP` 寄存器的 bit 6 软件控制
2. **CFG_TOP 配置错误**：之前的 `0x0E 0x00` 和 `0x0F 0x00` 中，bit 6 (CE) = 0，导致芯片未进入工作状态
3. **Port 层未实现**：`XL2400_NSS_Low/High()` 等函数没有实现，导致驱动无法工作
4. **EN_AA 配置错误**：自动应答全开（`0x3F`），但使用的是 NoAck 模式

---

## 修改内容

### 1. 在 `main.c` 中实现 Port 层

```c
void XL2400_NSS_Low(void)
{
  HAL_GPIO_WritePin(XL2400_NSS_GPIO_Port, XL2400_NSS_Pin, GPIO_PIN_RESET);
}

void XL2400_NSS_High(void)
{
  HAL_GPIO_WritePin(XL2400_NSS_GPIO_Port, XL2400_NSS_Pin, GPIO_PIN_SET);
}

uint8_t XL2400_SPI_Transfer(uint8_t data)
{
  uint8_t rx;
  HAL_SPI_TransmitReceive(&hspi1, &data, &rx, 1, 100);
  return rx;
}

void XL2400_DelayMs(uint32_t ms)
{
  HAL_Delay(ms);
}

void XL2400_CE_High(void)
{
  /* 通过 CFG_TOP 寄存器 bit 6 触发 CE */
  uint8_t cfg[2];
  uint8_t cmd = 0x00;
  HAL_GPIO_WritePin(XL2400_NSS_GPIO_Port, XL2400_NSS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
  HAL_SPI_Receive(&hspi1, cfg, 2, 100);
  HAL_GPIO_WritePin(XL2400_NSS_GPIO_Port, XL2400_NSS_Pin, GPIO_PIN_SET);
  
  cfg[1] |= 0x40;
  
  cmd = 0x20;
  HAL_GPIO_WritePin(XL2400_NSS_GPIO_Port, XL2400_NSS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
  HAL_SPI_Transmit(&hspi1, cfg, 2, 100);
  HAL_GPIO_WritePin(XL2400_NSS_GPIO_Port, XL2400_NSS_Pin, GPIO_PIN_SET);
}
```

### 2. 修正 `xl2400.c` 中的 `CFG_TOP` 配置

**SetTxMode**:
```c
cbuf[0] = 0x0E;  /* PWR_ON=1, RX_ON=0 */
cbuf[1] = 0x40;  /* CE=1 */
```

**SetRxMode**:
```c
cbuf[0] = 0x0F;  /* PWR_ON=1, RX_ON=1 */
cbuf[1] = 0x40;  /* CE=1 */
```

### 3. 禁用自动应答

```c
XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_EN_AA, 0x00);
```

### 4. 设置功率

```c
XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_RF_SETUP, XL2400_RF_DR_250K | XL2400_RF_0DB);
```

---

## 接线说明

**XL2400T 模块只有 4 个控制引脚**（无 CE 引出）：

```
XL2400T 模块      STM32F103
------------      ----------
CSN           →   PA4
SCK           →   PA5
DATA          →   PA7 (MOSI, 经 1K 电阻) + PA6 (MISO, 直连)
VDD           →   3.3V
GND           →   GND
```

**1K 电阻的作用**：防止 PA6 和 PA7 同时输出时短路。

---

## CFG_TOP 寄存器位定义（XL2400T）

| Bit | 名称 | 说明 |
|-----|------|------|
| 7 | LOOPBACK_EN | 1=环回测试 |
| 6 | CE | 1=使能 TX/RX |
| 5:4 | - | 保留 |
| 3 | PWR_ON | 1=上电 |
| 2:1 | RX_ON | 11=RX, 10=TX, 00=待机 |
| 0 | - | 保留 |

---

## 测试方法

1. 编译项目
2. 修改 `XL2400_LOOPBACK_MODE` 为 1，烧录到 TX 板
3. 修改 `XL2400_LOOPBACK_MODE` 为 0，烧录到 RX 板
4. 上电，观察串口输出

**预期结果**：
- TX 板：每秒发送 `AA 55 00 00 ...`
- RX 板：收到 `RX: AA 55 00 00 ...`，LED 闪烁

---

## 如果还是不工作

可能的原因：
1. **SPI 时序问题**：降低 SPI 时钟（改为 `SPI_BAUDRATEPRESCALER_16`）
2. **DATA 引脚冲突**：改用半双工模式（只连 PA7）
3. **模块硬件问题**：检查供电、天线、晶振
