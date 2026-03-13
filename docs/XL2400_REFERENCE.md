# XL2400 / XL2400P / XL2400T 驱动参考

> 基于 [py32f0-template](https://github.com/IOsetting/py32f0-template) 中 `Examples/PY32F0xx/LL/SPI/XL2400_Wireless/` 整理

---

## 1. 驱动位置

- **SPI 版**：`Examples/PY32F0xx/LL/SPI/XL2400_Wireless/xl2400.c` + `xl2400.h`
- **GPIO 模拟 SPI**：`Examples/PY32F0xx/LL/GPIO/XL2400_Wireless/`
- **型号切换**：头文件内 `#define USE_XL2400P` 或 `USE_XL2400`

---

## 2. SPI 命令

| 命令 | 值 | 说明 |
|------|-----|------|
| R_REGISTER | 0x00 | 读寄存器 [000A AAAA] |
| W_REGISTER | 0x20 | 写寄存器 [001A AAAA] |
| R_RX_PAYLOAD | 0x61 | 读 RX 数据 |
| W_TX_PAYLOAD | 0xA0 | 写 TX 数据 |
| W_TX_PAYLOAD_NOACK | 0xB0 | 写 TX 数据，无 ACK 请求 |
| FLUSH_TX | 0xE1 | 清空 TX FIFO |
| FLUSH_RX | 0xE2 | 清空 RX FIFO |
| R_RX_PL_WID | 0x60 | 读 RX 数据长度 |
| NOP | 0xFF | 空操作（读状态时使用） |

---

## 3. 核心寄存器

| 地址 | 名称 | 说明 |
|------|------|------|
| 0x00 | CFG_TOP | 配置（含 CE 控制） |
| 0x01 | EN_AA | 自动应答使能 |
| 0x02 | EN_RXADDR | 接收地址使能 |
| 0x03 | SETUP_AW | 地址宽度（5 字节） |
| 0x04 | SETUP_RETR | 重发与间隔 |
| 0x05 | RF_CH | 射频通道 |
| 0x06 | RF_SETUP | 射频设置（速率、功率） |
| 0x07 | STATUS | 状态 |
| 0x0A | RX_ADDR_P0 | 接收地址 Pipe 0 |
| 0x0B | RX_ADDR_P1 | 接收地址 Pipe 1 |
| 0x10 | TX_ADDR | 发送地址 |
| 0x11 | RX_PW_PX | RX 各 Pipe 负载宽度 |
| 0x12~0x15 | ANALOG_CFG0~3 | 模拟配置 |
| 0x17 | FIFO_STATUS | FIFO 状态 |
| 0x1D | FEATURE | 功能配置 |

---

## 4. 状态标志

| 标志 | 值 | 说明 |
|------|-----|------|
| RX_DR | 0x40 | 接收完成 |
| TX_DS | 0x20 | 发送完成 |
| MAX_RT | 0x10 | 最大重试 |
| TX_FULL | 0x01 | TX FIFO 满 |

---

## 5. RF 参数

| 速率 | 宏 | 值 |
|------|-----|-----|
| 2Mbps | XL2400_RF_DR_2M | 0x08 |
| 1Mbps | XL2400_RF_DR_1M | 0x00 |
| 250Kbps | XL2400_RF_DR_250K | 0x20（推荐） |
| 125Kbps | XL2400_RF_DR_125K | 0x28 |

| 功率 | 宏 | 值 |
|------|-----|-----|
| 10dBm | XL2400_RF_10DB | 0x3F |
| 0dBm | XL2400_RF_0DB | 0x10 |
| -12dBm | XL2400_RF__12DB | 0x04 |

- **频点**：channel 0~80，TX 频点比 RX 高 1MHz

---

## 6. 关键 API

| 函数 | 说明 |
|------|------|
| XL2400_WriteReg / XL2400_ReadReg | 单字节读写 |
| XL2400_WriteFromBuf / XL2400_ReadToBuf | 多字节读写 |
| XL2400_Init | 初始化 |
| XL2400_SetChannel | 设置通道 |
| XL2400_SetTxAddress / XL2400_SetRxAddress | 设置收发地址 |
| XL2400_SetPower | 设置发射功率 |
| XL2400_SetTxMode / XL2400_SetRxMode | 切换收发模式 |
| XL2400_Tx / XL2400_Rx | 收发数据 |
| XL2400_Sleep / XL2400_WakeUp | 休眠 / 唤醒 |
| XL2400_CE_Low / XL2400_CE_High | CE 引脚（软件控制） |
| XL2400_ClearStatus / XL2400_FlushRxTX | 清除状态与 FIFO |
| XL2400_SPI_Test | SPI 自检 |

---

## 7. XL2400T 与 XL2400P 说明

- XL2400T 为 XL2400P 升级版，传输距离约 300m
- 功耗更低：TX 6.97mA，RX 8.83mA，休眠约 1.53μA
- 寄存器以 XL2400T 数据手册为准；若无手册，可先用 XL2400P 驱动验证（硬件引脚兼容）
