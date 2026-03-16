# 修改索引总表

| 版本号 | 日期 | 修改要点 | 关联文件 |
|--------|------|----------|----------|
| v1.0.0 | 2025-03-09 | 初始化：规则文件、索引表、规格书 | PROJECT_RULES.md, CHANGELOG_INDEX.md, SPECIFICATION.md |
| v1.1.0 | 2025-03-09 | MCU/无线/电源/过放过充/滞回/IO 分配/积木化 | SPECIFICATION.md |
| v1.2.0 | 2025-03-09 | 无线同步协议、XL2400 驱动移植、初步通讯验证 | docs/, drivers/, firmware/, XL2400T data/ |
| v1.3.0 | 2025-03-12 | 双板收发环回测试、LED/串口反馈 | Core/Src/main.c, docs/LOOPBACK_TEST.md |
| v1.3.1 | 2025-03-12 | 修正 XL2400T RX/TX 模式切换（CFG_TOP 寄存器） | drivers/xl2400/xl2400.c |
| v1.3.2 | 2025-03-12 | 实现 Port 层、修正 CE 控制（CFG_TOP bit 6）、禁用 EN_AA | Core/Src/main.c, drivers/xl2400/xl2400.c, docs/XL240T_CE_FIX_v1.3.2.md |
| v1.4.0 | 2026-03-09 | 集成官方 XL2400T DEMO、三线软件 SPI 打通收发，新增通用接口 `RF_Link_*`，后续正式同步协议统一使用该接口 | Core/Src/XL2400T.c, Core/Src/rf_xl2400.c, Core/Src/main.c, PROJECT_RULES.md |
| v1.5.0 | 2026-03-09 | 增加本地 900ms 周期时间基准与 LED_DRV 同步闪烁，实现周期/相位同步信标包（SYNC TX/RX 打印） | Core/Src/main.c, Core/Inc/main.h |
| v1.5.1 | 2026-03-09 | 修正同步算法，基于周期内相位差（忽略绝对 cycle）做小步调整，避免时间轴跳变，便于多节点渐进对时 | Core/Src/main.c, Core/Inc/main.h |
| v1.6.0 | 2026-03-13 | 单固件多节点：删除编译时角色选择，每节点既发又收，随机错峰 TX（基于 UID+ADC 生成偏移） | Core/Src/main.c, Core/Inc/main.h |
| v1.6.1 | 2026-03-13 | 修正 RX 频道配置：XL2400T TX=76/RX=75 相邻频道才能互通 | Core/Src/main.c, Core/Inc/main.h |
| v1.6.2 | 2026-03-13 | 修正启动时序：时间基准移到耗时初始化之后；增加 LED ON/OFF 边沿打印调试 | Core/Src/main.c, Core/Inc/main.h |
| v1.6.3 | 2026-03-13 | 修正 LED 时序：在 SyncTime_Update 中检测周期边界并立即控制 LED，避免主循环延迟导致错过边沿 | Core/Src/main.c, Core/Inc/main.h |
| v1.6.4 | 2026-03-13 | 使用时间戳控制 LED：记录点亮时刻，确保至少亮 100ms 后才熄灭，彻底解决主循环延迟问题 | Core/Src/main.c, Core/Inc/main.h |
| v1.6.5 | 2026-03-13 | 添加 TX 随机抖动（每周期 0-99ms）：避免同步后两节点 TX 时间重叠导致持续碰撞 | Core/Src/main.c, Core/Inc/main.h |
| v1.7.0 | 2026-03-13 | 稳定版：恢复固定 TX 时间（移除随机抖动），同步后即使碰撞也不影响 LED 同步效果 | Core/Src/main.c, Core/Inc/main.h |
| v1.7.1 | 2026-03-13 | 修正相位差规范化边界：delta >= 450 才减 900，修复 448ms 差异无法收敛的问题 | Core/Src/main.c, Core/Inc/main.h |
| v1.7.2 | 2026-03-13 | 所有节点使用相同的固定 TX 时间 (450ms)：彻底解决"假同步"问题，TX 时间不同导致相位收敛到不同参考点 | Core/Src/main.c, Core/Inc/main.h |
| v1.7.3 | 2026-03-13 | 添加传输延迟补偿 (10ms)：发送时预估接收方收到时的相位，进一步减小同步误差 | Core/Src/main.c, Core/Inc/main.h |
| v1.8.0 | 2026-03-13 | 优化版：RF速率提升至1Mbps、相位分辨率提高到1ms（去除4ms量化）、传输延迟补偿减至5ms | Core/Src/main.c, Core/Inc/main.h, Core/Src/XL2400T.c |
| v1.8.1 | 2026-03-13 | 测试版：取消传输延迟补偿(SYNC_TX_DELAY_MS=0)，观察原始同步效果 | Core/Src/main.c, Core/Inc/main.h |
| v1.8.2 | 2026-03-13 | 优化版：传输延迟补偿6ms（11ms/2）、发射功率提升至最大13dBm | Core/Src/main.c, Core/Inc/main.h, Core/Inc/XL2400T.h |
| v1.9.0 | 2026-03-14 | 添加 HSE 外部晶振 + IWDG 看门狗（1.6s 超时自动复位） | Core/Src/main.c, Core/Inc/main.h, .ioc |
| v2.0.0 | 2026-03-14 | LED 升压驱动：PA2 PWM 124kHz 60%、PA3 BOOST_EN 上电 100ms 后使能 | Core/Src/main.c, Core/Inc/main.h, SPECIFICATION.md |
| v2.1.0 | 2026-03-09 | 日/夜检测：PA0 太阳能板 ADC，<0.29V 开灯、>0.4V 关灯，滞回防抖，白天关灯不跑同步 | Core/Src/main.c, Core/Inc/main.h, CHANGELOG_INDEX.md |
| v2.2.0 | 2026-03-09 | 充电控制：PA1 电池 ADC 直采(1K+104 RC 无分压)，过充≥1.55V 关断 CHG_MOS、<1.35V 滞回重开 | Core/Src/main.c, Core/Inc/main.h, SPECIFICATION.md, CHANGELOG_INDEX.md |

