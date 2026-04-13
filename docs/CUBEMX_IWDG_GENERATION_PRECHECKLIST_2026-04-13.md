# CubeMX 启用 IWDG 生成前检查清单（逐项勾选）

> 日期：2026-04-13  
> 用途：在不改业务代码前提下，确保“仅启用 IWDG 的代码生成”可控、可回溯、可回退。

---

## A. 生成前（Git 与工程状态）

- [ ] A1. 当前分支确认：`main`（或你指定分支）
- [ ] A2. 运行 `git status`，确认变更范围与预期一致
- [ ] A3. 先完成一次“备份提交 + 推送”到 GitHub（重要）
- [ ] A4. 记录本次备份提交号（commit hash）到工作记录

---

## B. CubeMX 项目保护设置（.ioc）

> 目标：最大程度避免误删与不可回退。

- [ ] B1. `ProjectManager.KeepUserCode = true`
- [ ] B2. `ProjectManager.BackupPrevious = true`（建议本次开启）
- [ ] B3. `ProjectManager.DeletePrevious = false`（建议本次关闭）
- [ ] B4. 不更改 Toolchain / ProjectName / MainLocation / ProjectStructure

说明：
- 当前工程已是 `KeepUserCode=true`，但同时存在 `DeletePrevious=true` 与 `BackupPrevious=false` 风险组合；
- 本次建议先临时切换到“有备份、不删旧文件”的安全组合。

---

## C. 本次仅允许改动项（最小变更原则）

- [ ] C1. 仅启用外设：`IWDG`
- [ ] C2. 仅设置 IWDG 参数（参考 `IWDG_CUBEMX_PARAMETER_AND_FEED_PLAN_2026-04-13.md`）
- [ ] C3. 不改 GPIO/ADC/TIM/UART/RTC/PWR/RCC 其他参数
- [ ] C4. 不改 Pinout（避免触发大范围重生）

---

## D. IWDG 参数录入（本次建议值）

二选一：

### 方案 D-1（推荐）1.6s
- [ ] Activated = Enabled
- [ ] Prescaler = 64
- [ ] Reload = 799
- [ ] Window = 4095

### 方案 D-2（备选）2.5s
- [ ] Activated = Enabled
- [ ] Prescaler = 64
- [ ] Reload = 1249
- [ ] Window = 4095

---

## E. 生成后第一时间检查（不改代码）

- [ ] E1. `git status`：确认仅出现 IWDG 相关文件差异
- [ ] E2. 检查 `Core/Src/main.c` 是否新增 `MX_IWDG_Init()` 调用
- [ ] E3. 检查 `Core/Inc/main.h` / `Core/Src/main.c` 是否新增 `IWDG_HandleTypeDef hiwdg` 与 `MX_IWDG_Init` 函数
- [ ] E4. 检查 USER CODE 区块内自定义逻辑是否完整保留
- [ ] E5. 若出现与预期无关的大范围 diff，立即停止并回退到备份提交

---

## F. 生成后禁止动作（在下一阶段前）

- [ ] F1. 不立即加入喂狗代码
- [ ] F2. 不混入其他功能改动
- [ ] F3. 不做“顺手清理”类改动（防止 diff 污染）

---

## G. 回退方案（故障兜底）

- [ ] G1. 若生成结果异常：`git reset --hard <备份提交号>`（仅你确认后执行）
- [ ] G2. 或基于备份分支重新开始 IWDG 最小变更
- [ ] G3. 回退后记录“异常触发点”（参数或操作步骤）

---

## H. 执行记录（人工填写）

- 执行人：
- 执行时间：
- 备份提交号：
- 使用参数方案：D-1 / D-2
- 生成后差异文件数：
- 是否通过 E1~E5：是 / 否
- 备注：
