# DualPad Work Packages

本文是工作包注册表。当前可枚举事实以 `.dualpad-builder/` 和 `docs/generated/` 为准。

## 当前推进状态

- `WF0`：`completed`
- `DP1`：`completed`
- `DP1a`：`completed`
- `DP2`：`completed`
- `DP3`：`completed`
- `DP4`：`completed`
- `DP4a`：`completed`
- `PH0` - `PH8b`：`completed`
- `DP5`：`planned`（post-closeout hardening；不是新的 runtime phase）
- 当前活跃 Sprint：无

状态模型：

- `PH0` - `PH8b` 是已完成的 rearchitecture / closeout 链。
- `DP1` - `DP4` 已按 PH8b baseline 结算为 completed，不再代表未完成 current runtime work。
- `DP5` / `S-DP5` 仍是 post-closeout hardening backlog；DP5-RC20 issue 结构已建立，但它不阻塞 PH8b closeout，也不能重开 runtime mainline。

## PH8b Governance Closeout

目标：

- 固定 `DualPadDocGen` provenance。
- 把 generated facts 固定到 `docs/generated/`。
- reviewed docs 去重，只保留 narrative。
- 默认 CI 直接接同名 canonical runtime targets，并额外运行 public-surface support proof target。
- builder memory、baseline、CI 和 graphify close-out 口径一致。

当前结论：

- `PH8b` / `S-PH8b` 已完成。
- `DP1` - `DP4` 已同步结算为 `completed` / `passes=true`，避免与 `PH0` - `PH8b` closeout 形成第二状态口径。
- `DP5` / `S-DP5` 保持 `planned`，但 DP5-RC20 U0 合同锁定已完成，U1-U5 后续按 GitHub issue 顺序推进。
- `.dualpad-builder/feature_list.json` 中 `PH8b` 为 `completed` / `passes=true`。
- `.dualpad-builder/sprint_plan.json` 中 `S-PH8b` 为 `completed`，`current_sprint=null`。
- 本 closeout 不新增后续 runtime phase。

## DP5-RC20 Post-Closeout Hardening

入口：

- `docs/authoritative-baseline/dp5_rc20_contract_zh.md`
- GitHub Meta issue：#13
- U0-U5 issues：#7 - #12

状态：

- U0：contract preflight and scope lock 已完成。
- U1：runtime determinism hardening，下一步实现入口。
- U2：legacy boundary collapse。
- U3：product integration and release readiness。
- U4：config / prompt / menu coverage closure。
- U5：verification / observability / governance closeout。

硬边界：

- 不新增 runtime phase。
- 不重开 `input_v2` runtime mainline。
- 不改 canonical target 名称、replay root 或旧 SWF 返回 shape。
- 不恢复 `FavoritesMenu` workspace 或 legacy glyph authority。
- visual icon artwork production 与 DualSense haptics / vibration 都不是本 milestone 默认范围。

首读：

- `docs/plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md`
- `.dualpad-builder/feature_list.json`
- `.dualpad-builder/sprint_plan.json`
- `.dualpad-builder/progress.md`
- `xmake.lua`
- `scripts/ci/run_phase8_ci.ps1`
- `.github/workflows/dualpad-ci.yml`

硬边界：

- 不改 runtime mainline。
- 不改 input-v2 runtime 合同。
- 不恢复 legacy authority。
- 不改旧 SWF 返回 shape。
- 不恢复 `FavoritesMenu` workspace。
- 不重命名 canonical test targets。
- 不迁移 replay root。
- 不把 `09a` runtime deletion 移到 `09b`。

默认 CI 自动执行：

- `xmake build DualPad`
- `xmake build/run` 六个 canonical test targets
- `xmake build/run DualPadPresentationProjectionTests`（public-surface support proof；不是 canonical target）
- `xmake build DualPadDocGen`
- `xmake run DualPadDocGen`
- `python scripts/dev/generate_dualpad_docs.py`
- `python scripts/ci/check_reviewed_docs_consistency.py`
- `git diff --exit-code -- docs/generated`

人工 close-out 必做：

- `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`
- `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- JSON / diff hygiene checks

## Generated Facts

- `docs/generated/context_catalog_zh.md`
- `docs/generated/action_sets_zh.md`
- `docs/generated/prompt_matrix_zh.md`
- `docs/generated/policies_zh.md`

这些文件由 `DualPadDocGen` 生成，不在 reviewed docs 手写复制。

## 工作包使用方式

- planning 时，先确定当前属于哪个工作包和 slice。
- 实现时只改当前 slice 范围内的代码、测试、脚本和必要文档。
- close-out 时同步 `.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json`、`.dualpad-builder/progress.md`。
- 若改动涉及代码文件，必须执行 graphify rebuild。
