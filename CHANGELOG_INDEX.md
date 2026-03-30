# 修改索引总表

## STM32C011 移植上下文（2026-03-23 建立）

**项目结构**：
- 参考（旧 MCU）：`reference Solar-powered synchronized flashing lamps/STM32f103zet6-xl2400t`（STM32F103ZET6，功能完整）
- 目标（新 MCU）：`STM32c011f6p6-xl2400t`（STM32C011F6P6 TSSOP20，通讯已通，其余功能待移植）

**移植意图**：将旧 MCU 的完整功能按积木化方式逐块移植到新 MCU，功能保持一致。通讯为最核心模块，已移植成功。

**详细文档**：
- `docs/MIGRATION_CONTEXT.md`：移植上下文、功能状态、积木顺序
- `docs/IO_MAPPING.md`：新旧 MCU IO 映射（PB5→PB6、PA11→PA6 等，C011 引脚少需重映射）

---

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
| v2.2.0 | 2026-03-09 | 充电控制：PA1 电池 ADC 直采(1K+104 RC 无分压)，过充≥1.55V 关断 CHG_MOS、<1.35V 滞回重开；过放改为由 ME2188A 0.9V 启动阈值 + 整机掉电实现（MCU 不再实现过放逻辑）；预留后续 ADC 多通道扫描优化 | Core/Src/main.c, Core/Inc/main.h, SPECIFICATION.md, CHANGELOG_INDEX.md |
| v2.3.0 | 2026-03-09 | 省电优化：日夜采样改为 1s；充电过充采样改为 3s 且仅在太阳能电压高(认为在充电)时判断；过充阈值增加二次确认（连续两次满足才关断） | Core/Src/main.c, Core/Inc/main.h, SPECIFICATION.md, CHANGELOG_INDEX.md |
| v2.3.1 | 2026-03-09 | 规划：流水灯/序列灯（长距离多节点）建议采用“波前/令牌逐跳转发”，不依赖 RSSI 排序；关键策略：令牌包包含 seq/ttl/dir，收到后按 \(seq \times \Delta t\) 延时点亮并转发一次，冲突用随机退避与去重窗口抑制 | CHANGELOG_INDEX.md |
| v2.4.0 | 2026-03-21 | 低功耗优化：夜间仅在RX窗口(850-900ms)保持RF唤醒，其余时间RF睡眠，降低夜间功耗约20% | Core/Src/main.c, Core/Inc/main.h, docs/LOW_POWER_IMPLEMENTATION.md |
| v2.4.1 | 2026-03-22 | 强化 RX 窗口/RF状态：实现 RX/非RX锁定后睡眠、TX优先模式 | Core/Src/main.c, Core/Inc/main.h |
| v2.4.2 | 2026-03-22 | RX 窗口调整为 500-650，TX 固定450，不引入抖动；保留小功耗段睡眠 | Core/Src/main.c, Core/Inc/main.h |
| v2.4.5 | 2026-03-22 | 无线频道修复：TX@76/RX@75 相邻频道策略避免自干扰；TX后立即切回RX全程侦听，消除睡眠窗口漏包问题；采用RF_Link API 规范接口 | Core/Src/main.c, Core/Inc/main.h |
| v2.4.4 | 2026-03-22 | 兼容 v2.3.0 RX 行为；TX/RX 均触发 LED(PB6)/LED_DRV(PA2) 点亮，增加状态可视化 | Core/Src/main.c, Core/Inc/main.h |
| v2.4.3 | 2026-03-22 | 兼容 v2.3.0 RX 行为：TX 后立即切回 RX，夜间常开 RX，便于排查同步未收包问题 | Core/Src/main.c, Core/Inc/main.h |
| v2.4.1 | 2026-03-22 | 强化RX窗口与TX互斥：TX后的硬件保持TX状态，RX窗口关闭后唤醒RX，避免 TX/RX 状态混淆 | Core/Src/main.c, Core/Inc/main.h |
| v1.1.0-rules | 2026-03-23 | 开发规则：强制要求代码使用中文注释；建立 C011 移植上下文、IO 映射文档 | PROJECT_RULES.md, CHANGELOG_INDEX.md, docs/MIGRATION_CONTEXT.md, docs/IO_MAPPING.md |
| v2.4.6 | 2026-03-23 | C011 积木1：ADC 双通道 Read_ADC1_Channel(PA0/PA1)，DayNight/Charge 改用该接口；增加 DEBUG_ADC_VERBOSE 调试打印 | STM32c011f6p6-xl2400t/Core/Src/main.c, main.h |
| v2.4.7 | 2026-03-23 | 积木2：日/夜 3s 持续确认 | main.c |
| v2.4.8 | 2026-03-23 | 积木3：充电控制增强（太阳能预判、过充二次确认） | main.c |
| v2.4.9 | 2026-03-23 | 积木4/5：LED 驱动改为 TIM1 PWM，闪灯逻辑对齐 | main.c |
| v2.5.0 | 2026-03-23 | 积木6/7/8：主循环日夜分支、白天 RF 睡眠+CPU WFI、看门狗恢复、BuildSyncPacket 传输延迟补偿 | main.c, main.h |
| v2.5.1 | 2026-03-23 | IWDG 修复：移至主循环入口前启动；Prescaler 256 + Reload 4095 约 32s 超时；Window=0 禁用窗口 | main.c, .ioc |
| v2.5.2 | 2026-03-30 | 文档规划更新：补充项目未完成事项；新增 PA1 过充/过放联合保护评估与待办，明确 0.9V 作为硬件兜底、软件采用预过放阈值策略 | SPECIFICATION_v2.0.0.md, CHANGELOG_INDEX.md |
| v2.5.3 | 2026-03-30 | 索引补充：新增“未完成事项总览”与“PA1 预过放软件策略”条目，明确后续待办与文档计划 | SPECIFICATION_v2.0.0.md, CHANGELOG_INDEX.md |
| v2.13.1 | 2026-03-30 | 欠压更深低功耗：由 STOP 切换为 Standby（RTC Alarm A 周期唤醒，Standby exit=复位启动），启动阶段快速判断欠压并再次进入 Standby | STM32c011f6p6-xl2400t/Core/Src/main.c, STM32c011f6p6-xl2400t/Core/Inc/main.h |
| v2.13.2 | 2026-03-30 | 低功耗关键踩坑修复：进入 Standby 后 XL2400T 被 GPIO 漂移/毛刺唤醒导致 ~600uA；改为在 Standby 期间用 PWR 的 Pull-up/Pull-down + APC 保持 RF_CSN=高、RF_SCK/RF_DATA=低，并增加 WFI 观测窗口验证 RF 睡眠电流（可降至 µA 级） | STM32c011f6p6-xl2400t/Core/Src/main.c, docs/LOW_POWER_IMPLEMENTATION.md, docs/XL2400T_STANDBY_TRAPS_AND_TIPS.md |
| v2.13.3 | 2026-03-30 | Proof 对照测量补充：`WFI(SLEEP)`/忙等浅睡眠窗口电流约 190uA/160uA；进入 `enter STANDBY now` 后电流瞬降至约 1.6uA（验证 Standby 引脚保持机制已真正生效） | docs/XL2400T_STANDBY_TRAPS_AND_TIPS.md, docs/LOW_POWER_IMPLEMENTATION.md |

---

## 规格书更新记录

| 版本 | 日期 | 修改要点 |
|------|------|----------|
| v1.2.0 | 2026-03-14 | LED 驱动改为电感升压方式；PA2 改为 TIM PWM 输出 124kHz 60%；新增 PA3 (BOOST_EN) 升压使能；PA1 预留电池检测 |
| v2.0.0 | 2026-03-24 | MCU 移植到 STM32C011F6P6；IO 重映射更新；新增详细 IO 状态表；充电控制逻辑详细说明 |
| v2.0.1 | 2026-03-30 | 规格书补充未完成事项与待办计划；明确 PA1 过充可行、0.9V 过放不建议由 MCU ADC 直接执行，改为“软件预保护+硬件兜底”策略 |

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