---

## 规格书更新记录

| 版本 | 日期 | 修改要点 |
|------|------|----------|
| v1.2.0 | 2026-03-14 | LED 驱动改为电感升压方式；PA2 改为 TIM PWM 输出 124kHz 60%；新增 PA3 (BOOST_EN) 升压使能；PA1 预留电池检测 |

---

## 常见问题与教训

### 问题 #1: CubeMX GPIO 引脚配置与硬件不匹配 (2026-03-14)

**现象**：
- 使用 CubeMX 重新生成代码后，RF 模块无法通信（STATUS 寄存器始终读取 0xFF）
- 切换时钟源（HSI → HSE）后问题出现

**根本原因**：
- CubeMX 中 RF_DATA 引脚配置为 **PA6**，但硬件实际连接的是 **PA7**
- 重新生成代码后，`main.h` 中的引脚定义被覆盖为错误的 PA6

**排查过程耗时**：约 2 小时

**误导方向**：
- 一度怀疑是 HSE 时钟配置问题
- 尝试修改 SPI 时序（添加 NOP 延时、改 GPIO 速度、加上拉电阻）均无效
- 实际上原厂 DEMO 的 SPI 实现本身没有问题

**教训**：
1. **CubeMX 重新生成代码前，务必核对所有 GPIO 引脚配置是否与硬件一致**
2. 出现"原本工作正常，重新生成后失效"的问题时，优先检查 CubeMX 配置而非代码逻辑
3. 不要轻易修改经过验证的原厂驱动代码（如 SPI 时序）

**正确配置**：
| 信号 | 引脚 |
|------|------|
| RF_CSN | PA4 |
| RF_SCK | PA5 |
| RF_DATA | **PA7** |
