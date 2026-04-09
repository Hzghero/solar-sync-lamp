# 同步接收节能方案（草案）

> 状态：待实现
> 日期：2026-04-06
> 适用工程：`STM32c011f6p6-xl2400t/Core/Src/main.c`

---

## 1. 背景与目标

当前实现为：每个周期都保持接收轮询（连续侦听）。

问题：
- RF 接收常开功耗较高；
- 在已经稳定同步后，继续全时接收的收益下降。

目标：
1. 同步初期保持高可靠（连续接收）；
2. 稳定同步后改为“跨周期接收”（间歇接收）降低功耗；
3. 一旦检测到失锁，立即回退到连续接收重新同步；
4. 参数通过宏可配置，便于现场调参。

---

## 2. 核心思路（状态机）

### 2.1 状态定义

- `SYNC_STATE_ACQUIRE`：捕获态（连续接收）
  - 每个周期都开接收窗口；
  - 用于首次同步或失锁后重同步。

- `SYNC_STATE_LOCKED_SPARSE`：锁定省电态（跨周期接收）
  - 仅每隔 N 个周期开一次接收窗口；
  - 其余周期关闭 RX（可进入更低功耗 RF 状态）。

### 2.2 状态迁移

1. `ACQUIRE -> LOCKED_SPARSE`
   - 满足“锁定判据”（见第 4 节）。

2. `LOCKED_SPARSE -> ACQUIRE`
   - 满足“失锁判据”（见第 4 节）。

---

## 3. 宏参数（可调）

> 以下参数先给默认值，后续按实测调整。

```c
/* ---------- 同步省电策略参数（建议新增到 main.c 宏区） ---------- */
#define SYNC_RX_EVERY_N_CYCLES            4U   /* 锁定后每4个周期开1次接收（可改） */
#define SYNC_LOCK_ERR_TH_MS              20U   /* 锁定误差阈值：|phase_err| <= 20ms */
#define SYNC_LOCK_NEED_GOOD_COUNT         5U   /* 连续5次良好判定为“已锁定” */
#define SYNC_UNLOCK_ERR_TH_MS            60U   /* 失锁误差阈值：|phase_err| >= 60ms */
#define SYNC_UNLOCK_NEED_BAD_COUNT        2U   /* 连续2次坏包判定失锁 */
#define SYNC_UNLOCK_MISS_COUNT            2U   /* 应开RX却连续2次收不到，判定失锁 */
#define SYNC_LOCK_ASSUME_NO_RX_CYCLES     6U   /* 锁定后若连续6个周期“应同时发射导致无包”，可维持锁定 */
```

说明：
- `SYNC_RX_EVERY_N_CYCLES` 是你要求可随时修改的主开关参数；
- 默认先取 `4`（即 3 个周期不开，第 4 个周期开一次 RX）；
- 现场如果偏差大可降到 `2~3`；稳定后可升到 `5~8`。

---

## 4. 判据设计（含“同步后可能收不到包”场景）

### 4.1 相位误差定义

- 每次收到有效同步包，计算：
  - `phase_err = wrap(rx_phase - local_phase)`，映射到 `[-T/2, +T/2]`。

### 4.2 “已同步（锁定）”判据

满足任一组合可进入或保持锁定：

1. **常规锁定判据**（主判据）
   - 连续 `SYNC_LOCK_NEED_GOOD_COUNT` 次满足：
   - `abs(phase_err) <= SYNC_LOCK_ERR_TH_MS`。

2. **对撞无包保持判据**（你提出的场景）
   - 在已进入稳定阶段后，若出现“本机与他机同时发射，接收端可能无包”的情况：
   - 连续若干周期（默认 `SYNC_LOCK_ASSUME_NO_RX_CYCLES`）未收到包，但本机本地相位演进平稳，且前序锁定质量良好；
   - 可**保持锁定态**，不立即判失锁。

> 解释：完全同步后，节点发射时刻趋同，确实可能出现“有时收不到别人包”的对撞现象。若历史锁定质量好，不应因短时无包立即回退。

### 4.3 失锁判据

满足任一条件回退 `ACQUIRE`：

1. 开 RX 窗口时连续 `SYNC_UNLOCK_MISS_COUNT` 次无有效包，且已超过“对撞无包保持区间”；
2. 连续 `SYNC_UNLOCK_NEED_BAD_COUNT` 次 `abs(phase_err) >= SYNC_UNLOCK_ERR_TH_MS`；
3. 出现明显异常（包格式错误率过高、相位突变超上限等，按实现可选）。

---

## 5. 接收窗口策略

为进一步省电，建议采用“窗口化接收”而非整半周期接收：

- 保持固定 TX 时刻（现有 450ms）；
- RX 仅在预测同步到达附近打开窗口（例如 40~80ms，可宏配置）；
- 在 `LOCKED_SPARSE` 只在“计划接收周期”打开该窗口。

这样比“整段 450~900ms 持续 RX”更省电。

---

## 6. 积木化实施计划（按 PROJECT_RULES）

### 积木1：参数与状态变量
- 新增宏（第 3 节）;
- 新增状态机变量、计数器（good/bad/miss/skip_cycle）。

### 积木2：判据函数
- 新增：
  - `Sync_CalcPhaseErr()`
  - `Sync_UpdateLockQualityOnRx()`
  - `Sync_UpdateLockQualityOnMiss()`

### 积木3：接收调度器
- 新增 `Sync_ShouldOpenRxThisCycle()`：
  - `ACQUIRE` 下每周期开；
  - `LOCKED_SPARSE` 下按 `SYNC_RX_EVERY_N_CYCLES` 开。

### 积木4：状态迁移
- 在主循环中按第 4 节判据切换状态；
- 打印关键调试日志（状态切换、锁定质量、miss计数）。

### 积木5：联调与参数扫描
- 固定其余参数，仅扫 `SYNC_RX_EVERY_N_CYCLES`（2/3/4/5/6）；
- 记录同步稳定性、失锁恢复时间、电流下降幅度。

---

## 7. 验收标准

1. 省电：平均电流较“连续接收”基线明显下降；
2. 稳定：同步偏差维持在目标范围（例如 ±20ms 内）；
3. 健壮：人为扰动后可自动回到 `ACQUIRE` 并重新锁定；
4. 可调：仅改宏即可调整策略，无需改核心流程。

---

## 8. 下一步执行建议

1. 先按本方案落地代码（仅改 `main.c`，最小侵入）；
2. 默认参数先用本文件给出的值；
3. 你现场调 `SYNC_RX_EVERY_N_CYCLES` 与阈值后，再固化最终参数。
