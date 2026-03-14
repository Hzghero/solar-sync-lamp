# XL2400T 驱动分析报告

> 基于 v1.8.2 可用版本分析，版本日期：2026-03-09

---

## 1. 硬件连接

### 1.1 引脚定义（3线软件SPI）

| XL2400T 引脚 | STM32 引脚 | 功能说明 |
|-------------|-----------|---------|
| CSN | PA4 (RF_CSN_Pin) | SPI 片选，低电平有效 |
| SCK | PA5 (RF_SCK_Pin) | SPI 时钟 |
| DATA | PA7 (RF_DATA_Pin) | SPI 数据（双向） |
| VCC | 3.3V | 电源 1.7~3.6V |
| GND | GND | 地 |

### 1.2 3线SPI特点

- **单线双向数据**：DATA 引脚在写操作时为输出，读操作时切换为输入
- **软件位操作**：通过 GPIO 模拟 SPI 时序
- **无硬件 CE 引脚**：XL2400T SOP8 封装通过 SPI 寄存器命令控制 CE 状态

---

## 2. 驱动层次结构

```
┌─────────────────────────────────────────────┐
│              应用层 (main.c)                 │
│  Sync_MainLoop() / BuildSyncPacket() 等     │
├─────────────────────────────────────────────┤
│           RF 链路层 (rf_xl2400.c)            │
│  RF_Link_Init/ConfigTx/ConfigRx/Send/Poll   │
├─────────────────────────────────────────────┤
│          底层驱动 (XL2400T.c)                │
│  XL2400T_Init/RF_TX_Data/RF_RX_Data 等      │
├─────────────────────────────────────────────┤
│           软件SPI (XL2400T.c)                │
│  RF_SPI_Write_Reg/RF_SPI_Read_Reg 等        │
└─────────────────────────────────────────────┘
```

---

## 3. 关键寄存器配置（XL2400T_Init）

### 3.1 初始化流程

```c
1. 复位序列：CFG_TOP = 0x02 → 延时2ms → CFG_TOP = 0x3E → 延时2ms
2. 模拟参数配置：PGA_SETTING 寄存器
3. 地址配置：SETUP_AW = 0xAF (5字节地址)，默认地址 0xCCCCCCCCCC
4. 数据速率：RF_SETUP = C_DR_1M (1Mbps)
5. 接收配置：
   - EN_RXADDR = 0x01 (使能通道0)
   - RX_PW_PX = 8 (固定8字节负载)
   - EN_AA = 0x00 (禁用自动应答 - 重要！普通模式)
   - DYNPD = 0x00 (禁用动态负载长度)
   - FEATURE = 0x18 (FEC + 白化使能)
6. 发射配置：
   - SETUP_RETR = 0x33 (重传配置，但EN_AA=0时不生效)
   - RF_TX_Power = C_RF13dBm (最大功率13dBm)
```

### 3.2 关键配置值说明

| 配置项 | 值 | 说明 |
|-------|-----|------|
| EN_AA | 0x00 | **禁用自动应答** - 普通模式，单向发送/接收 |
| DYNPD | 0x00 | 禁用动态负载长度，使用固定8字节 |
| RF_PACKET_SIZE | 8 | 固定负载长度8字节 |
| C_DR_1M | 0x02 | 1Mbps 数据速率（比250Kbps快4倍） |
| C_RF13dBm | 36 | 最大发射功率13dBm |

---

## 4. 发送流程（RF_TX_Data）- 重要！

```c
unsigned char RF_TX_Data(unsigned char* tx_buff)
{
  // 1. 清除状态标志和FIFO
  RF_Refresh_State();

  // 2. 写入发送数据到 TX FIFO
  RF_Write_Buff(W_TX_PLOAD, tx_buff, RF_PACKET_SIZE);

  // 3. CE 脉冲触发发射
  RF_CE_High();
  RF_DelayUs(100);  // CE 至少保持 10us 以上
  RF_CE_Low();
  
  // 4. 等待发射完成（约100ms固定延时）
  RF_DelayMs(100);  // ⚠️ 关键：必须等待发射完成后再读状态

  // 5. 检查发射状态
  if (RF_SPI_Read_Reg(R_REGISTER + RF_STATUS) & TX_DS) {
    RF_Refresh_State();
    return 0x20;  // 发送成功
  } else {
    RF_Refresh_State();
    return 0;     // 发送失败
  }
}
```

### 4.1 发送时序关键点

1. **RF_Refresh_State()** 清除之前的状态标志和 FIFO
2. **CE 脉冲**：高电平至少 10us，触发发射
3. **⚠️ 延时等待**：CE 低电平后必须等待约1ms让发射完成，再进行SPI操作
4. **状态检查**：读取 STATUS 寄存器的 TX_DS 位判断发射成功

### 4.2 ⚠️ 重要警告（来自数据手册）

