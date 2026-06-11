# DP5-RC20 U5 RC readiness closeout

本文记录 `DP5-RC20 U5 Verification / observability / governance closeout` 的当前结论。U5 是 RC 外层验证与治理收口，不是新的 runtime phase，不替代 `PH0` - `PH8b` 已冻结的 canonical targets。

## Gate hierarchy

`Phase8 是 canonical base gate`。它继续由 `scripts/ci/run_phase8_ci.ps1` 直接构建并运行同名 canonical runtime targets：

- `DualPadReplayTests`
- `DualPadInputV2Tests`
- `DualPadIngressTests`
- `DualPadPromptSnapshotTests`
- `DualPadPropertyTests`
- `DualPadFuzzRegressionTests`

`DualPadPresentationProjectionTests` 仍是 public-surface support proof，不被重新命名为 canonical target。

U5 新增 RC readiness outer gate：`scripts/ci/run_rc_readiness.ps1`。该脚本先调用 `scripts/ci/run_phase8_ci.ps1`，再聚合以下 release-candidate 证明：

- `replay diff`：`DualPadReplayHarness` 生成 `tests/replay/golden/phase0` dispatcher output，再由 `scripts/dev/dualpad_trace_diff.py` 对比。
- `builder JSON`：校验 `.dualpad-builder/feature_list.json` 与 `.dualpad-builder/sprint_plan.json`。
- `generated docs`：由 Phase8 内部 `DualPadDocGen` 与 generated docs clean diff 证明。
- `legacy boundary`：运行 `scripts/ci/check_legacy_authority_boundary.py`。
- `release readiness`：运行 `scripts/ci/check_release_readiness.py`。
- `U4 contract gate`：运行 `scripts/ci/check_config_prompt_menu_glyph_closure.py`。
- `release artifact manifest`：构建 `DualPadDInput8Proxy` 后运行 `scripts/dev/generate_release_artifact_manifest.py --require-build-artifacts`；最终干净提交后用 `--expect-clean` 绑定 final source commit。`--expect-clean` 检查 tracked content diff 与 index diff，不把 generated docs 的 CRLF stat-only 状态误判为 release blocker。
- `graphify`：运行 `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`。
- diff hygiene：运行 `git diff --check`。

RC outer gate 只能聚合 Phase8、replay、builder memory、graphify、release artifact 与 governance checks；不得把 Phase8 canonical targets 包装成新名字，也不得从 outer gate 反向驱动 Phase8。

RC closeout 只有在远端 GitHub Actions 同时通过以下两个 job 后才恢复为可发布证据：

1. `phase8`
2. `rc-readiness`

PR-A 合入前，本仓库只能称为 RC candidate baseline with local readiness evidence；不得写成 fully certified RC baseline。

## Real-game QA matrix

U5 不把本机手工 QA 写成自动通过项；以下矩阵是 RC 人工验证清单，任何失败都要按 release blocker 处理或显式降级为 non-blocking known limitation。

| Area | Scenario | Expected result | Evidence |
| --- | --- | --- | --- |
| 启动 | Game start with DualSense connected | 插件加载，hook/runtime gate 输出明确 supported runtime 结论，首帧不输出 dirty action | `DualPad.log`、debug snapshot |
| 启动 | Game start without DualSense | HID reader retry open，不输出 dirty input，不刷屏日志 | `DualPad.log` |
| 存档 | Load save | context / manifest / config generation 保持一致，prompt 不跨旧 scope 解析 | `runtime_debug_snapshot.csv` 或日志 |
| 存档 | Death / reload | transition frame 后不输出 stale press/release，prompt frozen 或 unavailable 有原因 | replay trace、日志 |
| 场景切换 | Fast travel / load screen | load screen / gameplay handoff 不重复触发 dirty action | 手工输入记录、日志 |
| 桌面切换 | Alt-tab / focus loss / regain | focus 恢复后 primary path 重新仲裁，不保留旧 menu cursor owner | 手工 QA 记录 |
| 设备 | Hot-plug DualSense | open 后 reset live facts、haptics handle 绑定，sequence 从 fresh state 开始 | 日志 |
| 设备 | Disconnect / reconnect DualSense | disconnect 清空 facts 和 haptics handle，reconnect 不复用 stale state | 日志 |
| 菜单 | InventoryMenu / MagicMenu / MapMenu / JournalMenu | prompt 与 owner 跟随当前 context，不解析错 glyph | 屏幕检查、日志 |
| 菜单 | DialogueMenu / ContainerMenu / BookMenu / MessageBoxMenu / Lockpicking | ignored / passthrough / direct binding 分类符合 generated facts | 屏幕检查、generated docs |
| 菜单 | FavoritesMenu | 只验证 repo-owned action / prompt facts；不恢复页面级 SWF workspace | generated facts、non-restore 记录 |
| 输入压力 | High-frequency input | ingress queue high-water 未接近容量，overflow count 为 0 或有清晰 transition | debug snapshot |
| 组合输入 | Long press + menu switch | primary path 和 prompt scope 不交叉污染 | 手工 QA 记录 |
| 组合输入 | Chord + reload | reload 后 latch invalidated，不输出 dirty chord pulse | replay / focused tests |
| 图标 | Prompt resolve + config reload | prompt 使用 frame-bound baseline，错误 scope fail-closed | debug snapshot |
| 图 | Graph reload + context change | `manifestEpoch` / `configGeneration` / `contextRevision` 可定位 skew reason | debug snapshot |

