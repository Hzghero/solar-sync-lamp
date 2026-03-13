# 初步通讯验证 - STM32F103 + XL2400T

## 1. 集成到 STM32CubeMX 工程

1. 用 STM32CubeMX 新建 STM32F103ZET6 工程
2. 配置 SPI1：PA5=SCK, PA6=MISO, PA7=MOSI，主机模式
3. 配置 PA4 为 GPIO_Output（NSS）
4. 配置 USART1：PA9=TX, PA10=RX，115200
5. 生成代码

## 2. 添加驱动文件

将以下文件加入工程：

- `drivers/xl2400/xl2400.c`
- `drivers/xl2400/xl2400_port_stm32f1.c`
- `drivers/xl2400/xl2400.h`
- `drivers/xl2400/xl2400_port.h`

确保包含路径包含 `drivers/xl2400` 目录。

## 3. 验证步骤

### 3.1 SPI 自检

在 `main.c` 中调用：

```c
#include "xl2400.h"

// 在 main() 中，初始化 SPI1 和 GPIO 后：
if (XL2400_SPI_Test() == XL2400_OK) {
    // 通过 - 说明 SPI 通讯正常
} else {
    // 失败 - 检查接线和供电
}
```

### 3.2 收发环回（需两块板）

- **板 A**：编译为 TX 模式，每 1 秒发送一包
- **板 B**：编译为 RX 模式，收到后通过串口打印

参考 `main_xl2400_loopback.c` 中的逻辑，通过 `#define XL2400_APP_MODE 0` (RX) 或 `1` (TX) 切换。

## 4. 接线

| XL2400T | STM32F103 |
|---------|-----------|
| CSN     | PA4       |
| SCK     | PA5       |
| MISO    | PA6       |
| MOSI    | PA7       |
| VCC     | 3.3V      |
| GND     | GND       |