> **在发送过程中，进行SPI读写操作会引起电源纹波，影响发射信号的质量。**
> 
> 建议：CE high 30us 后 CE low，等待发送完成后（约1ms）再进行SPI读写操作。

**v1.8.2 使用 RF_DelayMs(100) 固定等待100ms，确保发射完成。**

---

## 5. 接收流程（RF_RX_Data）

```c
unsigned char RF_RX_Data(unsigned char* rx_buff)
{
  unsigned char RfPlwid = 0;
  
  // 1. 检查是否有数据到达（RX_DR 标志）
  if (RF_SPI_Read_Reg(RF_STATUS) & RX_DR) {
    // 2. CE 拉低暂停接收
    RF_CE_Low();
    
    // 3. 读取负载长度（动态负载模式下使用）
    RfPlwid = RF_SPI_Read_Reg(R_RX_PL_WID);
    
    // 4. 从 RX FIFO 读取数据
    RF_Read_Buff(R_RX_PLOAD, rx_buff, RfPlwid);
    
    // 5. 清除状态标志
    RF_Refresh_State();
    
    // 6. CE 拉高恢复接收
    RF_CE_High();
    return 1;  // 收到数据
  } else {
    return 0;  // 无数据
  }
}
```

### 5.1 接收注意事项

- **RX_DR 标志**：数据到达时置1，需要写1清除
- **R_RX_PL_WID**：读取动态负载长度（本项目实际使用固定8字节）
- **CE 控制**：读取数据时 CE 拉低，读完后拉高恢复接收

---

## 6. 模式切换（CE 控制）

### 6.1 软件 CE 控制

XL2400T SOP8 封装无物理 CE 引脚，通过 CFG_TOP 寄存器控制：

```c
void RF_CE_High(void) {
  RF_SPI_Write_Reg(W_REGISTER + CFG_TOP, 0xEF);  // CE=1
}

void RF_CE_Low(void) {
  RF_SPI_Write_Reg(W_REGISTER + CFG_TOP, 0xEE);  // CE=0
}
```

### 6.2 TX/RX 模式切换

```c
// 发送模式
void RF_Tx_Mode(void) {
  Mode_Buff[0] = 0xEE;  // CFG_TOP 低字节
  Mode_Buff[1] = 0x80;  // CFG_TOP 高字节：PTX模式
  RF_Write_Buff(W_REGISTER + CFG_TOP, Mode_Buff, 2);
  RF_Refresh_State();
  RF_DelayMs(1);
}

// 接收模式
void RF_Rx_Mode(void) {
  Mode_Buff[0] = 0xEE;  // CFG_TOP 低字节
  Mode_Buff[1] = 0xC0;  // CFG_TOP 高字节：PRX模式
  RF_Write_Buff(W_REGISTER + CFG_TOP, Mode_Buff, 2);
  RF_Refresh_State();
  RF_CE_High();         // RX模式需要CE=1才能接收
  RF_DelayMs(1);
}
```

---

## 7. 通道配置

### 7.1 当前配置

| 方向 | 通道号 | 说明 |
|-----|-------|------|
| TX | 76 | 发送通道 |
| RX | 75 | 接收通道（相邻通道） |

### 7.2 通道设置函数

```c
void RF_Set_Chn(unsigned char Chn) {
  // 需要先清除 EN_AA bit6，设置通道后再恢复
  temp = RF_SPI_Read_Reg(R_REGISTER + EN_AA);
  temp &= ~(1 << 6);
  RF_SPI_Write_Reg(W_REGISTER + EN_AA, temp);
  
  RF_SPI_Write_Reg(W_REGISTER + RF_CH, Chn + 0x60);  // 通道 = Chn + 0x60
  
  temp = RF_SPI_Read_Reg(R_REGISTER + EN_AA);
  temp |= (1 << 6);
  RF_SPI_Write_Reg(W_REGISTER + EN_AA, temp);
}
```

### 7.3 通道与频率对应关系

RF_CH 寄存器值 = 通道号 + 0x60，实际频率 = 2400 + 通道号 MHz

---

## 8. 同步协议设计

### 8.1 同步包格式（4字节有效）

| 字节 | 内容 | 说明 |
|-----|------|------|
| 0 | 0xAA | 包头标识 |
| 1 | 0x55 | 包头标识 |
| 2 | phase_ms >> 8 | 相位高字节 |
| 3 | phase_ms & 0xFF | 相位低字节 |

### 8.2 同步参数

| 参数 | 值 | 说明 |
|-----|-----|------|
| SYNC_CYCLE_MS | 900 | 同步周期 900ms |
| SYNC_LED_ON_MS | 100 | LED亮灯时间 100ms |
| SYNC_TX_TIME_MS | 450 | TX发送时间：周期中点 |
| SYNC_TX_DELAY_MS | 6 | 传输延迟补偿 6ms |

### 8.3 同步算法