## Performance budget

U5 只定义 RC budget 与可观察指标，不把当前性能优化偷换成 runtime 合同变更。

| Metric | Budget | Release blocker threshold |
| --- | --- | --- |
| Stable runtime processing | average <= 0.50 ms/frame, P99 <= 2.00 ms/frame | P99 > 4.00 ms/frame in repeated gameplay QA |
| Prompt resolve | average <= 0.20 ms, P99 <= 1.00 ms | wrong prompt/glyph 或 P99 > 2.00 ms with log spam |
| Graph/config reload promotion | <= 100 ms for default config on local debug build | reload causes dirty action or prompt scope mismatch |
| Ingress queue high-water | < 75% capacity during stress QA | sustained overflow or unbounded backlog |
| Overflow count | 0 in normal QA; nonzero must have `QueueOverflow` transition and compaction summary | overflow without degraded reason or stale action output |
| Degraded reason transition count | one log per reason-transition key | per-frame repeated identical degraded logs |
| Log volume | <= 100 KB/min during normal QA; degraded bursts must settle after transition log | log storm that hides root cause |

`Performance budget` evidence can come from debug snapshot, trace CSV, logs, or profiler notes. If a metric is not currently emitted as a numeric counter, U5 requires the corresponding reason/debug surface to be present so a field report can still isolate the failure path.

## Observability coverage

Current debug snapshot/log surface covers the U5 required failure reasons:

- `RuntimeDebugSnapshot` exposes health reason names: `GraphUnavailable`, `ManifestEpochSkew`, `ContextRevisionSkew`, `QueueOverflow`, `SequenceGap`, `BoundaryMismatch`, `PromptScopeFrozen`, `HookInstallFailed`.
- Prompt freeze / unavailable state carries `PromptScopeFrozen`, `promptDebugReason`, prompt scope state, `manifestEpoch`, and related prompt baseline facts.
- `overflow compaction` records retained boundary facts and dropped volatile input summary; `QueueOverflow` is a transition reason, not a silent queue mutation.
- Hook failure carries install status and debug reason, including unsupported runtime, signature mismatch, failed and partial install paths.
- Manifest/config/context generation can be correlated through `manifestEpoch`, `configGeneration`, and `contextRevision`.
- Runtime logs are reason-transition keyed to prevent `log storm` behavior; identical degraded / recovered snapshots are deduplicated.
- Optional `runtime_debug_snapshot.csv` from replay/debug tracing is additive and does not become Phase0 golden required schema.

## Governance closeout

旧 #5 `GameplayKbmFactTracker` ControlMap lookup cleanup is retained as U5 `non-authority cleanup`: it does not own `input_v2` core runtime decisions and is not a release blocker while the legacy boundary static gate and RC performance budget hold. Future hot-path cleanup can be tracked separately if profiling shows a budget breach.

U5 closeout updates #12 and #13 with the RC outer gate, real-game QA matrix, performance budget and observability evidence. `DP5-RC20` child governance issues are closed only after the RC outer gate passes against the final source commit.
