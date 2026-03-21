# 初步通讯验证 - STM32F103 + XL2400T

> 注意：本文档已更新至 v1.4.0+ 主线接口（三线软件 SPI + RF_Link_* 通用接口）。

## 1. 集成到 STM32CubeMX 工程

1. 用 STM32CubeMX 新建 STM32F103ZET6 工程
2. 配置 GPIO（三线软件 SPI，无需硬件 SPI）：
   - PA4 = RF_CSN（GPIO_Output）
   - PA5 = RF_SCK（GPIO_Output）
   - PA7 = RF_MOSI（GPIO_Output，双向数据线）
3. 配置 USART1：PA9=TX, PA10=RX，115200
4. 生成代码

## 2. 添加驱动文件

将以下文件加入工程：

- `Core/Src/XL2400T.c`（官方 DEMO 底层驱动）
- `Core/Src/rf_xl2400.c`（通用接口封装层）
- `Core/Inc/XL2400T.h`
- `Core/Inc/rf_xl2400.h`

确保包含路径包含 `Core/Inc` 目录。

## 3. 验证步骤

### 3.1 RF 初始化

在 `main.c` 中调用：

```c
#include "rf_xl2400.h"

// 在 main() 中，初始化 GPIO 后：
RF_Link_Init();
// 通过串口打印观察是否初始化成功
```

### 3.2 收发环回（需两块板）

- **板 A**：调用 `RF_Link_ConfigTx(76)` 进入发送模式，每 1 秒调用 `RF_Link_Send()` 发送一包
- **板 B**：调用 `RF_Link_ConfigRx(75)` 进入接收模式，轮询 `RF_Link_PollReceive()` 收到后通过串口打印

参考 `main_xl2400_loopback.c` 中的逻辑。

## 4. 接线

| XL2400T | STM32F103 | 说明 |
|---------|-----------|------|
| CSN     | PA4       | 片选（低有效） |
| SCK     | PA5       | 时钟 |
| DATA    | PA7       | 数据（三线双向） |
| VCC     | 3.3V      | 供电 |
| GND     | GND       | 地 |

> **注意**：XL2400T 使用三线软件 SPI（非标准四线 SPI），无 MISO 引脚，数据线 PA7 为双向。

## 5. 通用接口说明（RF_Link_*）

| 函数 | 说明 |
|------|------|
| `RF_Link_Init()` | 初始化 SPI 和 XL2400T 参数 |
| `RF_Link_ConfigTx(ch)` | 切换到发送模式，设置频道 |
| `RF_Link_ConfigRx(ch)` | 切换到接收模式，设置频道 |
| `RF_Link_Send(buf, len)` | 发送数据包，返回 0 成功 |
| `RF_Link_PollReceive(buf, &len)` | 轮询接收，返回 1 表示收到数据 |
| `RF_Link_Sleep()` | 进入低功耗睡眠模式 |

## 6. 通过标准

- RF 初始化后串口无错误打印
- 双板环回时，RX 端能稳定收到 TX 端数据
- 串口打印 `RX: ph=xxx` 表示收到同步包
