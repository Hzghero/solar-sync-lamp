# XL2400T 双板收发环回测试

> v1.3.0 | 2025-03-12

---

## 1. 切换模式

在 `main.c` 中修改宏：

```c
#define XL2400_LOOPBACK_MODE  0   /* RX 板 */
#define XL2400_LOOPBACK_MODE  1   /* TX 板 */
```

---

## 2. 烧录步骤

1. **板 A（TX）**：设 `XL2400_LOOPBACK_MODE` 为 1，编译烧录
2. **板 B（RX）**：设 `XL2400_LOOPBACK_MODE` 为 0，编译烧录，接串口 115200

---

## 3. 预期现象

- **TX 板**：LED 每秒翻转一次，串口打印 "TX Mode - sending packets"
- **RX 板**：收到数据时串口打印 `RX: AA 55 xx xx ...`，LED 翻转

---

## 4. 通过标准

RX 板串口能稳定、周期性收到 TX 板发送的数据，格式为 `AA 55` + 2 字节计数 + 其余填充。
