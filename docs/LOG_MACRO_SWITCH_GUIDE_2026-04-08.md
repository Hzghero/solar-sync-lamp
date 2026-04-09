# 日志宏开关说明（2026-04-08）

本文档记录今天新增/使用的串口日志宏，方便后续快速恢复。

- 工程：`STM32c011f6p6-xl2400t`
- 当前版本号：`v2.15.0-ForcedScanTxDither`
- 版本号定义位置：`STM32c011f6p6-xl2400t/Core/Inc/main.h`
- 日志宏定义位置：`STM32c011f6p6-xl2400t/Core/Src/main.c`

---

## 1) 版本号打印（必须项）

> 你要求“版本号一定要打印出来”，当前已满足。

- 宏：`LOG_FW_VERSION_ENABLE`
- 当前值：`1`
- 作用：控制上电后 `[FW] <版本号>` 是否打印。
- 相关代码：`DebugPrint("[FW] " FW_VERSION "\r\n");`

建议保持为 `1`，便于确认固件是否烧录成功。

---

## 2) 今日新增的可关闭日志宏

### 2.0 快速入锁试运行宏（先跑省电策略）
- 宏：`SYNC_FAST_LOCK_ENABLE`
- 当前值：`1`（已开启）
- 作用：在 `ACQ` 状态下，满足较宽松条件即可快速进入 `LOCKED_SPARSE`，用于先验证“跨周期关 RX”省电策略是否生效。

- 宏：`SYNC_FAST_LOCK_GOOD_COUNT`
- 当前值：`1`
- 作用：开启快速入锁时，需要的 `good_count` 阈值。

说明：
- 当前为“激进入锁、保守退锁”策略：
  - 入锁放宽（更容易进入 `LOCK`）
  - 退锁逻辑保持原阈值（`bad/miss` 仍较严格）
- 后续若要收敛鲁棒性，可把 `SYNC_FAST_LOCK_ENABLE` 改回 `0`，恢复常规 `SYNC_LOCK_NEED_GOOD_COUNT`。

### 2.0b 强制省电巡检 + TX偏移探测（v2.15.0 新增）

- 宏：`SYNC_FORCED_SCAN_ENABLE`
- 当前值：`1`
- 作用：开启“上电先捕获、无包则强制巡检”的省电机制。

- 宏：`SYNC_BOOT_ACQ_CYCLES`
- 当前值：`5`
- 作用：上电后连续开 RX 的周期数（快速捕获窗口）。

- 宏：`SYNC_FORCED_SLEEP_CYCLES`
- 当前值：`30`
- 作用：强制巡检里“连续关闭 RX”周期数。

- 宏：`SYNC_FORCED_PROBE_RX_CYCLES`
- 当前值：`2`
- 作用：强制巡检里“连续打开 RX 探测”周期数。

- 宏：`SYNC_TX_DITHER_ENABLE`
- 当前值：`1`
- 作用：TX 偏移探测总开关（可一键关闭）。

- 宏：`SYNC_TX_DITHER_OFFSET_MS`
- 当前值：`20`
- 作用：TX 偏移毫秒数（本次按你的要求设为 20ms）。

- 宏：`SYNC_TX_DITHER_PROBE_ONLY`
- 当前值：`1`
- 作用：仅在“强制巡检的 PROBE 窗口”启用 TX 偏移，正常锁定状态不偏移。

提示：如果你担心大规模网络失步，可直接将 `SYNC_TX_DITHER_ENABLE` 改为 `0`，立即恢复固定 TX 时间。

以下宏都在 `main.c` 顶部定义，统一用于“只控制日志，不改变功能逻辑”。

### 2.1 `LOG_BOOT_INFO_ENABLE`
- 当前值：`0`（已关闭）
- 作用：启动/电源流程日志开关（如 `PWR/UV/RF Init/TX固定窗口`）
- 典型日志：
  - `[PWR] ...`
  - `[UV] Boot undervolt ...`
  - `RF Init... / RF Ready`
  - `[SYNC] TX fixed at ...`

### 2.2 `LOG_SYNC_SCHEDULE_ENABLE`
- 当前值：`1`（已开启）
- 作用：每周期调度日志 `[SCH]`，用于确认当周期 `RX=ON/OFF`
- 典型日志：
  - `[SCH] cyc=xxxx st=ACQ/LOCK N=4 sc=x rx=ON/OFF`

