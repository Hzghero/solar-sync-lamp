# 2026-04-10 观测增强与参数收敛调试总结

## 背景

本次工作基于 `docs/IMPROVEMENTS_2026-04-10_SYNC_RECOVERY.md` 已落地的分层恢复方案继续推进，目标是：

1. 在**不改变功能逻辑**前提下，增强运行时可观测性；
2. 便于现场快速判断“先协议自救、后底层恢复”是否按预期执行；
3. 为后续“偏移量收敛调参”提供明确数据依据；
4. 修复本次联调中出现的编译兼容问题。

---

## 今日主要修改点（代码）

关联文件：`STM32c011f6p6-xl2400t/Core/Src/main.c`

### 1) 新增观测日志总开关与分组开关（可随时关闭）

新增宏（仅控制打印，不影响功能逻辑）：

- `LOG_SYNC_OBSERVE_ENABLE`：观测增强总开关
- `LOG_SYNC_OBS_DITHER_ENABLE`：偏移递进观测日志开关
- `LOG_SYNC_OBS_RESCUE_ENABLE`：协议自救观测日志开关
- `LOG_SYNC_OBS_RECOVER_ENABLE`：RF 恢复观测日志开关

建议：
- 联调阶段保持开启（1）
- 功耗测试/量产阶段把 `LOG_SYNC_OBSERVE_ENABLE` 置 0，一键关闭新增观测日志

### 2) 新增仅统计计数项

- `g_sync_rescue_count_total`：协议自救累计触发次数（仅统计，不参与控制判据）

### 3) 新增观测日志内容

#### A. `[OBS][DITHER]`
在 FSCAN+PROBE 连续 miss 导致偏移递进时打印：
- `idx`
- `ms`
- `probe_miss_rounds`

作用：验证偏移序列是否按预期递进/回绕。

#### B. `[OBS][RESCUE]`
在触发 `[RESCUE] LOCK no-rx -> FORCE ACQ` 时打印：
- `no_rx` 当前值
- `th` 阈值
- `force_rx` 强制 ACQ 连续 RX 周期
- `rounds` 自救失败轮次
- `cnt` 自救累计次数（低 16 位显示）

作用：验证协议自救触发条件与触发频率。

#### C. `[OBS][RECOVER]`
在触发 `[RF-RECOVER] reinit + force ACQ` 时打印：
- `no_valid_ms`（触发前无有效包时长）
- `rounds`（触发前协议自救失败轮次）
- `st_rx/st_tx/st_send/st_poll`（底层异常证据）
- `hour/max`（本小时触发次数/上限）
- `total`（累计触发次数低 16 位）

作用：验证分层恢复末级触发是否“必要且受控”。

> 说明：`no_valid_ms` 与 `rounds` 使用触发前快照打印，避免因函数内重置变量造成观测误读。

---

## 编译问题与修复

### 问题现象

Keil/ARMClang 报错：
- `invalid operands to binary expression ('void' and 'int')`
- 发生在 `RF_Link_ConfigTx(...) == 0` 与 `RF_Link_ConfigRx(...) == 0` 等比较语句

### 根因

`rf_xl2400.h` 中接口定义为：
- `void RF_Link_ConfigTx(uint8_t channel);`
- `void RF_Link_ConfigRx(uint8_t channel);`

即返回值为 `void`，不能参与 `== 0` 比较。

### 修复措施

将上述位置改回“直接调用”方式，不再比较返回值；保持既有功能行为不变。

另外，将 `for(uint32_t i = 0; ... )` 调整为 C89 兼容写法（先声明再 `for`），消除兼容性警告。

---

## 现场观察结论（基于日志）

已观察到：
- `target=450ms`（基础）
- `target=490ms`、`target=470ms`（偏移生效后）

说明：
- PROBE 场景下 TX 目标时刻已发生偏移；
- 偏移机制已从“固定发送时刻”变为“可递进探索并回绕”。

---

## 参数收敛建议（下一步）

针对“同步后偶发错开体感偏大，但能快速恢复”的反馈，建议先收敛偏移参数：

首轮建议：
- `SYNC_TX_DITHER_OFFSET_MS = 10`
- `SYNC_TX_DITHER_STEP_MS = 2`
- `SYNC_TX_DITHER_MAX_MS = 18`

更温和备选：
- `SYNC_TX_DITHER_OFFSET_MS = 8`
- `SYNC_TX_DITHER_STEP_MS = 1`
- `SYNC_TX_DITHER_MAX_MS = 12`

目标：
- 保留破对称能力；
- 降低示波器上“错开幅度”体感；
- 通过 `[OBS][DITHER]` + `target=` 观察收敛效果。

---

## 今日心得

1. 对于同步系统，**“功能实现”与“现场观感”是两件事**：可恢复不代表观感最佳，参数仍需工程化收敛。
2. 观测日志必须可总控可分组，才能在“联调可见性”与“功耗/量产简洁”之间平衡。
3. 在嵌入式项目中，接口签名一致性（`void` vs `int`）要优先确认，避免引入无效判错路径。
4. 分层恢复策略有效与否，最终要靠“触发链路可证据化”来判断：
   - 先看到 `[RESCUE]`，
   - 仅在必要时看到 `[RF-RECOVER]`，
   - 且恢复频率受控。

---

## 关联项

- `docs/IMPROVEMENTS_2026-04-10_SYNC_RECOVERY.md`
- `STM32c011f6p6-xl2400t/Core/Src/main.c`
- `STM32c011f6p6-xl2400t/Core/Inc/main.h`
- `CHANGELOG_INDEX.md`
