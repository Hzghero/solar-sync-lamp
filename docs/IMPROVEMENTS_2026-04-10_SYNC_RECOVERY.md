# 2026-04-10 同步死角排查与分层恢复方案总结

## 背景

在双节点夜间运行中出现“长时间不同步、复位其中一个节点后又恢复”的现象。并排日志显示常见形态为：

- 节点A：`st=FSCAN`（含 SLEEP/PROBE 循环）
- 节点B：`st=LOCK` 且 `no_rx` 长期高位

该现象符合“对称系统进入坏吸引子（窗口长期错开）”的特征，而非单纯 MCU 卡死。

## 关键结论

1. **Standby 唤醒后会重新初始化 RF**（`main()` 冷启动路径会执行 `RF_Link_Init` 与 `RF_Link_ConfigRx`）。
2. **连续无包不等于 RF 硬故障**。在高同步对撞场景，连续无包可自然发生。
3. **必须分层恢复**：先做协议层自救（破对称），再在“长期失败+底层异常证据”时做 RF 重初始化。

## 本次已落地改动（`main.c`）

### 1) 协议层自救（LOCK -> FORCE ACQ）

- 新增阈值：`SYNC_LOCK_NO_RX_FORCE_ACQ_TH`
- 当 `LOCK` 下长期无包达到阈值时，触发：
  - 切回 `ACQUIRE`
  - 强制连续开 RX `SYNC_FORCE_ACQ_RX_CYCLES` 个周期
  - 日志：`[RESCUE] LOCK no-rx -> FORCE ACQ`

### 2) PROBE 偏移由固定20ms改为递进探索

- 保留 `PROBE_ONLY` 语义，在 `FSCAN+PROBE` 中启用偏移。
- 偏移改为递进：`20/25/30/35/40ms`（超上限后回到起点）。
- 参数：
  - `SYNC_TX_DITHER_OFFSET_MS`（基础值）
  - `SYNC_TX_DITHER_STEP_MS`
  - `SYNC_TX_DITHER_MAX_MS`

### 3) 提升探测命中机会

- `SYNC_FORCED_PROBE_RX_CYCLES` 从 4 调整到 5（连续开RX周期更长）。

### 4) 底层异常证据计数

新增 streak 计数器（示例）：

- `g_rf_cfg_rx_fail_streak`
- `g_rf_cfg_tx_fail_streak`
- `g_rf_send_fail_streak`
- `g_rf_poll_err_streak`

用于区分“协议错窗”与“底层可能异常”。

### 5) RF 最后手段恢复（分层恢复末级）

新增 `RF_Recovery_Check()`，在夜间同步循环中调用。

触发需同时满足：

- 全局长时间无有效包（`SYNC_RECOVER_NO_VALID_MS`）
- 协议自救失败轮次达到阈值（`SYNC_RECOVER_FAIL_ROUNDS_TH`）
- 存在底层异常证据（streak >= `RF_ERR_STREAK_TH`）
- 不在冷却期（`SYNC_RECOVER_COOLDOWN_MS`）
- 每小时次数未超限（`SYNC_RECOVER_MAX_PER_HOUR`）

触发动作：

- 执行 `RF_Link_Init()`
- `ConfigRx` 并回到 `ACQUIRE`
- 再给一段强制 ACQ 连续 RX 窗口
- 打印：`[RF-RECOVER] reinit + force ACQ`

## 设计原则（本次确认）

1. **连续无包默认先按协议层问题处理**，不直接 RF 重置。
2. **破对称机制应“探索并可回收敛”**，而非长期固定偏置。
3. **RF 重初始化必须带冷却与速率限制**，避免“越修越抖”。

## 建议测试要点

1. 两节点同时冷启动，观察是否更快脱离 `FSCAN/LOCK` 长期错位。
2. 人为制造遮挡/弱信号，验证是否先触发 `[RESCUE]`，且仅在满足证据时触发 `[RF-RECOVER]`。
3. 统计一小时内 `RF-RECOVER` 次数，应明显低于上限且不频繁抖动。

## 关联文件

- `STM32c011f6p6-xl2400t/Core/Src/main.c`
- `CHANGELOG_INDEX.md`
