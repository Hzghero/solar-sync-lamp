# 2026-04-10 同构无主同步改造总结（Stage1 + Stage2）

## 1. 今日目标

在“全节点同硬件、同固件、不可主从”的约束下，围绕 200 节点可扩展性做两阶段改造：

1. 降低同步后同相对撞概率（少说多听）；
2. 降低多参考互拉导致的相位漂移（接收择优 + 动态调相）；
3. 保持原有分层恢复框架（`RESCUE/FSCAN/RECOVER`）不被破坏。

---

## 2. 已完成改造内容

关联代码：`STM32c011f6p6-xl2400t/Core/Src/main.c`

### 2.1 Stage1：发送稀疏化（少说多听）

新增宏：
- `SYNC_TX_SPARSE_ENABLE`
- `SYNC_TX_PROB_ACQ_PERCENT`
- `SYNC_TX_PROB_LOCK_PERCENT`
- `SYNC_TX_SILENT_AFTER_RX_CYCLES`

新增状态变量：
- `g_sync_tx_enable_this_cycle`
- `g_sync_tx_silent_cycles_left`

行为变化：
- ACQ/FSCAN 阶段保持高发送积极度；
- LOCK 阶段降低发送概率，减少同步后同相并发发射；
- 收到有效包后短静默，降低“收后回喷”冲突。

可观测性：
- `[SCH]` 新增 `tx=ON/OFF` 输出，便于现场判断每周期是否允许发送。

### 2.2 Stage1：基础接收择优（首包采纳）

新增宏：
- `SYNC_RX_SELECT_FIRST_VALID_ONLY`

新增状态变量：
- `g_sync_rx_select_done_this_cycle`

行为变化：
- 同周期仅采纳一组参考，避免同周期多包连续调相导致互拉。

---

### 2.3 Stage2：可信接收择优（结合示波器漂移现象）

新增宏：
- `SYNC_RX_ACCEPT_MAX_DIFF_MS`
- `SYNC_RX_ACCEPT_IMMEDIATE_MS`
- `SYNC_RX_CONSISTENCY_DIFF_MS`

新增状态变量：
- `g_sync_rx_candidate_valid`
- `g_sync_rx_candidate_phase_ms`

行为变化：
1. **超大偏差拒收**：偏差大于 `SYNC_RX_ACCEPT_MAX_DIFF_MS` 的包不参与调相；
2. **小偏差首包直采纳**：偏差小于 `SYNC_RX_ACCEPT_IMMEDIATE_MS` 直接采纳；
3. **中偏差双包一致性确认**：首包先缓存候选，后续包与候选差异小于 `SYNC_RX_CONSISTENCY_DIFF_MS` 才采纳。

目标：
- 抑制异常包误拉；
- 降低“飘来飘去”的相位拉扯感。

---

### 2.4 Stage2：动态调相步长

新增宏：
- `SYNC_ADJ_SMALL_ERR_MS`
- `SYNC_ADJ_MID_ERR_MS`
- `SYNC_ADJ_STEP_CAP_MS`

行为变化：
- `Sync_AdjustFromPacket()` 改为分段步长：
  - 小误差：更小步，稳态抖动更低；
  - 中误差：中等步长；
  - 大误差：较大步长，但受 `SYNC_ADJ_STEP_CAP_MS` 限幅。

目标：
- 稳态更稳；
- 失步时仍有回拉能力；
- 避免单次过大调整造成反向过冲。

---

## 3. 版本与索引

### 3.1 固件版本号

`STM32c011f6p6-xl2400t/Core/Inc/main.h`
- `FW_VERSION` 已更新为：`v2.16.5-PeerSync200`

### 3.2 索引更新

`CHANGELOG_INDEX.md` 已新增：
- `v2.16.4`：200节点同构无主架构建议文档
- `v2.16.5`：Stage2 可信择优 + 动态调相步长改造
- `v2.16.6`：本文档（当日总结与观测结论）

---

## 4. 今日观测结论（现场）

基于示波器反馈：

- 相比改造前，**频繁跳动已显著减少**；
- 当前主要表现为：两节点之间出现**相对固定错位**，约 **28ms**（以现场截图为准）；
- 这说明系统已从“高频漂移”转为“稳定偏移”阶段，后续可通过参数收敛进一步压缩相位差。

---

## 5. 明日调优建议（仅参数，不改架构）

建议优先按以下顺序微调：

1. `SYNC_RX_ACCEPT_IMMEDIATE_MS`（首包直采纳阈值）
2. `SYNC_RX_CONSISTENCY_DIFF_MS`（双包一致性阈值）
3. `SYNC_TX_PROB_LOCK_PERCENT`（锁定态发送概率）
4. `SYNC_TX_SILENT_AFTER_RX_CYCLES`（收包后静默周期）

调参原则：
- 一次只改 1 个参数；
- 每组参数至少跑 10~20 分钟；
- 用示波器相位差与 `[OBS][RESCUE]` 触发频度联合判定。

---

## 6. 结语

本次改造已完成“先稳定，再逼近”的阶段目标：
- 从“频繁跳动”进入“可控稳定偏移”；
- 框架可继续在不引入主从的前提下做参数收敛；
- 下一步重点是把约 28ms 固定错位进一步收敛到更小区间。