### 2.3 `LOG_SYNC_LOCK_DIAG_ENABLE`
- 当前值：`1`（已开启）
- 作用：锁定诊断日志 `[DIAG]`，用于分析为何无法进入 LOCK
- 典型日志：
  - `[DIAG] good=x bad=x miss=x no_rx=x st=ACQ/LOCK`

### 2.4 `LOG_SYNC_RXTX_VERBOSE`
- 当前值：`0`（已关闭）
- 作用：同步详细日志（量大）
- 覆盖内容：`[TX] / RX / [ADJ] / [CYCLE]`

### 2.5 `LOG_LED_VERBOSE_ENABLE`
- 当前值：`0`（已关闭）
- 作用：LED 详细日志开关

### 2.6 `LOG_ADC_VERBOSE_ENABLE`
- 当前值：`0`（已关闭）
- 作用：ADC/充电/欠压详细日志（量大，测流时建议关闭）

---

## 3) 兼容旧宏映射关系（保留）

为了不大改原代码，保留了旧宏名，并映射到新分组宏：

- `DEBUG_ADC_VERBOSE  -> LOG_ADC_VERBOSE_ENABLE`
- `DEBUG_SYNC_VERBOSE -> LOG_SYNC_RXTX_VERBOSE`
- `DEBUG_LED_VERBOSE  -> LOG_LED_VERBOSE_ENABLE`

这意味着：以后你主要改 `LOG_*` 宏即可，旧逻辑仍能工作。

---

## 4) 当前“诊断版”推荐配置（今天实装）

```c
#define LOG_FW_VERSION_ENABLE       1
#define LOG_BOOT_INFO_ENABLE        0
#define LOG_SYNC_SCHEDULE_ENABLE    1
#define LOG_SYNC_LOCK_DIAG_ENABLE   1
#define LOG_SYNC_RXTX_VERBOSE       0
#define LOG_LED_VERBOSE_ENABLE      0
#define LOG_ADC_VERBOSE_ENABLE      0
```

用途：
- 保留关键诊断（`[SCH]` + `[DIAG]` + `[FW]`）
- 关闭大流量日志，减少串口干扰和测流噪声

---

## 5) 对撞保持策略（2026-04-08 新增）

为应对“双方高度同步后长期对撞、导致计划接收周期也收不到包”的场景，新增：

- `SYNC_COLLISION_HOLD_ENABLE`（当前 `1`）
  - `1`：启用“对撞保持”
  - `0`：关闭，回到原先 miss 超时就退回 ACQ

- `SYNC_COLLISION_HOLD_MIN_NO_RX`（当前 `5`）
  - 当 `no_rx_keep_count >= 5` 且触发 miss 超时时，判定为“疑似对撞无包”
  - 不退回 ACQ，继续保持 `LOCKED_SPARSE`
  - 串口打印：`[SYNC] COLLISION-HOLD keep SPARSE RX`

这样可以实现你说的目标：
- 即便连续收不到包，也持续执行“关4开1”的稀疏接收节奏，优先保住省电收益。

---

## 6) 后续恢复建议（按场景）

### A. 只看是否关 RX
- 保持：`LOG_SYNC_SCHEDULE_ENABLE=1`
- 可选：`LOG_SYNC_LOCK_DIAG_ENABLE=0`

### B. 排查为什么进不了 LOCK
- 保持：`LOG_SYNC_SCHEDULE_ENABLE=1`
- 保持：`LOG_SYNC_LOCK_DIAG_ENABLE=1`
- 可临时开：`LOG_SYNC_RXTX_VERBOSE=1`（看 RX/TX 细节）

### C. 做功耗测试（尽量干净）
- 建议：
  - `LOG_FW_VERSION_ENABLE=1`
  - 其余 `LOG_*` 全部 `0`

---

## 6) 版本号管理约定（建议）

每次你让我改逻辑或日志配置时：
1. 递增 `FW_VERSION`
2. 保持 `[FW]` 启动打印
3. 在本文件（或 `CHANGELOG_INDEX.md`）追加一行变更摘要

这样你串口一看就知道是否是最新固件。