1. **固定TX时间**：所有节点在相位 450ms 时发送，避免"假同步"
2. **相位调整**：收到同步包后，计算相位差，小步调整（每次最多20ms）
3. **死区**：相位差 < 5ms 时不调整，避免抖动
4. **延迟补偿**：发送时补偿 6ms 传输延迟

---

## 9. CubeMX 代码保护区域

### 9.1 安全区域（不会被覆盖）

CubeMX 重新生成代码时，以下区域会被保留：

```c
/* USER CODE BEGIN xxx */
// 这里的代码是安全的
/* USER CODE END xxx */
```

### 9.2 main.c 中的用户代码区域

| 区域 | 位置 | 内容 |
|-----|------|------|
| USER CODE BEGIN Includes | #include 之后 | 自定义头文件 |
| USER CODE BEGIN PD | Private define | 宏定义 |
| USER CODE BEGIN PV | Private variables | 全局变量 |
| USER CODE BEGIN PFP | Function prototypes | 函数声明 |
| USER CODE BEGIN 0 | main() 之前 | 辅助函数可放这里 |
| USER CODE BEGIN 2 | main() 初始化之后 | RF初始化代码 |
| USER CODE BEGIN WHILE | while(1) 之后 | - |
| USER CODE BEGIN 3 | while(1) 内部 | 主循环代码 |
| USER CODE BEGIN 4 | main() 之后 | 所有用户函数 |

### 9.3 会被覆盖的区域

- `SystemClock_Config()` - 时钟配置会被CubeMX重新生成
- `MX_GPIO_Init()` - GPIO配置会被CubeMX重新生成
- `MX_xxx_Init()` - 所有外设初始化函数

### 9.4 切换到外部晶振后的注意事项

1. **SystemClock_Config() 会变化**：HSI → HSE，PLL 配置会改变
2. **GPIO 配置可能变化**：如果添加了 IWDG，会新增相关代码
3. **XL2400T 驱动不受影响**：都在 USER CODE 区域
4. **延时函数可能需要调整**：RF_DelayUs/RF_DelayMs 基于 NOP 循环，主频变化后可能需要调整

---

## 10. 看门狗(IWDG)集成建议

### 10.1 目的

解决 2.4G 模块电源断开再接上时程序卡住的问题。

### 10.2 实现方案

```c
// 在 USER CODE BEGIN 2 中添加 IWDG 初始化
MX_IWDG_Init();  // CubeMX 生成

// 在主循环中定期喂狗
while (1) {
  Sync_MainLoop();
  HAL_IWDG_Refresh(&hiwdg);  // 喂狗
}
```

### 10.3 IWDG 配置建议（CubeMX）

| 参数 | 建议值 | 说明 |
|-----|-------|------|
| 预分频器 | 64 | |
| 重载值 | 1000 | |
| 超时时间 | ~1.6秒 | 需大于最长阻塞时间(100ms发送) |

### 10.4 RF 模块健康检查（可选增强）

```c
int RF_Link_CheckHealth(void) {
  uint8_t status = RF_SPI_Read_Reg(R_REGISTER + RF_STATUS);
  // 0x00 = SPI无响应, 0xFF = 总线悬空
  if (status == 0x00 || status == 0xFF) {
    return 0;  // 异常
  }
  return 1;    // 正常
}

void RF_Link_Reinit(void) {
  XL2400T_SPI_Init();
  XL2400T_Init();
}
```

---

## 11. 常见问题与解决方案

### 11.1 发送后收不到数据

- 检查 TX/RX 通道配置是否正确（TX=76, RX=75）
- 检查 EN_AA = 0x00（普通模式，无自动应答）
- 检查两边地址是否一致（默认 0xCCCCCCCCCC）

### 11.2 LED 脉冲宽度不准

- 确保使用时间戳控制 LED（g_led_on_tick）
- 避免在发送过程中进行 SPI 操作
- 初始化顺序：先完成所有耗时操作，最后初始化时间基准

### 11.3 模块断电后无法恢复

- 需要重新调用 RF_Link_Init() 初始化
- 添加 IWDG 看门狗，超时自动复位
- 可选：周期性检查 STATUS 寄存器，异常时重新初始化

---

## 12. 文件清单

| 文件 | 说明 |
|-----|------|
| Core/Inc/XL2400T.h | XL2400T 驱动头文件（寄存器定义、宏、函数声明） |
| Core/Src/XL2400T.c | XL2400T 驱动实现（SPI、初始化、收发） |
| Core/Inc/rf_xl2400.h | RF 链路层接口头文件 |
| Core/Src/rf_xl2400.c | RF 链路层实现（封装底层驱动） |
| Core/Src/main.c | 主程序（同步协议实现） |
| Core/Inc/main.h | 主程序头文件（FW_VERSION、引脚定义） |

---

*文档版本：1.0*
*创建日期：2026-03-09*
*基于固件版本：v1.8.2*
