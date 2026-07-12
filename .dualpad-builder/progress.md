# DualPad Builder Progress

## 2026-07-12 11:17:19 +08:00

- `S-DP5-MIXED-INPUT / WP2 start`：
  - 基于独立 WP1 commit `eb6aa1d` 开始实现 Skyrim KBM adapter 与 input_v2 pure producer。
  - raw ledger/quarantine 身份固定为 `(KbmPhysicalDevice,idCode)`；binding index 仅允许在 immutable snapshot 内临时查找。
  - I-KBM 的 raw reconcile 与 synthetic suppression 继续分离：production adapter 默认不声称 complete physical-only raw provider；只有显式 verified/reserved exact receipt 可被 pure producer suppression，unproven 一律按 physical 处理。
  - 本 WP 只发布 coherent KBM facts，不启用 gameplay gate、channel arbitration 或 current-cycle mutation。

## 2026-07-12 11:15:09 +08:00

- `S-DP5-MIXED-INPUT / WP1 completed`：
  - `GamepadActivityClassifier.cpp` 已实现 context-neutral raw 分类：每份有效 report 都发布 complete current-state；digital press/release 有序发布；只有 press、analog enter/change 和 touchpad press 生成 meaningful/source activity；unchanged held、release、回中与 neutral report 不生成 takeover activity。
  - stick enter/change 阈值为 `0.25 / L1 0.12 / direction 12°`，trigger enter/change 阈值为 `0.15 / 0.08`，未改变计划既有手感阈值。
  - live `HidReader` 已迁到 `Classify -> PublishGamepadBatch`，connect 使用独立 connection draft，disconnect 使用 `PublishGamepadDisconnect`；不再调用 `CollectGamepadSourceEvidence`、`LiveInputFactProducer` 或 legacy `SubmitSnapshot`。dispatcher 只提供 `NotifyIngressPublished` 调度 shim，不恢复 authority。
  - `FrameAssembler` 只为 classified digital edge 保留现有 kernel/pulse 兼容入口；gamepad meaningful/source activity 仍是 ordered shadow，尚未在 WP4 前取得 presentation authority。
  - RED：classifier 链接符号缺失；classified press 未进入 pulse ledger；live HID wiring static contract 仍检测到旧无条件 gamepad evidence 路径。
  - GREEN：`xmake run -y DualPadIngressTests` exit 0；覆盖 500/1000 Hz producer × 30/60/120 Hz owner neutral interleave、120 次 unchanged held、release/no-takeover、connect/disconnect、context-neutral draft 与 classified digital kernel compatibility。
  - Wiring：`python tests/python/test_mixed_input_wp1_wiring.py` exit 0，3 tests passed。
  - 相邻回归：`xmake run -y DualPadPresentationProjectionTests`、`xmake run -y DualPadInputV2Tests`、`xmake build -y DualPad` 均 exit 0。
  - `rg` 确认 `PublishGamepadBatch` 的唯一 production caller 是 `HidReader`；旧 `RecordGamepadEvidence(true)` 仅保留在未被 live HID 调用的 legacy source-evidence helper 中。
  - 本 WP 未实现 WP2 KBM producer、WP3 coherence、WP4 routing/presentation、WP5 arbitration/receipt，也未启用任何 IDA gate capability。

## 2026-07-12 11:08:09 +08:00

- `S-DP5-MIXED-INPUT / WP1 start`：
  - 从已推送 commit `63f439c5e92ad04eb7ee89151fbf25eeec2c8501` 继续，开工时工作树干净，分支为 `codex/mixed-input-implementation`。
  - 本切片只实现 gamepad connectivity、complete current-state、digital edge 与 meaningful activity 分类，并把 live HID 从旧的无条件 gamepad evidence 路径迁到 WP0.5 batch API。
  - classifier 继续保持 context-neutral；不实现 source routing、presentation projection、channel arbitration、Poll receipt 或任何 IDA gated patch。

## 2026-07-12 11:03:13 +08:00

- `S-DP5-MIXED-INPUT / WP0.5 completed`：
  - 新增 `InputResetReason.h`、`MeaningfulSourceActivity.h`、`KbmGameplayFacts.h` 与 `GamepadActivityClassifier.h` 公共 transport/scaffold；classifier 只有 raw draft/result 接口，未实现 WP1 分类行为。
  - `IngressHub` 新增 `PublishGamepadBatch`、`PublishOwnerKbmBatch`、reset/disconnect receipt、gamepad connection / KBM optional latest slots、联合 owner boundary fingerprint revision、原子容量预检和累计 `_lastConsumedOrderedSeq`。`FrameAssembler` 新增 capture/coherence shadow overload；`xmake.lua` 用共享 scaffold source list 同时覆盖 runtime wildcard 与 focused ingress target。
  - RED 1：`xmake run -y DualPadIngressTests` exit 1，编译失败于缺少 `GamepadActivityClassifier.h`，证明 batch/transport API 尚不存在。
  - RED 2/3：新增 fail-closed 断言分别捕获 overflow-retained physical state 会形成 dispatchable stable frame、`InputReset` marker 会落入 stable frame；最小修复使 `virtualGameplayEligible=false` 的 latest 不进入 gameplay，并把 `InputReset` 纳入 health marker。
  - Batch 合同覆盖：API 可在无 WP1/WP2 producer 时编译链接；容量不足只发布 overflow marker，不半提交 connection/semantic records；完整 physical pad current-state 可保留但 `virtualGameplayEligible=false`；ControlMap fingerprint 与 KBM latest/receipt 在同一 transaction 使用同一 revision；drain 到 seq 7 后两次空 capture 的 `orderedCutoffSeq` 都保持 7。
  - Focused GREEN：`xmake run -y DualPadIngressTests` exit 0；`xmake run -y DualPadInputV2Tests` exit 0。
  - 相邻回归：`xmake run -y DualPadReplayTests` exit 0；`xmake run -y DualPadPresentationProjectionTests` exit 0；`xmake build -y DualPad` exit 0。
  - Source-list fixture：`MixedInputCloseoutContractTests.test_source_list_contract_keeps_runtime_and_focused_targets_in_sync` passed。
  - `rg` 审计确认新 batch/reset API 仅由测试调用，未接入 HID、KBM、Poll hook 或 runtime owner；WP1 及全部 IDA 动态门禁 capability 仍为 pending / shadow / NO-GO。

## 2026-07-12 10:50:42 +08:00

- `S-DP5-MIXED-INPUT / WP0 completed`：
  - 新增 phase-aware `tests/python/test_mixed_input_closeout_contracts.py` 和 `.dualpad-builder/mixed_input_evidence.json`，固结 audit snapshot、`implementationBaseCommit=3985eea35a84ec7952a88f8dfd13c068391fc28b`、审计范围 diff 摘要、NativeButton content audit 与 source-list parity 合同；close-out 只要求 base 为祖先，不要求相对 base 零代码差异。
  - RED：`python tests/python/test_mixed_input_closeout_contracts.py --phase preflight` exit 1；预期失败为 evidence manifest 缺失、Phase8 缺少 `DualPadGameplayProjectionTests` build/run。NativeButton target/file 存在，Sprint contributor/source-mask fixture 尚缺失，preflight 仅报告并保留给 gated WP6。
  - 最小实现只把 `DualPadGameplayProjectionTests` build/run 接入 `scripts/ci/run_phase8_ci.ps1` 并落地证据/契约检查，未修改 runtime authority 或启用 production capability。
  - GREEN：`python tests/python/test_mixed_input_closeout_contracts.py --phase preflight` exit 0，5 tests passed。
  - 相邻回归：`xmake build -y DualPadGameplayProjectionTests` exit 0；`xmake build -y DualPadNativeButtonCommitTests` exit 0。
  - `git diff --check` exit 0，仅有 Windows CRLF 提示，无 whitespace error。
  - Sprint 状态已推进为 `WP0 completed / WP0.5 in_progress`；后续 gate 仍全部保持 shadow / NO-GO。

## 2026-07-12 10:45:51 +08:00

- `S-DP5-MIXED-INPUT / repository preflight + WP0 start`：
  - 已按要求完整读取 goal attachment、Windows 仓库入口、authoritative baseline、builder memory、Graphify report、README、architecture、DOC index 与正式 mixed-input 实施计划。
  - 计划提交 `3985eea35a84ec7952a88f8dfd13c068391fc28b` 已确认存在，且正是开工时 `HEAD`；祖先检查通过。
  - 开工前 `git status --porcelain=v1 --untracked-files=all` 为 0 行，未发现不属于本任务的工作树改动。
  - 已在同一 checkout 从该提交创建专用分支 `codex/mixed-input-implementation`。
  - `implementationBaseCommit` 固结为 `3985eea35a84ec7952a88f8dfd13c068391fc28b`；固定审计快照只用于 preflight，最终 close-out 不要求相对该 base 零代码差异。
  - 已登记独立工作包 `DP5-MIXED-INPUT` 和 Sprint `S-DP5-MIXED-INPUT`；当前只启动 `WP0`，`WP0.5` 为下一个强制切片，不启用任何需 IDA 动态门禁的 production capability。
  - 范围继续固定为 post-closeout hardening：不创建 `PH9`，不重开 `PH0-PH8b` authority，不修改 Favorites native gate、`Interface/**`、SWF、glyph 资源、haptics、rumble 或无关 bindings。

## 2026-06-27 12:27:00 +08:00

- `[RC20][Hostile Hardening] menu refresh eligibility / async readiness follow-up` 按外部 review NACK 项继续收口：
  - `PresentationProjection` 新增并发布 `MenuRefreshEligibility`，由 `ResolvedContextSnapshot` 的 `hostMode`、`menuObserverCompleteness`、`identityQuality` 与 `topMenuInstanceId` 派生；`SkyrimCompatibilitySurface` 不再维护 `UiContextId` 菜单白名单，也不再直接消费 observer completeness / degraded bool。
  - `SkyrimCompatibilitySurface` 的 menu refresh 改为显式请求状态机：`inFlight`、`pendingLatest`、serial 和 captured key 分离；任务完成只能完成它实际捕获的 request key，飞行中提交的新 request 会锁存为 pending latest，旧 task stale / superseded 不得冒领新状态。
  - `RefreshPlatform` 执行结果区分 `Completed`、`DeferredNotReady`、`Superseded`；`RE::UI` 缺失、零刷新或半初始化 menu 会走 bounded Deferred retry，Deferred 不写 completed key，后续 ready tick 可以重新排队同一 intent。
  - 测试覆盖 `MenuRefreshEligibility` 矩阵、stable generic `UnknownTrackedMenu`、in-flight / pending latest 竞态、completion ownership、queue failure 不消费 request、Deferred bounded retry 与未完成重试。
- RC readiness 首次复跑失败于 phase0 replay：`03_main_menu_glyph` 的 `glyph_queries.csv` actual 只有 header，golden 有 1 行。根因定位为实机性能修正把 `trace_record_glyph_queries=false` 写入默认现场配置后，replay-only `ScaleformGlyphBridge::ReplayResolveActionGlyph()` 也被该现场 trace gate 关闭。修复为 replay-only resolver 始终记录 replay session glyph query，live trace gate 仍保持关闭。
- Focused 验证：
  - `xmake run -y DualPadPresentationProjectionTests`：exit 0。
  - `xmake run -y DualPadInputV2Tests`：exit 0。
  - `xmake run -y DualPadReplayHarnessTests`：exit 0。
  - `xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0，10 个 phase0 场景均 `no diff`。
- Close-out 验证：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；构建/运行 Phase 8 canonical targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 generated docs、reviewed docs consistency、legacy authority boundary、release readiness、config/prompt/menu/glyph closure 与 `docs/generated` clean gate。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1`：exit 0；覆盖内嵌 Phase8、ReplayHarness、phase0 trace diff、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 contract gate、RC readiness closeout、`DualPadDInput8Proxy` build、release artifact manifest、graphify rebuild 与 `git diff --check`。
  - graphify rebuild 输出 `1800 nodes / 4054 edges / 147 communities`。
  - `git diff --check` 仅输出 Windows 行尾提示，无 whitespace error。
- 构建部署：
  - `xmake build -y DualPad` 已部署 `DualPad.dll` 到本机 MO2 插件目录；`DualPad.pdb` 因目标文件占用跳过复制。
  - `xmake build -y DualPadDInput8Proxy` 已部署 `dinput8.dll` 到本机 Skyrim 目录；`dinput8.pdb` 因目标文件占用跳过复制。

## 2026-06-07 22:18:13 CST

- `DP5-RC20` U2 Legacy boundary collapse 基于 `main` merge commit `376462f` 开始推进：
  - PR #17 已通过 `gh pr merge 17 --merge` 合并到 `main`，本地 `main` 已 fast-forward 到 `376462f`。
  - 已从该 merge commit 创建分支 `codex/dp5-rc20-u2-legacy-boundary-collapse`。
  - 本切片不新增 runtime phase，不改变 `input_v2` mainline，不改 canonical target 名称、replay root、旧 SWF 返回 shape 或 `FavoritesMenu` workspace。
- 旧 #2/#3/#4/#5/#6 迁移复核：
  - #2：`GameplayOwnershipCoordinator.*` 与 `InputModalityTracker.*` 已从 `src/` 删除，反向依赖不再存在。
  - #3：`InputModalityTracker.*` 已删除；gameplay presentation adaptation 已迁移到 input_v2 presentation / publisher surface。
  - #4：旧 tracker setter 触发 `RefreshMenus` 的路径已删除；menu refresh 当前由 `SkyrimCompatibilitySurface` published presentation dirty/epoch 驱动。
  - #5：`GameplayKbmFactTracker` 仍是 legacy KBM fact / diagnostic support surface，但不进入 `input_v2` core runtime；ControlMap lookup cleanup 重新归类为 U5 / non-authority cleanup。
  - #6：`PadEventSnapshotProcessor` 已退为 compat/replay ingress bridge；degraded recovery 归属 ingress transition / runtime recovery input。
- Focused 实现：
  - 新增 `scripts/ci/check_legacy_authority_boundary.py`，禁止旧 authority 文件/标识回流、禁止 `legacySnapshot` 越过 allowlist、禁止 core runtime 决策读取 legacy tokens，并要求 Phase8 CI 调用该检查。
  - 红灯验证：首次运行 `python scripts/ci/check_legacy_authority_boundary.py` exit 1，失败点为 `scripts/ci/run_phase8_ci.ps1` 尚未调用该检查。
  - `scripts/ci/run_phase8_ci.ps1` 已接入 legacy authority boundary check；`scripts/ci/check_reviewed_docs_consistency.py` 同步要求该接线存在。
  - `DualPadIngressTests` 新增 `TestLegacySnapshotCannotOverrideKernelFacts()`，证明 `BuildKernelFrame(...)` 只从 input_v2 boundary key、monotonic time 与 `controlSamples` 构造 kernel，`legacySnapshot` 不能覆盖 kernel facts。
- 已通过的 focused 验证：
  - `python scripts/ci/check_legacy_authority_boundary.py`：exit 0，输出 `legacy authority boundary check passed`。
  - `xmake build -y DualPadIngressTests && xmake run -y DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
- 待收尾验证：
  - Phase 8 CI
  - replay diff
  - builder JSON
  - reviewed/generated docs consistency
  - graphify rebuild
  - `git diff --check`
  - GitHub U2 issue #9 checklist

## 2026-06-07 22:21:00 CST

- `DP5-RC20` U2 Legacy boundary collapse close-out 验证完成：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；构建/运行 `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 generated docs、reviewed docs consistency、legacy authority boundary static check 与 `git diff --exit-code -- docs/generated`。DocGen manifest hash 保持 `5f5914014e46c91d`。
  - `xmake build -y DualPadReplayHarness`：exit 0。
  - `xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0，输出 `reviewed docs consistency check passed`。
  - `python scripts/ci/check_legacy_authority_boundary.py`：exit 0，输出 `legacy authority boundary check passed`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1625 nodes, 3430 edges, 143 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 状态结论：
  - `PadEventSnapshotProcessor` 已由 CI static check 固定为 compat/replay ingress bridge，不持有 core runtime authority。
  - `legacySnapshot` 已由 focused test 与 static allowlist 固定为 compat/replay/debug payload，不能覆盖 input_v2 kernel facts。
  - 旧 #2/#3/#4/#6 的迁移结论仍准确；旧 #5 已重新归类为 U5 / non-authority cleanup。
  - 本轮未新增 runtime phase，未改变 `input_v2` mainline。

## 2026-06-07 20:18:23 CST

- `DP5-RC20` U1.8 Debug degraded observability 基于 `d4d44ab` 推进并完成本地 close-out：
  - 本切片只处理 degraded reason / prompt freeze / overflow compaction / hook install failure 的 debug snapshot 与 reason-transition 日志可观察性。
  - 未新增 runtime phase，未改变 `input_v2` mainline，未改 canonical target 名称、replay root、旧 SWF 返回 shape 或 `FavoritesMenu` workspace。
  - 已新增计划文档：`docs/superpowers/plans/2026-06-07-dp5-rc20-u1-8-debug-degraded-observability.md`。
- 实现结果：
  - 新增 `RuntimeDiagnostics`，将 `RuntimeHealthReasonMask` 投影为稳定名称：`GraphUnavailable`、`ManifestEpochSkew`、`ContextRevisionSkew`、`QueueOverflow`、`SequenceGap`、`BoundaryMismatch`、`PromptScopeFrozen`、`HookInstallFailed`。
  - `DualPadRuntime` 现在保留最近一帧 `RuntimeDebugSnapshot`，并用 reason-transition key 去重日志；首次健康帧不刷 recovered，重复 degraded / recovered snapshot 不重复记录。
  - degraded stable frame 会把 `PromptScopeFrozen` 加入 reason mask，并在 debug snapshot 中展示 prompt frozen / unavailable reason。
  - hook install failure debug snapshot 展示 hook status 与 debug reason；focused coverage 包括 `partial_install`、`signature_mismatch`、`unsupported_runtime`。
  - `IngressHub` overflow payload 现在携带 dropped control samples / pulse ledger / legacy snapshot debug flags；`FrameAssembler` typed compaction stable baseline 携带可读 summary。
  - `InputTraceRecorder` 支持可选 `runtime_debug_snapshot.csv` debug 文件，但它不进入 Phase0 golden required schema。
- Focused 验证：
  - `xmake build -y DualPadIngressTests && xmake run -y DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build -y DualPadInputV2Tests && xmake run -y DualPadInputV2Tests`：exit 0；stdout 仍包含既有 negative-path manifest mismatch / duplicate binding 日志，以及本轮新增 runtime debug degraded transition 日志，进程返回 0。
- Close-out 验证：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：前两次停在 `git diff --exit-code -- docs/generated`，原因是 `DualPadDocGen` 将 generated docs manifest hash 从 `9287f16196d09423` 更新为 `39a4b74cae31a15a`；暂存 `docs/generated/*.md` 后重跑 exit 0。
  - `xmake build -y DualPadReplayHarness`：exit 0。
  - `xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0，输出 `reviewed docs consistency check passed`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1607 nodes, 3391 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- GitHub #8 checklist：
  - 已将 `U1.8 Debug degraded observability` 勾选完成。
  - 已追加 U1.8 close-out gates，记录 reason mask、hook status/debugReason、prompt freeze/unavailable、overflow typed compaction、日志去重、focused tests 与完整验证。
  - `gh issue edit 8 --body ...` 返回 `https://github.com/airoucat/dualpad/issues/8`；重试 read-back verification 确认 U1.8 checklist 已勾选，且 `## U1.8 close-out gates` 已存在。

## 2026-06-07 19:29:05 CST

- `DP5-RC20` U1.7 Hook install result / Skyrim signature gate 开始推进：
  - 当前基线确认：`HEAD == origin/main == 33639e7`。
  - 已创建分支 `codex/dp5-rc20-u1-7-hook-install-result`。
  - 本切片只处理 `SkyrimCompatibilitySurface` compatibility hook 安装结果显式化、runtime version / relocation signature gate、hook failure fail-closed 与 runtime degraded/debug surface 接线。
  - 本切片不新增 runtime phase，不重开 `input_v2` mainline，不改 canonical target 名称、replay root、旧 SWF 返回 shape 或 `FavoritesMenu` workspace。
  - 已新增计划文档：`docs/superpowers/plans/2026-06-07-dp5-rc20-u1-7-hook-install-result.md`。
- 当前实现 checkpoint：
  - `SkyrimCompatibilitySurface` 新增 `HookInstallResult` / `HookInstallStatus`，显式区分 `success`、`unsupported_runtime`、`signature_mismatch`、`already_installed`、`failed`、`partial_install`。
  - 安装前新增 Skyrim SE 1.5.97 runtime gate，以及两个 call patch site 的 6-byte `write_call<6>` pattern gate；vfunc patch site 也会在 patch 前验证 relocation slot。
  - patch 过程若发生异常或中途失败会记录 `failed` / `partial_install`，并拒绝 silent retry。
  - `DualPadRuntime` 只消费抽象 `HookInstallFailed` reason 和 debug string；hook failure stable frame 不构造 live native executor，不调用 test executor，不 publish prompt scope，不输出 resolved action commands。
- Focused 验证：
  - `xmake build -y DualPadPresentationProjectionTests`：exit 0。
  - `xmake run -y DualPadPresentationProjectionTests`：exit 0。
  - `xmake build -y DualPadInputV2Tests`：exit 0。
  - `xmake run -y DualPadInputV2Tests`：exit 0；stdout 仍包含既有 negative-path publisher epoch mismatch / duplicate binding 日志，进程返回 0。
  - `xmake build -y DualPad`：exit 0；本机 xmake 配置仍会部署到 `AGENTS.win.md` 记录的本机 MO2 插件目录，该路径不写入共享 truth。
- Close-out 验证（2026-06-07 19:31:08 CST）：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；完整 build/run `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 generated docs 与 reviewed-doc consistency 检查。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0，输出 `reviewed docs consistency check passed`。
  - `xmake build -y DualPadReplayHarness && xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 0。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 dispatcher replay 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1583 nodes, 3317 edges, 145 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- GitHub #8 checklist：
  - 已将 `U1.7 Hook install result/signature gate` 勾选完成。
  - 已追加 U1.7 close-out gates，记录 hook result、runtime/signature gate、fail-closed、focused tests 与完整验证。

## 2026-06-06 01:13:53 CST

- `DP5-RC20` U1 ingress/frame contract hardening / 第二切片：
  - 已创建分支 `codex/dp5-rc20-u1-ingress-frame-contract`。
  - 本切片只处理 `FrameAssembler` strict seq / stable-window monotonic time invariant，以及 `IngressHub` typed overflow compaction；不处理 hook install failure、Axis2D、chord timestamp 或 primary path exclusivity。
  - 已新增 `docs/superpowers/plans/2026-06-06-dp5-rc20-u1-ingress-frame-contract.md`。
  - `IngressHub` overflow 现在生成带 `QueueOverflowPayload` 的单个 marker，保留最新 `ManifestEpochChanged`、`UiSnapshot`、`DeviceFamilyChanged`、`SourceEvidence`，并丢弃 volatile pad input backlog。
  - `FrameAssembler` 现在按 drain 到达顺序消费，不再用排序修复乱序输入；seq gap / regression 与 stable window 内 monotonic time regression 会 fail-closed 到 `SequenceGap` transition。
  - `QueueOverflow` payload 会先产出 `QueueOverflow` transition，再重建一份不含 `controlSamples` / `pulseLedger` / `legacySnapshot` 的 degraded baseline stable frame。
- TDD / focused 验证结果：
  - RED：`xmake build -y DualPadIngressTests` 首轮因 `IngressEvent::overflow` 不存在按预期编译失败。
  - RED：补齐 payload 后，`xmake run -y DualPadIngressTests` 因旧 assembler 会把 out-of-order seq 排序回正常顺序按预期失败。
  - GREEN：`xmake build -y DualPadIngressTests && xmake run -y DualPadIngressTests`：exit 0。
  - `xmake build -y DualPadPropertyTests && xmake run -y DualPadPropertyTests`：exit 0。
  - `xmake build -y DualPadReplayTests && xmake run -y DualPadReplayTests`：exit 0。
- 完整验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；完整 build/run `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 generated docs 与 reviewed-doc consistency 检查。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1554 nodes, 3188 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-06-06 00:49:41 CST

- `DP5-RC20` U1 PR #15 远端 Phase8 CI 修复：
  - 根因：`DualPadDocGen` 的 manifest hash 直接消费 provenance 输入文件的 raw bytes；本机工作区和 GitHub Windows runner 对部分文本输入采用不同 checkout 行尾策略，导致同一输入事实在本机生成 `a3e0989cf4a5da80`，远端生成 `a5c9ff75a458cdff`。
  - 修复：`tools/docgen/DualPadDocGenMain.cpp` 在 hash 前统一把 `CRLF` / lone `CR` 规范化为 `LF`，使 generated docs provenance hash 不再依赖工作区行尾策略。
  - 已重新生成 `docs/generated/*.md`，新的稳定 manifest hash 为 `c350db93c7217dbf`。
- 根因验证：
  - `python scripts/dev/generate_dualpad_docs.py`：exit 0，输出 `DualPadDocGen wrote docs/generated with manifest hash c350db93c7217dbf` 与 `generated docs verified`。
  - 一次性 hash 复现脚本确认：raw、强制 LF、强制 CRLF 三种输入在新规范下均生成 `c350db93c7217dbf`。
- 完整验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：先在 generated docs 未暂存时按预期停在 `git diff --exit-code -- docs/generated`；暂存本轮生成物后重跑 exit 0，完整 build/run `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 generated docs 与 reviewed-doc consistency 检查。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1545 nodes, 3137 edges, 142 communities`。
  - `git diff --check` 与 `git diff --cached --check`：exit 0。

## 2026-06-06 00:31:24 CST

- `DP5-RC20` U1 runtime determinism hardening / 首切片：
  - 已创建分支 `codex/dp5-rc20-u1-runtime-determinism`。
  - 本切片只处理 frame-bound baseline、prompt publish / resolve causality 与 runtime health reason mask；不处理 FrameAssembler strict seq/time、typed overflow compaction、hook install failure 或 Axis2D/chord/primary path exclusivity。
  - 已新增 `docs/superpowers/plans/2026-06-06-dp5-rc20-u1-runtime-frame-envelope.md` 作为本切片实现计划。
  - 已新增 `RuntimeConfigSnapshot` / `FrameRuntimeEnvelope`，stable runtime frame 入口一次性绑定 bundle、graph、context、manifest epoch 与 config generation。
  - `DualPadRuntime` 的 stable resolve、presentation projection 与 prompt publish 已改为消费同一份 envelope；context 在 output apply 期间变化时，当前 frame 仍使用 frame-bound context。
  - `PromptRuntimeOwner` 新增 explicit baseline publish API，`Resolve()` 使用已发布 baseline 的 bundle / graph，不再在 resolve 时读取 active bundle / graph。
  - `runtimeHealthDegraded` 已改为 `RuntimeHealthReasonMask` 派生 helper，并新增 `ManifestEpochSkew` 等 reason 断言。
  - 额外修复 `DualPadGlyphResolutionCompatTests` 的 xmake source list：该目标现在补齐 `ph7_ingress_files`，以满足 `ActionManifestPublisher -> IngressHub` 链接依赖。
- Focused 验证结果：
  - `xmake build -y DualPadInputV2Tests && xmake run -y DualPadInputV2Tests`：exit 0。
  - `xmake build -y DualPadPromptSnapshotTests && xmake run -y DualPadPromptSnapshotTests`：exit 0。
  - `xmake build -y DualPadGlyphResolutionCompatTests && xmake run -y DualPadGlyphResolutionCompatTests`：exit 0。
- 完整验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；第一次运行先生成了新的 `docs/generated` manifest hash，暂存 generated docs 后重跑通过。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1544 nodes, 3135 edges, 145 communities`。
  - `git diff --check` 与 `git diff --cached --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- Builder JSON 状态：
  - `.dualpad-builder/feature_list.json` 中 `DP5` 继续保持 `planned` / `passes=false`。
  - `.dualpad-builder/sprint_plan.json` 中 `S-DP5` 继续保持 `planned`，`current_sprint` 继续保持 `null`；这是 PH8b lint 对 post-closeout hardening 的要求，不表示 U1 issue slice 未推进。

## 2026-06-05 23:58:56 CST

- `DP5-RC20` U0 PR #14 远端 Phase8 CI 修复：
  - 根因：GitHub Windows runner 是干净环境，首次 `xmake build DualPad` 会请求安装 `hidapi 0.14.0` 并等待交互确认；原 `scripts/ci/run_phase8_ci.ps1` 没有传 `-y`，因此远端报 `packages(hidapi): must be installed!`。
  - 修复：将 Phase8 脚本内所有 `xmake build/run <target>` 调整为 `xmake build/run -y <target>`，保留原 canonical target 顺序，只移除 CI 上的交互确认。
  - 追加根因：非交互修复后，远端继续失败在 `unknown rule(commonlibsse-ng.plugin)`；原因是 `lib/` 被忽略，`.gitmodules` 只有 CommonLib 条目但当前 `HEAD` 没有 `lib/commonlibsse-ng` gitlink，GitHub checkout 不会自动恢复该目录。
  - 追加修复：在 `.github/workflows/dualpad-ci.yml` 中新增 `Checkout CommonLibSSE-NG` step，固定 checkout `alandtse/CommonLibVR` 的 `82e62861168308139339e5b8754586bbb556744e` 到 `lib/commonlibsse-ng`，并启用 `submodules: recursive` 恢复 `extern/openvr`。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；完整 build/run `DualPad`、`DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadPresentationProjectionTests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests` 与 `DualPadDocGen`，并通过 generated docs 与 reviewed-doc consistency 检查。
  - `git ls-remote https://github.com/alandtse/CommonLibVR.git refs/tags/v4.7.0 refs/heads/ng`：exit 0；`refs/tags/v4.7.0` 指向 `82e62861168308139339e5b8754586bbb556744e`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1530 nodes, 3097 edges, 141 communities`。

## 2026-06-04 00:48:58 CST

- `DP5-RC20` issue migration / U0 contract preflight：
  - 已按 `dualpad_dp5_rc20_github_issues.zip` runbook 创建 GitHub milestone：
    - `DP5 post-closeout hardening & RC readiness`
  - 已创建 GitHub issue 结构：
    - Meta #13 `[DP5-RC20][Meta] Post-closeout hardening & RC readiness`
    - U0 #7 `Contract preflight and scope lock`
    - U1 #8 `Runtime determinism hardening`
    - U2 #9 `Legacy boundary collapse`
    - U3 #10 `Product integration and release readiness`
    - U4 #11 `Config / prompt / menu coverage closure`
    - U5 #12 `Verification / observability / governance closeout`
  - 已将旧 #2-#6 加入 DP5-RC20 milestone，标记 `status:superseded`，并以 `not_planned` 关闭；迁移评论说明这是 tracking 结构重组，不是声明旧验收项已经自然完成。
  - 已新增 `docs/authoritative-baseline/dp5_rc20_contract_zh.md`，定义 runtime frame baseline、degraded health、prompt freeze、overflow compaction、hook install failure、legacy shim authority、config/prompt/menu coverage、glyph/icon contract、release blocker 与 non-goals。
  - 已从 `docs/authoritative-baseline/README.md`、`docs/authoritative-baseline/work-packages/README.md` 与 `docs/DOC_INDEX_zh.md` 接入 DP5-RC20 contract。
  - `.dualpad-builder/feature_list.json` 中 `DP5` 继续保持 `planned` / `passes=false`，因为 PH8b governance lint 要求 DP5 不升级为 active runtime work；DP5-RC20 作为 issue / contract 结构记录。
  - `.dualpad-builder/sprint_plan.json` 中 `S-DP5` 继续保持 `planned`，`current_sprint` 仍为 `null`，因为 U0 已完成且当前没有正在执行的 implementation slice。
- 本轮边界：
  - 不新增 runtime phase。
  - 不重开 `input_v2` runtime mainline。
  - 不改 canonical target 名称、replay root 或旧 SWF 返回 shape。
  - 不恢复 `FavoritesMenu` workspace、旧 SWF authority、`BindingManager` 或 trigger reverse lookup authority。
  - visual icon artwork production 与 DualSense haptics / vibration 都不纳入本 milestone，除非后续单独 promotion。
- 本轮只改治理文档与 builder memory，未改 runtime 代码。
- 验证结果：
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0，输出 `reviewed docs consistency check passed`。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 generated docs 与 reviewed-doc consistency 检查。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1530 nodes, 3097 edges, 141 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-04-18 21:34:08 CST

- 已初始化 `WF0` workflow bootstrap，准备把 `DualPad` 收口到 `harness + ce + graphify` 默认工作流。
- 本轮新增：
  - `AGENTS.md`
  - `AGENTS.win.md`
  - `AGENTS.mac.md`
  - `docs/harness/dualpad-builder.md`
  - `docs/authoritative-baseline/`
  - `.dualpad-builder/`
  - `.graphifyignore`
  - `.githooks/`
  - `scripts/dev/setup_graphify_local.py`
- 本轮同步更新：
  - `README.md`
  - `docs/DOC_INDEX_zh.md`
  - `.gitignore`
- 当前 `.dualpad-builder/` 采用 conservative seeding：
  - 只把 `WF0` 标为 `completed`
  - `DP1-DP5` 先按 existing repo reality 收口成 `in_progress / active / planned`
  - 不 retroactively 把旧工作直接记成 `passes=true`
- 本轮计划验证：
  - `python3 scripts/dev/setup_graphify_local.py`
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
  - `python3 -m graphify query "show the main flow"`

## 2026-04-18 21:44:12 CST verification

- 已执行验证：
  - `python3 scripts/dev/setup_graphify_local.py`
    - 结果：已注册 `.codex/hooks.json` 的 `PreToolUse` hook，并把 `core.hooksPath` 切到 `.githooks`
    - 结果：首轮代码图已生成到 `graphify-out/graph.json`
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
    - 结果：代码图重建成功
  - `python3 -m graphify query "show the main flow"`
    - 结果：返回的主链节点已覆盖 `PadEventSnapshotProcessor`、`NativeButtonCommitBackend`、`PollCommitCoordinator`、`PadEventSnapshotDispatcher` 等当前 repo 主线
- 当前 graphify 输出：
  - `970` nodes
  - `1769` edges
  - `99` communities
- 当前本地状态确认：
  - `git config --local core.hooksPath` => `.githooks`
  - `graphify-out/` 已存在：
    - `.graphify_python`
    - `graph.json`
    - `GRAPH_REPORT.md`

## 2026-04-18 22:03:00 CST

- 本轮整理了当前工作路入口文档，目标是把“先看什么、当前推进到哪、每条线怎么回跳”收口到现有入口层，而不是新增并行说明文档。
- 本轮更新：
  - `docs/authoritative-baseline/README.md`
  - `docs/authoritative-baseline/work-packages/README.md`
  - `docs/DOC_INDEX_zh.md`
- 当前口径：
  - 活跃 Sprint 仍是 `S-DP4`
  - `WF0` 仍为 `completed`
  - `DP1-DP3` 仍为 `in_progress`
  - `DP4` 仍为 `active`
  - `DP5` 仍为 `planned`
- 本轮未改代码文件，因此没有触发 graphify rebuild，也没有新增验证通过声明。

## 2026-04-18 23:02:00 CST

- 本轮把外部 `dualpad_rearchitecture_plan_zh.md` 导入 repo，并在 `docs/plans/dualpad_rearchitecture/` 下收口成可顺序执行的计划包。
- 本轮新增：
  - `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`
  - `docs/plans/dualpad_rearchitecture/README_zh.md`
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
  - `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
  - `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
  - `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
  - `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`
  - `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md`
- 本轮同步更新：
  - `docs/DOC_INDEX_zh.md`
- 这套计划包当前已经完成：
  - 原始总计划保留
  - `Phase 0-8` 全部切成独立 slice
  - 每个 slice 都固定了目标、设计决定、代码落点、实施顺序、验证入口、退出条件和交接合同
- 本轮只有文档改动，没有运行构建或测试，也没有变更 `.dualpad-builder/feature_list.json` / `sprint_plan.json` 的状态口径。

## 2026-04-18 23:46:00 CST

- 本轮根据外部敌意审查意见对 `docs/plans/dualpad_rearchitecture/` 做了按主题修订，并使用 subagent 分工修改、交叉审核和 final pass 收口。
- 本轮新增：
  - `docs/plans/dualpad_rearchitecture/09a_slice_phase8_runtime_closeout_zh.md`
  - `docs/plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md`
- 本轮重写或显著收口：
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
  - `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
  - `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
  - `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
  - `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`
  - `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md`
  - `docs/plans/dualpad_rearchitecture/README_zh.md`
  - `docs/DOC_INDEX_zh.md`
- 本轮收口的重点合同：
  - `UiContextId / canonicalContextName / legacyInputContext`
  - `Base Set + Layer Stack + scopeAnchorIds`
  - `Layer / Combo` 非顺序语义
  - `FactFrame / KernelFrame`
  - `menuStackRevision / deviceFamilyRevision`
  - `PublishedGameplayPresentation`
  - `PromptSnapshotRecord`
  - replay root、DocGen provenance、Phase 8 二拆
- 本轮执行了 subagent 交叉审核和 final pass，最终没有剩余高信号 cross-slice finding。
- 本轮仍只有文档改动，没有运行构建或测试，也没有触发 graphify rebuild。

## 2026-04-19 09:20:38 CST

- 本轮针对上一轮文档审查后仍成立/部分成立的 8 条合同问题，继续在 `docs/plans/dualpad_rearchitecture/` 做了一次文档返工，并明确按 `worker -> reviewer -> final pass` 的 subagent 流程收口。
- 本轮重写或补充的文档：
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
  - `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
  - `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
  - `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`
  - `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md`
  - `docs/plans/dualpad_rearchitecture/09a_slice_phase8_runtime_closeout_zh.md`
  - `docs/plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md`
- 本轮补齐/收口的重点合同：
  - `presentationPolicyId` 已正式穿过 `Phase 2 -> Phase 3`，owner 固定为 `ContextResolver`
  - `Phase 2 Shadow Compare Exit Gate` 已新增字段级 parity gate，不再只有场景和白名单
  - `FactFrame / KernelFrame` 内层 schema 已冻结到字段、哨兵值、`kernelRevision` 参与规则
  - `ManifestEpochChanged / DeviceFamilyChanged` 的 marker authoritative 来源、配对规则、fail-closed 行为已写死
  - `manifestEpoch` 已从普通 boundary 中剥离，固定触发 `HardReset` 并建立新 clean baseline
  - `PromptSnapshotRecord` 已扩成 11 字段，`status` 进入 black-box / snapshot / CI；成功态统一按 `status == Ok` 推导
  - `DeviceFamilyMismatch`、`HiddenOnly`、`NoVisibleBinding`、empty-scope / no-scope context 的 fail-closed 优先级已写成正式判定表
  - `GameplayProjectionFrame.context` 已迁到 `LegacyInputContextCompat`，不再依赖将被删除的 `InputContext.*` owning files
  - `Phase 8` canonical target 矩阵已统一到 `DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests`
  - `DualPadReplayHarnessTests` 已明确只属于 Phase 0 bootstrap，进入 Phase 8 前必须并入 `DualPadReplayTests`
- 本轮 subagent 交叉审核结论：
  - 一次 cross-slice review 抓到 `08_slice_phase7_ingress_and_resync_zh.md` 仍残留一条旧的 `manifest reload -> 不触发 gameplay recovery` 口径
  - 已在本地 final pass 中修掉该冲突，并再次用 reviewer 复核 `manifest reload / ManifestEpochChanged / boundary / recovery` 语义
  - 最终 reviewer 结论为：无剩余高置信 finding
- 本轮仍只有文档改动：
  - 没有改代码文件，因此没有运行构建或测试
  - 没有触发 `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`，因为本轮不涉及代码文件

## 2026-04-19 10:05:00 CST

- 本轮只处理 `dualpad_rearchitecture` 与 `.dualpad-builder` 的 authority 边界澄清，不重写 builder memory 的 Sprint/feature 状态机。
- 本轮更新：
  - `docs/plans/dualpad_rearchitecture/README_zh.md`
  - `docs/authoritative-baseline/README.md`
  - `docs/authoritative-baseline/work-packages/README.md`
- 本轮新增澄清口径：
  - `docs/plans/dualpad_rearchitecture/` 当前是计划包 / 设计输入，不是默认 workflow current truth
  - 只有在 slice 或路线同步进入 `.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json` 或被明确 promotion 后，才可升格为当前执行 authority
  - 在 promotion 发生前，默认 workflow 仍只认 `docs/authoritative-baseline/` 与 `.dualpad-builder/` 的 current state，不能把 `dualpad_rearchitecture` 越级当成当前主线
- 本轮仍未变更 `.dualpad-builder/feature_list.json` / `sprint_plan.json` 的状态口径，因为当前活跃 Sprint 仍是 `S-DP4`
- 本轮只有文档改动，没有运行构建或测试，也没有触发 graphify rebuild

## 2026-04-19 10:05:00 CST

- 本轮继续根据敌意审查结果做文档返工，目标是收口 3 类剩余问题：
  - `presentationPolicyId` 虽已进入 Phase 2/3 发布合同，但仍未进入 Phase 2 pre-cutover gate
  - `09a / 09b` 对 canonical prove-out targets 的职责边界仍容易被读成“由 09b 首次补建”
  - `dualpad_rearchitecture` 计划包仍被部分文本读成默认执行主线，和 `.dualpad-builder` current-state authority 冲突
- 本轮收口结果：
  - `03_slice_phase2_menu_instance_truth_zh.md`
    - `presentationPolicyId` 已进入 `Shadow Compare Exit Gate`
    - 其 diff 语义已冻结为 `publish parity field`
  - `09_slice_phase8_cutover_cleanup_and_ci_zh.md`
  - `09a_slice_phase8_runtime_closeout_zh.md`
  - `09b_slice_phase8_governance_closeout_zh.md`
    - 已统一写成：`09a` 先落 `xmake.lua` 中的 6 个 canonical prove-out targets 与必要 prove-out 入口，再执行 prove-out；`09b` 只复用同名 targets 接默认 CI / governance
  - `docs/plans/dualpad_rearchitecture/README_zh.md`
  - `docs/authoritative-baseline/README.md`
  - `docs/authoritative-baseline/work-packages/README.md`
    - 已明确把 `docs/plans/dualpad_rearchitecture/` 压回计划包 / 设计输入；只有同步进 `.dualpad-builder` 并留下 promotion 痕迹后，才可升格为当前执行 authority
- 本轮还重建了敌意审查 bundle：
  - 补入 `docs/authoritative-baseline/2026-04-18-dualpad-authoritative-baseline.md`
  - 补入 `docs/plans/dualpad_rearchitecture/` 显式引用、且 repo 当前真实存在的前置文档
  - 用全新 staging 目录重建，避免旧 bundle 因重复复制出现伪造的 `src/input/input/` 镜像树
  - 在 bundle 的 `README_zh.md` 里显式列出仍然不存在、但计划文档频繁提及的路径，避免下轮把 repo reality 缺口误判成 bundle 漏打
- 本轮交叉审核：
  - reviewer 先抓到 `09a_slice_phase8_runtime_closeout_zh.md` 仍残留一条“6 个 canonical targets 已在当前 repo reality 落地”的假前提
  - 已在本地 final pass 中改为“必须在 `09a` prove-out 前落地，否则不得把缺口推给 `09b`”
  - 复核后，`presentationPolicyId` pre-cutover gate、`09a/09b` target 职责、以及 plan authority 边界都已无剩余高置信 finding
- 本轮仍只有文档与审查包改动：
  - 没有运行构建或测试
  - 没有触发 `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`

## 2026-04-19 10:40:00 CST

- 本轮针对敌意审查里仍然成立的 repo-level finding 做了口径返工，目标是不再让 `Phase 0 / Phase 8` 的未来 gate 被误读成“当前 repo reality 已存在”。
- 本轮更新：
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md`
  - `docs/plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md`
  - `build/review_bundles/2026-04-19-adversarial-review/dualpad_adversarial_review_bundle/README_zh.md`
- 本轮新增固定口径：
  - `src/input_v2/`、`scripts/ci/`、`scripts/dev/dualpad_trace_diff.py` 与 canonical prove-out / CI targets 在实现前属于显式缺口清单，不是 bundle 漏打
  - 这些入口只有在对应 slice 被宣称 done / promoted / prove-out 通过后，缺失才构成正式 breach
  - 审查包 README 现在显式列出本轮审核点名文件清单，避免再把 authority 文档、config 或 assets 漏判成未随包提供
- 本轮随后需要重建 adversarial review bundle 与 zip，使包内说明与最新 slice 文本一致
- 本轮只有文档与打包说明改动，没有运行构建或测试，也没有触发 graphify rebuild

## 2026-04-19 11:20:00 CST

- 本轮根据最新敌意审查继续返工，目标是收口 5 类剩余问题：
  - `Gameplay presentation owner` 仍保留可执行的 runtime 第二路由
  - `dualpad_rearchitecture_plan_zh.md` 仍被放进首读路径，容易把历史总纲重新读成 slice authority
  - 审查包 ZIP 仍使用 Windows `\` entry 名，POSIX 常规解包会打坏目录树
  - 缺口清单漏掉 `DualPadManifestCompilerTests` / `DualPadContextResolverTests`
  - bundle 漏掉 `AGENTS.mac.md`、`docs/DOC_INDEX_zh.md` 当前入口点名的方案文档 / review 文档，以及 `lib/commonlibsse-ng/`
- 本轮代码与配置收口：
  - `src/input/RuntimeConfig.h`
  - `src/input/RuntimeConfig.cpp`
  - `src/input/InputModalityTracker.cpp`
  - `src/input/injection/PadEventSnapshotProcessor.cpp`
  - `src/input/backend/NativeButtonCommitBackend.cpp`
  - `config/DualPadDebug.ini`
  - 已移除 `enable_gameplay_ownership` 对运行时行为的控制分支；旧 key 若仍出现在配置里只会记录“retired and ignored”告警，不再形成第二路由
  - gameplay `IsUsingGamepad()`、frame planning ownership gate、gameplay digital transient suppression 现在都固定走单一路径
- 本轮计划文档与 honesty 边界收口：
  - `docs/plans/dualpad_rearchitecture/README_zh.md`
  - `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`
  - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
  - `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
  - 已把 master plan 压回历史导入 / 背景材料，不再放在默认首读路径；若与 slice 冻结合同冲突，以拆分后的 slice 文档为准
  - 已为 Phase 1 / Phase 2 补上当前 repo reality 缺口与 breach 边界，显式列出 `DualPadManifestCompilerTests` / `DualPadContextResolverTests`
- 本轮重建了 adversarial review bundle：
  - 新增 `AGENTS.mac.md`
  - 新增 `docs/ui_input_ownership_arbitration_plan_zh.md`
  - 新增 `docs/sprint_native_source_mediation_plan_zh.md`
  - 新增 `docs/reviews/` 全目录
  - 新增 `lib/commonlibsse-ng/`
  - `README_zh.md` 已补“本轮审核点名文件清单”与完整缺口清单
  - 最终 ZIP 改为标准 `/` entry 名，避免 POSIX 常规解包把 repo-relative 目录树打坏
- 本轮验证：
  - `xmake build -j 1 DualPad` 通过
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` 通过
  - 仍未运行专门测试 target，因为当前 repo 还不存在 `DualPadManifestCompilerTests`、`DualPadContextResolverTests`、`DualPadReplayTests` 等计划中的未来入口

## 2026-04-19 11:45:00 CST

- 本轮继续根据新一轮敌意审查返工共享文档与审查包 honesty，重点收口：
  - 历史评审文档不再以当前必要交叉核对身份参与 Phase 2 收口判断
  - 共享文档中出现的机器私有绝对路径
  - 当前 upstream rollback / stale-poll assist 与 DP4 glyph reverse lookup 的隐性 authority 风险
- 本轮更新：
  - `docs/reviews/2026-04-10-owner-arbitration-gameplay-injection-review_zh.md`
  - `docs/phase1_phase4_code_review_findings_zh.md`
  - `docs/verification/menu_context_runtime_policy_matrix_zh.md`
  - `docs/sprint_native_source_mediation_plan_zh.md`
  - `docs/current_input_pipeline_zh.md`
  - `docs/main_menu_glyph_current_status_zh.md`
  - `docs/authoritative-baseline/work-packages/README.md`
- 本轮新增口径：
  - `phase1_phase4_code_review_findings_zh.md` 明确压回历史审阅快照，不再单独作为当前收口依据
  - owner review 中对该文档的引用只保留为历史背景材料
  - 共享验证文档不再硬编码本机日志绝对路径；本机路径只回到 `AGENTS.win.md` / `AGENTS.mac.md`
  - sprint mediation 计划中的源码 / CommonLib 引用已改回 repo-relative
  - 当前输入主链文档已显式声明 `use_upstream_gamepad_hook=false` rollback gate 与 stale-poll assist drain 是当前偏差，不是已经完全收口的单一路由
  - 当前 DP4 glyph 文档与 work-package 入口已显式声明 action -> trigger reverse lookup 只是过渡兼容实现，不是稳定 authority
- 本轮随后需要重建 adversarial review bundle 与 zip，使新增 honesty 说明与 repo 最新文本一致
- 本轮只有共享文档与审查包改动，没有新增代码构建；沿用上一轮 `xmake build -j 1 DualPad` 与 graphify rebuild 的验证结果


## 2026-04-19 22:42:58 CST

- 本轮落地了输入主链的最小 route-health contract slice，目标是不改当前 owner 语义，只把过渡兼容分叉收成可观测、可测试的正式合同。
- 本轮代码与配置更新：
  - `src/input/injection/RouteHealthContract.h`
  - `src/input/injection/RouteHealthContract.cpp`
  - `src/input/injection/UpstreamGamepadHook.h`
  - `src/input/injection/UpstreamGamepadHook.cpp`
  - `src/input/InputFramePump.cpp`
  - `src/input/injection/PadEventSnapshotDispatcher.h`
  - `src/input/injection/PadEventSnapshotDispatcher.cpp`
  - `src/input/RuntimeConfig.h`
  - `src/input/RuntimeConfig.cpp`
  - `config/DualPadDebug.ini`
  - `tests/RouteHealthContractTests.cpp`
  - `xmake.lua`
- 本轮冻结的当前合同：
  - `route_state` 只允许 `active_fresh` / `active_stale` / `disabled`
  - `drain_reason` 只允许 `upstream_poll` / `frame_pump_assist_stale` / `task_fallback_high_water` / `frame_pump_disabled`
  - `UpstreamGamepadHook` 现在可直接导出 `last_poll_age_ms`
  - `PadEventSnapshotDispatcher::DrainOnMainThread(...)` 现在能按调用路径记录 route-health telemetry；`log_route_health=true` 时输出 `route_state`、`drain_reason`、`last_poll_age_ms`、`hook_installed`、`budget`、`drained`、`pending_before`、`pending_after`
- 本轮验证：
  - `xmake build -j 1 DualPadRouteHealthContractTests`
  - `xmake run DualPadRouteHealthContractTests`
  - `xmake build -j 1 DualPad`
  - `git diff --check`
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- 本轮文档同步：
  - `docs/current_input_pipeline_zh.md` 已写回当前 `route_state / drain_reason` 合同与 `log_route_health` 诊断口径
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md` 已把 `dispatcher_schedule.csv` 的未来 schema 收紧到当前冻结的 route-health 标签
- 本轮刻意未做：
  - 没有删除 `use_upstream_gamepad_hook=false`
  - 没有删除 stale assist drain
  - 没有引入 `DrainCaller` 之类的新 owner 抽象
  - 没有直接跳到 Phase 1 compiler 或 Phase 6 prompt service


## 2026-04-19 23:29:04 CST

- 本轮只做 plan 定稿，不继续实现。
- 已把当前推荐正式写回 builder memory 与工作包入口：
  - `DP1a Route-health contract freeze`
  - `DP4a Glyph compat diagnostics freeze`
- 已冻结当前执行顺序：
  - `DP1a -> DP4a -> Phase 0 -> Phase 1 -> Phase 2 -> Phase 3 -> Phase 4 -> Phase 5 -> Phase 6`
- 已写死两条关键边界：
  - `route_state` 继续固定为 `active_fresh / active_stale / disabled`，当前 secondary diagnostics 继续固定为 `drain_reason / last_poll_age_ms / hook_installed`
  - `status / fallback / ambiguity` 在 `Phase 6` 前只进入内部诊断面，不扩展旧 SWF / delegate 返回 shape
- 本轮更新：
  - `docs/authoritative-baseline/work-packages/README.md`
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
- 本轮只有 plan / builder memory 改动，没有继续推进实现代码，也没有运行构建或测试。

## 2026-04-20 00:22:00 CST

- 本轮只修正 builder memory / 摘要术语 / 共享文档私有路径残留，不改代码实现。
- 已把 `DP4a Glyph compat diagnostics freeze` 从纯 `planned` 同步为 `in_progress`：
  - 当前 worktree 与 current docs 已经出现 compat diagnostics 相关实现与说明
  - 但它仍不是当前活跃 Sprint，也没有被写成已完成
- 已清掉把 `disabled_reason` 误写成当前正式 secondary diagnostics 的摘要口径：
  - 当前正式口径继续以 `drain_reason / last_poll_age_ms / hook_installed` 为准
- 已把共享文档中的机器私有绝对路径移回机器入口说明引用，避免继续在 shared current docs 中固化本机部署路径。
- 本轮更新：
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
  - `.dualpad-builder/progress.md`
  - `docs/controlmap_combo_profile_zh.md`
  - `build/review_bundles/2026-04-19-deep-adversarial-review/dualpad_deep_adversarial_review_bundle/README_zh.md`
- 本轮只做计划与摘要修正，没有运行构建或测试。

## 2026-04-20 10:20:00 CST

- 本轮继续做 plan / builder memory 收口，不改实现代码。
- 已修正 5 个仍不该留到实现阶段决定的计划缺口：
  - `AGENTS.md` 的 glyph 快速路由不再绕过 `DP1a -> DP4a` gate
  - `DP1a` gate 现在把 `hook_installed` 明确写进 acceptance / exit / verification
  - `DP4a` 的旧 glyph 外层 shape 现在明确区分：
    - `DualPad_GetActionGlyphToken` 返回单个 token string
    - `DualPad_GetActionGlyph` descriptor 保持 `ok / buttonArtToken / semanticId / contextName`
  - `Phase 0` 前置依赖不再把 `dualpad_rearchitecture_plan_zh.md` 当 current truth
  - `Phase 0` 对当前 repo 测试面的描述已更新到当前 snapshot
- 本轮更新：
  - `AGENTS.md`
  - `docs/authoritative-baseline/README.md`
  - `docs/authoritative-baseline/work-packages/README.md`
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
  - `docs/main_menu_glyph_current_status_zh.md`
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `.dualpad-builder/progress.md`
- 本轮只有计划与摘要修正，没有运行构建或测试。


## 2026-04-21 00:00:00 CST

- 本轮根据最新审核结论继续修正 authority 文本，不改实现代码。
- 本轮收口 2 类问题：
  - `DP1a` / `DP4a` 的 prove-out 不再只写成 `focused validation`，已固定回当前 repo 已存在的可执行命令。
  - future slice 的执行前置不再把总纲、历史 review、brainstorm / ideation、调查稿重新拉回 current authority。
- 本轮更新：
  - `.dualpad-builder/sprint_plan.json`
  - `docs/harness/dualpad-builder.md`
  - `docs/authoritative-baseline/work-packages/README.md`
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
  - `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
  - `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
  - `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
  - `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
  - `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`
  - `.dualpad-builder/progress.md`
- 本轮验证计划：
  - `git diff --check -- .dualpad-builder/sprint_plan.json docs/harness/dualpad-builder.md docs/authoritative-baseline/work-packages/README.md docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md .dualpad-builder/progress.md`
  - `Get-Content .dualpad-builder/sprint_plan.json | ConvertFrom-Json | Out-Null`
  - `rg -n "DualPadRouteHealthContractTests|DualPadGlyphResolutionCompatTests|dualpad_rearchitecture_plan_zh|brainstorms/|ideation/|phase1_phase4_code_review_findings_zh|gameplay_input_ownership_investigation_and_plan_zh" docs/authoritative-baseline/work-packages/README.md docs/harness/dualpad-builder.md .dualpad-builder/sprint_plan.json docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`


## 2026-04-21 00:49:34 CST verification

- 已执行验证：
  - `git diff --check -- .dualpad-builder/sprint_plan.json docs/harness/dualpad-builder.md docs/authoritative-baseline/work-packages/README.md docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md .dualpad-builder/progress.md`
    - 结果：无输出，未引入 diff whitespace / patch 格式问题。
  - `Get-Content .dualpad-builder/sprint_plan.json | ConvertFrom-Json | Out-Null`
    - 结果：解析成功，`sprint_plan.json OK`。
  - `rg -n "DualPadRouteHealthContractTests|DualPadGlyphResolutionCompatTests|dualpad_rearchitecture_plan_zh|brainstorms/|ideation/|phase1_phase4_code_review_findings_zh|gameplay_input_ownership_investigation_and_plan_zh" ...`
    - 结果：`DualPadRouteHealthContractTests` / `DualPadGlyphResolutionCompatTests` 已出现在 `sprint_plan.json`、`dualpad-builder.md`、`work-packages/README.md` 的 fixed prove-out 位置。
    - 结果：剩余 `dualpad_rearchitecture_plan_zh.md`、review、brainstorm、ideation、调查稿引用只保留在显式“如需背景材料 / 历史背景”说明中，不再挂在当前执行输入或 prove-out 入口下。


## 2026-05-17 21:03:29 CST

- 本轮根据外部 GPT Pro 实现前设计审查的 No-Go 结论修复 4 类 blocking issue。
- 审查结论中确认成立并已处理的点：
  - `Phase 0-09b` 不能只停留在 `docs/plans/dualpad_rearchitecture/` 设计包里；本轮已把 `PH0` - `PH8B` 作为 planned backlog 登记进 `.dualpad-builder/feature_list.json` 与 `.dualpad-builder/sprint_plan.json`，但没有把它们误标成 active / completed。
  - `Phase 0` 的 `06_favorites_page_lr_accept_cancel` 不再作为默认 mandatory repo-owned glyph capture；它现在是条件场景，只有恢复 `FavoritesMenu` workspace、页面源码和 artifact inventory 后才进入退出条件。
  - `Phase 7` 的 `manifestEpoch / deviceFamilyRevision` boundary authority 已归一到正文：二者唯一 authoritative 来源都是 ingress marker payload；`SourceEvidence` 只做配对、镜像和一致性校验，不再作为独立 boundary source。
  - `xmake.lua` 不再默认写入本机 `G:` 路径；默认 build 输出到 repo-local `build/bin/...`，只有显式设置 `dualpad_deploy=true` 与本机路径时才部署到 Skyrim / MO2。
- 本轮同步更新：
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
  - `docs/authoritative-baseline/README.md`
  - `docs/authoritative-baseline/work-packages/README.md`
  - `docs/harness/dualpad-builder.md`
  - `docs/plans/dualpad_rearchitecture/README_zh.md`
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`
  - `AGENTS.win.md`
  - `xmake.lua`
- 本轮刻意未做：
  - 没有把 `PH0` - `PH8B` 晋升为当前 active Sprint；当前 active 仍是 `S-DP1a`。
  - 没有恢复 `FavoritesMenu` workspace；因此 Favorites glyph capture 仍不是 Phase 0 默认退出条件。
  - 没有声称 Phase 0-09b 已实现或验证通过。


## 2026-05-17 21:36:22 CST

- 本轮根据外部 GPT Pro 复审的 `Conditional Go` 结论做低风险补强，并执行 `DP1a` close-out。
- 本轮补强：
  - 新增 `docs/plans/dualpad_rearchitecture/phase0_scenarios.json`，用 `mandatory=false` / `conditional_live` 机器可读地标记 `FavoritesMenu` 条件场景。
  - `Phase 1` 明确 `AtomicConfigReloader::Promote()` 只负责 active bundle / epoch / LKG；`ActionManifestPublisher::PublishPromotedBundle(...)` 是唯一 `ManifestEpochChanged` producer seam。
  - `Phase 3` exit gate 明确 `DeviceFamilyChangedPayload` 与紧随其后的 `SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision` 必须一致。
  - `Phase 4` 明确 `CompiledActionGraphPublisher::Publish(...)` 是 compiled graph publication / hot-swap 的唯一入口。
  - `Phase 6` 把 repo-owned `Interface/startmenu.swf` smoke 固定为退出验证项，不再写成“必要时”。
  - `Phase 7` 清理最终合同汇总里的 Markdown / 编号残留，不改语义。
- `DP1a` close-out 状态变更：
  - `.dualpad-builder/feature_list.json`：`DP1a` -> `completed` / `passes=true`；`DP4a` -> `active`
  - `.dualpad-builder/sprint_plan.json`：`current_sprint` -> `S-DP4a`；`S-DP1a` -> `completed`；`S-DP4a` -> `active`
  - `docs/authoritative-baseline/README.md` 与 `docs/authoritative-baseline/work-packages/README.md` 已同步当前活跃 Sprint。
- 本轮已执行验证：
  - `git diff --check`
    - 结果：无 diff whitespace 错误；仅有 Windows CRLF 工作区提示。
  - `Get-Content .dualpad-builder/sprint_plan.json | ConvertFrom-Json | Out-Null`
    - 结果：解析成功。
  - `Get-Content .dualpad-builder/feature_list.json | ConvertFrom-Json | Out-Null`
    - 结果：解析成功。
  - `Get-Content docs/plans/dualpad_rearchitecture/phase0_scenarios.json | ConvertFrom-Json | Out-Null`
    - 结果：解析成功。
  - `xmake build -j 1 DualPadRouteHealthContractTests`
    - 结果：build ok。
  - `xmake run DualPadRouteHealthContractTests`
    - 结果：exit 0。
  - `xmake build -j 1 DualPad`
    - 结果：build ok；默认输出为 repo-local `build/bin/DualPad/DualPad.dll`。
  - `xmake build -j 1 DualPadGlyphResolutionCompatTests`
    - 结果：build ok。
  - `xmake run DualPadGlyphResolutionCompatTests`
    - 结果：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
    - 结果：graphify rebuild 成功，生成 1005 nodes / 1856 edges / 102 communities。
- 本轮刻意未做：
  - 没有把 `DP4a` 标成 completed；它只是当前 active Sprint。
  - 没有晋升 `PH0`；Phase 0 仍是 planned backlog。
  - 没有做真正 clean clone 验证；只验证了当前 checkout 的默认 xmake repo-local 输出。

## 2026-05-17 22:16:06 CST

- 本轮按用户要求开始 sequential close-out，只推进到 `PH0`，不启动 `PH1`。
- `DP1a` start：
  - 目标：复跑 `DP1a Route-health contract freeze` 的固定 prove-out，确认当前 checkout 仍满足 `route_state / drain_reason / last_poll_age_ms / hook_installed` 合同。
  - 计划执行：
    - `xmake build DualPadRouteHealthContractTests`
    - `xmake run DualPadRouteHealthContractTests`
    - `xmake build DualPad`

## 2026-05-17 22:18:00 CST

- `DP1a` done：
  - `xmake build DualPadRouteHealthContractTests`
    - 结果：build ok，exit 0。
  - `xmake run DualPadRouteHealthContractTests`
    - 结果：exit 0。
  - `xmake build DualPad`
    - 结果：build ok，输出 `build/bin/DualPad/DualPad.dll`。
  - 结论：`DP1a` 固定 prove-out 通过；本轮未改变 `DP1a` 代码或合同。

## 2026-05-17 22:18:00 CST

- `DP4a` start：
  - 目标：关闭 `Glyph compat diagnostics freeze`，保持旧 `DualPad_GetActionGlyphToken` 单 token string 与旧 `DualPad_GetActionGlyph` descriptor shape 不变。
  - 计划执行：
    - `xmake build DualPadGlyphResolutionCompatTests`
    - `xmake run DualPadGlyphResolutionCompatTests`
    - `xmake build DualPad`

## 2026-05-17 22:20:00 CST

- `DP4a` done：
  - `xmake build DualPadGlyphResolutionCompatTests`
    - 结果：build ok，exit 0。
  - `xmake run DualPadGlyphResolutionCompatTests`
    - 结果：exit 0。
  - `xmake build DualPad`
    - 结果：build ok，输出 `build/bin/DualPad/DualPad.dll`。
  - 状态同步：`.dualpad-builder/feature_list.json` 中 `DP4a` 已更新为 `completed` / `passes=true`；`.dualpad-builder/sprint_plan.json` 中 `S-DP4a` 已更新为 `completed`。
  - 边界确认：未改变旧 `DualPad_GetActionGlyphToken` 单 token string 返回；未改变旧 `DualPad_GetActionGlyph` 的 `ok / buttonArtToken / semanticId / contextName` descriptor shape；未实现 `PromptService`。

## 2026-05-17 22:20:00 CST

- `PH0` promotion / start：
  - 前置 `DP1a` 与 `DP4a` 的固定验证均已通过，因此本轮把 `PH0` 从 `planned` 晋升为当前执行 slice。
  - 状态同步：`.dualpad-builder/feature_list.json` 中 `PH0` 已更新为 `active` / `passes=false`；`.dualpad-builder/sprint_plan.json` 中 `current_sprint` 已更新为 `S-PH0`，`S-PH0` 已更新为 `active`。
  - 目标：实现 Phase 0 replay barrier 的 schema、golden bundle、replay harness target、harness tests 与 batch diff runner。
  - 硬边界：
    - 不启动 `PH1`。
    - 不实现 `PromptService`。
    - 不改变旧 SWF 返回 shape。
    - 不把 `FavoritesMenu` 设为 mandatory；除非先恢复 workspace/source/artifact inventory 并记录。

## 2026-05-17 22:55:00 CST

- `PH0` done：
  - 本轮新增/更新的 PH0 入口：
    - `src/input_v2/telemetry/TraceSchema.*`
    - `src/input_v2/telemetry/InputTraceRecorder.*`
    - `src/input_v2/telemetry/ReplayHarness.*`
    - `src/input_v2/telemetry/ReplayHarnessMain.cpp`
    - `tests/ReplayHarnessTests.cpp`
    - `tests/replay/golden/phase0/`
    - `scripts/dev/dualpad_trace_diff.py`
    - `xmake.lua` 中 `DualPadReplayHarness` / `DualPadReplayHarnessTests`
  - `config/DualPadDebug.ini` 与 `RuntimeConfig` 已新增 `[Replay]` 段：
    - `enable_trace_recording=false`
    - `trace_output_dir=build/replay-captures`
    - `trace_session=default`
    - `trace_record_glyph_queries=true`
  - `phase0_scenarios.json` 中 `06_favorites_page_lr_accept_cancel` 仍为 `mandatory=false` / `conditional_live`；本轮未恢复 `FavoritesMenu` workspace、页面源码或 artifact inventory，因此没有把它设为默认退出条件。
  - 本轮刻意未做：
    - 未启动 `PH1`。
    - 未实现 `PromptService`。
    - 未改变旧 `DualPad_GetActionGlyphToken` / `DualPad_GetActionGlyph` 返回 shape。
- PH0 必跑验证结果：
  - `xmake build DualPad`
    - 结果：build ok，输出 `build/bin/DualPad/DualPad.dll`。
  - `xmake build DualPadDInput8Proxy`
    - 结果：build ok，输出 `build/bin/DualPadDInput8Proxy/dinput8.dll`。
  - `xmake build DualPadReplayHarness`
    - 结果：build ok，exit 0。
  - `xmake build DualPadReplayHarnessTests`
    - 结果：build ok，exit 0。
  - `xmake run DualPadReplayHarnessTests`
    - 结果：exit 0。
    - 调试记录：首次运行失败的根因是 `xmake run` 从 target 输出目录启动，测试使用 repo-relative 路径；已改为测试侧向上定位包含 `xmake.lua` 的 repo root 后复跑通过。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`
    - 结果：exit 0；10 个 mandatory scenario 均输出 `no diff`。
      - `01_gameplay_walk_attack_block_sprint`
      - `02_gameplay_menu_roundtrip`
      - `03_main_menu_glyph`
      - `04_journal_confirm_cancel`
      - `05_map_cursor_zoom_open_journal`
      - `07_book_page_lr`
      - `08_console_creations_lockpicking`
      - `09_combo_native_pause_screenshot_hotkeys`
      - `10_backlog_gap_overflow`
      - `11_config_reload_success_failure`
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
    - 结果：exit 0；graphify rebuild 成功，生成 `1076` nodes / `2081` edges / `105` communities。
- 状态同步：
  - `.dualpad-builder/feature_list.json` 中 `PH0` 已更新为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json` 中 `S-PH0` 已更新为 `completed`；`S-PH1` 仍保持 `planned`。
  - `docs/authoritative-baseline/README.md`、`docs/authoritative-baseline/work-packages/README.md`、`docs/DOC_INDEX_zh.md` 与本 Phase 0 slice 文档已同步为 closed through `PH0`。

## 2026-05-17 23:20:20 CST

- `PH0` proof honesty correction / start：
  - 更正上一条 `PH0 done` 结论：当前只能诚实声明 `PH0` schema / harness bootstrap 已完成。
  - `PH0` behavioral replay barrier 尚未完全证明；此前 `ReplayHarness` 的 dispatcher / processor 路径只做 schema header validation 后 copy golden 到 actual，不能作为 runtime dispatcher / processor / glyph / keyboard behavior proof。
  - 本轮选择较小 builder memory 变更：不拆 `PH0a/PH0b`，而是把 `PH0` / `S-PH0` 回退为 `active`，`passes=false`；`PH1` 保持 `planned` / not started。
  - 计划修正：
    - `ReplayHarness` 增加明确模式边界：`validate-schema`、`materialize-fixture`、`dispatcher`、`processor`。
    - copy-only 行为只允许称为 `materialize-fixture`，不得称为 runtime replay。
    - `dispatcher` / `processor` 在真正行为回放实现前必须 fail，不得 materialize golden files。
    - `10_backlog_gap_overflow` 增加非空 synthetic data row，并让 `DualPadReplayHarnessTests` 在 actual 输出缺少该 row 时失败。
  - 边界确认：
    - 未启动 `PH1`。
    - 未实现 `ContextCatalog` 或 `ActionManifest`。
    - 未改变旧 SWF 返回 shape。
    - 未改变 `PromptService` / `PromptProjection`。
    - `FavoritesMenu` 仍保持 conditional，未恢复 workspace/source/artifact inventory 时不成为 mandatory。

## 2026-05-17 23:36:46 CST

- `PH0` proof honesty correction / validation results：
  - 本轮代码状态：
    - `ReplayHarness` 已区分 `validate-schema`、`materialize-fixture`、`dispatcher`、`processor`。
    - `materialize-fixture` 是唯一 copy-only 路径；`dispatcher` / `processor` 当前返回 not implemented failure，不再复制 golden files 伪装 runtime replay。
    - `10_backlog_gap_overflow` 已增加非空 synthetic rows；`DualPadReplayHarnessTests` 会检查 materialized actual output 保留这些 rows，缺 row 会失败。
  - 状态同步：
    - `.dualpad-builder/feature_list.json` 中 `PH0` 已回退为 `active` / `passes=false`。
    - `.dualpad-builder/sprint_plan.json` 中 `S-PH0` 已回退为 `active`；`S-PH1` 仍为 `planned`。
    - 共享入口文档已明确：`PH0` schema / harness bootstrap 已完成，behavioral replay barrier 尚未完全证明。
  - `xmake build DualPadReplayHarness`
    - 结果：exit 0；输出包含 `linking.release DualPadReplayHarness.exe` 与 `build ok, spent 0.406s`。
  - `xmake build DualPadReplayHarnessTests`
    - 结果：exit 0；输出包含 `linking.release DualPadReplayHarnessTests.exe` 与 `build ok, spent 0.406s`。
  - `xmake run DualPadReplayHarnessTests`
    - 结果：exit 0；stdout 为空。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`
    - 结果：exit 0；输出：
      - `01_gameplay_walk_attack_block_sprint: no diff`
      - `02_gameplay_menu_roundtrip: no diff`
      - `03_main_menu_glyph: no diff`
      - `04_journal_confirm_cancel: no diff`
      - `05_map_cursor_zoom_open_journal: no diff`
      - `07_book_page_lr: no diff`
      - `08_console_creations_lockpicking: no diff`
      - `09_combo_native_pause_screenshot_hotkeys: no diff`
      - `10_backlog_gap_overflow: no diff`
      - `11_config_reload_success_failure: no diff`
    - 口径：该 diff 只证明当前 `materialize-fixture` actual bundle 与 golden 一致；不证明 dispatcher / processor runtime replay behavior。
  - `xmake build DualPad`
    - 结果：exit 0；输出包含 `Built: build/bin/DualPad/DualPad.dll` 与 `build ok, spent 0.828s`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
    - 结果：exit 0；输出包含 `[graphify watch] Rebuilt: 1081 nodes, 2107 edges, 106 communities`，并写入 `graphify-out/graph.json` 与 `graphify-out/GRAPH_REPORT.md`。
  - 结论：
    - 本轮 PH0 bootstrap proof honesty 修正已通过要求的本地验证。
    - `PH0` 不得标记为 `completed` / `passes=true`，直到 behavioral dispatcher / processor replay proof 真正实现并验证通过。
    - `PH1` 保持 `planned` / not started。

## 2026-05-18 00:13:09 CST

- `PH0` status drift doc fix：
  - 按 review verdict 修正 `docs/authoritative-baseline/work-packages/README.md` 的局部自相矛盾：
    - `DP1a` 小节状态恢复为 `已完成`。
    - `PH0` 小节状态改为 `active；已落地 schema / harness bootstrap，behavioral replay barrier 未完全证明`。
    - `PH0` prove-out 段补充：当前 `materialize-fixture` diff 只证明 schema / diff plumbing 与 fixture materialization，不是 dispatcher / processor runtime replay proof。
  - 同步微调 `docs/authoritative-baseline/README.md` 中触发状态 drift grep 的措辞，避免出现可误读的 `PH0 ... 已完成`。
  - 边界确认：
    - 未启动 `PH1`。
    - 未实现 `ContextCatalog` 或 `ActionManifest`。
    - 未改变 `PromptService` / `PromptProjection`。
    - 未改变旧 SWF 返回 shape。
- 验证结果：
  - `git diff --check`
    - 结果：exit 0；stdout 仅包含 Windows 换行提示：
      - `warning: in the working copy of '.dualpad-builder/progress.md', LF will be replaced by CRLF the next time Git touches it`
      - `warning: in the working copy of 'docs/authoritative-baseline/README.md', LF will be replaced by CRLF the next time Git touches it`
      - `warning: in the working copy of 'docs/authoritative-baseline/work-packages/README.md', LF will be replaced by CRLF the next time Git touches it`
  - `Get-Content .dualpad-builder/feature_list.json | ConvertFrom-Json | Out-Null; Write-Output "feature_list json parse ok"`
    - 结果：exit 0；输出 `feature_list json parse ok`。
  - `Get-Content .dualpad-builder/sprint_plan.json | ConvertFrom-Json | Out-Null; Write-Output "sprint_plan json parse ok"`
    - 结果：exit 0；输出 `sprint_plan json parse ok`。
  - `rg "PH0.*已完成|PH0.*completed|passes=true" docs/authoritative-baseline/work-packages/README.md docs/authoritative-baseline/README.md .dualpad-builder/feature_list.json .dualpad-builder/sprint_plan.json`
    - 结果：exit 1；无输出。该命令用来确认没有命中 forbidden status drift，因此 exit 1/no matches 是预期结果。

## 2026-05-18 00:32:49 CST

- `PH0` behavioral replay proof done：
  - 本轮实现范围：
    - `ReplayHarness` 的 `dispatcher` mode 读取 `dispatcher_schedule.csv` 与 ingress snapshot CSV，按 submit/drain 顺序生成 candidate bundle。
    - `ReplayHarness` 的 `processor` mode 读取 `processed_snapshot_frames.csv` / `processed_snapshot_events.csv`，生成 `expected_authoritative_poll.csv` candidate output。
    - `dispatcher` / `processor` mode 不复制 golden files；生成 actual 后会与 golden bundle 比对，behavioral mismatch 会返回 failure 并保留 generated candidate output 供排查。
    - `materialize-fixture` 保持独立 plumbing mode，只用于 schema / diff plumbing，不作为 runtime replay proof。
    - `10_backlog_gap_overflow` 与 `11_config_reload_success_failure` 已作为非空 synthetic behavioral scenarios 覆盖 dispatcher / processor 输出。
  - 边界确认：
    - 未启动 `PH1`。
    - 未实现 `ContextCatalog` 或 `ActionManifest`。
    - 未改变 `PromptService` / `PromptProjection`。
    - 未改变旧 SWF 返回 shape。
    - `FavoritesMenu` 仍保持 conditional，未恢复 workspace/source/artifact inventory 时不成为 mandatory。
  - 状态同步：
    - `.dualpad-builder/feature_list.json` 中 `PH0` 已更新为 `completed` / `passes=true`。
    - `.dualpad-builder/sprint_plan.json` 中 `S-PH0` 已更新为 `completed`；`S-PH1` 仍为 `planned` / not started。
    - 入口文档已同步为 closed through `S-PH0`，并保留 `materialize-fixture` 不是 behavioral proof 的边界。
- 必跑验证结果：
  - `xmake build DualPadReplayHarness`
    - 结果：exit 0；输出包含 `linking.release DualPadReplayHarness.exe` 与 `build ok, spent 0.578s`。
  - `xmake build DualPadReplayHarnessTests`
    - 结果：exit 0；输出 `build ok, spent 0.25s`。
  - `xmake run DualPadReplayHarnessTests`
    - 结果：exit 0；stdout 为空。
  - `xmake build DualPad`
    - 结果：exit 0；输出包含 `Built: build/bin/DualPad/DualPad.dll` 与 `build ok, spent 1.297s`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`
    - 结果：exit 0；输出：
      - `01_gameplay_walk_attack_block_sprint: no diff`
      - `02_gameplay_menu_roundtrip: no diff`
      - `03_main_menu_glyph: no diff`
      - `04_journal_confirm_cancel: no diff`
      - `05_map_cursor_zoom_open_journal: no diff`
      - `07_book_page_lr: no diff`
      - `08_console_creations_lockpicking: no diff`
      - `09_combo_native_pause_screenshot_hotkeys: no diff`
      - `10_backlog_gap_overflow: no diff`
      - `11_config_reload_success_failure: no diff`
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
    - 结果：exit 0；输出包含 `[graphify watch] Rebuilt: 1103 nodes, 2231 edges, 106 communities`，并写入 `graphify-out/graph.json` 与 `graphify-out/GRAPH_REPORT.md`。

## 2026-05-19 00:00:00 CST

- `PH0` runtime replay barrier correction / start：
  - 更正上一条 `PH0 behavioral replay proof done` 结论：当前 dispatcher / processor mode 仍未完成真实 runtime replay proof。
  - 现状边界：
    - schema / harness bootstrap 已落地。
    - `validate-schema` 与 `materialize-fixture` 只能证明 schema / fixture plumbing。
    - 既有 CSV simulator 可保留为测试辅助，但不得称为 runtime dispatcher / processor replay proof。
    - 真正 PH0 runtime proof 必须驱动 `PadEventSnapshotDispatcher` / `PadEventSnapshotProcessor` 并采集 `AuthoritativePollState`、keyboard bridge、glyph 与 presentation surface 输出。
  - 状态同步：
    - `.dualpad-builder/feature_list.json` 中 `PH0` 已回退为 `active` / `passes=false`。
    - `.dualpad-builder/sprint_plan.json` 中 `S-PH0` 已回退为 `active`。
    - `PH1` 仍为 `planned` / not started。
  - 本轮边界确认：
    - 未启动 `PH1`。
    - 未实现 `ContextCatalog` 或 `ActionManifest`。
    - 未改变 `PromptService` / `PromptProjection`。
    - 未改变旧 SWF 返回 shape。
    - `FavoritesMenu` 保持 conditional，未恢复 workspace/source/artifact inventory 时不成为默认退出条件。
  - 验证执行状态：
    - 按用户要求，本轮不由 Codex 自行启动测试或 replay 验证；需要验证时仅列出命令，由用户手动执行并回传结果。

## 2026-05-19 00:00:00 CST

- `PH0` runtime replay barrier / implementation update：
  - 本轮继续实装真实 runtime replay proof，但不关闭 `PH0`：
    - `ReplayHarness` 的 `dispatcher` mode 保持通过 `PadEventSnapshotDispatcher::SubmitSnapshot(...)` 和 replay-only `DrainForReplay(...)` 驱动真实 dispatcher queue，再把 drained snapshot 交给 `PadEventSnapshotProcessor::Process(...)`。
    - `ReplayHarness` 的 `processor` mode 保持直接向 `PadEventSnapshotProcessor::Process(...)` 喂 `processed_snapshot_*.csv` 构造出的 snapshot。
    - `InputTraceRecorder` 负责采集 runtime candidate bundle：dispatcher schedule、ingress / processed snapshot、`AuthoritativePollState`、keyboard bridge、presentation surface 与 glyph results。
    - `materialize-fixture` 仍是唯一 copy-only 模式；`dispatcher` / `processor` 不复制 golden。
    - 旧 CSV poll simulator 已明确降级为 fixture-test helper，不作为 runtime replay proof。
  - 本轮修正 replay target 的 Skyrim-only 依赖边界：
    - `NativeButtonCommitBackend` 在 `DUALPAD_REPLAY_HARNESS` 下不再包含 `PollCommitCoordinator` 成员，也不再继承 `IPollCommitEmitter`，避免 replay CLI 静态构造 `RE::BSFixedString` 状态。
    - `InputModalityTracker.h` 移除不必要的 `PollCommitCoordinator.h` include，降低 replay target 间接拉入 `RE::BSFixedString` 的风险。
    - `PadEventSnapshotDispatcher::ResetForReplay()` 开启 manual drain mode，replay submit 不再调度 SKSE task；submit/drain queue 语义仍由 dispatcher 本体执行。
  - 本轮补齐 runtime surface 覆盖：
    - `03_main_menu_glyph` 增加非空 glyph query / expected result。
    - `09_combo_native_pause_screenshot_hotkeys` 增加非空 dispatcher / processor rows、keyboard bridge row 与 presentation surface row。
    - `10_backlog_gap_overflow` 与 `11_config_reload_success_failure` 的 expected poll / keyboard / presentation rows 已按 runtime processor 输出口径更新。
  - 状态同步：
    - `.dualpad-builder/feature_list.json`：`PH0` 仍为 `active` / `passes=false`。
    - `.dualpad-builder/sprint_plan.json`：`S-PH0` 仍为 `active`；`S-PH1` 仍为 `planned`。
    - `docs/authoritative-baseline/README.md`、`docs/authoritative-baseline/work-packages/README.md`、`docs/DOC_INDEX_zh.md` 与 Phase 0 slice 文档已同步为 `S-PH0` active，不再写 closed through `S-PH0`。
  - 本轮边界确认：
    - 未启动 `PH1`。
    - 未实现 `ContextCatalog` 或 `ActionManifest`。
    - 未改变 `PromptService` / `PromptProjection`。
    - 未改变旧 SWF 返回 shape。
    - `FavoritesMenu` 保持 conditional，未恢复 workspace/source/artifact inventory 时不成为默认退出条件。
- 验证执行状态：
  - 按用户要求，Codex 未自行启动测试、build、replay CLI 或 graphify rebuild；以下命令待用户手动运行并回传结果后再记录 exact results：
    - `xmake build DualPadReplayHarness`
    - `xmake build DualPadReplayHarnessTests`
    - `xmake run DualPadReplayHarnessTests`
    - `xmake build DualPad`
    - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`
    - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`
    - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`
    - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`
    - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`

## 2026-05-23 15:38:00 CST

- `PH0` runtime replay barrier / done：
  - 本轮完成真实 runtime dispatcher / processor replay proof，并关闭 `PH0`：
    - `dispatcher` mode 通过 `PadEventSnapshotDispatcher::SubmitSnapshot(...)` 与 replay-only `DrainForReplay(...)` 驱动真实 dispatcher queue，再把 drained snapshot 交给 `PadEventSnapshotProcessor::Process(...)`。
    - `processor` mode 从 `processed_snapshot_*.csv` 构造 snapshot，并直接驱动 `PadEventSnapshotProcessor::Process(...)`。
    - `InputTraceRecorder` 采集 runtime candidate bundle：dispatcher schedule、ingress / processed snapshot、`AuthoritativePollState`、keyboard bridge、presentation surface 与 glyph results。
    - `materialize-fixture` 仍是独立 copy-only plumbing mode，不作为 runtime replay proof。
    - `03_main_menu_glyph`、`09_combo_native_pause_screenshot_hotkeys`、`10_backlog_gap_overflow` 与 `11_config_reload_success_failure` 均已有非空 runtime coverage rows。
  - 状态同步：
    - `.dualpad-builder/feature_list.json`：`PH0` 已更新为 `completed` / `passes=true`。
    - `.dualpad-builder/sprint_plan.json`：`S-PH0` 已更新为 `completed`，`current_sprint=null`；`S-PH1` 仍为 `planned` / not started。
    - `docs/authoritative-baseline/README.md`、`docs/authoritative-baseline/work-packages/README.md`、`docs/DOC_INDEX_zh.md` 与 Phase 0 slice 文档已同步为 closed through `S-PH0`。
  - 本轮边界确认：
    - 未启动 `PH1`。
    - 未实现 `ContextCatalog` 或 `ActionManifest`。
    - 未改变 `PromptService` / `PromptProjection`。
    - 未改变旧 SWF 返回 shape。
    - `FavoritesMenu` 保持 conditional，未恢复 workspace/source/artifact inventory 时不成为默认退出条件。
- 中途失败与修正记录：
  - `xmake build DualPadReplayHarness`
    - 初次结果：exit 1；link 阶段 unresolved `ParseReplayMode`、`ReplayScenario`、`ReplayBatch`。
    - 根因：`xmake.lua` 中 `add_files(table.unpack(replay_runtime_files), "ReplayHarnessMain.cpp")` 在 Lua vararg 规则下只接入了第一个 unpack 项。
    - 修正：将 replay runtime files 与 main/tests files 拆成两次 `add_files(...)`。
  - `xmake run DualPadReplayHarnessTests`
    - 初次结果：exit 1；`expected_authoritative_poll.csv` mismatch，expected `Gameplay,1`，actual `Gameplay,0`。
    - 根因：当前 `AuthoritativePollState` 在无 native committed button 的 runtime replay 中保持 `context_epoch=0`；已按真实 runtime 输出修正 golden / tests。
    - 再次结果：exit 1；`expected_presentation_surface.csv` mismatch，runtime owner 与测试期望不一致。
    - 根因：09 场景触发 keyboard bridge 命令，不是 gate-aware `NativeButtonCommit`，不会给 gameplay presentation 发 Gamepad lease；已按真实 runtime seam 修正 09 presentation 期望。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`
    - 中途结果：exit 1；`phase0 root is not a directory: tests/replay/golden/phase0`。
    - 根因：xmake 运行目标时 cwd 不稳定，relative path 在 exe 工作目录下解析失败。
    - 修正：`ReplayHarnessMain` 忽略 standalone `--`，并将相对路径解析到最近的 `xmake.lua` project root。
- 必跑验证结果：
  - `xmake build DualPadReplayHarness`
    - 最终结果：exit 0；输出包含 `linking.release DualPadReplayHarness.exe` 与 `build ok, spent 0.5s`。
  - `xmake build DualPadReplayHarnessTests`
    - 最终结果：exit 0；输出 `build ok, spent 0.265s`。
  - `xmake run DualPadReplayHarnessTests`
    - 最终结果：exit 0；输出为 runtime config / binding config 日志，未再出现 test failure；命令成功结束。
  - `xmake build DualPad`
    - 结果：exit 0；输出包含 `linking.release DualPad.dll`、`Deployed: local configured deploy target` 与 `build ok, spent 5.187s`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`
    - 最终结果：exit 0；输出结尾 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`
    - 结果：exit 0；输出：
      - `01_gameplay_walk_attack_block_sprint: no diff`
      - `02_gameplay_menu_roundtrip: no diff`
      - `03_main_menu_glyph: no diff`
      - `04_journal_confirm_cancel: no diff`
      - `05_map_cursor_zoom_open_journal: no diff`
      - `07_book_page_lr: no diff`
      - `08_console_creations_lockpicking: no diff`
      - `09_combo_native_pause_screenshot_hotkeys: no diff`
      - `10_backlog_gap_overflow: no diff`
      - `11_config_reload_success_failure: no diff`
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`
    - 结果：exit 0；输出结尾 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`
    - 结果：exit 0；输出：
      - `01_gameplay_walk_attack_block_sprint: no diff`
      - `02_gameplay_menu_roundtrip: no diff`
      - `03_main_menu_glyph: no diff`
      - `04_journal_confirm_cancel: no diff`
      - `05_map_cursor_zoom_open_journal: no diff`
      - `07_book_page_lr: no diff`
      - `08_console_creations_lockpicking: no diff`
      - `09_combo_native_pause_screenshot_hotkeys: no diff`
      - `10_backlog_gap_overflow: no diff`
      - `11_config_reload_success_failure: no diff`
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
    - 结果：exit 0；输出包含 `[graphify watch] Rebuilt: 1207 nodes, 2477 edges, 108 communities`，并写入 `graphify-out/graph.json` 与 `graphify-out/GRAPH_REPORT.md`。

## 2026-05-23 16:00:00 CST

- `PH0` close-out hygiene：
  - 本轮只补齐 PH0 hygiene validation，不启动 `PH1`。
  - 新增验证 `xmake build DualPadDInput8Proxy` 已通过，因此 `PH0` 继续保持 `completed` / `passes=true`。
  - 清理 `.dualpad-builder/progress.md` 中的机器私有路径：
    - repo 内构建产物统一记录为 repo-relative 路径，例如 `build/bin/DualPad/DualPad.dll`。
    - 本机部署输出统一记录为 `local configured deploy target`，不写入具体盘符路径。
  - 边界确认：
    - `PH1` 仍为 `planned` / not started。
    - 未实现 `ContextCatalog` 或 `ActionManifest`。
    - 未改变 `PromptService` / `PromptProjection`。
    - 未改变旧 SWF 返回 shape。
- 验证结果：
  - `xmake build DualPadDInput8Proxy`
    - 结果：exit 0；输出包含 `Deployed dinput8 proxy: local configured deploy target` 与 `build ok, spent 0.031s`。
  - `git diff --check`
    - 结果：exit 0；stdout 仅包含 Windows 换行提示：`warning: in the working copy of '.dualpad-builder/progress.md', LF will be replaced by CRLF the next time Git touches it`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`
    - 结果：exit 0；无输出。
  - 私有路径扫描命令（按用户给定模式扫描 progress、authoritative baseline 与 dualpad rearchitecture plans）
    - 结果：exit 1；无输出。该命令用于确认没有机器私有路径命中，因此 exit 1/no matches 是预期结果。
  - PH0 / PH1 状态确认：
    - `.dualpad-builder/feature_list.json`：`PH0` 为 `completed` / `passes=true`；`PH1` 为 `planned`。
    - `.dualpad-builder/sprint_plan.json`：`S-PH0` 为 `completed`，`current_sprint=null`；`S-PH1` 为 `planned`。

## 2026-05-23 16:22:05 CST

- `PH1` start：
  - 已将 `PH1` / `S-PH1` 从 `planned` 晋升为 `in_progress`（`current_sprint=S-PH1`）。
  - 本轮范围：`ContextCatalog`、`ActionManifest`、`LegacyIniImporter`、`ManifestValidator`、`AtomicConfigReloader`、manifest publication handoff。
  - 本轮非目标：`PH2` 菜单实例真相、`PH3` presentation split、`PH4` InteractionEngine、`PH6` PromptService / PromptProjection、`PH7` IngressHub / FrameAssembler；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace。

## 2026-05-23 19:50:00 CST

- `PH1` close-out verification：
  - 验证结果：
    - `xmake build DualPadManifestCompilerTests`
      - 结果：exit 0。
    - `xmake run DualPadManifestCompilerTests`
      - 结果：exit 0。
    - `xmake build DualPadManifestCompilerTests`
      - 结果：exit 0。
    - `xmake run DualPadManifestCompilerTests`
      - 结果：exit 0。
    - `xmake build DualPad`
      - 结果：exit 0；输出包含 `Deployed: local configured deploy target` 与 `build ok`。
    - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`
      - 结果：exit 0；输出包含 `batch dispatcher runtime replay matched scenarios=10`。
    - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`
      - 结果：exit 0；全部场景 `no diff`。
    - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`
      - 结果：exit 0；输出包含 `batch processor runtime replay matched scenarios=10`。
    - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`
      - 结果：exit 0；全部场景 `no diff`。
    - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
      - 结果：exit 0；输出包含 `Rebuilt: 1317 nodes, 2648 edges, 116 communities`，并写入 `graphify-out/graph.json` 与 `graphify-out/GRAPH_REPORT.md`。

## 2026-05-23 19:53:49 CST

- `PH1` builder memory close-out：
  - `.dualpad-builder/feature_list.json`：`PH1` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH1` 已标为 `completed`，并将 `current_sprint=null`。
  - `PH2` / `S-PH2` 保持 `planned`，未启动。

## 2026-05-23 20:43:10 CST

- `PH1` close-out blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH1` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH1` 回退为 `active`，并将 `current_sprint` 设为 `S-PH1`。
  - `PH2` / `S-PH2` 保持 `planned`，本轮不启动。
  - 本轮修复范围限定为 `PH1` close-out blocker：ActionManifest schema、manifest publication fail-closed、AtomicConfigReloader promote 顺序、reload failure propagation、MenuContextPolicy runtime parser authority。

## 2026-05-23 21:08:26 CST

- `PH1` close-out blocker / fixed：
  - `ActionManifest` schema 已补齐 `actions / actionSets / actionLayers / bindings / displayBindings / outputDescriptors / policies / touchpadConfig / legacyBindingProjection`。
  - `actions` metadata 已显式包含 `id / valueKind / contract / outputDescriptorId / promptHintId / domain`，并由 validator 校验 descriptor 引用与必填字段。
  - `ActionManifestPublisher::PublishPromotedBundle(...)` 改为 epoch mismatch fail-closed：不发布、不增加 `publishCount`、返回 failure；`AtomicConfigReloader::Promote()` 会感知 publisher failure。
  - `AtomicConfigReloader::Promote()` 顺序改为先发布 manifest epoch seam，发布成功后才 swap active bundle；测试证明 publish seam 观察到的是旧 active epoch。
  - `BindingConfig::Reload()` 与 `MenuContextPolicy::Reload()` 在 reloader 失败时返回 failure，并保留旧 active bundle。
  - `MenuContextPolicy::ParseConfig()` 已移出生产 runtime surface；当时的 focused menu policy test 入口后续已由 manifest compiler / input_v2 tests 接管。
  - 未启动 `PH2`，未实现 `PH2 / PH3 / PH4 / PH6 / PH7`，未改变旧 SWF 返回 shape。
- 验证结果：
  - `xmake build DualPadManifestCompilerTests`：exit 0。
  - `xmake run DualPadManifestCompilerTests`：exit 0。
  - `xmake build DualPadManifestCompilerTests`：exit 0。
  - `xmake run DualPadManifestCompilerTests`：exit 0。
  - `xmake build DualPad`：exit 0。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1328 nodes, 2679 edges, 117 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH1` 已重新标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH1` 已重新标为 `completed`，`current_sprint=null`。
  - `PH2` / `S-PH2` 保持 `planned`，未启动。

## 2026-05-23 21:30:00 CST

- `PH2` start：
  - 已将 `PH2` / `S-PH2` 从 `planned` 晋升为 `active`（`current_sprint=S-PH2`）。
  - 本轮范围：`UiMenuObserver`、`MenuInstanceRegistry`、`ContextResolver`、`ActionSetResolver`、`contextRevision`、`menuStackRevision`、`presentationPolicyId` 与 shadow compare gate。
  - 本轮硬边界：`CompiledContextCatalog` / `CompiledActionManifest` 继续作为 config truth；`MenuContextPolicy`、`InputContextNames`、`BindingManager` 只保留兼容职责，不重新成为 menu instance / context / binding truth。
  - 本轮非目标：不启动 `PH3` presentation split、`PH4` InteractionEngine、`PH5` GameplayProjection、`PH6` PromptService / PromptProjection、`PH7` IngressHub / FrameAssembler；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace。

## 2026-05-23 21:52:00 CST

- `PH2` menu instance truth / done：
  - 本轮新增并接入：
    - `src/input_v2/menu/UiMenuObserver.*`
    - `src/input_v2/menu/MenuInstanceRegistry.*`
    - `src/input_v2/context/ContextResolver.*`
    - `src/input_v2/actions/ActionSetResolver.*`
    - `tests/input_v2/ContextResolverTests.cpp`
    - `xmake` target `DualPadContextResolverTests`
  - `UiMenuObserver` 负责 RE::UI open-menu snapshot；`MenuOpenCloseEvent` 只标记 dirty 并触发刷新。
  - `MenuInstanceRegistry` 发布 `menuStackRevision`，覆盖 stable pointer、same-name duplicate、partial snapshot 与 unavailable 保持旧发布值。
  - `ContextResolver` 只消费 `CompiledContextCatalog` 与 `MenuInstanceRegistry`，发布 `contextRevision`、`presentationPolicyId`、canonical `UiContextId` 与 legacy mirror。
  - `ActionSetResolver` 只输出 `baseSetId + layerIds + scopeAnchorIds`，未引入 per-context `.Base` set。
  - `ContextManager` 已退化为 legacy mirror；`_menuStack` / `_passthroughMenuCounts` / `RefreshCurrentContextLocked` 已移除，旧 `OnMenuOpen/OnMenuClose` 只保留兼容 shim。
  - Shadow compare helper 覆盖 `topMenuInstanceId`、`identityQuality`、`menuStackRevision`、`uiContextId`、`ActionSetStack`、`presentationPolicyId`、`legacyInputContext`、`legacyContextEpoch`、`contextRevision`。
  - 边界确认：未启动 `PH3`、`PH4`、`PH5`、`PH6`、`PH7`；未改变旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- 中途失败与修正记录：
  - `xmake run DualPadContextResolverTests` 初次失败：stable pointer 第二帧错误推进 `menuStackRevision`。
    - 根因：新建的非零 pointer 实例首帧被标成 `DegradedIdentity`，第二帧才变成 `StablePointer`。
    - 修正：非零 `menuPtr` 首次出现即标记为 `StablePointer`，纯 last-seen bookkeeping 不推动 revision。
  - `xmake run DualPadReplayHarness ... --mode dispatcher` 初次失败：`ContextManagerReplayStub` 仍实现旧 `RefreshCurrentContextLocked`。
    - 修正：stub 改为实现 `ApplyResolvedContext(...)` 与新的 `SetCurrentContextLocked(...)` mirror 语义。
  - 边界 grep 初次发现 `ContextResolver.cpp` 为 epoch helper 引入 `InputContextNames.h`。
    - 修正：resolver 改用本地 legacy mirror domain 判定，不消费 `InputContextNames`。
- 验证结果：
  - `xmake build DualPadContextResolverTests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadContextResolverTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，日志中的部署目标按本机配置处理，不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1374 nodes, 2745 edges, 120 communities`。
  - `git diff --check`：exit 0；stdout 仅包含 Windows 换行提示。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - 新 PH2 boundary grep：exit 1 / no matches，确认新 PH2 resolver/registry/action-set 文件未消费 `MenuContextPolicy`、`InputContextNames`、`ParseInputContextName`、`BindingManager`、`value_or(InputContext::Menu)` 或 per-context `.Base`。
  - 旧 authority grep：exit 1 / no matches，确认 `src/input` 与 replay stub 中已无 `_menuStack`、`_passthroughMenuCounts`、`RefreshCurrentContextLocked`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH2` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH2` 已标为 `completed`，`current_sprint=null`。
  - `PH3` / `S-PH3` 保持 `planned`，未启动。

## 2026-05-23 22:04:43 CST

- `PH2` close-out blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH2` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH2` 回退为 `active`，并将 `current_sprint` 设为 `S-PH2`。
  - `PH3` / `S-PH3` 保持 `planned`，本轮不启动。
  - 本轮修复范围限定为 `PH2` close-out blocker：`MenuOpenCloseEvent` 只 dirty、单一主线程 refresh tick、`presentationPolicyId` catalog truth、passthrough overlay action-set 语义、`menuStackRevision` 语义、gameplay facts 经 `ContextResolver` 发布。

## 2026-05-23 22:29:24 CST

- `PH2` close-out blocker / fixed：
  - `ContextEventSink` 的 `MenuOpenCloseEvent` 路径只调用 `UiMenuObserver::MarkMenuEvent(...)` 标记 dirty，不再直接 `Capture / Publish / Reconcile / Resolve / ApplyResolvedContext`。
  - 新增 `ContextRefreshTick` 作为主线程单一刷新入口；每个 frame token 最多执行一次 `Capture -> MenuInstanceRegistry -> ContextResolver -> ContextManager mirror`。
  - combat event 只记录 gameplay fact；combat / gameplay substate 变化经 `ContextRefreshTick -> ContextResolver::ResolveAndPublish(...) -> ContextManager::ApplyResolvedContext(...)` 发布，legacy `ContextManager` 只做 mirror。
  - `CompiledContextEntry` 已增加 `presentationPolicyId`，catalog seed 填入字段；`ContextResolver` 只转发 `entry.presentationPolicyId`，不再从 `canonicalContextName` 推导。
  - `ActionSetResolver` 明确区分 `PassthroughOverlay` 与 `UnknownTrackedMenu`：passthrough 返回空 action-set stack；只有 unknown tracked menu 返回 `MenuBase + UnknownTrackedMenuLayer`；未引入 per-context `.Base` set。
  - `menuStackRevision` 决策保持为 published shape 变化才推进；`lastSeenRevision` bookkeeping 不参与 revision，已由 `DualPadContextResolverTests` 覆盖。
  - 边界确认：未启动 `PH3`、`PH4`、`PH5`、`PH6`、`PH7`；未改变旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- 验证结果：
  - `xmake build DualPadContextResolverTests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadContextResolverTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1384 nodes, 2775 edges, 119 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH2` 已重新标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH2` 已重新标为 `completed`，`current_sprint=null`。
  - `PH3` / `S-PH3` 保持 `planned`，未启动。

## 2026-05-23 23:21:31 CST

- `PH2` close-out doc hygiene：
  - 只更新 `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md` 与 `.dualpad-builder/progress.md`。
  - Phase 2 计划文档已明确：`lastSeenRevision` 是 bookkeeping / diagnostic 字段，不参与 `menuStackRevision` 推进。
  - `menuStackRevision` 的推进条件收口为 published shape、identity、top tracked menu、tracked / overlay classification 等实际发布语义变化。
  - 状态保持：`PH2` / `S-PH2` 继续为 `completed` / `passes=true`；`PH3` / `S-PH3` 保持 `planned` / not started。

## 2026-05-23 23:51:42 CST

- `PH3` start：
  - 已将 `PH3` / `S-PH3` 从 `planned` 晋升为 `active`（`current_sprint=S-PH3`）。
  - 本轮范围：`InputModalityTracker` presentation 职责拆分、`SourceEvidenceCollector`、`PresentationProjection`、`SkyrimCompatibilitySurface`、`DeviceFamilyChanged` marker 与紧随 `SourceEvidenceSnapshot` 配对、presentation rollback / shadow parity。
  - 本轮硬边界：`PresentationProjection` 只能消费 PH2 `ResolvedContextSnapshot` / `presentationPolicyId`，不得重新推导菜单语义；`SkyrimCompatibilitySurface` 只负责 legacy 兼容输出，不承担 presentation truth；失败 rollback 退回旧 compatibility surface，不污染 PH2 context truth。
  - 本轮非目标：不启动 `PH4` Action Graph / InteractionEngine、`PH5` GameplayProjection、`PH6` PromptService / PromptProjection、`PH7` IngressHub / FrameAssembler；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace。

## 2026-05-24 00:23:07 CST

- `PH3` presentation split / done：
  - 本轮新增并接入：
    - `src/input_v2/presentation/SourceEvidenceCollector.*`
    - `src/input_v2/presentation/PresentationProjection.*`
    - `src/input_v2/presentation/SkyrimCompatibilitySurface.*`
    - `tests/input_v2/PresentationProjectionTests.cpp`
    - `xmake` target `DualPadPresentationProjectionTests`
  - `DeviceFamilyIngressPublisher` 是本 slice 内唯一递增 `deviceFamilyRevision` 的 source-evidence producer；`SourceEvidenceCollector::CollectAfterDeviceFamilyIngress(...)` 在 device family 变化时发布 `DeviceFamilyChanged` marker，并紧随一条 `SourceEvidenceSnapshot`。
  - `DeviceFamilyChangedPayload.newRevision == SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision` 已由 `DualPadPresentationProjectionTests` 覆盖。
  - `PresentationProjection` 只消费 `SourceEvidenceSnapshot`、PH2 `ResolvedContextSnapshot` 与 `PublishedGameplayPresentation`；`presentationPolicyId`、`uiContextId` 与 `ActionSetStack` 逐字段转发 PH2 snapshot，不反查 `ContextCatalog` 或重新推导菜单语义。
  - `SkyrimCompatibilitySurface` 只把 `PublishedPresentationState` 适配为 legacy hook / refresh 输出；rollback 模式只返回旧 compatibility output，不修改 committed `PublishedPresentationState` 或 PH2 context truth。
  - Shadow parity 落在 `SkyrimCompatibilitySurface::CompareShadowParity(...)`，只记录 legacy vs projected 输出 diff 与 `contextRevision / deviceFamilyRevision / gameplayPresentationRevision / epoch / reason`，不回答 hook、不改 published state、不触发 refresh。
  - 边界确认：未启动 `PH4`、`PH5`、`PH6`、`PH7`；未改变旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- 验证结果：
  - `xmake build DualPadPresentationProjectionTests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadPresentationProjectionTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1418 nodes, 2826 edges, 124 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH3` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH3` 已标为 `completed`，`current_sprint=null`。
  - `PH4` / `S-PH4` 保持 `planned`，未启动。

## 2026-05-24 00:52:13 CST

- `PH3` close-out blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH3` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH3` 回退为 `active`，并将 `current_sprint` 设为 `S-PH3`。
  - `PH4` / `S-PH4` 保持 `planned`，本轮不启动。
  - 本轮修复范围限定为 `PH3` close-out blocker：`GameplayPresentationAdapter`、`SourceEvidenceCollector` 采证迁移、`InputModalityTracker` façade、menu refresh cutover、三个 compatibility hook cutover、rollback 不污染 published presentation / PH2 context truth。

## 2026-05-24 01:14:33 CST

- `PH3` close-out blocker / fixed：
  - 新增 `src/input_v2/presentation/GameplayPresentationAdapter.*`：只读 `GameplayOwnershipCoordinator::GetPublishedGameplayPresentationState()`，发布 `PublishedGameplayPresentation`，并在 `engineOwner / menuEntryOwner / reason` 变化或 clean baseline resync 时递增 `gameplayPresentationRevision`。
  - `SourceEvidenceCollector` 已接管 `keyboardEvidence / mouseButtonEvidence / mouseMoveEvidence / gamepadEvidence / syntheticKeyboardWindow / gamepadLease / pointerSignal / context boundary reset`；collector 不产生 `PresentationOwner / CursorOwner / NavigationOwner` 结论。
  - `InputModalityTracker` 的外层入口保留，内部将 synthetic keyboard、mouse accumulator、gamepad lease 与 context boundary reset 转发给 `SourceEvidenceCollector`，并通过 `GameplayPresentationAdapter -> PresentationProjection -> SkyrimCompatibilitySurface` 发布 runtime presentation。
  - `IsUsingGamepadHook`、`GamepadControlsCursorHook` 与 remap-mode `IsGamepadDeviceEnabledHook` 已切到 `SkyrimCompatibilitySurface`。
  - `SetPresentationOwner` / `SetCursorOwner` 不再直接 `RefreshMenus()`；menu refresh 只由 `SkyrimCompatibilitySurface::ShouldRefreshMenus()` 根据 `PublishedPresentationState.epoch + dirty` 去重触发。
  - rollback 保持在 `SkyrimCompatibilitySurface`：只返回 legacy compatibility output，不修改 committed `PublishedPresentationState`，不污染 PH2 context truth。
  - 边界确认：未启动 `PH4`、`PH5`、`PH6`、`PH7`；未改变旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- 验证结果：
  - `xmake build DualPadPresentationProjectionTests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadPresentationProjectionTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1436 nodes, 2893 edges, 123 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH3` 已重新标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH3` 已重新标为 `completed`，`current_sprint=null`。
  - `PH4` / `S-PH4` 保持 `planned`，未启动。

## 2026-05-24 09:17:07 CST

- `PH3` menu owner takeover blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH3` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH3` 回退为 `active`，并将 `current_sprint` 设为 `S-PH3`。
  - `PH4` / `S-PH4` 保持 `planned`，本轮不启动。
  - 根因：`PresentationProjection` 在 `HostMode::Menu` 下进入首帧后只保留 `_published.owner`，未消费 `SourceEvidenceSnapshot.keyboardEvidence / mouseButtonEvidence / mouseMoveEvidence / gamepadEvidence / gamepadLease` 来决定 menu owner，导致 menu 内 keyboard takeover / gamepad reclaim 不能稳定推进 `PublishedPresentationState.owner`、`dirty.Owner` 与 `epoch`。

## 2026-05-24 09:19:20 CST

- `PH3` menu owner takeover blocker / fixed：
  - `PresentationProjection` 在 `HostMode::Menu` 下现在按 source evidence 更新 owner：
    - `keyboardEvidence / mouseButtonEvidence / mouseMoveEvidence` -> `PresentationOwner::KeyboardMouse`
    - `gamepadEvidence / gamepadLease` -> `PresentationOwner::Gamepad`
  - `Gameplay -> Menu` 首帧仍继承 `PublishedGameplayPresentation.menuEntryOwner`。
  - `Menu -> Gameplay` 仍重新消费 `PublishedGameplayPresentation.engineOwner`。
  - owner 变化通过现有 dirty diff 机制设置 `PresentationDirtyFlags::Owner` 并推进 `epoch`；unchanged publish 不抖动 `epoch`。
  - `SourceEvidenceCollector` 在显式键鼠证据进入时清理旧 gamepad lease，避免旧 lease 压过真实 keyboard / mouse takeover。
  - 边界确认：未回退到 `InputModalityTracker` 旧字段作为 hook truth；未启动 `PH4`、`PH5`、`PH6`、`PH7`；未改变旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- 验证结果：
  - `xmake build DualPadPresentationProjectionTests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadPresentationProjectionTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1437 nodes, 2899 edges, 124 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH3` 已重新标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH3` 已重新标为 `completed`，`current_sprint=null`。
  - `PH4` / `S-PH4` 保持 `planned`，未启动。

## 2026-05-24 09:39:34 CST

- `PH4` start：
  - 已将 `PH4` / `S-PH4` 从 `planned` 晋升为 `active`（`current_sprint=S-PH4`）。
  - 本轮范围限定为 Action Graph / InteractionEngine：`ControlPath`、`BindingModifier`、`InteractionSpec`、`DisplayBinding`、`CompiledActionGraph`、`InteractionEngine`、`CompiledActionGraphPublisher::Publish(...)` 与 `DualPadInputV2Tests`。
  - 本轮硬边界：`PH1 ActionManifest` 仍是 action / binding / display metadata truth；`PH2 ActionSetStack` 仍是 active scope truth；`PH3 PublishedPresentationState` 仍是 presentation truth；`CompiledActionGraphPublisher::Publish(...)` 是 compiled graph publication 与 hot-swap 的唯一入口，`Phase 7` 不得另造 graph publish 点。
  - 本轮非目标：不启动 `PH5 GameplayProjection`、`PH6 PromptService / PromptProjection`、`PH7 IngressHub / FrameAssembler`；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace；不引入 per-context `.Base` set；不让 `BindingManager` 重新成为 action graph truth。

## 2026-05-24 10:01:09 CST

- `PH4` action graph and interaction engine / done：
  - 本轮新增并接入：
    - `src/input_v2/actions/ControlPath.*`
    - `src/input_v2/actions/InteractionSpec.*`
    - `src/input_v2/actions/CompiledActionGraph.*`
    - `src/input_v2/actions/InteractionEngine.*`
    - `src/input_v2/actions/CompiledActionGraphPublisher.*`
    - `tests/input_v2/InputV2Tests.cpp`
    - `xmake` target `DualPadInputV2Tests`
  - `ControlPath` 只表达物理路径；`BindingModifier` 只表达物理值整形；`InteractionSpec` 收口 `Value / Press / Hold / Tap / Repeat / Toggle / Chord / Gesture`，不把 Layer 当新 interaction kind。
  - `ActionGraphCompiler` 从 `PH1 ActionManifest` 的 legacy projection 编译不可变 `CompiledActionGraph`，并保持 `PH1 ActionManifest` 是 action / binding / display metadata truth。
  - `Layer` 降格为主路径交互的 required-path 约束，非顺序、无独立 combo 时间窗、`ExactOnly`。
  - `Combo` 降格为无序 `Chord`，固定两键、固定 `kLegacyComboWindowUs`、`ExactOnly`；三键 combo fail-closed。
  - `DisplayBindingRecord` 作为 compiled graph 的显示候选产物生成；显式 manifest display token 可让 axis 从默认 hidden 变为 primary，combo 会标记 legacy token bridge 不可直接渲染。
  - `CompiledActionGraphPublisher::Publish(...)` 已作为 compiled graph publication 与 hot-swap 的唯一入口落地；manifest epoch mismatch fail-closed 且不替换 active graph。
  - `InteractionEngine` 只消费 `CompiledActionGraph`、`ActionSetStack` 和 `InteractionInputFrame`，输出 `ResolvedActionFrame`，不调用 backend、不生成 `FrameActionPlan`，也不消费 `BindingManager`。
  - 边界确认：未启动 `PH5`、`PH6`、`PH7`；未改旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace；未引入 per-context `.Base` set；未让 `BindingManager` 重新成为 action graph truth。
- 验证结果：
  - `xmake build DualPadInputV2Tests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1493 nodes, 3030 edges, 131 communities`。
  - `git diff --check`：exit 0；stdout 仅包含 Windows 换行提示。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH4` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH4` 已标为 `completed`，`current_sprint=null`。
  - `PH5` / `S-PH5` 保持 `planned`，未启动。

## 2026-05-24 10:24:28 CST

- `PH4` close-out blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH4` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH4` 回退为 `active`，并将 `current_sprint` 设为 `S-PH4`。
  - `PH5` / `S-PH5` 保持 `planned`，本轮不启动。
  - 根因范围：上一轮 PH4 骨架缺少 `BindingMatchPolicy` 运行时匹配、同 primary path conflict resolution、unordered combo duplicate detection、命名的 `LegacyInteractionInputAdapter` / `KernelFrame` 输入 seam、manifest promote 后 graph publication runtime owner，以及 `ResolvedActionFrame` 到旧生命周期面的过渡 bridge / parity seam。
  - 本轮边界：不实现 `PH5`、`PH6`、`PH7`；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace。

## 2026-05-24 10:45:31 CST

- `PH4` close-out blocker / fixed：
  - `InteractionEngine` 已实现 `BindingMatchPolicy`：`ExactOnly` 要求同 primary-kind active path 精确匹配；`PreferExactThenSubset` 在无 exact candidate 时允许 subset fallback。
  - 同 primary path conflict resolution 已按 frozen policy 收口：exact 优先于 subset，更高 specificity 优先，仍冲突时以 `bindingId` 稳定排序；输出继续保留 `bindingId`。
  - `Layer` exact 命中时不再同时触发 base Button fallback；`Layer / Combo / Gesture / Axis` 继续由 compiler 固定为 `ExactOnly`，`Button / Hold / Tap` 才允许 `PreferExactThenSubset`。
  - `Combo` duplicate detection 已按 unordered path set canonicalize；`Combo:L1+R1` 与 `Combo:R1+L1` 会视作同一 shape 并 fail-closed。
  - 对 legacy context alias collapse 产生的同 action / 同 shape / 非 combo 重复，compiler 会去重而不是生成多个 runtime binding；真实 `BookMenu` / `Book` -> `BookLayer` 重复因此不再阻断 graph publication。
  - 新增 `LegacyInteractionInputAdapter` 与最小 `KernelFrame`，`InteractionEngine` 不再消费匿名 `InteractionInputFrame`；adapter 删除条件固定为 `InputKernel::BuildKernelFrame` 直接消费 `AssembledFactFrame` 后删除或退化为测试夹具。
  - `CompiledActionGraphPublisher::GetRuntimeOwner()` 已作为 runtime graph publication owner 接入 `ActionManifestPublisher::PublishPromotedBundle(...)`；manifest publish 成功前会先 compile/publish graph，graph compile/publish 失败不会记录 manifest publication，也不会替换 active bundle。
  - 新增 `LegacyLifecycleBridge::BuildShadowFrameActionPlan(...)`，过渡期可消费 `ResolvedActionFrame` 做 legacy `FrameActionPlan` parity / shadow bridge；旧 `ActionLifecycleCoordinator` 不再是 PH4 唯一可验证的 interaction truth。
  - 边界确认：未启动 `PH5`、`PH6`、`PH7`；未改旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- 中途失败与修正记录：
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher` 初次失败：`DualPadReplayHarness` 未链接 PH4 graph compiler / publisher。
    - 修正：所有消费 `ph1_manifest_compiler_files` / replay runtime 的 target 同步接入 `ph4_action_graph_files`。
  - 修正后 replay 首次进入真实 config promotion 时失败：`BookMenu` 与 `Book` collapse 到同一 `BookLayer` 后产生同 action / 同 shape duplicate。
    - 修正：compiler 对非 combo 的同 action alias duplicate 去重；不同 action duplicate 与 unordered combo duplicate 继续 fail-closed。
- 验证结果：
  - `xmake build DualPadInputV2Tests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0；测试内的 expected fail-closed 日志包含 epoch mismatch 与 duplicate graph compile failure。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1513 nodes, 3077 edges, 134 communities`。
  - `git diff --check`：exit 0；stdout 仅包含 Windows 换行提示。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH4` 已重新标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH4` 已重新标为 `completed`，`current_sprint=null`。
  - `PH5` / `S-PH5` 保持 `planned`，未启动。

## 2026-05-24 12:53:14 CST

- `PH5` start：
  - 已将 `PH5` / `S-PH5` 从 `planned` 晋升为 `active`（`current_sprint=S-PH5`）。
  - 本轮范围限定为 GameplayProjection：`GameplayProjectionFrame`、`RecoveryPlan`、`GameplayPresentationPublisher`、`HardReset / SoftResync / clean baseline` 执行顺序、native / helper / presentation 三面同步验证，以及 legacy coordinator feedback loop authority 收缩。
  - 本轮硬边界：`PH4 ResolvedActionFrame` 是 gameplay projection 输入 truth；`PH3 PublishedGameplayPresentation / PresentationProjection` 是 presentation seam；`GameplayProjectionFrame` 结构固定，不临场补字段；`RecoveryPlan` 明确 HardReset / SoftResync / clean baseline 顺序。
  - 本轮非目标：不启动 `PH6 PromptService / PromptProjection`、`PH7 IngressHub / FrameAssembler`；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace。

## 2026-05-24 13:37:57 CST

- `PH5` gameplay projection / done：
  - 本轮新增并接入：
    - `src/input_v2/gameplay/GameplayProjectionFrame.*`
    - `src/input_v2/gameplay/RecoveryPlan.*`
    - `src/input_v2/gameplay/GameplayPresentationPublisher.*`
    - `tests/input_v2/GameplayProjectionTests.cpp`
    - `xmake` target `DualPadGameplayProjectionTests`
  - `GameplayProjectionFrame` 已按 PH5 固定结构落地，包含 native transient/sustained、helper、gate、recovery、presentation plan 与 reasons；`LegacyInputContextCompat` 只作为 frame/debug/replay compatibility 标签。
  - `RecoveryPlan` 已固定 `HardReset / SoftResync / clean baseline` 顺序：清输出面、清 sustained aggregator、清 projection sticky owner、执行 output plan、必要时提交 clean baseline。
  - `GameplayPresentationPublisher` 成为 PH5 runtime gameplay presentation seam；`PadEventSnapshotProcessor` 在 output apply 后发布 `PublishedGameplayPresentation`，`InputModalityTracker` 与 replay stub 改读 publisher seam。
  - `PadEventSnapshotProcessor::FinishFramePlanning(...)` 不再调用 `GameplayOwnershipCoordinator::UpdateDigitalOwnership(...)` 或 `ApplyOwnership(...)`；legacy coordinator live feedback loop 不再是 gameplay projection authority，剩余 backend gate/reset 只消费 `GameplayProjectionFrame.gatePlan`。
  - native / helper / presentation 三面同步验证已进入 `DualPadGameplayProjectionTests`，覆盖 transient gate、Sprint sustained source aggregation、helper plan、overflow hard reset、publisher output-apply-after-publish 顺序。
  - 边界确认：未启动 `PH6`、`PH7`；未改变旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- 中途失败与修正记录：
  - dispatcher replay 初次失败：PH5 默认 trigger enter threshold 过高，导致 golden 中 `0.25` trigger 被错误归零。
    - 修正：`GameplayPolicy` 默认阈值对齐旧 runtime：look/move enter `0.25`、sustain `0.15`，trigger enter `0.15`、sustain `0.08`。
  - dispatcher replay 随后出现 presentation surface mismatch：capture 仍读旧 coordinator presentation state。
    - 修正：processor output apply 后发布 `GameplayPresentationPublisher` runtime seam，presentation capture 与 replay stub 改读该 seam。
  - native transient action 一度被误当作 presentation Gamepad evidence。
    - 修正：presentation owner 只由 look/move/combat analog owners 推动；`digitalOwner` 继续只治理 transient native digital gate，不再驱动 presentation。
- 验证结果：
  - `xmake build DualPadGameplayProjectionTests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadGameplayProjectionTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1559 nodes, 3157 edges, 136 communities`。
  - `git diff --check`：exit 0；stdout 仅包含 Windows 换行提示。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH5` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH5` 已标为 `completed`，`current_sprint=null`。
  - `PH6` / `S-PH6` 保持 `planned`，未启动。

## 2026-05-24 15:41:19 CST

- `PH5` close-out blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH5` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH5` 回退为 `active`，并将 `current_sprint` 设为 `S-PH5`。
  - `PH6` / `S-PH6` 保持 `planned`，本轮不启动。
  - 根因范围：上一轮 PH5 只让 `PadEventSnapshotProcessor` 直接消费 projection 的 gate / analog / presentation plan；`GamepadOutputPlan` / `KeyboardHelperOutputPlan` 没有由固定 executor 应用，`GameplayPresentationPublisher` 的 runtime owner 也不够明确，`GameplayOwnershipCoordinator` presentation API 仍在 live runtime path 可达。
  - 本轮边界：只修 PH5 runtime executor、publisher seam 和 coordinator authority 收缩；不实现 `PH6`、`PH7`；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace。

## 2026-05-24 16:10:16 CST

- `PH5` close-out blocker / fixed：
  - 新增 `PollOutputAdapter` / `IPollOutputExecutor`，固定执行顺序为：RecoveryPlan 清 native/helper/sustained/projection sticky、下发 GatePlan、应用 sustainedDigital、应用 transientDigital、应用 helperPlan.commands、发布 analog、必要时提交 clean baseline；`PollOutputApplyResult.outputApplySucceeded` 成为 presentation publish gate。
  - 新增 `DualPadRuntime` runtime owner；`GameplayPresentationPublisher` 不再提供 runtime singleton，只能由 `DualPadRuntime::PublishGameplayPresentation(...)` 在 output apply 成功后调用。
  - `PadEventSnapshotProcessor` 已退化为 legacy snapshot runtime adapter：保留旧输入组装 / lifecycle shadow plan / ResolvedActionFrame 构造，不再直接执行 gate、legacy dispatch、analog publish 或 gameplay presentation publish。
  - `InputModalityTracker` 不再调用 `RecordGameplayPresentationHint(...)`；`GameplayPresentationAdapter::PublishFromCoordinator(...)` 与 coordinator presentation API 已移除；live runtime 可达面不再有 coordinator presentation bridge。
  - 修复 replay 中 hard reset 空 helper reset 回归：`KeyboardHelperBackend::Reset()` 只在 helper 侧已有活动输出时才向 bridge 下发 reset，仍会清内部 helper 状态。
- 验证结果：
  - `xmake build DualPadGameplayProjectionTests`：exit 0，输出包含 `build ok`。
  - `xmake run DualPadGameplayProjectionTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出包含 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1575 nodes, 3159 edges, 139 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH5` 已标回 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH5` 已标回 `completed`，`current_sprint=null`。
  - `PH6` / `S-PH6` 保持 `planned`，未启动。

## 2026-05-24 16:13:14 CST

- `PH5` runtime publisher seam / final rerun：
  - 收紧 `DualPadRuntime`：`outputApplySucceeded=false` 时不再调用 publisher，直接保留已发布 presentation；publisher 只在 runtime owner 且 output apply 成功后进入。
  - 已重新执行本轮要求的完整验证命令，结果仍全部通过：
    - `xmake build DualPadGameplayProjectionTests`：exit 0。
    - `xmake run DualPadGameplayProjectionTests`：exit 0。
    - `xmake build DualPad`：exit 0。
    - dispatcher replay + diff：exit 0，10 个 mandatory 场景均为 `no diff`。
    - processor replay + diff：exit 0，10 个 mandatory 场景均为 `no diff`。
    - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1575 nodes, 3159 edges, 139 communities`。

## 2026-05-24 16:30:00 CST

- `PH6` start：
  - 已将 `PH6` / `S-PH6` 从 `planned` 晋升为 `active`（`current_sprint=S-PH6`）。
  - 本轮范围限定为 PromptProjection / PromptService：`PromptScope`、`PromptSnapshotRecord`、prompt fail-closed matrix、旧 SWF API 兼容策略与 repo-owned `Interface/startmenu.swf` smoke 记录。
  - 本轮硬边界：`PH4 CompiledActionGraph / DisplayBinding` 是 prompt binding truth；`PH5 GameplayProjectionFrame / PublishedGameplayPresentation` 只能作为输入事实，prompt 层不得反推 gameplay；`PromptProjection` 只能消费已发布合同，不直接查 `BindingManager` / `Trigger` / legacy INI；`PromptService` 是 prompt snapshot 的唯一服务入口。
  - 本轮非目标：不启动 `PH7 IngressHub / FrameAssembler`；不做 `09a` runtime deletion；不改 replay/golden authority；不恢复 `FavoritesMenu` workspace；不让 `GameplayOwnershipCoordinator` 回到 prompt / glyph / presentation truth。

## 2026-05-24 18:16:00 CST

- `PH6` prompt projection / done：
  - 本轮新增并接入：
    - `src/input_v2/prompt/PromptScope.*`
    - `src/input_v2/prompt/PromptSnapshotRecord.*`
    - `src/input_v2/prompt/PromptProjection.*`
    - `src/input_v2/prompt/PromptService.*`
    - `tests/input_v2/PromptSnapshotTests.cpp`
    - `xmake` target `DualPadPromptSnapshotTests`
  - `PromptProjection` 只消费 `PublishedPresentationState` 与 `manifestEpoch`，发布 `PublishedPromptScope`；presentation unavailable、empty scope 或 missing epoch 均 fail closed 为 `Unavailable`。
  - `PromptService` 只消费 `PublishedPromptScope + CompiledContextCatalog + CompiledActionGraph`，候选来自 PH4 `DisplayBindingRecord`；未读取 `BindingManager`、`Trigger`、legacy INI，也未引入 gameplay projection 反推。
  - `PromptSnapshotRecord` 固定为 11 字段派生投影：`actionId / status / resolvedSet / resolvedContext / primary / alternates / resolutionSource / fallback / deviceProfile / promptScopeRevision / manifestEpoch`；成功态由 `status == Ok` 推导。
  - fail-closed matrix 已在 `DualPadPromptSnapshotTests` 覆盖：missing graph、missing display binding、unknown action、device mismatch、scope mismatch，并补充 unknown context、hidden-only、legacy API failure shape。
  - 旧 SWF API 兼容策略已收口为 `PromptService` wrappers：`ResolveLegacyGlyphToken(...)` 成功返回 primary token、失败返回 `""`；`ResolveLegacyGlyph(...)` 保持 `ok / buttonArtToken / semanticId / contextName`，并允许附带 `failureReason / resolvedContextId / resolvedActionSetId / resolutionSource / fallback / deviceProfile / manifestEpoch / promptScopeRevision`。
  - 边界确认：未启动 `PH7`；未做 `09a` runtime deletion；未改 replay/golden authority；未恢复 `FavoritesMenu` workspace；未让 `GameplayOwnershipCoordinator` 回到 prompt / glyph / presentation truth。
- repo-owned `Interface/startmenu.swf` smoke：
  - 静态检查：`Interface/startmenu.swf` 存在，大小 `103800` bytes，last write `2026-04-07 23:46:23`。
  - live SWF / Skyrim smoke：`unavailable`。本轮没有可用的自动化 SWF runner 或已启动 Skyrim UI 会话，且不恢复外部 `FavoritesMenu` workspace；因此未声称 live SWF 调用已执行。
- 验证结果：
  - `xmake build DualPadPromptSnapshotTests`：初次失败，原因是 `PromptSnapshotRecord.h` 使用 `ActionId / BindingId` 但未包含 PH4 `CompiledActionGraph.h`；修正 include 后 rerun exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1616 nodes, 3241 edges, 143 communities`。
  - `git diff --check`：exit 0；stdout 仅包含 Windows 换行提示。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH6` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH6` 已标为 `completed`，`current_sprint=null`。
  - `PH7` / `S-PH7` 保持 `planned`，未启动。

## 2026-05-24 18:45:00 CST

- `PH6` close-out blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH6` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH6` 回退为 `active`，并将 `current_sprint` 设为 `S-PH6`。
  - `PH7` / `S-PH7` 保持 `planned`，本轮不启动。
  - 根因范围：上一轮 PH6 只落地 `PromptProjection` / `PromptService` core 和 snapshot tests；live SWF delegate 仍由 `ScaleformGlyphBridge` 直接调用 `ResolveActionGlyphCompat(...)`，后者继续走 `BindingManager::GetTriggerForAction(...)`、`ParseInputContextName(...).value_or(InputContext::Menu)` 和 specific context miss -> generic `Menu` retry。
  - 本轮边界：只修 PH6 runtime façade / runtime owner / prompt scope 接管 / tests；不实现 `PH7`、不做 `09a` runtime deletion、不恢复 `FavoritesMenu` workspace。

## 2026-05-24 18:58:00 CST

- `PH6` close-out blocker / fixed：
  - 新增 `PromptRuntimeOwner`，SWF/glyph runtime 查询现在从 active `CompiledContextCatalog`、active `CompiledActionGraph` 和 `PublishedPromptScope` 构造 `PromptService`；missing graph 或 missing scope 均 fail closed。
  - 新增 `ScaleformPromptAdapter` 作为 runtime façade：负责旧 `DualPad_GetActionGlyphToken` / `DualPad_GetActionGlyph` delegate 的参数解码、PromptService 查询和返回编码；旧 token API 返回 string，旧 descriptor 继续保持 `ok / buttonArtToken / semanticId / contextName`。
  - `ScaleformGlyphBridge` 已退化为 forwarding shim：`RegisterInitialMenus()`、`OnMenuOpened(...)`、`Accept(...)` 与 replay resolve 均转发到 prompt adapter / prompt owner，不再直接调用 `ResolveActionGlyphCompat(...)`。
  - `GlyphResolutionCompat` 已降级为 deprecated compat helper：内部委托 `PromptRuntimeOwner -> PromptService`，不再包含 `BindingManager`、`GetTriggerForAction`、`ParseInputContextName(...).value_or(Menu)` 或 specific context miss -> generic `Menu` retry。
  - `InputModalityTracker::PublishPresentationState(...)` 在 PH3 `PublishedPresentationState` 发布后同步 publish prompt scope；`PromptRuntimeOwner` 会在 active manifest epoch 变化时用最后一份 presentation truth 推进 `promptScopeRevision`。
  - `ActionGraphCompiler` 现在把 PH1 compiled display binding 的 legacy single-button/trigger display token 编译成旧 SWF 可消费的 ButtonArt token（如 `360_Y`），这是 compile-time display binding truth，不是 runtime trigger reverse lookup。
  - replay stub 只为缺少输入帧的 legacy glyph-only Phase 0 场景补发布 replay presentation truth；未改 golden 文件。
  - 边界确认：未启动 `PH7`；未做 `09a` runtime deletion；未恢复 `FavoritesMenu` workspace；未让 `GameplayOwnershipCoordinator` 回到 prompt / glyph / presentation truth。
- TDD / blocker verification：
  - RED：新增 runtime owner / adapter 测试后，`xmake build DualPadPromptSnapshotTests` 初次失败，报 `PromptRuntimeOwner.h` 不存在，证明测试覆盖了当前 blocker 缺口。
  - GREEN：实现 runtime owner / adapter / forwarding shim 后，`xmake build DualPadPromptSnapshotTests` exit 0，`xmake run DualPadPromptSnapshotTests` exit 0。
  - 测试覆盖：legacy token API 成功走 PromptService、legacy descriptor API 成功走 PromptService、invalid context fail closed 不 fallback Menu、old return shape 保持兼容、missing scope 返回空 token / `ok=false`。
  - 静态 guard：`rg` 检查 `src/input/glyph` 与 `src/input_v2/prompt` 中不再存在 runtime glyph reverse lookup 关键调用：`BindingManager`、`GetTriggerForAction`、`ParseInputContextName`、`value_or(InputContext::Menu)`、`TriggerToButtonArtToken`、`ButtonCodeToToken`。
- repo-owned `Interface/startmenu.swf` smoke：
  - 静态检查：`Interface/startmenu.swf` 存在，大小 `103800` bytes，last write `2026-04-07 23:46:23`。
  - live SWF / Skyrim smoke：`unavailable`。本轮没有可用自动化 SWF runner 或已启动 Skyrim UI 会话，因此未声称 live SWF 调用已执行。
- 验证结果：
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1641 nodes, 3294 edges, 146 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH6` 已标回 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH6` 已标回 `completed`，`current_sprint=null`。
  - `PH7` / `S-PH7` 保持 `planned`，未启动。

## 2026-05-24 19:00:00 CST

- `PH6` close-out blocker / final façade tightening：
  - 发现并移除 `ScaleformPromptAdapter::ResolveCompatForReplay(...)` 对 deprecated `ResolveActionGlyphCompat(...)` 的间接调用；Scaleform façade / bridge 现在不再引用 `ResolveActionGlyphCompat(...)`。
  - `DualPad_GetActionGlyphToken` / `DualPad_GetActionGlyph` 继续只经 `PromptRuntimeOwner` 查询 `PromptService` legacy wrappers；replay compat result 也由 `PromptRuntimeOwner::Resolve(...)` 的 `PromptDescriptor` 转换得到。
  - `GlyphResolutionCompat` 仍保留为 deprecated compat helper，内部委托 `PromptRuntimeOwner -> PromptService`，不再参与 Scaleform runtime façade。
  - `PH7` / `S-PH7` 保持 `planned`，未启动；未做 `09a` runtime deletion；未恢复 `FavoritesMenu` workspace。
- 重新验证结果：
  - static guard：`src/input/glyph` 与 `src/input_v2/prompt` 中无 `BindingManager`、`GetTriggerForAction`、`ParseInputContextName`、`value_or(InputContext::Menu)`、`TriggerToButtonArtToken`、`ButtonCodeToToken`。
  - static guard：`src/input_v2/prompt` 与 `ScaleformGlyphBridge.*` 中无 `ResolveActionGlyphCompat` 引用。
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - repo-owned `Interface/startmenu.swf` static smoke：存在，大小 `103800` bytes，last write `2026-04-07 23:46:23`；live SWF / Skyrim smoke 仍为 `unavailable`，无自动化 SWF runner 或已启动 Skyrim UI 会话。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1644 nodes, 3300 edges, 147 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH6` 保持 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH6` 保持 `completed`，`current_sprint=null`。
  - `PH7` / `S-PH7` 保持 `planned`，未启动。

## 2026-05-24 19:18:00 CST

- `PH6` legacy glyph compat target / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH6` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH6` 回退为 `active`，并将 `current_sprint` 设为 `S-PH6`。
  - `PH7` / `S-PH7` 保持 `planned`，本轮不启动。
  - 根因范围：`GlyphResolutionCompat.cpp` 已委托 `PromptRuntimeOwner -> PromptService`，但 `DualPadGlyphResolutionCompatTests` target 仍按旧模型链接 `BindingManager / InputContextNames / GlyphResolutionCompat.cpp`，`tests/GlyphResolutionCompatTests.cpp` 也仍直接把 `BindingManager` 当 glyph authority。

## 2026-05-24 20:52:00 CST

- `PH6` legacy glyph compat target / fixed：
  - `DualPadGlyphResolutionCompatTests` 已更新为 deprecated compat wrapper test target：链接 `ph1_manifest_compiler_files`、`ph4_action_graph_files`、`ph6_prompt_files` 与 `GlyphResolutionCompat.cpp`。
  - target 不再链接 `src/input/BindingManager.cpp` 或 `src/input/InputContextNames.cpp`，旧 `BindingManager / Trigger` reverse lookup 不再作为 glyph authority。
  - `tests/GlyphResolutionCompatTests.cpp` 已重写为 PH6 runtime owner 测试：先通过 `AtomicConfigReloader` 发布 active manifest / graph，再通过 `PromptRuntimeOwner` 发布 prompt scope，最后验证 `ResolveActionGlyphCompat(...)` 的 deprecated wrapper 行为。
  - 覆盖场景：missing prompt scope fail closed；成功路径 token 与 `PromptRuntimeOwner::ResolveLegacyGlyphToken(...)` 一致且来自 compiled display binding token `360_Y`；invalid context fail closed，不 fallback `Menu`，也不重写 resolved context。
  - 本轮未启动 `PH7` / `S-PH7`，未做 `09a` runtime deletion，未恢复 `FavoritesMenu` workspace。
  - bookkeeping correction：本地一次过宽状态 patch 曾误触 `DP1a` / `S-DP1a`，已在提交前恢复为 `completed`。
- RED / GREEN：
  - RED：`xmake build DualPadGlyphResolutionCompatTests` 初次 exit 1，链接失败，缺 `ContextCatalog::BuiltInCatalog/ResolveAlias/ToLegacyInputContext` 与 `PromptRuntimeOwner::GetSingleton/Resolve`，证明 target 缺 PH1 / PH6 runtime 依赖。
  - GREEN：更新 target 与测试后，`xmake build DualPadGlyphResolutionCompatTests` exit 0，输出 `build ok`；`xmake run DualPadGlyphResolutionCompatTests` exit 0。
- 验证结果：
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1646 nodes, 3306 edges, 141 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH6` 已标回 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH6` 已标回 `completed`，`current_sprint=null`。
  - `PH7` / `S-PH7` 保持 `planned`，未启动。

## 2026-05-24 21:22:33 CST

- `PH7` ingress and resync / start：
  - 已将 `.dualpad-builder/feature_list.json` 中 `PH7` 从 `planned` 晋升为 `active`，`passes=false`。
  - 已将 `.dualpad-builder/sprint_plan.json` 中 `S-PH7` 从 `planned` 晋升为 `active`，并将 `current_sprint` 设为 `S-PH7`。
  - 本轮范围限定为 `IngressHub`、`FrameAssembler`、四字段 boundary key、transition frame、`SequenceGap` / `QueueOverflow` recovery、`ManifestEpochChanged` / `DeviceFamilyChanged` marker、dispatcher / coalescing / resync 重做，以及 legacy dispatcher / processor adapter 化。
  - 本轮硬边界：`manifestEpoch` 的唯一 authoritative source 是 `ManifestEpochChanged` marker payload；`deviceFamilyRevision` 的唯一 authoritative source 是 `DeviceFamilyChanged` marker payload；boundary key 固定为 `manifestEpoch / contextRevision / menuStackRevision / deviceFamilyRevision`；`FrameAssembler` 不得从 active graph / `PromptService` / `PresentationProjection` 反查这些 revision。
  - 本轮非目标：不启动 `PH8` / `09a` runtime deletion；不启动 `PH8` / `09b` governance closeout；不重新设计 `PH0-PH6` 已冻结合同；不改旧 SWF 返回 shape；不恢复 `FavoritesMenu` workspace。

## 2026-05-24 21:30:41 CST

- `PH7` ingress and resync / done：
  - 本轮新增：
    - `src/input_v2/ingress/IngressHub.*`
    - `src/input_v2/ingress/FrameAssembler.*`
    - `src/input_v2/ingress/IngressBoundaryKey.*`
    - `src/input_v2/ingress/IngressMarkers.*`
    - `src/input_v2/ingress/IngressRecovery.*`
    - `tests/input_v2/IngressTests.cpp`
    - `tests/input_v2/ReplayTests.cpp`
    - `xmake` target `DualPadIngressTests`
    - `xmake` target `DualPadReplayTests`
  - `IngressHub` 作为 producer adapter ingress 队列落地：统一分配单调 `seq` / `monotonicUs`，queue full 正式产出 `QueueOverflow` marker，显式支持 `SequenceGap` / `ExplicitReset`。
  - `PadEventSnapshotDispatcher` 已在 submit seam 转发 `PadEventSnapshot` 到 `IngressHub`，旧 dispatcher 不再新增 boundary / recovery truth；现有 drain 仍保留以维持 Phase 0 replay parity。
  - `FrameAssembler` 已成为 PH7 新 coalescing 合同入口：只消费 ingress events 与 assembler state；输出 `Stable` / `Transition` 两类 frame；`Transition` frame 不进入 `InteractionEngine`。
  - boundary key 已固定为四字段：`manifestEpoch / contextRevision / menuStackRevision / deviceFamilyRevision`。
  - `manifestEpoch` 只从 `ManifestEpochChangedPayload.manifestEpoch` 进入 boundary；`deviceFamilyRevision` 只从 `DeviceFamilyChangedPayload.deviceFamilyRevision` 进入 boundary；`SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision` 只做配对 / 校验 / 镜像。
  - `SequenceGap` 映射为 `SoftResync`；`QueueOverflow`、`ExplicitReset` 与 `ManifestEpochChanged` 映射为 `HardReset`；marker mismatch 走 fail-closed `ExplicitReset -> HardReset`。
  - 静态 guard：`rg -n "GetActiveGraph|ActiveManifestEpoch|PromptService|PresentationProjection|PublishedPresentationState|deviceFamilyRevision|manifestEpoch" src/input_v2/ingress` 只命中 marker payload、boundary key、source evidence 校验与 kernel mirror 字段，未命中 active graph / prompt / presentation 反查入口。
  - 本轮未启动 `PH8` / `09a` runtime deletion；未启动 `PH8` / `09b` governance closeout；未改旧 SWF 返回 shape；未恢复 `FavoritesMenu` workspace。
- RED / GREEN：
  - RED：`xmake build DualPadIngressTests` 初次 exit 1，原因是 `input_v2/ingress/FrameAssembler.h` 与 PH7 source files 尚不存在，证明测试覆盖了本轮缺口。
  - GREEN：补齐 ingress 模块后，`xmake build DualPadIngressTests` / `xmake run DualPadIngressTests` 与 `xmake build DualPadReplayTests` / `xmake run DualPadReplayTests` 均 exit 0。
- 验证结果：
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `git diff --check`：exit 0；stdout 仅包含 Windows 换行提示。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1720 nodes, 3473 edges, 148 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH7` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH7` 已标为 `completed`，`current_sprint=null`。
  - `PH8` / `S-PH8`、`PH8a` / `S-PH8a`、`PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-24 22:28:18 CST

- `PH7` runtime ingress cutover blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH7` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH7` 回退为 `active`，并将 `current_sprint` 设为 `S-PH7`。
  - 根因范围：上一轮 `IngressHub` / `FrameAssembler` 已落地合同与 targeted tests，但 live runtime 仍以 `PadEventSnapshotDispatcher` pending queue、旧 coalescing 与 `PadEventSnapshotProcessor::Process(snapshot)` 为 authoritative path；legacy snapshot 未完整转出 `ControlSample` / pulse ledger，`SequenceGap` detection 也未基于上一条 observed/processed sequence。
  - 本轮修复范围限定为 PH7 runtime ingress cutover、marker producer 接线、transition recovery 接线与 targeted tests。
  - `PH8` / `09a` / `09b` 保持 `planned`，本轮不启动。

## 2026-05-24 22:58:15 CST

- `PH7` runtime ingress cutover blocker / fixed：
  - `PadEventSnapshotDispatcher::SubmitSnapshot(...)` 已退为 legacy ingress adapter：不再把 snapshot 写入旧 pending queue 作为 authoritative drain source，而是经 `IngressHub::PushPadSnapshot(...)` 转成正式 ingress events。
  - `DrainOnMainThread(...)` 已切到 `IngressHub::Drain -> FrameAssembler::Assemble -> PadEventSnapshotProcessor::ProcessIngressFrame(...)`。
  - `DrainForReplay(...)` 也走同一 assembled frame path；dispatcher replay 不再通过 sink 直接绕到旧 `PadEventSnapshotProcessor::Process(snapshot)`。
  - 新增 `LegacyIngressAdapter`：legacy snapshot 会转出 `UiSnapshot` + `PadSnapshot` ingress facts；digital down、press、release 和 analog values 均进入 `ControlSample`，press/release 进入 assembler pulse ledger。
  - `SequenceGap` detection 已改为基于 `lastObservedSequence + 1 != current.firstSequence`，不再使用错误的 `firstSequence > sequence` 判定。
  - `ActionManifestPublisher::PublishPromotedBundle(...)` 已接入 `ManifestEpochChanged` marker producer seam；marker payload 是 `manifestEpoch` 的唯一 ingress authority。
  - `SourceEvidenceCollector` 产出的 `DeviceFamilyChanged` marker + `SourceEvidenceSnapshot` 可通过 `PublishSourceEvidenceFrameToIngressHub(...)` 成对进入 ingress；`SourceEvidence` 只做配对 / 校验 / mirror，不作为 `deviceFamilyRevision` authority。
  - `PadEventSnapshotProcessor::ProcessIngressFrame(...)` 对 `Transition` frame 只执行 recovery，不进入 interaction / gameplay / prompt；对 `Stable` frame 先 `BuildKernelFrame(...)`，再桥接 legacy snapshot 兼容处理。
  - 旧 processor 的 sequence/degraded recovery 判定在 ingress-authoritative stable frame 内被禁用，避免绕过 `Transition` frame 重复拥有 recovery truth；原始 snapshot metadata 仍保留用于 trace parity。
  - 本轮没有启动 `PH8` / `09a` / `09b`，也没有改旧 SWF 返回 shape或恢复 `FavoritesMenu` workspace。
  - bookkeeping correction：本地状态 patch 曾误触 `DP1a` / `S-DP1a`，已恢复为 `completed`。
- RED / GREEN：
  - RED：新增 blocker tests 后，`xmake build DualPadIngressTests` 初次 exit 1，原因是缺 `input_v2/ingress/LegacyIngressAdapter.h`，证明测试覆盖了 legacy snapshot -> ingress facts 缺口。
  - GREEN：补齐 adapter、producer seam 与 runtime drain cutover 后，`DualPadIngressTests` / `DualPadReplayTests` 均通过。
- 验证结果：
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `git diff --check`：exit 0；stdout 仅包含 Windows 换行提示。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null; python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1737 nodes, 3529 edges, 149 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH7` 保持 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH7` 保持 `completed`，`current_sprint=null`。
  - `PH8` / `S-PH8`、`PH8a` / `S-PH8a`、`PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-24 23:16:21 CST

- `PH7` close-out 漏洞 / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH7` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH7` 回退为 `active`，并将 `current_sprint` 设为 `S-PH7`。
  - 根因范围：上一轮新增的 legacy snapshot / marker producer targeted tests 已存在，但未全部接入 `DualPadIngressTests` 的 `main()`；同时 `InputModalityTracker::PublishPresentationState(...)` 已生成 `SourceEvidenceFrame`，但还未把该 frame 推入 `IngressHub`，导致真实 runtime device-family marker producer seam 未接线。
  - 本轮修复范围限定为 PH7 close-out 漏洞：补齐 `IngressTests` main 调用、接入 `PublishSourceEvidenceFrameToIngressHub(frame)`，并确认 `SourceEvidence` 仍只做配对 / 校验 / mirror。
  - `PH8` / `09a` / `09b` 保持 `planned`，本轮不启动。

## 2026-05-24 23:28:33 CST

- `PH7` close-out 漏洞 / fixed：
  - `tests/input_v2/IngressTests.cpp` 的 `main()` 已接入 `TestLegacySnapshotAdapterProducesControlSamplesAndPulseLedger`、`TestLegacySequenceDiscontinuityProducesSequenceGap`、`TestManifestPublisherProducesIngressMarker`、`TestDeviceFamilyProducerProducesMarkerAndPairedSourceEvidence`。
  - `InputModalityTracker::PublishPresentationState(...)` 已在 `CollectAfterDeviceFamilyIngress(...)` 产出 `SourceEvidenceFrame` 后调用 `PublishSourceEvidenceFrameToIngressHub(frame)`，真实 runtime device-family marker producer seam 已推入 ingress。
  - `FrameAssembler` 仍只通过 `DeviceFamilyChangedPayload.deviceFamilyRevision` 写入 boundary key；`SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision` 只用于 pending marker 配对、mismatch fail-closed 校验与 facts mirror，不成为 `deviceFamilyRevision` authority。
  - 本轮没有启动 `PH8` / `09a` / `09b`。
- 验证结果：
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`：exit 0，输出 `batch dispatcher runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`：exit 0，10 个 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1737 nodes, 3533 edges, 149 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH7` 已标回 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH7` 已标回 `completed`，`current_sprint=null`。
  - `PH8` / `S-PH8`、`PH8a` / `S-PH8a`、`PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-24 23:37:50 CST

- `PH8` cutover entry gate / start：
  - 已将 `.dualpad-builder/feature_list.json` 中 `PH8` 从 `planned` 晋升为 `active`，`passes=false`。
  - 已将 `.dualpad-builder/sprint_plan.json` 中 `S-PH8` 从 `planned` 晋升为 `active`，并将 `current_sprint` 设为 `S-PH8`。
  - 本轮范围仅限 entry gate：审查 `09` / `09a` / `09b` 职责边界，确认 `09a` 只做 runtime closeout / deletion / shim shrink，确认 `09b` 只做 governance / docgen / CI closeout。
  - 本轮硬边界：不启动 `PH8a` / `09a` runtime deletion，不启动 `PH8b` / `09b` governance closeout，不删除 legacy runtime 文件，不改旧 SWF 返回 shape，不恢复 `FavoritesMenu` workspace，不重命名 canonical test targets。
  - `PH8a` / `S-PH8a`、`PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-24 23:38:32 CST

- `PH8` cutover entry gate / done：
  - 本轮只完成 entry gate，不是 implementation slice；没有启动 `09a` runtime deletion，也没有启动 `09b` governance closeout。
  - 已审查 `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md`、`09a_slice_phase8_runtime_closeout_zh.md`、`09b_slice_phase8_governance_closeout_zh.md`、`08_slice_phase7_ingress_and_resync_zh.md` 与 `src/ARCHITECTURE.md`。
  - 审查结论：`09` 只承担 Phase 8 entry / 共享约束 / handoff gate；`09a` 只承担 runtime public surface swap、legacy deletion、shim shrink 与 canonical prove-out target 首次落地；`09b` 只承担 governance、docgen provenance、reviewed docs 去重和默认 CI 接线。
  - 6 类 canonical prove-out targets 不推迟到 `09b` 首次补建：`09` 与 `09a` 均明确要求 `DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests` 由 `09a` 先落地并 prove-out；`09b` 只能复用同名 targets 接默认 CI。
  - `PH0-PH7` 已冻结 runtime 合同不在 `PH8` 重新设计；`08` 交给 `PH8` 的合同明确为删除旧路径、清理和 CI 覆盖，不能改回 snapshot/source-driven boundary、legacy resync flag 或重写 boundary key。
  - replay root 继续固定为 `tests/replay/golden/`；canonical target 名称保持不变。
  - 本轮没有删除 legacy runtime 文件，没有改旧 SWF 返回 shape，没有恢复 `FavoritesMenu` workspace，没有重命名 canonical test targets。
  - `.dualpad-builder/feature_list.json`：`PH8` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH8` 已标为 `completed`，`current_sprint=null`。
  - `PH8a` / `S-PH8a`、`PH8b` / `S-PH8b` 保持 `planned` / 未启动。

## 2026-05-26 00:00:00 CST

- `PH8a` runtime closeout / start：
  - 已将 `.dualpad-builder/feature_list.json` 中 `PH8a` 从 `planned` 晋升为 `active`，`passes=false`。
  - 已将 `.dualpad-builder/sprint_plan.json` 中 `S-PH8a` 从 `planned` 晋升为 `active`，并将 `current_sprint` 设为 `S-PH8a`。
  - 本轮范围限定为 `SingleAuthorityAssembly -> PublicSurfaceSwap -> LegacyDeletion / ShimShrink`。
  - 本轮必须把默认 runtime mainline 收口到 `src/input_v2/`，旧 runtime 只能删除或缩成无 authority shim。
  - 本轮只允许保留 `src/input/injection/PadEventSnapshotProcessor.*` 与 `src/input/glyph/ScaleformGlyphBridge.*` 两组 legacy-named shim；若保留，只能做参数打包、注册、转发和 error boundary。
  - `LegacyInputContextCompat` 是唯一允许残留的 legacy context compatibility type。
  - 本轮 canonical prove-out targets 固定为 `DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests`，replay root 继续固定为 `tests/replay/golden/`。
  - 本轮不启动 `PH8b` / `09b`，不做 docgen provenance、reviewed docs 去重或默认 CI 接线，不恢复 `FavoritesMenu` workspace，不改旧 SWF 返回 shape，不新增长期 runtime 开关。

## 2026-05-26 23:20:00 CST

- `PH8a` runtime closeout / done：
  - `SingleAuthorityAssembly` 已完成：`DualPadRuntime` 新增 `ProcessAssembledFrame(...)`，assembled ingress frame 的 interaction resolve、recovery 消费与 gameplay projection 统一收口到 `src/input_v2/gameplay/DualPadRuntime.*`。
  - `PublicSurfaceSwap` 已完成：`IsUsingGamepad`、`GamepadControlsCursor` 与 `BSPCGamepadDeviceHandler::IsEnabled` 的 hook 安装点切到 `src/input_v2/presentation/SkyrimCompatibilitySurface.*`；`DualPad_GetActionGlyphToken` / `DualPad_GetActionGlyph` 保持经 `ScaleformGlyphBridge` shim 转发到 `ScaleformPromptAdapter` / `PromptRuntimeOwner`。
  - `LegacyDeletion / ShimShrink` 已按本轮顺序完成：已物理删除 `GameplayOwnershipCoordinator.*`、`InputModalityTracker.*`、`InputContext.*`、`InputContextNames.*`、`MenuContextPolicy.*`、`BindingManager.*` 与 `src/input/mapping/*`。
  - 保留的 legacy-named shim 只有 `src/input/injection/PadEventSnapshotProcessor.*` 与 `src/input/glyph/ScaleformGlyphBridge.*`；processor 只负责 snapshot / frame 转发、reset/error boundary 与 replay trace emission，glyph bridge 只负责 Scaleform 注册和 prompt adapter 转发。
  - `LegacyInputContextCompat` 已落到 `src/input_v2/compat/LegacyInputContextCompat.h`，旧 `InputContext.*` owning files 不再存在。
  - `xmake.lua` 已补齐 6 个 canonical prove-out targets：`DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests`。
  - replay root 继续固定为 `tests/replay/golden/`；本轮未启动 `PH8b` / `09b`，未做 docgen provenance、reviewed docs 去重或默认 CI 接线，未恢复 `FavoritesMenu` workspace，未改旧 SWF 返回 shape，未新增长期 runtime 开关。
- RED / GREEN：
  - RED：`xmake build DualPad` 首轮 exit 1，原因是 `LegacyInputContextCompat` alias 引入 `ToString` ADL 歧义；修复为 compat 内部 `ToLegacyInputContextString(...)` + `dualpad::input::ToString(...)` 单一外层入口。
  - RED：`xmake build DualPadPropertyTests` 首轮 exit 1，原因是测试引用不存在的 `IngressSource::HidReader`；修复为现有 `IngressSource::LegacyDispatcher`。
  - RED：`xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay` 暴露 replay context publication 与 trace emission 缺口；已改为 input_v2 replay helper 发布 context，并把 processed snapshot trace emission 挂到 stable legacy snapshot frame。
- 验证结果：
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPadInputV2Tests`：exit 0，输出 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程仍按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPadPropertyTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPropertyTests`：exit 0。
  - `xmake build DualPadFuzzRegressionTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadFuzzRegressionTests`：exit 0。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0，9 个 phase0 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1443 nodes, 2764 edges, 137 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH8a` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH8a` 已标为 `completed`，`current_sprint=null`。
  - `PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-26 23:32:34 CST

- `PH8a` runtime closeout blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH8a` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH8a` 回退为 `active`，并将 `current_sprint` 设为 `S-PH8a`。
  - 根因范围：上一轮 `PH8a` 已标 completed，但 `DualPadRuntime::ProcessAssembledFrame(...)` 没有在 stable runtime mainline 中持续发布 `PresentationProjection -> SkyrimCompatibilitySurface -> PromptRuntimeOwner`；同时 transition frame 仍会继续进入 gameplay projection / output apply 路径，违反 `PH7` 冻结合同。
  - 本轮修复范围限定为 `PH8a` blocker：补齐 published surface pipeline、修正 transition recovery contract、补 targeted tests，并解释或修复 phase0 replay coverage 从 10 变 9 的问题。
  - `PH8b` / `S-PH8b` 保持 `planned`，本轮不启动。

## 2026-05-26 23:49:26 CST

- `PH8a` runtime closeout blocker / fixed：
  - `DualPadRuntime` stable assembled-frame mainline 已补齐持续发布链：`PresentationProjection::Project(...) -> SkyrimCompatibilitySurface::Commit(...) -> PromptRuntimeOwner::PublishPresentationState(...)`。
  - `IsUsingGamepad`、`GamepadControlsCursor`、`BSPCGamepadDeviceHandler::IsEnabled` 现在只读取 `SkyrimCompatibilitySurface` committed published state；rollback override 已收缩为 no-op，旧 surface 不再持有 authority。
  - `DualPad_GetActionGlyphToken` / `DualPad_GetActionGlyph` 继续只经 `ScaleformGlyphBridge` shim 转发到 `ScaleformPromptAdapter` / `PromptRuntimeOwner`，prompt scope 由 stable presentation publish 驱动。
  - transition frame 合同已修正：只消费 recovery、清空本地 state、记录 pending recovery；transition frame 不进入 `InteractionEngine`、`ResolveGameplayProjection` 或 `PollOutputAdapter`，下一份 stable frame 才应用 clean recovery baseline。
  - 已补 targeted tests：stable frame 更新 committed epoch、gamepad evidence 更新 `IsUsingGamepadHook`、presentation publish 后 prompt scope 更新、transition frame 不调用 gameplay projection/output executor、hard transition 后下一份 stable frame 应用 recovery clean baseline。
  - phase0 replay coverage 变 9 的根因不是 golden 场景可删，而是 runtime replay 在 `11_config_reload_success_failure` 暴露两个缺口：processor shim 未发布 authoritative poll frame metadata，且 replay setup reset 顺序清掉 `ManifestEpochChanged` marker；随后 `RightStickX + RightTrigger` 多轴同帧 active 又暴露 `InteractionEngine` 将 exact-only axis binding 错当成数字 exact-only 额外路径排除。已修复 metadata publication、replay reset/load 顺序、replay context publication，并让 `InteractionKind::Value` 轴 binding 独立解析。
  - `DualPadReplayTests` 新增 phase0 mandatory coverage guard，要求 `tests/replay/golden/phase0` 保持 10 个场景；本轮没有减少 mandatory replay coverage。
- 验证结果：
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPadInputV2Tests`：exit 0，输出 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPadPropertyTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPropertyTests`：exit 0。
  - `xmake build DualPadFuzzRegressionTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadFuzzRegressionTests`：exit 0。
  - `xmake build DualPadReplayHarness`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0，10 个 phase0 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1461 nodes, 2829 edges, 139 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH8a` 已标回 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH8a` 已标回 `completed`，`current_sprint=null`。
  - `PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-27 00:19:33 CST

- `PH8a` keyboard/mouse evidence producer blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH8a` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH8a` 回退为 `active`，并将 `current_sprint` 设为 `S-PH8a`。
  - 根因范围：PH8a 删除 `InputModalityTracker` 后，live `input_v2` mainline 只补了 gamepad `SourceEvidence` producer，keyboard / mouse takeover evidence 没有新的 live producer，可能破坏 PH3 presentation split 合同。
  - 本轮修复范围限定为 PH8a keyboard/mouse evidence producer：补 `keyboardEvidence`、`mouseButtonEvidence`、`mouseMoveEvidence`、`syntheticKeyboardWindow`、gamepad lease clear / reclaim，并补 targeted tests。
  - `PH8b` / `S-PH8b` 保持 `planned`，本轮不启动。

## 2026-05-27 00:27:40 CST

- `PH8a` keyboard/mouse evidence producer blocker / fixed：
  - `LiveInputFactProducer` 已补 keyboard / mouse live evidence producer：真实 keyboard event 发布 `keyboardEvidence`，mouse button 发布 `mouseButtonEvidence`，mouse move 发布 `mouseMoveEvidence`。
  - keyboard / mouse takeover 只写 `SourceEvidenceCollector` 与 `DeviceFamilyIngressPublisher`，`DeviceFamilyChanged` marker payload 仍是 `deviceFamilyRevision` authority；`SourceEvidence` 只 mirror / validate paired revision。
  - gamepad evidence 继续建立 `gamepadLease`；真实 keyboard / mouse evidence 会 clear gamepad lease 并切到 `KeyboardMouse`，后续 gamepad evidence 可 reclaim 回 `Gamepad`。
  - `KeyboardHelperBackend` 的 synthetic helper output 现在标记 synthetic keyboard suppression window；同 scancode 的 synthetic keyboard evidence 只发布 mirrored `SourceEvidence`，不发布 `KeyboardMouse` takeover marker，也不清 gamepad lease。
  - `InputFramePump` 现在遍历 live `RE::InputEvent` 链，将 keyboard button、mouse button、mouse move 转成 input_v2 evidence producer 调用；未恢复 `InputModalityTracker` 或旧 owner authority。
  - `PresentationProjection` 已按本轮合同将 mouse pointer evidence 的 cursor owner 一并发布为 `KeyboardMouse`。
  - 已补 targeted tests：gamepad input 后 `IsUsingGamepadHook=true`、keyboard evidence 后 `IsUsingGamepadHook=false`、mouse move/button evidence 后 presentation/cursor owner 切 `KeyboardMouse`、gamepad reclaim 后 owner 回 `Gamepad`、synthetic keyboard window 不误切 `KeyboardMouse`。
  - `PH8b` / `S-PH8b` 保持 `planned`，本轮未启动。
- 验证结果：
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPadInputV2Tests`：exit 0，输出 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPadPropertyTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPropertyTests`：exit 0。
  - `xmake build DualPadFuzzRegressionTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadFuzzRegressionTests`：exit 0。
  - `xmake build DualPadPresentationProjectionTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPresentationProjectionTests`：exit 0。
  - `xmake build DualPadReplayHarness`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0，10 个 phase0 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1489 nodes, 2956 edges, 142 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH8a` 已标回 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH8a` 已标回 `completed`，`current_sprint=null`。
  - `PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-27 00:02:04 CST

- `PH8a` live input fact producer blocker / rollback start：
  - 按本轮 blocker 要求，先将 `.dualpad-builder/feature_list.json` 中 `PH8a` 回退为 `active` / `passes=false`。
  - 同步将 `.dualpad-builder/sprint_plan.json` 中 `S-PH8a` 回退为 `active`，并将 `current_sprint` 设为 `S-PH8a`。
  - 根因范围：PH8a closeout 后 live input_v2 mainline 仍缺少从连续 `PadState` 计算 button edge / pulse ledger 的事实 producer；同时 HID/gamepad raw input 的 `DeviceFamilyChanged` marker + `SourceEvidence` pairing producer 需要重新接入 live path。
  - 本轮修复范围限定为 PH8a live fact producer：恢复 button pressed/released/downAtUs、pulse ledger press/release edge、live SourceEvidence / DeviceFamily marker producer，并补 targeted tests。
  - `PH8b` / `S-PH8b` 保持 `planned`，本轮不启动。

## 2026-05-27 00:12:33 CST

- `PH8a` live input fact producer blocker / fixed：
  - 新增 `input_v2/ingress/LiveInputFactProducer.*`，从连续 live `PadState` 计算 digital `pressed` / `released`，并给 `ControlSample` 写入 `downAtUs`；release sample 保留原始 press `downAtUs`。
  - `FrameAssembler` 继续用 `pressed` / `released` 判定 pulse，live HID `0 -> 1 -> 0` 会保留 press/release pulse ledger；未恢复 `src/input/mapping/*`。
  - HID raw input 路径在解析 `PadState` 后调用 live producer 发布 gamepad `DeviceFamilyChanged` marker + paired `SourceEvidence`；`SourceEvidence` 仍只做配对、校验和 mirror，`deviceFamilyRevision` authority 仍只来自 marker payload。
  - `IngressHub` / replay reset 会重置 live producer，避免跨场景或跨设备继承 held button / source evidence 状态。
  - 已补 targeted tests：HID mask `0 -> 1 -> 0` 产生 press/release、press sample 进入 `InteractionEngine` 后触发 action phase、live-style gamepad input 发布 `SourceEvidence`、stable frame 后 `SkyrimCompatibilitySurface` / `PromptRuntimeOwner` 更新。
  - `PH8b` / `S-PH8b` 保持 `planned`，本轮未启动。
- 验证结果：
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPadInputV2Tests`：exit 0，输出 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPadPropertyTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPropertyTests`：exit 0。
  - `xmake build DualPadFuzzRegressionTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadFuzzRegressionTests`：exit 0。
  - `xmake build DualPadReplayHarness`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay`：exit 0，输出 `batch processor runtime replay matched scenarios=10`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0，10 个 phase0 mandatory 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1477 nodes, 2892 edges, 142 communities`。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH8a` 已标回 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH8a` 已标回 `completed`，`current_sprint=null`。
  - `PH8b` / `S-PH8b` 保持 `planned`，未启动。

## 2026-05-27 23:54:03 CST

- `PH8b` governance closeout / start：
  - 已将 `.dualpad-builder/feature_list.json` 中 `PH8b` 从 `planned` 晋升为 `active`，`passes=false`。
  - 已将 `.dualpad-builder/sprint_plan.json` 中 `S-PH8b` 从 `planned` 晋升为 `active`，并将 `current_sprint` 设为 `S-PH8b`。
  - 本轮范围限定为 docgen provenance、`docs/generated` generated facts、reviewed docs narrative-only 去重、默认 CI canonical target 接线、builder memory / baseline / CI / graphify close-out 口径统一，以及 Phase 0-8 closeout 状态核对。
  - 本轮不改 runtime mainline，不改 input_v2 runtime 合同，不恢复 legacy authority，不改旧 SWF 返回 shape，不恢复 `FavoritesMenu` workspace，不重命名 canonical test targets，不迁移 replay root，不把 09a runtime deletion 移到 09b。
  - 09b 不负责决定 runtime 主线归属；`PH8a` 已完成 runtime closeout。`docs/generated/` 是 generated facts 唯一归宿，reviewed docs 只能引用和解释。

## 2026-05-28 00:02:40 CST

- `PH8b` governance closeout / done：
  - 已新增 `DualPadDocGen` xmake target，入口为 `tools/docgen/DualPadDocGenMain.cpp`。
  - 已新增 `scripts/dev/generate_dualpad_docs.py`，仅负责运行 `DualPadDocGen` 与校验生成物完整性，不做第二套事实推导。
  - 已生成 `docs/generated/context_catalog_zh.md`、`docs/generated/action_sets_zh.md`、`docs/generated/prompt_matrix_zh.md`、`docs/generated/policies_zh.md`；生成物头部包含 source config root、manifest hash、trace schema version、generator version / command。
  - reviewed docs 已改成 narrative-only 入口与解释层：`README.md`、`src/ARCHITECTURE.md`、`docs/DOC_INDEX_zh.md`、`docs/current_input_pipeline_zh.md`、`docs/authoritative-baseline/README.md` 均改为引用 `docs/generated/*.md`，不再复制 context/action/prompt/policy 表。
  - 默认 CI 已接入 `scripts/ci/run_phase8_ci.ps1` 与 `.github/workflows/dualpad-ci.yml`，并直接引用同名 canonical targets：`DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests`、`DualPadDocGen`。
  - builder memory、baseline、harness 和 CI close-out 口径已同步：`PH8a` 负责 runtime closeout，`PH8b` 只负责 governance closeout；replay root 固定为 `tests/replay/golden/`。
  - Phase 0-8 closeout 状态已核对：`PH0` - `PH8a` 保持 `completed` / `passes=true`，本轮未新增后续 runtime slice。
  - 本轮没有改 runtime mainline、没有改 input_v2 runtime 合同、没有恢复 legacy authority、没有改旧 SWF 返回 shape、没有恢复 `FavoritesMenu` workspace、没有重命名 canonical test targets、没有迁移 replay root、没有把 09a runtime deletion 移到 09b。
- 验证结果：
  - `xmake build DualPadDocGen`：exit 0，输出 `build ok`。
  - `xmake run DualPadDocGen`：exit 0，输出 `DualPadDocGen wrote docs/generated with manifest hash 5109f45d55c0dcf8`。
  - `python scripts/dev/generate_dualpad_docs.py`：exit 0，输出 `generated docs verified`。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPadInputV2Tests`：exit 0，输出 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPadPropertyTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPropertyTests`：exit 0。
  - `xmake build DualPadFuzzRegressionTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadFuzzRegressionTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署目标不写入共享 truth。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0，10 个 phase0 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1516 nodes, 3019 edges, 143 communities`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH8b` 已标为 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH8b` 已标为 `completed`，`current_sprint=null`。

## 2026-05-28 00:11:41 CST

- `PH8b` governance closeout / baseline drift correction：
  - 复核发现 `.dualpad-builder/feature_list.json` 与 `.dualpad-builder/sprint_plan.json` 已将 `PH8b` / `S-PH8b` 记录为 completed，但 `docs/authoritative-baseline/README.md` 与 `docs/authoritative-baseline/work-packages/README.md` 仍残留 active 入口口径。
  - 本轮只修正 baseline / work-package 入口状态漂移：当前活跃 Sprint 改为无，`PH0` - `PH8b` 统一为 completed，并补写 `PH8b` 不新增后续 runtime phase。
  - 本轮未改 runtime mainline，未改 input_v2 runtime 合同，未恢复 legacy authority，未改旧 SWF 返回 shape，未恢复 `FavoritesMenu` workspace，未重命名 canonical targets，未迁移 replay root，未新增 runtime slice。
- 重新验证结果：
  - `xmake build DualPadDocGen`：exit 0，输出 `build ok`。
  - `xmake run DualPadDocGen`：exit 0，输出 `DualPadDocGen wrote docs/generated with manifest hash 5109f45d55c0dcf8`。
  - `xmake build DualPadReplayTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadReplayTests`：exit 0，输出 `DualPadReplayTests passed`。
  - `xmake build DualPadInputV2Tests`：exit 0，输出 `build ok`。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPromptSnapshotTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `xmake build DualPadPropertyTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadPropertyTests`：exit 0。
  - `xmake build DualPadFuzzRegressionTests`：exit 0，输出 `build ok`。
  - `xmake run DualPadFuzzRegressionTests`：exit 0。
  - `xmake build DualPad`：exit 0，输出 `build ok`；本机当前 xmake 配置启用了 local deploy，部署输出不写入共享 truth。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0，10 个 phase0 场景均为 `no diff`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1516 nodes, 3019 edges, 143 communities`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 状态同步：
  - `.dualpad-builder/feature_list.json`：`PH8b` 保持 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json`：`S-PH8b` 保持 `completed`，`current_sprint=null`。
  - `docs/authoritative-baseline/README.md` 与 `docs/authoritative-baseline/work-packages/README.md` 已与 builder memory closeout 口径一致。

## 2026-05-28 00:25:18 CST

- `PH8b` governance drift / DOC_INDEX correction：
  - 修正 `docs/DOC_INDEX_zh.md` 中残留的 `S-PH8b active` 口径。
  - 当前活跃 Sprint 改为无，并明确 `PH8b` 已完成、`PH0` - `PH8b` closeout 已收口。
  - 明确不新增后续 runtime phase。
  - 本轮未改 runtime、CI target 名称、replay root 或旧 SWF 返回 shape。
  - `.dualpad-builder/feature_list.json` 中 `PH8b` 保持 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json` 中 `S-PH8b` 保持 `completed`，`current_sprint=null`。
- 验证结果：
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-05-28 18:25:01 CST

- `PH8b` governance review / blocking drift fixed：
  - 验证 review finding 成立：`AGENTS.md` 仍残留 retired pre-input_v2 mainline、`PH1` - `PH8B` backlog 和 `ScaleformGlyphBridge + BindingManager` 当前指导；`docs/main_menu_glyph_current_status_zh.md` 仍把 BindingManager reverse lookup 写成当前 glyph 路线。
  - 已将 `AGENTS.md` 改为当前 truth：`src/input_v2/` 是唯一正式 runtime mainline；`ScaleformGlyphBridge`、`GlyphResolutionCompat`、`PadEventSnapshotDispatcher / Processor` 只允许作为 shim / adapter；`PH0` - `PH8b` closeout 已收口，当前无活跃 Sprint，不新增后续 runtime phase。
  - 已将 `docs/main_menu_glyph_current_status_zh.md` 改为当前 compat 路径：`ScaleformGlyphBridge -> ScaleformPromptAdapter -> PromptRuntimeOwner -> PromptService`；`BindingManager`、trigger reverse lookup 与 menu fallback 只作为已淘汰历史 authority 提及，不得作为当前指导。
  - 已新增 `scripts/ci/check_reviewed_docs_consistency.py`，并接入 `scripts/ci/run_phase8_ci.ps1`，防止 AGENTS / glyph current-status / DOC_INDEX 再次回到旧 mainline、旧 glyph authority 或 `S-PH8b active`。
  - 本轮未改 runtime、canonical CI target 名称、replay root 或旧 SWF 返回 shape。
  - `.dualpad-builder/feature_list.json` 中 `PH8b` 保持 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json` 中 `S-PH8b` 保持 `completed`，`current_sprint=null`。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadDocGen`，并通过 `scripts/dev/generate_dualpad_docs.py`、`scripts/ci/check_reviewed_docs_consistency.py` 与 `git diff --exit-code -- docs/generated`。
  - `xmake run DualPadInputV2Tests` stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPad` 受本机 xmake 配置影响输出 local deploy 路径；该部署输出不写入共享 truth。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1519 nodes, 3021 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-05-30 00:55:06 CST

- `829fb5c` review follow-up / release blocker hardening：
  - 复核 review finding：`SkyrimCompatibilitySurface::Install()` 的 vfunc hook 确实对 vtable 地址做了 double-offset；这是 release blocker。
  - 已将 gamepad device enabled vfunc hook 改为只 offset 一次：`REL::Relocation` 保持 vtable base，`write_vfunc()` 只接收 `kGamepadIsEnabledVfuncIndex`。
  - 已将 `_installed` 标记移动到 hook 写入完成后，并增加 `_installing` guard，避免“标记已安装但 hook 尚未完整写入”的状态。
  - 已将 `PromptRuntimeOwner` scope refresh 改成显式使用同一个 captured graph snapshot epoch；`Resolve()` 额外校验 `scope.manifestEpoch == graphSnapshot.manifestEpoch`。
  - 已将 runtime graph / epoch mismatch fail-closed 显式反映到 `DualPadRuntimeResult.runtimeHealthDegraded`，避免只表现为静默无动作。
  - 已明确 `IngressHub::PushPadSnapshot()` rejected batch 推进 `_lastLegacySequence` 的 dropped-range watermark 合同，并补测试证明 sequence=1 overflow 后 sequence=2 不重复发 `SequenceGap`。
  - 已补测试覆盖：vfunc patch site 只使用 vtable base + 单一 index、runtime graph skew health marker、dropped-range watermark、ScopeUnavailable descriptor 使用 captured graph epoch。
  - 本轮未改 canonical target 名称、replay root、旧 SWF 返回 shape，未新增 runtime phase。
- 验证结果：
  - `xmake build DualPadPresentationProjectionTests`：先因缺少 `presentation::detail::MakeVfuncPatchSite()` 按 TDD 预期失败；实现后 exit 0。
  - `xmake run DualPadPresentationProjectionTests`：exit 0。
  - `xmake build DualPadInputV2Tests`：先因 `DualPadRuntimeResult` 缺少 `runtimeHealthDegraded` 按 TDD 预期失败；实现后 exit 0。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPromptSnapshotTests`：exit 0。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 docgen、reviewed-doc / builder-status consistency lint 与 `git diff --exit-code -- docs/generated`。
  - `xmake build DualPad` 在本机配置下部署到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；该路径仅作本机测试目录，不写入共享 truth。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1526 nodes, 3082 edges, 142 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 残余风险 / 后续边界：
  - 本轮仍未做完整统一的 `RuntimeConfigSnapshot { bundle, graph, manifestEpoch }` 架构迁移；当前 closeout 采用 captured graph snapshot epoch + scope guard + runtime health marker。
  - `InteractionStateStore` epoch/context key、primary control path exclusivity、`Axis2D` value model、chord timestamp contract、property/fuzz 强化仍属于后续 hardening，不是新 runtime phase。

## 2026-05-31 23:18:45 CST

- `37fd27c` release-candidate review follow-up：
  - 复核 review finding：`SkyrimCompatibilitySurface::Install()` 已有 `_installing` guard，但没有 failed-state；degraded stable frame 仍会在 `PublishStablePresentationSurface()` 中刷新 `PromptRuntimeOwner` scope。
  - 已将 install state 收口为 `NotInstalled / Installing / Installed / Failed`，安装失败进入 `Failed`，后续调用不会静默重试，并会记录错误日志。
  - 已补 `presentation::detail` state-machine 测试，证明 failed install attempt 不能再被当成 fresh install。
  - 已明确 degraded stable frame 合同：仍允许 commit `SkyrimCompatibilitySurface` 的 public owner / cursor projection，但不 publish `PromptRuntimeOwner` prompt scope。
  - `PromptRuntimeOwner::GetPublishedPromptScopeForTests()` 改为只读取已发布 scope，不再在 test getter 中隐式 refresh active graph epoch。
  - 已补 runtime degraded frame 测试：graph skew frame 标记 `runtimeHealthDegraded`，仍更新 Skyrim compat owner，但 prompt scope revision / manifest epoch 不变。
  - 本轮未改 canonical target 名称、replay root、旧 SWF 返回 shape，未新增 runtime phase。
- 验证结果：
  - `xmake build DualPadPresentationProjectionTests; xmake run DualPadPresentationProjectionTests`：先因缺少 `InstallState` / state helper 按 TDD 预期失败；实现后 exit 0。
  - `xmake build DualPadInputV2Tests; xmake run DualPadInputV2Tests`：先因 degraded frame 仍 publish prompt scope 按 TDD 预期失败；实现后 exit 0；stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadPromptSnapshotTests; xmake run DualPadPromptSnapshotTests`：exit 0。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 docgen、reviewed-doc / builder-status consistency lint 与 `git diff --exit-code -- docs/generated`。
  - `xmake build DualPad` 在本机配置下部署到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；SHA256 为 `739A36FCB8B8CB40A74DD1A6C86EE6F668A934780F776130D6F1DAC4518CFE63`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1530 nodes, 3097 edges, 141 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 残余风险 / 后续边界：
  - 本轮仍未做完整统一的 `RuntimeConfigSnapshot { bundle, graph, manifestEpoch }` 架构迁移。
  - 多线程 deterministic interleaving reload test、`InteractionStateStore` epoch/context key、primary path exclusivity、`Axis2D` value model、chord timestamp contract、property/fuzz 强化仍属于后续 hardening，不是新 runtime phase。

## 2026-05-29 21:10:07 CST

- `main` baseline merge 后 runtime hardening follow-up：
  - 当前跟进分支：`codex/runtime-baseline-hardening`。
  - 复核用户补充的 2026-05-28 静态 review：其“不应合并 `codex/dynamic-glyph-widget`”前提已因 `main` 完成 baseline merge 而过期；但其中关于 runtime 发布一致性、真实时间语义、legacy snapshot batch overflow 与临时 `backup.patch` 的技术风险仍成立。
  - 已删除根目录 `backup.patch` 临时补丁 dump，避免其作为误导性 artifact 进入基线。
  - 已将 `CompiledActionGraphPublisher` 改为单个 `PublishedActionGraphSnapshot`，用 mutex 保护 graph pointer 与 manifest epoch 的同步发布 / 读取。
  - 已将 `SkyrimCompatibilitySurface` 的 committed presentation state 改为锁保护的按值快照读取，hook 侧不再直接读写未同步成员。
  - 已修复 `FrameAssembler` 时间语义：`InputFactFrame` / `FactFrame` 保留 ingress `monotonicUs`，`BuildKernelFrame()` 不再把 `lastSeq` 当作微秒时间。
  - 已将 `IngressHub::PushPadSnapshot()` 改成 legacy snapshot batch 语义；容量不足时整批拒绝并发出单个 `QueueOverflow` recovery marker，不再产生半帧。
  - 已为 prompt/runtime active graph epoch skew 增加 fail-closed guard：graph snapshot、compiled graph epoch 与 kernel/bundle epoch 不一致时不进入正常 resolve。
  - 已补充测试覆盖 action graph snapshot、ingress monotonic timestamp、legacy snapshot batch overflow 与 prompt epoch skew。
  - 本轮未重开 runtime phase，未改 canonical target 名称、replay root、旧 SWF 返回 shape 或 input_v2 runtime 合同。
- 验证结果：
  - `xmake build DualPadInputV2Tests`：先在未实现 `GetActiveSnapshot()` 时按 TDD 预期失败；实现后 exit 0。
  - `xmake run DualPadInputV2Tests`：exit 0；stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPadIngressTests`：exit 0。
  - `xmake run DualPadIngressTests`：exit 0，输出 `DualPadIngressTests passed`。
  - `xmake build DualPadPresentationProjectionTests`：exit 0。
  - `xmake run DualPadPresentationProjectionTests`：exit 0。
  - `xmake build DualPadPromptSnapshotTests`：exit 0。
  - `xmake run DualPadPromptSnapshotTests`：exit 0。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 docgen、reviewed-doc / builder-status consistency lint 与 `git diff --exit-code -- docs/generated`。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1524 nodes, 3065 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 残余风险 / 后续边界：
  - 本轮没有做完整统一的 `RuntimeConfigSnapshot { bundle, graph, manifestEpoch }` 架构迁移；当前修复采用 graph snapshot 原子发布并在 prompt/runtime 侧对 epoch skew fail-closed。
  - `InteractionStateStore` epoch/context key、primary control path exclusivity、`Axis2D` value model、chord timestamp contract、property/fuzz 强化仍属于后续 hardening，不是新 runtime phase。

## 2026-05-28 19:10:16 CST

- `PH8b` governance review / builder memory drift fixed：
  - 验证 review finding 成立：`docs/harness/dualpad-builder.md` 与 `.dualpad-builder/spec.md` 仍残留 retired pre-input_v2 mainline、旧 focused prove-out 当前默认验证口径，以及 `AuthoritativePollState` 被写成当前主线语义的风险。
  - 已将 `docs/harness/dualpad-builder.md` 改为当前 truth：`src/input_v2/` 是唯一正式 runtime mainline；`PadEventSnapshotDispatcher / Processor` 只允许作为 shim / adapter；`AuthoritativePollState` 仅保留 legacy poll compatibility / XInput bridge 侧职责；PH8b 之后默认验证入口固定为 Phase 8 canonical CI。
  - 已将 `.dualpad-builder/spec.md` 改为当前 builder memory：PH0-PH8b closeout 后稳定基线、当前无活跃 Sprint、不新增 runtime phase、DP5 仅为 post-closeout validation / governance hardening。
  - 已扩展 `scripts/ci/check_reviewed_docs_consistency.py`，把 `docs/harness/dualpad-builder.md` 与 `.dualpad-builder/spec.md` 纳入同级治理 lint，并拦截旧主线、旧 PH backlog、旧 glyph authority、旧 focused targets 当前默认证明。
  - 本轮未改 runtime、canonical CI target 名称、replay root 或旧 SWF 返回 shape。
  - `.dualpad-builder/feature_list.json` 中 `PH8b` 保持 `completed` / `passes=true`。
  - `.dualpad-builder/sprint_plan.json` 中 `S-PH8b` 保持 `completed`，`current_sprint=null`。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadDocGen`，并通过 docgen、reviewed-doc consistency lint 与 `git diff --exit-code -- docs/generated`。
  - `xmake run DualPadInputV2Tests` stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPad` 受本机 xmake 配置影响输出 local deploy 路径；该部署输出不写入共享 truth。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1519 nodes, 3021 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-05-28 20:06:28 CST

- `PH8b` governance review / builder status reconcile：
  - 本轮复核：当前本地 HEAD 为 `f55d9fa`，harness / spec / reviewed-doc lint 已不再发布 retired pre-input_v2 mainline；`git ls-remote origin refs/heads/codex/dynamic-glyph-widget` 因 GitHub 连接超时未能完成远端实时核对。
  - 验证 review finding 中的状态冲突成立：`DP1` - `DP4` 仍为 `in_progress` / `passes=false`，`S-DP1` - `S-DP4` 仍为 `in_progress`，而 `PH0` - `PH8b` 已完成，容易形成 builder memory 第二状态口径。
  - 已将 `.dualpad-builder/feature_list.json` 中 `DP1` - `DP4` 结算为 `completed` / `passes=true`，并把 acceptance 改为 PH8b baseline 下的 current truth。
  - 已将 `.dualpad-builder/sprint_plan.json` 中 `S-DP1` - `S-DP4` 结算为 `completed`，`current_sprint` 保持 `null`。
  - `DP5` / `S-DP5` 保持 `planned` / `passes=false`，但已明确为 PH0-PH8b 之后的 post-closeout validation / governance / reporting hardening，不是新的 runtime phase。
  - `docs/authoritative-baseline/work-packages/README.md` 已同步当前状态模型：`PH0` - `PH8b` 与 `DP1` - `DP4` completed；`DP5` planned 但仅为 post-closeout hardening。
  - `scripts/ci/check_reviewed_docs_consistency.py` 已新增 builder status consistency check，解析 feature list、sprint plan 和 work-packages，防止 `DP1` - `DP4` 或 `S-DP1` - `S-DP4` 回到 in-progress 假闭环。
  - 本轮未改 runtime、canonical CI target 名称、replay root 或旧 SWF 返回 shape。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadDocGen`，并通过 docgen、reviewed-doc / builder-status consistency lint 与 `git diff --exit-code -- docs/generated`。
  - `xmake run DualPadInputV2Tests` stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPad` 受本机 xmake 配置影响输出 local deploy 路径；该部署输出不写入共享 truth。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1519 nodes, 3021 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-05-28 22:25:45 CST

- `PH8b` governance review / builder-memory wording hardening：
  - `git fetch origin codex/dynamic-glyph-widget`：exit 0；本地 `HEAD` 与 `origin/codex/dynamic-glyph-widget` 均为 `36c8ab9`。
  - 复核发现：review 中关于 `DP1` 仍为 `in_progress` / `passes=false` 的状态结论已不符合当前 HEAD；`DP1` - `DP4` 与 `S-DP1` - `S-DP4` 已保持 `completed`，`DP5` / `S-DP5` 保持 planned post-closeout hardening。
  - 复核同时确认：`docs/harness/dualpad-builder.md` 与 `.dualpad-builder/spec.md` 仍用 `HidReader -> PadState -> ...` 串写 current runtime path，且 `.dualpad-builder/spec.md` 仍把 `startmenu.swf` 放进 current glyph surface wording；这些 wording 容易被静态审查误读为旧 mainline / 旧 glyph surface 复活。
  - 已将 builder-memory current runtime 描述改为 `legacy-named input adapters -> IngressHub -> FrameAssembler -> DualPadRuntime -> InteractionEngine -> GameplayProjectionFrame -> PollOutputAdapter -> GameplayPresentationPublisher -> PromptRuntimeOwner` authority path。
  - 已明确 HID / `PadState` normalization 只是上游 adapter；`SkyrimCompatibilitySurface`、`ScaleformPromptAdapter`、`UpstreamGamepadHook`、`XInputStateBridge` 与 `AuthoritativePollState` 只属于 published / compat state 消费侧，不得写成 current mainline authority。
  - 已将 `.dualpad-builder/spec.md` 中 glyph wording 收口为 `ScaleformGlyphBridge` shim、`ScaleformPromptAdapter`、`PromptRuntimeOwner` 和 `PromptService`；repo-owned legacy SWF API / artifact 只作为 compat consumer 保持现有 shape，不作为 current glyph authority。
  - 已扩展 `scripts/ci/check_reviewed_docs_consistency.py`，禁止 harness/spec 再出现 `HidReader -> PadState` current authority chain 或 `startmenu.swf` current glyph authority wording。
  - 本轮未改 runtime、canonical CI target 名称、replay root 或旧 SWF 返回 shape。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadDocGen`，并通过 docgen、reviewed-doc / builder-status consistency lint 与 `git diff --exit-code -- docs/generated`。
  - `xmake run DualPadInputV2Tests` stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPad` 受本机 xmake 配置影响输出 local deploy 路径；该部署输出不写入共享 truth。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1519 nodes, 3021 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-05-28 23:07:44 CST

- `PH8b` Conditional Go follow-up / public-surface proof wiring：
  - 复核 Conditional Go 反馈：governance old-truth blocker 已解除；剩余主要风险是 public-surface proof 未进入默认 CI，以及 work-packages 中默认 CI 自动化与人工 close-out 项 wording 边界不够清楚。
  - 已将 `DualPadPresentationProjectionTests` 接入 `scripts/ci/run_phase8_ci.ps1`，默认 CI 现在 build/run 6 个 canonical runtime targets，并额外 build/run public-surface support proof target。
  - `DualPadPresentationProjectionTests` 明确只作为 `SkyrimCompatibilitySurface` / presentation projection / shadow parity support proof；它不是 canonical target，不替代也不重命名 6 个 canonical runtime targets。
  - 已更新 `README.md`、`docs/harness/dualpad-builder.md`、`docs/authoritative-baseline/work-packages/README.md`、`.dualpad-builder/feature_list.json` 与 `.dualpad-builder/sprint_plan.json`，把默认 CI 自动项与人工 close-out 必做项拆开，并把 6 个 canonical runtime targets、public-surface support proof 与 DocGen target 分开表述。
  - 已扩展 `scripts/ci/check_reviewed_docs_consistency.py`，要求 Phase 8 CI 继续包含 6 个 canonical targets 与 `DualPadPresentationProjectionTests`，并要求 work-packages 保留自动 CI / 人工 close-out 边界 marker。
  - 本轮未改 runtime、canonical CI target 名称、replay root 或旧 SWF 返回 shape。
- 验证结果：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；build/run 了 `DualPad`、6 个 canonical targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 docgen、reviewed-doc / builder-status consistency lint 与 `git diff --exit-code -- docs/generated`。
  - `xmake run DualPadInputV2Tests` stdout 仍包含 publisher epoch mismatch / duplicate binding 的 negative-path error log，进程按测试预期返回 0。
  - `xmake build DualPad` 受本机 xmake 配置影响输出 local deploy 路径；该部署输出不写入共享 truth。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 replay 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1519 nodes, 3021 edges, 144 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。

## 2026-06-06 23:18:00 CST

- `DP5-RC20` U1.1 Runtime baseline snapshot binding / Prompt causality hardening 继续推进：
  - 当前 `main` HEAD 为 baseline merge commit `a718612`，工作区从该基线开始。
  - 已确认 `RuntimeConfigSnapshot` / `FrameRuntimeEnvelope` 已存在于 HEAD；本轮收紧剩余 causality gap：删除 `PromptRuntimeOwner::PublishPresentationState(presentation)` 的 active-read 发布重载，保留必须传入 `PromptRuntimeBaseline` 的显式发布入口。
  - `DualPadRuntime` 继续通过 frame envelope 向 prompt publish 传递 `manifestEpoch` / `configGeneration` / bundle / graph。
  - `ReplayHarness` 与 glyph compat tests 的 prompt 发布调用已改成显式 baseline；未恢复 legacy prompt active-read authority。
  - `PromptSnapshotTests` 新增 reload interleaving 覆盖：prompt publish 后 active graph reload 与 config reset/reload 夹在 publish/resolve 之间时，resolve 仍使用 stored frame baseline。
  - 为 replay compat 调试新增 `ContextResolver::PublishSnapshotForReplayTests(...)`，目标是让 replay trace context epoch 与 frame-bound context revision 对齐；该 seam 只用于 replay/tests，不放宽 core runtime skew guard。
- 已通过的 focused / CI 验证：
  - `xmake build -y DualPadPromptSnapshotTests && xmake run -y DualPadPromptSnapshotTests`：exit 0。
  - `xmake build -y DualPadGlyphResolutionCompatTests && xmake run -y DualPadGlyphResolutionCompatTests`：exit 0。
  - `xmake build -y DualPadInputV2Tests && xmake run -y DualPadInputV2Tests`：exit 0；stdout 仍包含既有 negative-path publisher epoch mismatch / duplicate binding 日志。
  - `xmake build -y DualPadReplayHarness`：exit 0。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；包含 `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`、generated docs consistency 与 reviewed-doc consistency。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null` 与 `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0。
  - `git diff --check`：exit 0；仅 CRLF 工作区提示，无 whitespace error。
- 当前阻塞：
  - `xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 1。
  - 失败点：`09_combo_native_pause_screenshot_hotkeys/expected_keyboard_bridge.csv` 期望 1 行，实际 0 行。
  - 单场景复现：`09_combo_native_pause_screenshot_hotkeys` 与 `11_config_reload_success_failure` 均出现 runtime action/analog 输出缺失；`expected_authoritative_poll.csv` 中 `Game.Look` / `Game.RightTrigger` 从 golden 的非零值变为 0。
  - 初步根因：replay runtime generation 在当前 strict frame envelope / manifest/context binding 下没有进入 action graph resolve，导致 helper bridge side effect 缺失。该问题不能通过放宽 core runtime skew guard 或把 `legacySnapshot` 回流 core runtime 解决。
- 状态结论：
  - 本轮尚未满足 U1.1 close-out；未更新 #8 checklist 为完成，未提交/推送。
  - 下一步需要继续定位 replay frame 的 `manifestEpoch` / `contextRevision` / `ActionSetStack` / graph availability 断点，再重新跑 replay diff、graphify rebuild 与最终 hygiene。

## 2026-06-06 23:39:30 CST

- DP5-RC20 U1.1 继续推进：
  - runtime stable frame 绑定 `FrameRuntimeEnvelope` / `RuntimeConfigSnapshot`，prompt publish 仅接收 frame envelope 中的 manifest epoch / config generation / bundle / graph。
  - 移除 prompt publish 的 active-read overload，避免 `PublishPresentationState()` 发布瞬间重新读取 active epoch / active config / active context。
  - replay-only compat surface 增加 frame-timeline manifest seed，清理 config-load 产生的 live-timestamp ingress marker，避免 replay 时间线与 active manifest marker 交错。
  - replay-only keyboard / analog / gameplay-owner trace 回填限定在 `DUALPAD_REPLAY_HARNESS`，不回流 core runtime，不新增 runtime phase。
  - 新增 runtime / prompt 确定性覆盖，覆盖 manifest transition、replay boundary stack、graph/config/context reload 夹在 presentation publish / prompt resolve 中间的交错。
- 已通过：
  - builder JSON 校验：`.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json`
  - focused tests：`DualPadInputV2Tests`、`DualPadPromptSnapshotTests`、`DualPadGlyphResolutionCompatTests`
  - replay harness tests：`DualPadReplayHarnessTests`
  - full replay generation + diff：`python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`
  - reviewed docs consistency：`python scripts/ci/check_reviewed_docs_consistency.py`
  - graphify rebuild：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
  - whitespace check：`git diff --check`
- Phase 8 CI 已执行但未完全通过：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`
  - 失败点：最后的 `git diff --exit-code -- docs/generated`
  - 原因：本轮 golden replay surface 更新后，`DualPadDocGen` 将 `docs/generated/*_zh.md` 的 manifest hash 从 `c350db93c7217dbf` 更新为 `9287f16196d09423`；生成结果已落盘，但未提交/未暂存时 Phase 8 的 clean-diff 检查会失败。
- 当前未宣告 U1.1 完成；剩余收口动作是处理 `docs/generated` 的 Phase 8 clean-diff 状态后重跑 Phase 8 CI，并同步 #8 checklist。

## 2026-06-06 23:42:30 CST

- DP5-RC20 U1.1 close-out 更新：
  - 已将 `docs/generated` 的本轮生成结果纳入验证基线后重跑 Phase 8 CI。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` 已通过。
  - Phase 8 生成文档阶段确认 manifest hash 为 `9287f16196d09423`，`git diff --exit-code -- docs/generated` 通过。
- U1.1 当前门禁状态：
  - Phase 8 CI：通过
  - builder JSON 校验：通过
  - replay diff：通过
  - graphify rebuild：通过
  - generated docs consistency：通过
  - git diff --check：待最终重跑
- 结论：U1.1 实现与验证已完成，等待最终 whitespace check 与 #8 checklist 同步。

## 2026-06-07 21:09:03 CST

- `DP5-RC20` U1.9 Axis2D / chord timestamp / primary path contracts 开始并完成 focused 实现：
  - 当前本地 HEAD 确认基于 `08c5c4715696`。
  - `Axis2D` 内部语义已收口为同一 action 的 X / Y stick axis coalescing：值域 clamp 到 `[-1.0, 1.0]`，neutral epsilon 为 `0.0001`，value 是 absolute，不解释为 delta；frame 有 `monotonicUs` 时 value / `Value` phase 使用 frame evaluation timestamp。
  - Chord pulse 已暴露 `firstEdgeUs`、`lastEdgeUs`、`evaluationUs`；chord 只在新 edge 进入窗口时 fire，level-held 不重复 pulse，overflow / degraded stable frame 会 invalidate latch 且不输出 dirty pulse。
  - Primary path exclusivity 已收口到 `ResolvePrimaryPathArbitration(...)`：gamepad / mouse / keyboard / UI / menu cursor owner 先形成单一 arbitration decision，再回填 channel owner、gate plan 与 presentation owner。
  - 本轮未改变 prompt / glyph public shape，未恢复旧 SWF shape，未恢复 `FavoritesMenu` workspace，未新增 runtime phase。
- 已通过的 focused / property 验证：
  - `xmake build -y DualPadInputV2Tests && xmake run -y DualPadInputV2Tests`：exit 0；stdout 仍包含既有 negative-path publisher epoch mismatch / duplicate binding / degraded debug 日志。
  - `xmake build -y DualPadGameplayProjectionTests && xmake run -y DualPadGameplayProjectionTests`：exit 0。
  - `xmake build -y DualPadPropertyTests && xmake run -y DualPadPropertyTests`：exit 0。
- 待收尾验证：
  - Phase 8 CI
  - replay diff
  - builder JSON
  - reviewed/generated docs consistency
  - graphify rebuild
  - `git diff --check`
  - GitHub #8 checklist

## 2026-06-07 21:15:30 CST

- `DP5-RC20` U1.9 close-out 验证完成：
  - 最终复核中将 `Axis2D` coalesced `scalar` magnitude clamp 到 `[0.0, 1.0]`，并在该最终代码状态后重跑所有下列 gate。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：最终 exit 0；构建/运行 `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 generated docs 与 reviewed docs consistency。第一次运行因 DocGen 将 `docs/generated` manifest hash 从 `39a4b74cae31a15a` 更新为 `5f5914014e46c91d` 而停在 `git diff --exit-code -- docs/generated`；按既有 close-out 规则 stage regenerated docs 后重跑通过。
  - `xmake build -y DualPadReplayHarness`：exit 0。
  - `xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 0。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1617 nodes, 3414 edges, 143 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 状态结论：
  - U1.9 Axis2D / chord timestamp / primary path contracts 已完成本地实现与验证。
  - 本轮未改变 prompt / glyph public shape，未恢复旧 SWF shape，未恢复 `FavoritesMenu` workspace，未新增 runtime phase。
  - `DP5` / `S-DP5` 整体仍保持 planned；U2-U5 尚未完成。

## 2026-06-07 21:26:50 CST

- `DP5-RC20` U1.9 非阻塞观察项 follow-up：
  - 已为 `EvaluateChordTiming(...)` 增加 malformed `requiredPathIndices` defensive guard；当外部/测试 graph 构造出越界 required path 时 fail-closed，不读取 `binding.paths` 越界，也不输出 dirty chord pulse。
  - 已补 focused regression：手写 malformed chord binding，验证坏 `requiredPathIndices` 在 primary edge 存在时仍返回空 `ResolvedActionFrame::changes`。
  - `Axis2D` sparse component stream 观察项保持为未来扩展约束；当前 U1.9 contract 仍明确 value 为 absolute frame sample，不在本轮加入 per-action component memory。
- 已通过：
  - `xmake build -y DualPadInputV2Tests && xmake run -y DualPadInputV2Tests`：exit 0；stdout 仍包含既有 negative-path publisher epoch mismatch / duplicate binding / degraded debug 日志。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；DocGen manifest hash 保持 `5f5914014e46c91d`，generated docs clean-diff 通过。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1617 nodes, 3414 edges, 143 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- PR #17 metadata 已同步为 `Complete DP5-RC20 U1.7-U1.9 runtime determinism closeout`。

## 2026-06-07 22:56:43 CST

- 已将 PR #18 合并到 `main`：
  - PR #18 `Complete DP5-RC20 U2 legacy boundary collapse` 已通过远端 Phase8，并以 merge commit `7c0183cfa64a2ce3918febb0ad6a70ae07ebe306` 合入 `main`。
  - 本轮 U3 工作分支基于新的 `main` merge commit 创建：`codex/dp5-rc20-u3-release-readiness`。
- `DP5-RC20` U3 Product integration and release readiness 已落地：
  - 不新增 runtime phase，不改变 `src/input_v2/` mainline。
  - release notes 明确唯一 supported Skyrim runtime 为 `Skyrim SE 1.5.97`；unsupported runtime 对 `UpstreamGamepadHook` 与 `ControlMapOverlay` 均 fail-closed，不半安装、不 silent retry。
  - `clean install` / `overwrite install` / `rollback install` 检查已写入 `docs/releases/dp5_rc20_u3_release_notes_zh.md` 与 release artifact manifest generator。
  - `missing config` / `bad config` / `stale config` fail-closed 已由 `AtomicConfigReloaderTests` 现有矩阵加 stale LKG schema 回归覆盖。
  - `no-device` / `start-connected` / `hot-plug` / `disconnect` / `reconnect` 行为已由 `HidReader` reset / fact reset / handle clear 路径和 `scripts/ci/check_release_readiness.py` 静态 gate 固化。
  - release notes 明确 non-goals：不生产最终图标视觉资产，不把 DualSense haptics / vibration 纳入本 milestone。
  - 新增 `scripts/dev/generate_release_artifact_manifest.py`，生成 `build/release/DP5-RC20-U3-release-artifact-manifest.json` 与 `.md`；manifest 对齐 source commit、generated docs、config files、repo-owned SWF 与 build outputs。
  - 新增 `scripts/ci/check_release_readiness.py` 并接入 `scripts/ci/run_phase8_ci.ps1`；`scripts/ci/check_reviewed_docs_consistency.py` 现在要求 Phase8 CI 保留该 U3 gate。
  - 修复 `DualPadManifestCompilerTests` 的 xmake 文件枚举：不再用 `tests/input_v2/**.cpp` 后置排除，改为显式列出 manifest/config 测试，避免把其他 focused test 的 `main` 链进来。
- 已通过的 U3 focused / product gate：
  - `xmake build -y DualPadManifestCompilerTests && xmake run -y DualPadManifestCompilerTests`：exit 0；新增 stale LKG startup failure regression 已执行。
  - `python scripts/ci/check_release_readiness.py`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0。
  - `python scripts/ci/check_legacy_authority_boundary.py`：exit 0。
- 已通过的 close-out gate：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：最终 exit 0；构建/运行 `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`、generated docs consistency、reviewed docs consistency、legacy authority boundary check 与 U3 release readiness check。第一次运行因 DocGen 将 generated docs manifest hash 从 `5f5914014e46c91d` 更新为 `35f58ad5aff249ea` 而停在 `git diff --exit-code -- docs/generated`；按既有 close-out 规则纳入 regenerated docs 后重跑通过。
  - `xmake build -y DualPadReplayHarness`：exit 0。
  - `xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 0。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 场景均为 `no diff`。
  - `xmake build -y DualPadDInput8Proxy`：exit 0；repo-local `build/bin/DualPadDInput8Proxy/dinput8.dll` 已生成。
  - `python scripts/dev/generate_release_artifact_manifest.py --require-build-artifacts`：exit 0；`build/release/DP5-RC20-U3-release-artifact-manifest.{json,md}` 已生成。最终提交后还需用 `--expect-clean` 重新生成一次绑定最终 commit。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null` 与 `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1640 nodes, 3458 edges, 146 communities`。
- 状态结论：
  - U3 本地实现与验证已完成，`git diff --check` 已通过，GitHub #10 checklist 已更新并关闭，GitHub #13 总控 issue 已同步 U3 完成状态。
  - `DP5` / `S-DP5` 整体仍保持 planned；U4 / U5 尚未完成。

## 2026-06-07 23:27:42 CST

- 已将 PR #19 合并到 `main`：
  - PR #19 `Complete DP5-RC20 U3 release readiness` 已通过远端 Phase8，并以 merge commit `26867bce299ac3f6d298507bfbbccf77b8789a43` 合入 `main`。
  - 本轮 U4 工作分支基于新的 `main` merge commit 创建：`codex/dp5-rc20-u4-config-prompt-menu-glyph-closure`。
- `DP5-RC20` U4 Config / prompt / menu / glyph contract closure 已开始推进：
  - 本切片不新增 runtime phase，不改变 `src/input_v2/` mainline，不改 canonical target 名称、replay root、旧 SWF token API 或 `FavoritesMenu` workspace。
  - 内部 `PromptCandidate` / `PromptLegacyGlyphDescriptor` diagnostics 已补 glyph id、platform id、button semantic name、fallback text、asset lookup path、missing icon behavior 与 debug reason。
  - 旧 `DualPad_GetActionGlyphToken` 继续只返回单 token；`ScaleformPromptAdapter` 的 GFx descriptor 暂不新增 U4 visual asset contract 字段。
  - 新增 `docs/releases/dp5_rc20_u4_config_prompt_menu_glyph_contract_zh.md`，记录 zero direct binding 分类、`unknown_menu_policy=passthrough` / ignored menu 行为、`FavoritesMenu` non-restore workspace、conflict detection、prompt fail-closed 矩阵与 glyph/icon contract。
  - 新增 `scripts/ci/check_config_prompt_menu_glyph_closure.py` 并接入 Phase8 CI；reviewed docs consistency 现在要求 Phase8 保留该 U4 gate。
- 已通过的 focused / static gate：
  - `xmake build -y DualPadPromptSnapshotTests && xmake run -y DualPadPromptSnapshotTests`：exit 0。
  - `python scripts/ci/check_config_prompt_menu_glyph_closure.py`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0。
- 待收尾验证：
  - `DualPadManifestCompilerTests`
  - Phase 8 CI
  - replay diff
  - builder JSON
  - legacy authority boundary check
  - graphify rebuild
  - `git diff --check`
  - GitHub #11 checklist 与 #13 总控同步

## 2026-06-07 23:30:25 CST

- `DP5-RC20` U4 close-out 验证完成：
  - `xmake build -y DualPadPromptSnapshotTests && xmake run -y DualPadPromptSnapshotTests`：exit 0。
  - `xmake build -y DualPadManifestCompilerTests && xmake run -y DualPadManifestCompilerTests`：exit 0；stdout 仍包含既有 negative-path epoch mismatch / bad config / stale LKG 日志。
  - `python scripts/ci/check_config_prompt_menu_glyph_closure.py`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：最终 exit 0；构建/运行 `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 reviewed docs consistency、legacy authority boundary、U3 release readiness、U4 config/prompt/menu/glyph closure 与 generated docs clean-diff。首次运行因 DocGen 将 generated docs manifest hash 从 `35f58ad5aff249ea` 更新为 `60ab6dd5bcb07037` 停在 `git diff --exit-code -- docs/generated`；按既有 close-out 规则暂存 regenerated docs 后重跑通过。
  - `xmake build -y DualPadReplayHarness`：exit 0。
  - `xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay`：exit 0。
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`：exit 0；10 个 phase0 场景均为 `no diff`。
  - `python -m json.tool .dualpad-builder/feature_list.json > $null` 与 `python -m json.tool .dualpad-builder/sprint_plan.json > $null`：exit 0。
  - `python scripts/ci/check_legacy_authority_boundary.py`：exit 0。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`：exit 0，输出 `Rebuilt: 1647 nodes, 3468 edges, 143 communities`。
  - `git diff --check`：exit 0；仅输出 CRLF 工作区提示，无 whitespace error。
- 状态结论：
  - U4 已冻结 config / prompt / menu / glyph 用户可见合同：zero-direct context 分类、`unknown_menu_policy=passthrough` / ignored menu 行为、`FavoritesMenu` non-restore workspace、prompt fail-closed 矩阵与 glyph/icon diagnostics。
  - 本轮未新增 runtime phase，未改变 `src/input_v2/` mainline，未改旧 SWF token API，未恢复 `FavoritesMenu` workspace、`BindingManager` 或 trigger reverse lookup authority。
  - `DP5` / `S-DP5` 整体仍保持 planned；U5 尚未完成。

## 2026-06-08 00:18:00 CST

- 已将 PR #20 合并到 `main`：
  - PR #20 `Complete DP5-RC20 U4 config prompt menu glyph closure` 的远端 `phase8` check 已在 head `08e28c3bc050b347b8a382d08e1ba0c928ddd572` 上通过。
  - PR #20 以 merge commit `356837d6b0c51c01671a4f7f273932347d435a91` 合入 `main`。
  - 本轮 U5 工作分支基于该 merge commit 创建：`codex/dp5-rc20-u5-verification-observability-governance`。
- `DP5-RC20` U5 Verification / observability / governance closeout 已开始推进：
  - U5 只新增 RC readiness outer gate 与治理/验证文档；不新增 runtime phase，不改变 `src/input_v2/` mainline，不改 canonical target 名称、replay root、旧 SWF 返回 shape 或 `FavoritesMenu` workspace。
  - `scripts/ci/run_rc_readiness.ps1` 作为 Phase8 外层 gate，先调用 `scripts/ci/run_phase8_ci.ps1`，再聚合 replay diff、builder JSON、reviewed/generated docs consistency、legacy boundary、release readiness、U4 contract gate、DInput8 proxy build、release artifact manifest、graphify rebuild 与 diff hygiene。
  - `docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md` 已记录 real-game QA matrix、performance budget、debug snapshot/log surface 覆盖和旧 #5 non-authority cleanup 结论。
  - `.dualpad-builder/feature_list.json` 与 `.dualpad-builder/sprint_plan.json` 已同步 U5 gate 与 closeout 口径；`DP5` / `S-DP5` 继续保持 planned，表示 post-closeout hardening backlog，而不是新的 runtime phase。

## 2026-06-08 00:22:30 CST

- `DP5-RC20` U5 本地 close-out 验证完成：
  - `python scripts/ci/check_rc_readiness_closeout.py`：exit 0，输出 `RC readiness closeout check passed`。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0，输出 `reviewed docs consistency check passed`。
  - `python -m json.tool .dualpad-builder/feature_list.json NUL` 与 `python -m json.tool .dualpad-builder/sprint_plan.json NUL`：exit 0。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；构建/运行 `DualPad`、6 个 canonical runtime targets、`DualPadPresentationProjectionTests`、`DualPadDocGen`，并通过 reviewed docs consistency、legacy authority boundary、release readiness、U4 config/prompt/menu/glyph closure 与 generated docs clean-diff。DocGen manifest hash 保持 `60ab6dd5bcb07037`。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1`：exit 0；先运行 Phase8，再完成 `DualPadReplayHarness` build、phase0 dispatcher replay generation、10 个 replay scenarios `no diff`、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 contract gate、U5 RC closeout static gate、`DualPadDInput8Proxy` build、release artifact manifest generation、graphify rebuild 与 `git diff --check`。
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` 由 RC gate 执行，输出 `Rebuilt: 1654 nodes, 3476 edges, 145 communities`。
- 状态结论：
  - U5 已建立 `scripts/ci/run_rc_readiness.ps1` 作为 Phase8 外层 RC readiness gate；Phase8 仍是 canonical base gate，不替代 canonical targets。
  - Real-game QA matrix、performance budget、debug snapshot/log surface 覆盖与旧 #5 non-authority cleanup 结论已写入 `docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md`。
  - U5 本轮未新增 runtime phase，未改变 `src/input_v2/` mainline，未改 canonical target 名称、replay root、旧 SWF 返回 shape 或 `FavoritesMenu` workspace。

## 2026-06-08 00:30:00 CST

- U5 final commit 后 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest` 首次运行失败于 release artifact manifest 步骤：
  - 失败前已通过 Phase8、phase0 replay generation / diff、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 contract gate、U5 RC closeout static gate 与 `DualPadDInput8Proxy` build。
  - 失败点：`scripts/dev/generate_release_artifact_manifest.py --require-build-artifacts --expect-clean` 报 `tracked working tree is dirty`。
  - 根因：Phase8 / DocGen 后 `docs/generated/*.md` 出现 CRLF stat-only working tree 状态；`git diff --exit-code -- docs/generated` 无内容 diff，但原 manifest generator 使用 `git status --porcelain --untracked-files=no`，会把该行尾状态误判为 tracked dirty。
  - 修复：`scripts/dev/generate_release_artifact_manifest.py` 的 dirty 判定改为 `git diff --quiet --` + `git diff --cached --quiet --`，只按 tracked content / index diff 判断 `trackedWorkingTreeDirty`。

## 2026-06-12 00:00:00 CST

- `DP5-RC20` PR-A RC gate / CI / release artifact evidence fix 已在分支 `codex/dp5-rc20-rc-gate-evidence-fix` 开始执行：
  - `.github/workflows/dualpad-ci.yml` 新增远端 `rc-readiness` job，并设置 `needs: phase8`，执行 `scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - `scripts/ci/run_phase8_ci.ps1` 已把 `DualPadManifestCompilerTests` 纳入 Phase8 build/run。
  - `scripts/dev/generate_release_artifact_manifest.py` 已将 release manifest 输出名升级为 `DP5-RC20-release-artifact-manifest.{json,md}`，并把 U4 / U5 reviewed docs 纳入 manifest。
  - `scripts/ci/check_rc_readiness_closeout.py` 与 `scripts/ci/check_reviewed_docs_consistency.py` 已增加 PR-A gate marker 检查。
- 已执行的轻量验证：
  - `python -m json.tool .dualpad-builder/feature_list.json NUL`：exit 0。
  - `python -m json.tool .dualpad-builder/sprint_plan.json NUL`：exit 0。
  - `python scripts/ci/check_rc_readiness_closeout.py`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0。
  - `python scripts/ci/check_release_readiness.py`：exit 0。
- 未预写结果：
  - Phase8 / RC readiness 完整本地验证尚未完成。
  - 远端 `phase8` / `rc-readiness` run id 尚未产生。

## 2026-06-12 01:03:00 CST

- `DP5-RC20` PR-A 本地验证完成：
  - `xmake build -y DualPadManifestCompilerTests`：exit 0。
  - `xmake run -y DualPadManifestCompilerTests`：exit 0；stdout 仍包含既有 negative-path epoch mismatch / bad config / stale LKG 日志。
  - `git diff --check`：exit 0；仅输出 Windows 行尾提示，无 whitespace error。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；Phase8 已构建并运行 `DualPadManifestCompilerTests`，并通过 canonical targets、DocGen、reviewed docs consistency、legacy boundary、release readiness 与 U4 closure gate。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1`：exit 0；完成 Phase8、phase0 dispatcher replay generation、10 个 replay scenarios `no diff`、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 contract gate、U5 RC closeout static gate、`DualPadDInput8Proxy` build、`DP5-RC20-release-artifact-manifest.{json,md}` generation、graphify rebuild 与 `git diff --check`。
- 备注：
  - `-ExpectCleanManifest` 需要 final commit 后的干净 tracked content / index 才能作为 source binding 验证；本条不预写该验证结果。
  - 远端 `phase8` / `rc-readiness` run id 尚未产生。

## 2026-06-12 01:25:00 CST

- PR-A 远端验证第一次反馈：
  - PR #22 `phase8` 已在 GitHub Actions 通过。
  - PR #22 `rc-readiness` 在 graphify rebuild step 失败；失败前已通过 Phase8、replay diff、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 closure、U5 closeout gate、DInput8 proxy build 与 `DP5-RC20-release-artifact-manifest.{json,md}` generation。
  - 失败根因：GitHub Windows runner 上 Python stdout 使用 cp1252，`scripts/dev/setup_graphify_local.py` 在 graphify 缺失时打印中文安装提示，触发 `UnicodeEncodeError`。
  - 修复：`scripts/ci/run_rc_readiness.ps1` 在执行 Python steps 前设置 `PYTHONUTF8=1` 与 `PYTHONIOENCODING=utf-8`。
- 待重新验证：
  - 本地 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR #22 `rc-readiness`。

## 2026-06-12 01:45:00 CST

- PR-A 远端验证第二次反馈：
  - `PYTHONUTF8` / `PYTHONIOENCODING` 已解决 cp1252 编码异常。
  - PR #22 `rc-readiness` 继续失败在 graphify rebuild step；失败根因变更为 fresh runner 自动安装 `graphifyy 0.8.37` 后仍无法 `import graphify`。
  - 本机 `python -m pip show graphifyy` 显示当前可用版本为 `0.4.14`，且 `python -c "import importlib.util; ..."` 确认 `graphify` module 存在。
  - 修复：`scripts/dev/setup_graphify_local.py` 将自动安装版本固定为 `graphifyy==0.4.14`，并把该 pin 纳入 RC static gate。
- 待重新验证：
  - 本地 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 02:05:00 CST

- PR-A 远端验证第三次反馈：
  - `graphifyy==0.4.14` pin 生效，GitHub runner 已安装固定版本。
  - `rc-readiness` 仍失败于同一 Python 进程的 `import graphify`；日志显示 package 安装成功，但 `--user` 安装目录没有在当前进程 import path 中立即可见。
  - 修复：`scripts/dev/setup_graphify_local.py` 安装后显式把 `site.getusersitepackages()` 加入 `sys.path`，并调用 `importlib.invalidate_caches()` 后再导入 `graphify`。
- 待重新验证：
  - 本地 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 02:10:00 CST

- PR-A 本地最终验证：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`：exit 0。
  - 覆盖 Phase8、`DualPadManifestCompilerTests`、phase0 dispatcher replay 10 个 scenario `no diff`、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 closure、U5 closeout static gate、`DualPadDInput8Proxy` build、`DP5-RC20-release-artifact-manifest.{json,md}` clean manifest check、graphify manual-closeout rebuild 与 `git diff --check`。
- 待重新验证：
  - 本条 progress-only commit 后再运行一次同一 RC gate。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 02:30:00 CST

- PR-A 远端验证：
  - PR: `https://github.com/airoucat/dualpad/pull/22`
  - Head commit: `199f391`
  - `phase8`：pass，run `27367759219` job `80871672853`，`https://github.com/airoucat/dualpad/actions/runs/27367759219/job/80871672853`。
  - `phase8`：pass，run `27367760931` job `80871680129`，`https://github.com/airoucat/dualpad/actions/runs/27367760931/job/80871680129`。
  - `rc-readiness`：pass，run `27367759219` job `80873536204`，`https://github.com/airoucat/dualpad/actions/runs/27367759219/job/80873536204`。
  - `rc-readiness`：pass，run `27367760931` job `80873236896`，`https://github.com/airoucat/dualpad/actions/runs/27367760931/job/80873236896`。
- 备注：
  - 两组 check 来自 PR push / pull_request 触发；均为远端可见绿色。

## 2026-06-12 02:50:00 CST

- PR-A 远端验证第四次反馈：
  - 追加远端证据 progress commit 后，PR #22 的 branch-trigger `rc-readiness` 通过，但 pull_request merge-ref trigger 的 `rc-readiness` 失败。
  - 失败 job：run `27368886206` job `80877148289`，`https://github.com/airoucat/dualpad/actions/runs/27368886206/job/80877148289`。
  - 失败根因：fresh GitHub runner 执行 `xmake build -y DualPad` 时按 `xmake-requires.lock` 更新 package repository，命中 `https://gitee.com/tboox/xmake-repo.git`，无交互认证导致 `fatal: could not read Username for 'https://gitee.com'`。
  - 对照：同一 head commit 的 branch-trigger job 已通过，说明 runtime / release manifest / graphify gate 本身未回归。
  - 修复：`xmake-requires.lock` 中的 `gitcode.com` / `gitee.com` xmake-repo mirror 统一改为 `https://github.com/xmake-io/xmake-repo.git`，并在 `check_rc_readiness_closeout.py` 增加静态 gate，禁止 gitee/gitcode mirror 回流。
- 待重新验证：
  - 本地 RC closeout static gate。
  - 本地 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 03:08:00 CST

- PR-A 远端验证第五次反馈：
  - `gitee.com` / `gitcode.com` mirror 修复后，PR #22 的两套 `phase8` 通过，但两套 `rc-readiness` 在 release manifest `--expect-clean` 处失败。
  - 失败 jobs:
    - run `27370051718` job `80881031952`，`https://github.com/airoucat/dualpad/actions/runs/27370051718/job/80881031952`。
    - run `27370054432` job `80881204519`，`https://github.com/airoucat/dualpad/actions/runs/27370054432/job/80881204519`。
  - 失败根因：GitHub Actions 使用 `xmake-version: latest`，当前解析为 xmake `3.0.9`；fresh runner 构建后 `xmake-requires.lock` 被重写，导致 tracked working tree dirty。主机本地验证使用 xmake `3.0.7`，该版本不会重写当前 lock。
  - 修复：`.github/workflows/dualpad-ci.yml` 将两个 `xmake-io/github-action-setup-xmake@v1` step 固定到 `xmake-version: 3.0.7`，并在 `check_rc_readiness_closeout.py` 增加静态 gate，防止 CI 回到 floating `latest`。
- 待重新验证：
  - 本地 RC closeout static gate。
  - 本地 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 03:35:00 CST

- PR-A 远端验证第六次反馈：
  - `xmake-version: 3.0.7` 已在两套 GitHub Actions job 中生效；失败不再来自 floating xmake 版本。
  - 两套 `rc-readiness` 仍在 release manifest `--expect-clean` 处失败：
    - run `27371055936` job `80884548738`，`https://github.com/airoucat/dualpad/actions/runs/27371055936/job/80884548738`。
    - run `27371054532` job `80884746721`，`https://github.com/airoucat/dualpad/actions/runs/27371054532/job/80884746721`。
  - 日志显示 Phase8 的 DocGen 通过 `git diff --exit-code -- docs/generated`，但随后全仓库 clean manifest check 看到 `docs/generated/*.md` 与 `xmake-requires.lock` 的 Windows 行尾告警，并判定 tracked working tree dirty。
  - 修复：新增 `.gitattributes`，将 `docs/generated/*.md` 与 `xmake-requires.lock` 固定为 `text eol=lf`；同时增强 release manifest dirty 诊断，失败时输出具体 dirty tracked file，并把 EOL 合同纳入 U5 static gate。
- 待重新验证：
  - 本地 RC closeout static gate。
  - 本地 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 03:45:00 CST

- PR-A 本地验证：
  - Head commit: `dee5fd4`。
  - `python scripts/ci/check_rc_readiness_closeout.py`：exit 0。
  - `python scripts/ci/check_reviewed_docs_consistency.py`：exit 0。
  - `git diff --check` / `git diff --cached --check`：exit 0；仅有 Windows 行尾提示，无 whitespace error。
  - `python scripts/dev/generate_release_artifact_manifest.py --expect-clean` 在未提交修复时按预期失败，并输出具体 dirty tracked file，验证新增诊断可用。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`：exit 0；覆盖 Phase8、`DualPadManifestCompilerTests`、phase0 dispatcher replay 10 个 scenario `no diff`、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 closure、U5 RC closeout static gate、`DualPadDInput8Proxy` build、`DP5-RC20-release-artifact-manifest.{json,md}` clean manifest check、graphify manual-closeout rebuild 与 `git diff --check`。
  - `git ls-files --eol docs/generated/*.md xmake-requires.lock`：均为 `i/lf w/lf attr/text eol=lf`。
- 待重新验证：
  - 本条 progress-only commit 后再运行一次同一 RC gate。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 04:05:00 CST

- PR-A 远端验证第七次反馈：
  - Head commit: `25b708f`。
  - 两套 `phase8` 均通过：
    - run `27372313050` job `80887493865`。
    - run `27372314562` job `80887499118`。
  - 两套 `rc-readiness` 仍在 release manifest `--expect-clean` 处失败：
    - run `27372313050` job `80888666639`。
    - run `27372314562` job `80888783940`。
  - 新增诊断确认 `docs/generated` 已不再 dirty；唯一 dirty tracked file 是 `xmake-requires.lock`。这说明 `.gitattributes` 修复了 DocGen 行尾问题，剩余问题是 GitHub fresh runner 上 xmake 构建过程刷新 lockfile。
  - 修复：`scripts/ci/run_rc_readiness.ps1` 在 GitHub Actions 环境中、release manifest clean check 前打印 `xmake-requires.lock` diff 并恢复该文件，避免构建工具自动刷新污染 source-binding manifest；本地运行不自动 restore，避免误删开发者未提交的 lockfile 修改。同时 release manifest 失败诊断扩展为输出 dirty file 的实际 diff。
- 待重新验证：
  - 本地 RC closeout static gate。
  - 本地 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR #22 `phase8` / `rc-readiness`。

## 2026-06-12 04:20:00 CST

- PR-A 本地与远端验证：
  - Head commit: `3ec972a`。
  - 本地 `GITHUB_ACTIONS=true powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`：exit 0；覆盖 Phase8、`DualPadManifestCompilerTests`、phase0 dispatcher replay 10 个 scenario `no diff`、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 closure、U5 RC closeout static gate、`DualPadDInput8Proxy` build、CI-only `xmake-requires.lock` diff/restore branch、`DP5-RC20-release-artifact-manifest.{json,md}` clean manifest check、graphify manual-closeout rebuild 与 `git diff --check`。
  - PR #22 push trigger:
    - `phase8`：pass，run `27373380388` job `80891206892`，`https://github.com/airoucat/dualpad/actions/runs/27373380388/job/80891206892`。
    - `rc-readiness`：pass，run `27373380388` job `80892656631`，`https://github.com/airoucat/dualpad/actions/runs/27373380388/job/80892656631`。
  - PR #22 pull_request trigger:
    - `phase8`：pass，run `27373385092` job `80891224204`，`https://github.com/airoucat/dualpad/actions/runs/27373385092/job/80891224204`。
    - `rc-readiness`：pass，run `27373385092` job `80892545879`，`https://github.com/airoucat/dualpad/actions/runs/27373385092/job/80892545879`。
- 备注：
  - 本条为 evidence-only progress 更新；推送后需确认最后一轮 PR #22 checks 仍为 green。

## 2026-06-12 04:35:00 CST

- PR-B1 start：MainMenu / menu glyph delegate reattach fix。
- 分支：`codex/dp5-rc20-menu-glyph-reattach-fix`，从 PR-A merge 后的 `main` 创建。
- 范围：
  - `ContextEventSink` 在 `MenuOpenCloseEvent` 后通知 `ScaleformGlyphBridge::OnMenuOpened` / `OnMenuClosed`。
  - `ScaleformGlyphBridge` 继续仅作为 facade 转发到 `ScaleformPromptAdapter`。
  - `ScaleformPromptAdapter` 将 delegate 注册记录改为 per-menu pointer map，菜单关闭时只清理记录，不访问旧 delegate / 不反注册。
  - U4 static gate 增加 menu glyph lifecycle / per-menu delegate map marker。
- 待验证：
  - `python scripts/ci/check_config_prompt_menu_glyph_closure.py`
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`
  - `git diff --check`

## 2026-06-12 04:50:00 CST

- PR-B1 本地验证：
  - `python scripts/ci/check_config_prompt_menu_glyph_closure.py`：exit 0。
  - `xmake build -y DualPadPromptSnapshotTests`：exit 0。
  - `xmake run -y DualPadPromptSnapshotTests`：exit 0。
  - `xmake build -y DualPad`：exit 0；覆盖 `ContextEventSink`、`ScaleformGlyphBridge`、`ScaleformPromptAdapter` 与 replay stub 编译面。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：exit 0；覆盖 canonical Phase8 targets、DocGen、reviewed docs consistency、legacy boundary、release readiness、U4 closure gate 与 generated docs diff。
  - `git diff --check`：exit 0；仅 Windows 行尾提示，无 whitespace error。
- 待验证：
  - 本条提交后运行 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`。
  - 推送后远端 PR-B1 `phase8` / `rc-readiness`。

## 2026-06-12 05:05:00 CST

- PR-B1 本地 RC 验证：
  - Head commit: `535a3d7`。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`：exit 0。
  - 覆盖 Phase8、`DualPadManifestCompilerTests`、phase0 dispatcher replay 10 个 scenario `no diff`、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 closure gate（含 B1 menu glyph lifecycle static markers）、U5 RC closeout static gate、`DualPadDInput8Proxy` build、`DP5-RC20-release-artifact-manifest.{json,md}` clean manifest check、graphify manual-closeout rebuild 与 `git diff --check`。
- 待验证：
  - 推送后远端 PR-B1 `phase8` / `rc-readiness`。

## 2026-06-12 04:51:53 +08:00

- PR-B2 upstream route health fix 开始：分支 codex/dp5-rc20-upstream-route-health-fix。范围：为 UpstreamGamepadHook 增加安装状态/原因合同，将 configured+failed upstream route 映射到 RuntimeHealthReason::HookInstallFailed，并在 hook 失败时跳过 ControlMapOverlay，避免 prompt/controlmap 与实际输入路由半安装。待验证：DualPadRouteHealthContractTests、DualPadInputV2Tests、phase8、rc-readiness、graphify rebuild、git diff --check。

## 2026-06-12 05:01:53 +08:00

- PR-B2 本地验证通过。已运行并通过：xmake build/run DualPadRouteHealthContractTests；xmake build/run DualPadInputV2Tests；xmake build DualPad；powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1（含 DualPadRouteHealthContractTests）；powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest。RC gate 覆盖 Phase8、DualPadReplayHarness phase0 dispatcher replay 10 个 scenario no diff、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 closure、U5 closeout、DualPadDInput8Proxy build、DP5-RC20 release artifact manifest clean check、graphify manual-closeout rebuild 与 git diff --check。待验证：推送后远端 PR-B2 phase8 / rc-readiness。

## 2026-06-12 05:21:02 +08:00

- PR-B2 远端验证通过：PR #24 https://github.com/airoucat/dualpad/pull/24，head a6e74dc。远端 checks：push phase8 run 27377256038 job 80904601807 pass；push rc-readiness run 27377256038 job 80906081844 pass；pull_request phase8 run 27377273931 job 80904662615 pass；pull_request rc-readiness run 27377273931 job 80906257898 pass。待处理：推送本 evidence 记录后等待最终 checks，再 merge PR-B2。

## 2026-06-12 05:39:39 +08:00

- PR-B2 已合入 main：PR #24 merge commit 8411a0d4a1bcd0635b3422ebdc8c15c2015828c2。开始 PR-B3 broken INI fail-closed：分支 codex/dp5-rc20-broken-ini-fail-closed。范围：LegacyIniImporter 区分 missing 与 existing-but-unreadable/broken；两份主配置 missing 仍允许 built-in defaults；存在但打不开、缺 '='、section 前 key 或 bindings 空 AST 必须可见失败并阻止静默当空配置成功。先补红测，待验证。

## 2026-06-12 05:46:00 +08:00

- PR-B3 本地验证通过。Head commit: `abe30d5`。已先执行 TDD 红测：`xmake build -y DualPadManifestCompilerTests; xmake run -y DualPadManifestCompilerTests` 失败于 `existing bindings file with entry before section must fail import`，确认旧行为会把破损 existing INI 静默当空配置处理。实现后 focused green：`xmake build -y DualPadManifestCompilerTests; xmake run -y DualPadManifestCompilerTests` exit 0。完整 gate：`powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest` exit 0，覆盖 Phase8、`DualPadManifestCompilerTests` build/run、phase0 dispatcher replay 10 个 scenario no diff、builder JSON、reviewed docs consistency、legacy boundary、release readiness、U4 closure、U5 closeout、`DualPadDInput8Proxy` build、DP5-RC20 release artifact manifest clean check、graphify manual-closeout rebuild 与 `git diff --check`。待验证：推送后远端 PR-B3 `phase8` / `rc-readiness`。

## 2026-06-12 06:11:00 +08:00

- PR-B3 远端验证通过：PR #25 https://github.com/airoucat/dualpad/pull/25，head `a8fcaf2713cfdc41be704a93818823a114fa9833`。远端 checks：push `phase8` run `27379474743` job `80912184185` pass；push `rc-readiness` run `27379474743` job `80913581414` pass；pull_request `phase8` run `27379485298` job `80912221999` pass；pull_request `rc-readiness` run `27379485298` job `80913439616` pass。待处理：推送本 evidence 记录后等待最终 checks，再 merge PR-B3。

## 2026-06-12 06:31:00 +08:00

- PR-B3 已合入 main：PR #25 merge 后 main head `51c70d7`。最终 evidence commit `20bb60d` 的远端 checks 通过：push `phase8` run `27380299870` job `80915035234` pass；push `rc-readiness` run `27380299870` job `80916161516` pass；pull_request `phase8` run `27380299950` job `80915035026` pass；pull_request `rc-readiness` run `27380299950` job `80916122483` pass。
- 开始 PR-C documentation contract hygiene：分支 `codex/dp5-rc20-doc-contract-hygiene`。范围：修正文档状态滞后、retired menu policy focused test target 死链、U4/U5 live implemented / parsed-only reserved / QA required 分类、#8 cockpit 残影 superseded note，并同步 builder memory。已在 GitHub #8 追加 historical cockpit note：`https://github.com/airoucat/dualpad/issues/8#issuecomment-4685593281`。待验证：docs consistency、U4/U5 gates、builder JSON、`git diff --check`。

## 2026-06-12 06:48:00 +08:00

- PR-C 本地验证通过。已运行并通过：`python scripts/ci/check_reviewed_docs_consistency.py`；`python scripts/ci/check_config_prompt_menu_glyph_closure.py`；`python scripts/ci/check_rc_readiness_closeout.py`；`python -m json.tool .dualpad-builder/feature_list.json NUL`；`python -m json.tool .dualpad-builder/sprint_plan.json NUL`；`git diff --check`。额外 grep：`DualPadMenuContextPolicyTests` / `xmake build|run DualPadMenuContextPolicyTests` / `U1-U5 待推进` / `fully certified RC baseline` 在 `docs AGENTS.win.md .dualpad-builder` 中无命中。`git diff --check` 仅有 Windows 行尾提示，exit 0。待验证：推送后远端 PR-C `phase8` / `rc-readiness`。

## 2026-06-12 07:05:00 +08:00

- PR-C 远端验证通过：PR #26 https://github.com/airoucat/dualpad/pull/26，head `bbfe3befd89990c6fa7d8e1e0d023dc31eea2cf1`。远端 checks：push `phase8` run `27381393993` job `80918707810` pass；push `rc-readiness` run `27381393993` job `80919803848` pass；pull_request `phase8` run `27381404943` job `80918743654` pass；pull_request `rc-readiness` run `27381404943` job `80919873507` pass。待处理：推送本 evidence 记录后等待最终 checks，再 merge PR-C。

## 2026-06-12 23:03:08 +08:00

- 实机 field readiness 热修开始并完成本地部署：用户进游戏反馈手柄不能操控。根因证据：`DualPad.log` 显示 live `DualPadBindings.ini` 为 4 月旧配置，含当前 manifest 不认识的 `Favorites.GroupConfirm`，导致 startup compile fail；同时 1.5.97 Address Library 解析 `REL::ID 67320/67321` 后，当前代码在 `+0xD` 期待 `E8` 直接 call，但实机字节为函数入口内虚表间接调用序列，触发 `is_using_gamepad_call_signature_mismatch` 并 fail-closed。修复：`SkyrimCompatibilitySurface` 改为验证 `IsUsingGamepad` / `GamepadControlsCursor` 函数入口 prologue 并用 `write_branch<5>` 安装入口 hook；`xmake.lua` deploy 改为每次覆盖 `DualPadDebug.ini`、`DualPadBindings.ini`、`DualPadMenuPolicy.ini`。已重新 `xmake build -y DualPad` 并部署到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；`xmake build -y DualPadDInput8Proxy` 保持通过并部署 `G:/g/SkyrimSE/dinput8.dll`。已验证并通过：`xmake build/run -y DualPadPresentationProjectionTests`；`xmake build/run -y DualPadInputV2Tests`；live 三份配置 SHA256 与 repo `config/` 对应文件一致。待验证：用户再次进游戏确认手柄操控恢复。

## 2026-06-12 23:10:00 +08:00

- 实机 field readiness 二次热修：用户反馈手柄仍未被转换为游戏输入。新日志显示 `skyrim_compat_hook_status=success`、upstream route installed，旧 hook/config 问题已解除；剩余断点为持续 `ContextRevisionSkew|PromptScopeFrozen`，并出现 `IngressHub` high-water fallback。根因：live HID `PadEventSnapshot` 只携带 legacy `contextEpoch`，`LegacyIngressAdapter` 将其当作 input_v2 `UiSnapshot.contextRevision`，导致 runtime 看到 frame revision 与 `ContextResolver` published revision 不一致并拒绝执行 action graph。TDD 红测：新增 `TestLegacySnapshotAdapterPrefersInputV2ContextRevision`，先确认旧 adapter 会把 revision=23 错降为 legacy epoch=1。修复：`PadEventSnapshot` 新增 `contextRevision`；`HidReader` 同时写入 legacy epoch 与 input_v2 revision；legacy-to-ingress adapter 优先使用 `contextRevision`，无值时回退 `contextEpoch` 保持 replay/旧路径兼容；snapshot coalescing 将 `contextRevision` 纳入上下文一致性判断。已重新 `xmake build -y DualPad` 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；已重新 `xmake build -y DualPadDInput8Proxy` 并部署 `G:/g/SkyrimSE/dinput8.dll`。已验证并通过：`xmake build/run -y DualPadIngressTests`；`xmake build/run -y DualPadInputV2Tests`；`xmake build/run -y DualPadPresentationProjectionTests`。待验证：用户再次进游戏确认 `ContextRevisionSkew|PromptScopeFrozen` 消失且手柄输入恢复。

## 2026-06-12 23:14:00 +08:00

- 实机 field debug 日志开关已开启并同步 live：用户反馈仍未恢复，要求打开日志方便后续快速定位。`config/DualPadDebug.ini` 与 live `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadDebug.ini` 已同步，SHA256 一致。开启项：`log_input_packets`、`log_input_state`、`log_mapping_events`、`log_synthetic_state`、`log_action_plan`、`log_native_injection`、`log_keyboard_injection`、`log_route_health`、`enable_trace_recording`；`log_input_hex` 暂保持 false，避免短时间生成过大的 HID 原始 hex 日志。trace 输出目录配置为 `Data/SKSE/Plugins/DualPadTrace/field-debug-2026-06-12`，已预创建本机目录 `G:/g/SkyrimSE/Data/SKSE/Plugins/DualPadTrace/field-debug-2026-06-12`。验证：`git diff --check` exit 0（仅 Windows 行尾提示）。

## 2026-06-12 23:24:00 +08:00

- 实机 field debug 诊断版已部署：用户再次测试仍反馈手柄没反应。新证据显示 HID reader 已打开 `vid=0x054C pid=0x0DF2`，USB report `0x01 size=64` 持续进入；trace 实际落在 MO2 overwrite：`G:/skyrim_mod_develop/overwrite/SKSE/Plugins/DualPadTrace/field-debug-2026-06-12`。`ingress_snapshot_frames.csv` 与 `processed_snapshot_frames.csv` 有帧且可见轻微 analog drift，但 `digital_mask`、trigger 与 `expected_authoritative_poll.csv` 输出均为 0；`expected_keyboard_bridge.csv` 只有表头。当前断点收敛为：链路已读到 HID packet，但本轮采样未看到有效按键/大摇杆/扳机，需用 raw packet 判定是测试窗口内未产生有效物理输入，还是 DualSense Edge `0x0DF2` 的 parser layout/offset 不匹配。
- 已把诊断日志升级为 field 可见：`log_input_hex=true`；`PadStateDebugger` 的 packet summary、前 32 字节 hex、parse success、state summary 改为 rate-limited info 日志（前 20 包、每 120 包、或 prefix/control state 变化时输出）；USB/BT parser raw button 字节也改为 info。已重新 `xmake build -y DualPad` 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；`xmake build -y DualPadDInput8Proxy` 已部署 `G:/g/SkyrimSE/dinput8.dll`。验证：`xmake run -y DualPadIngressTests` exit 0；`xmake run -y DualPadInputV2Tests` exit 0；`git diff --check` exit 0（仅 Windows 行尾提示）。注意：PDB 复制因目标被占用跳过，DLL 本体已覆盖。

## 2026-06-12 23:34:00 +08:00

- 实机 field readiness 三次热修已部署：用户按新日志版复测后仍反馈手柄没反应。新 trace 证明 HID/parser 已正常读到有效物理输入：`ingress_snapshot_frames.csv` / `processed_snapshot_frames.csv` 中 `digital_mask` 多种非零，左右扳机均到过 `1.0`，左摇杆到过满量程；但 `expected_authoritative_poll.csv` 的 `down_mask`、analog、trigger 仍全为 0。日志同一窗口显示 `AuthoritativePoll ctx=Menu`。根因定位为 `GameplayProjectionFrame` 在 `policy.gameplayContext=false` 时仍沿用 gameplay arbitration gate，把非 gameplay / Menu 上下文的 resolved native gamepad output 清零，导致 Menu action graph 已解析出的 `Menu.*` 数字/轴动作无法进入 `AuthoritativePollState`。
- TDD 红灯：新增 `RunMenuContextGamepadOutputTests` 后，`xmake build -y DualPadGameplayProjectionTests; xmake run -y DualPadGameplayProjectionTests` 先失败于 `menu context must not suppress resolved gamepad menu digital output`。实现：`ResolveGameplayProjection` 仅在 `policy.gameplayContext=true` 时应用 keyboard/mouse ownership analog zeroing 与 transient digital suppression；Menu/非 gameplay 上下文保持 presentation owner 由 UI 决定，但允许 resolved `Menu.Confirm`、`Menu.ScrollDown`、`Menu.LeftStick` 等 native output 进入 output plan。另补 `DualPadGameplayProjectionTests` 目标缺失的 route health stub/source 链接依赖。已重新 `xmake build -y DualPad` 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`。验证：`xmake build -y DualPadGameplayProjectionTests; xmake run -y DualPadGameplayProjectionTests` exit 0；`xmake run -y DualPadInputV2Tests` exit 0；`xmake run -y DualPadIngressTests` exit 0；`xmake run -y DualPadPresentationProjectionTests` exit 0；`xmake build -y DualPadDInput8Proxy` exit 0。注意：PDB 复制仍因目标被占用跳过，DLL 本体已覆盖。

## 2026-06-13 00:10:00 +08:00

- 实机 field readiness 四次结构修复已部署：用户复测后仍反馈手柄无反应，并指出上一版像临时止血。重新按 root-cause tracing 复核最新现场证据：HID/parser/processed trace 已有非零 `digital_mask` 与 trigger，但 live `DualPad.log` 中 `PollCommit`/`NativeButtonCommit` 动作日志为 0，`AuthoritativePoll` 全 0，且 runtime 反复 `BoundaryMismatch -> recovered`，poll 序号多次回到 1。结论：上一版 menu gate 只修了 projection 层症状，未覆盖 ingress source-evidence race 与 native commit epoch 合同。
- 结构修复：
  - `PublishSourceEvidenceFrameToIngressHub` 改为通过 `IngressHub::PushEvents` 原子批量入队 marker + source evidence，避免 gamepad/keyboard-mouse 双线程证据在 marker/source 之间插队。
  - `FrameAssembler` 对晚到的旧 `deviceFamilyRevision` source evidence 识别为 stale 并忽略，不再触发 `ExplicitReset` 硬重置；新增 `TestStaleSourceEvidenceAfterNewerDeviceMarkerDoesNotHardReset` 覆盖该 race。
  - live `RuntimePollOutputExecutor` 的 native commit epoch 改用 `legacyContextEpoch`，不再把 input_v2 `contextRevision` 写入 `PlannedAction.contextEpoch`；避免 Menu 中 `contextRevision != legacyContextEpoch` 时 slot 在 `CommitPollState()` 被立即判 stale。
  - 补充边界日志：`[DualPad][RuntimePlan]` / `[DualPad][RuntimePlanAction]` 输出 stable input、resolved action 与 projection plan counts；`[DualPad][NativeButtonCommit] apply/translate_failed/queue` 输出 native apply/translate/queue 边界；`RuntimeDebug` 日志增加 `frame` 与 `transition` reason；`PadEventSnapshotProcessor` 开始写 `runtime_debug_snapshot.csv`。
- 已构建并部署：
  - `xmake build -y DualPad` exit 0，已部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，SHA256 `D187DBF202FC02058A2A5DC5B18382E87A9296959FFEAC72CC9BA289863E37B4`。
  - `xmake build -y DualPadDInput8Proxy` exit 0，`G:/g/SkyrimSE/dinput8.dll` 本轮无内容变化，SHA256 `59667D5D9E2599C51F1F630F83D2B2E7CB08F925B3292F228B2D658D55F0409A`。
- 本地验证已通过：
  - `xmake build -y DualPadIngressTests`；`xmake run -y DualPadIngressTests`。
  - `xmake build -y DualPadGameplayProjectionTests`；`xmake run -y DualPadGameplayProjectionTests`。
  - `xmake build -y DualPadInputV2Tests`；`xmake run -y DualPadInputV2Tests`。
  - `xmake build -y DualPadReplayTests`；`xmake run -y DualPadReplayTests`。
  - `xmake build -y DualPadRouteHealthContractTests`；`xmake run -y DualPadRouteHealthContractTests`。
  - `git diff --check` exit 0；仅 Windows 行尾提示，无 whitespace error。
- 待验证：用户进游戏复测。若仍失败，下一轮直接看新增的 `RuntimePlan`、`NativeButtonCommit apply/queue` 和 `runtime_debug_snapshot.csv`，不得再靠猜测加止血条件。

## 2026-06-13 00:12:00 +08:00

- 四次结构修复 closeout 复核：已补 repo-local learning/error 记录，明确后续不得用输出层条件补丁替代 ingress/runtime/native commit 边界证据。已运行 `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`，graphify 输出 `1702 nodes, 3639 edges, 147 communities`。当前状态下重跑验证：`xmake build -y DualPad` exit 0 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`（PDB 因目标占用跳过，DLL 已覆盖）；`xmake build -y DualPadDInput8Proxy` exit 0 并部署 `G:/g/SkyrimSE/dinput8.dll`；`xmake run -y DualPadIngressTests` exit 0；`xmake run -y DualPadInputV2Tests` exit 0；`git diff --check` exit 0（仅 Windows 行尾提示）。部署版本标识：`DualPad.dll` SHA256 `D187DBF202FC02058A2A5DC5B18382E87A9296959FFEAC72CC9BA289863E37B4`，`dinput8.dll` SHA256 `59667D5D9E2599C51F1F630F83D2B2E7CB08F925B3292F228B2D658D55F0409A`。

## 2026-06-13 00:24:00 +08:00

- 实机 field readiness 五次结构修复已部署：当前 live 日志/trace 未发现 `2026-06-13 00:05` 之后的新实机样本，最新 `DualPad.log` 与 game-root trace 仍停在 `2026-06-12 23:36`，因此继续做代码层剩余边界审计。新根因风险：`Menu.ScrollDown` 等 menu repeat native actions 的 descriptor 为 `gateAware=true`，但 `NativeButtonCommitBackend` 旧的 context gate 对 `InputContext::Menu` 返回 false；即使 projection 已生成 menu native command，`PollCommitCoordinator` 仍会等待 gameplay gate，导致 DPad/滚动类 menu output 不能提交到 XInput poll。修复：新增 `IsNativeDigitalGateOpenForContext()` 合同，明确 gameplay ownership suppression 已在 queue 前处理，poll commit gate 不得阻塞 Menu / Favorites / Console / Cursor 等 context-local native output；`NativeButtonCommitBackend::IsGameplayGateOpen()` 改为使用该合同。
- TDD/验证：先新增 `DualPadNativeButtonCommitTests` 红灯，确认缺少 native digital gate policy 合同；中途直接构造 `RE::BSFixedString` 的 standalone coordinator 测试在非 Skyrim 环境挂住，已终止残留进程并记录 `.learnings/ERRORS.md`，最终收缩为纯 gate policy 合同测试。已运行并通过：`xmake build -y DualPadNativeButtonCommitTests`；`xmake run -y DualPadNativeButtonCommitTests`；`xmake build/run -y DualPadGameplayProjectionTests`；`xmake build/run -y DualPadInputV2Tests`；`xmake build/run -y DualPadIngressTests`；`xmake build -y DualPad`；`xmake build -y DualPadDInput8Proxy`；`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`（`1707 nodes, 3647 edges, 144 communities`）；`git diff --check` exit 0（仅 Windows 行尾提示）。部署版本标识：`G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll` SHA256 `DF76E39E26C4B6284C9763182D66FF290194250D41BADAD529BB0DE70763A06B`，`G:/g/SkyrimSE/dinput8.dll` SHA256 `59667D5D9E2599C51F1F630F83D2B2E7CB08F925B3292F228B2D658D55F0409A`。

## 2026-06-13 00:28:00 +08:00

- Phase8 接线复核：`DualPadNativeButtonCommitTests` 已接入 `scripts/ci/run_phase8_ci.ps1`，并更新 `scripts/ci/check_release_readiness.py` 以匹配当前部署策略（三份 live config 由 xmake 每次覆盖同步）。验证结果：`python scripts/ci/check_release_readiness.py` exit 0。两次运行 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：第一次在旧 release readiness token 处失败；修复后第二次已通过所有 build/run target（含新增 `DualPadNativeButtonCommitTests`）、docgen、reviewed docs consistency、legacy authority boundary、release readiness、config/prompt/menu/glyph closure；最终停在 `git diff --exit-code -- docs/generated`，因为 docgen 将 generated docs manifest hash 从 `83cf2c598e9c215d` 更新到 `a20d0b8aadef9076`。该 generated docs drift 已保留在工作树中，后续提交时应一并纳入；在未提交/未 staging 当前工作树前，不把完整 Phase8 称为 clean pass。已再次运行 `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`，输出仍为 `1707 nodes, 3647 edges, 144 communities`。

## 2026-06-14 20:58:00 +08:00

- 实机 field readiness 六次结构修复已部署：用户反馈上一轮仍不行且要求不得继续止血后，本轮先复核现场时间线，确认 `DualPad.log` 与 trace 没有 `2026-06-13 00:05` 后的新样本；旧样本中 HID/parser 已读到非零手柄状态，但 presentation surface trace 在 live 下仍写硬编码 KeyboardMouse，不能作为真实兼容面证据。
- 新增 TDD 红灯：`DualPadPresentationProjectionTests` 添加 startup Menu 场景，模拟 `_published` 为空、当前帧已有 `gamepadEvidence/gamepadLease`、但 `PublishedGameplayPresentation.menuEntryOwner` 仍为默认 KeyboardMouse。旧逻辑失败于 `startup menu gamepad evidence must override the default gameplay menu entry owner`，证明首次进主菜单时当前手柄证据会被默认 gameplay menu-entry owner 压掉。
- 结构修复：`PresentationProjection::Project()` 在非 gameplay 上下文中先消费当前 source evidence（gamepad / keyboard-mouse），只有没有新证据时才继承 `gameplay.menuEntryOwner`。这不是在输出层强制手柄，而是修正 presentation owner authority 顺序，避免启动主菜单和菜单 reclaim 被默认 KeyboardMouse 固定。另将 live `expected_presentation_surface.csv` 改为记录 `SkyrimCompatibilitySurface` 的实际 committed state；replay harness 仍保留原兼容预期，避免破坏 phase0 golden 合同。
- 已构建并部署：
  - `xmake build -y DualPad` exit 0，已部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`（PDB 因目标占用跳过，DLL 已覆盖），SHA256 `54E8FEB436F779160FE564EE9942CA5D7438341B03507F62C6835EB79C7282BE`。
  - `xmake build -y DualPadDInput8Proxy` exit 0，已部署/确认 `G:/g/SkyrimSE/dinput8.dll`，SHA256 `59667D5D9E2599C51F1F630F83D2B2E7CB08F925B3292F228B2D658D55F0409A`。
- 已通过 focused 验证：`xmake build/run -y DualPadPresentationProjectionTests`；`xmake build/run -y DualPadGameplayProjectionTests`；`xmake build/run -y DualPadInputV2Tests`；`xmake build/run -y DualPadIngressTests`；`xmake build/run -y DualPadNativeButtonCommitTests`；`xmake build/run -y DualPadReplayTests`。close-out：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1708 nodes, 3651 edges, 150 communities`；`git diff --check` exit 0，仅有 Windows 行尾提示，无 whitespace error。
- 待实机验证：用户进游戏后先在主菜单按 Cross / Triangle / DPad / 左摇杆，再进 gameplay 按移动/攻击；若仍无响应，优先检查新 trace 的 `expected_presentation_surface.csv`、`runtime_debug_snapshot.csv`、`expected_authoritative_poll.csv` 与日志中的 `[DualPad][RuntimePlan]` / `[DualPad][NativeButtonCommit]` 边界，而不是继续从输出层猜测。

## 2026-06-14 20:59:40 +08:00

- 六次结构修复补强验证：没有新的实机日志样本，继续从当前工作树补 live-style 组合覆盖。`RunRuntimeFrameEnvelopeUsesActiveConfigGraphForMenuBindingsTests` 现在在真实 `config/DualPadBindings.ini` / `DualPadMenuPolicy.ini` 编译出的 graph 上，先发布 live gamepad source evidence，再推送 Generic Menu 的 baseline snapshot 与 DPadDown snapshot，验证同一 stable frame 同时满足：
  - `Menu.ScrollDown` 被解析为 sustained native output，native control 保持 `MenuScrollDown`。
  - committed `SkyrimCompatibilitySurface` owner 为 `Gamepad`。
  - `IsUsingGamepadHook()` 返回 true。
- 本轮只补测试覆盖，生产 DLL 未再改动；部署版本仍为 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll` SHA256 `54E8FEB436F779160FE564EE9942CA5D7438341B03507F62C6835EB79C7282BE`，`G:/g/SkyrimSE/dinput8.dll` SHA256 `59667D5D9E2599C51F1F630F83D2B2E7CB08F925B3292F228B2D658D55F0409A`。
- 已运行并通过：`xmake build -y DualPadInputV2Tests`；`xmake run -y DualPadInputV2Tests`；`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`（`1708 nodes, 3654 edges, 149 communities`）；`git diff --check` exit 0，仅有 Windows 行尾提示，无 whitespace error。

## 2026-06-14 21:02:20 +08:00

- “手柄没翻译”补强验证：在同一个 live-style Generic Menu frame-envelope 测试中追加 prompt/glyph 断言，要求 runtime 处理 live gamepad source evidence + DPadDown snapshot 后，`PromptRuntimeOwner::ResolveLegacyGlyphToken("Menu.ScrollDown", "Menu")` 返回 `360_DPAD_DOWN`，且 legacy descriptor `ok=true`、`buttonArtToken=360_DPAD_DOWN`。这覆盖了“native output 已解析但 prompt scope / ButtonArt token 没切到 Gamepad”的结构风险。
- 额外 focused 验证已通过：`xmake build -y DualPadInputV2Tests`；`xmake run -y DualPadInputV2Tests`；`xmake build -y DualPadPromptSnapshotTests`；`xmake run -y DualPadPromptSnapshotTests`；`xmake build -y DualPadGlyphResolutionCompatTests`；`xmake run -y DualPadGlyphResolutionCompatTests`。
- close-out：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1708 nodes, 3656 edges, 149 communities`；`git diff --check` exit 0，仅有 Windows 行尾提示，无 whitespace error。
- 部署版本未变化：`G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll` SHA256 `54E8FEB436F779160FE564EE9942CA5D7438341B03507F62C6835EB79C7282BE`，`G:/g/SkyrimSE/dinput8.dll` SHA256 `59667D5D9E2599C51F1F630F83D2B2E7CB08F925B3292F228B2D658D55F0409A`。最新实机日志仍停在 `2026/6/12 23:36:31`，所以本轮仍不能把 goal 判定完成。

## 2026-06-14 21:08:00 +08:00

- 继续执行宽门禁复核：运行 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1 *> build/phase8-latest.log`，进程 `$LASTEXITCODE=1`。日志中的所有 build/run/doc/release readiness/config closure 步骤均已跑到最后，唯一 `command failed` 为最终 `git diff --exit-code -- docs/generated`。
- 失败原因仍是已知 generated docs drift：`python scripts/dev/generate_dualpad_docs.py` 重新生成 `docs/generated/*_zh.md`，manifest hash 从 `83cf2c598e9c215d` 更新为 `a20d0b8aadef9076`，因此当前未提交工作树下不能把 Phase8 记为 clean pass。该 drift 与本轮 field readiness 代码/配置改动保持在同一工作树，后续如提交应一并纳入。
- 复核日志开关：`config/DualPadDebug.ini` 与已部署的 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadDebug.ini` 内容一致，`log_input_packets`、`log_input_hex`、`log_input_state`、`log_mapping_events`、`log_synthetic_state`、`log_action_plan`、`log_native_injection`、`log_keyboard_injection`、`log_route_health`、`enable_trace_recording`、`trace_record_glyph_queries` 均为 `true`。部署 DLL 未变化，仍为 SHA256 `54E8FEB436F779160FE564EE9942CA5D7438341B03507F62C6835EB79C7282BE`；最新实机 `DualPad.log` 仍停在 `2026/6/12 23:36:31`，尚无本轮 DLL 的进游戏样本。

## 2026-06-14 21:11:00 +08:00

- 为避免后续实机样本继续混入旧 `field-debug-2026-06-12` trace session，将 `config/DualPadDebug.ini` 的 `trace_session` 改为 `field-debug-2026-06-14-rc20-live` 并运行 `xmake build -y DualPad`。构建 exit 0，DLL 已部署；PDB 因目标占用跳过。已核对 repo 配置与 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadDebug.ini` 均指向新 trace session，且 `enable_trace_recording=true`、`trace_record_glyph_queries=true`、`log_action_plan=true`、`log_native_injection=true`、`log_route_health=true`。新 trace 目录 `G:/skyrim_mod_develop/overwrite/SKSE/Plugins/DualPadTrace/field-debug-2026-06-14-rc20-live` 当前不存在；下一次实机启动若写入该目录即可证明采样来自本轮配置。
- 计划条目差距复核：`rg` 确认 PR-A 的 `rc-readiness` / `DualPadManifestCompilerTests` / release manifest 新命名标记存在；PR-B1 的 `ContextEventSink -> ScaleformGlyphBridge::OnMenuOpened/OnMenuClosed` 与 per-menu delegate map 存在；PR-B2 的 upstream install status / route health / `HookInstallFailed` / overlay gate 存在；PR-B3 的 `ParsedIniFile` / broken INI fail-closed 路径存在。
- 本轮 gate 验证均 exit 0：`python scripts/ci/check_rc_readiness_closeout.py`；`python scripts/ci/check_config_prompt_menu_glyph_closure.py`；`python scripts/ci/check_reviewed_docs_consistency.py`；`python scripts/ci/check_release_readiness.py`；`xmake build/run -y DualPadManifestCompilerTests`；`xmake build/run -y DualPadRouteHealthContractTests`；`xmake build/run -y DualPadPromptSnapshotTests`；`xmake build/run -y DualPadInputV2Tests`；`python -m json.tool .dualpad-builder/feature_list.json`；`python -m json.tool .dualpad-builder/sprint_plan.json`；`git diff --check`（仅 Windows 行尾提示）。
- 按仓库 close-out 规则重建 graphify：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1708 nodes, 3656 edges, 149 communities`。最终核对：部署 DLL SHA256 仍为 `54E8FEB436F779160FE564EE9942CA5D7438341B03507F62C6835EB79C7282BE`；`DualPadDebug.ini` 已部署为 `trace_session=field-debug-2026-06-14-rc20-live`；实机 `DualPad.log` 仍停在 `2026/6/12 23:36:31`；新 trace 目录仍不存在，尚无本轮配置的进游戏样本。

## 2026-06-14 21:15:00 +08:00

- 新增只读现场样本摘要工具：`scripts/dev/summarize_dualpad_field_capture.py`。该脚本读取 `DualPad.log` 与单个 `DualPadTrace` session，汇总 Skyrim compat hook、upstream hook、route health、HID input state、runtime plan、native commit、trace CSV 行数、authoritative poll、presentation owner 与 glyph 结果，并给出 `first_breakpoint`。用途是下一次实机反馈后快速定位断在输入、ingress、runtime plan、native commit、presentation owner 还是 glyph resolution，而不是继续手工扫大日志。
- 用旧样本验证工具：`python scripts/dev/summarize_dualpad_field_capture.py --log C:/Users/xuany/Documents/My Games/Skyrim Special Edition/SKSE/DualPad.log --trace-dir G:/skyrim_mod_develop/overwrite/SKSE/Plugins/DualPadTrace/field-debug-2026-06-12` exit 0。输出显示旧样本 `skyrim_compat_success=True`、`upstream_hook_installed=True`、`route_health=486 active_fresh`、`input_nonzero_mask_lines=2414`，但 `expected_presentation_surface.csv` 的 `gamepad_owner_rows=0`，`first_breakpoint=presentation_owner`；同时旧样本没有当前版 `[DualPad][RuntimePlan]` / `[DualPad][NativeButtonCommit]` 标记，证明它不能替代本轮 DLL 的实机证据。
- 用新 session 空样本验证工具：非 strict 运行 `field-debug-2026-06-14-rc20-live` exit 0 并报告 `first_breakpoint=trace_missing`；strict 运行 exit 2，输出已写入 `build/field-capture-missing-strict.log`。基础验证：`python -m py_compile scripts/dev/summarize_dualpad_field_capture.py` exit 0；`git diff --check` exit 0（仅 Windows 行尾提示）；`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1719 nodes, 3685 edges, 147 communities`。

## 2026-06-14 21:19:00 +08:00

- 增强 `scripts/dev/summarize_dualpad_field_capture.py` 的日志字段解析：现在除了 marker 计数，还会提取 `upstream_poll_active_lines`、`runtime_plan_active_lines`、`runtime_plan_actions`、`native_commit_stages`、`native_commit_actions`、`native_queue_false`、`native_translate_failed`。下一次实机日志若出现 `RuntimePlanAction` 但没有 `NativeButtonCommit`，或 native queue/translate 失败，脚本会把 `first_breakpoint` 指向 native commit 边界。
- 重新用旧 `field-debug-2026-06-12` 样本验证：exit 0，仍报告 `first_breakpoint=presentation_owner`，并额外识别 `upstream_poll_active_lines=64`、`runtime_plan_actions={}`、`native_commit_stages={}`，说明旧样本只适合证明旧断点，不含当前版 runtime/native commit 日志。新 session strict 验证仍 exit 2，`first_breakpoint=trace_missing`。
- 用 `build/field-capture-synthetic` 临时合成一个最小当前版日志/trace：包含 `RuntimePlanAction action=Menu.ScrollDown`、`NativeButtonCommit apply/queue/poll`、Gamepad presentation 与成功 glyph。运行 `python scripts/dev/summarize_dualpad_field_capture.py --strict --log build/field-capture-synthetic/DualPad.log --trace-dir build/field-capture-synthetic` exit 0，输出 `first_breakpoint=none`、`runtime_plan_actions={'Menu.ScrollDown': 1}`、`native_commit_stages={'apply': 1, 'queue': 1, 'poll': 1}`、`native_commit_actions={'Menu.ScrollDown': 3}`。
- 验证：`python -m py_compile scripts/dev/summarize_dualpad_field_capture.py` exit 0；`git diff --check` exit 0（仅 Windows 行尾提示）；`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1720 nodes, 3687 edges, 146 communities`。清理了 `py_compile` 生成的 `scripts/dev/__pycache__`。

## 2026-06-14 21:18:04 +08:00（阻塞审计）

- 复核本机外部状态：`C:/Users/xuany/Documents/My Games/Skyrim Special Edition/SKSE/DualPad.log` 仍停在 `2026/6/12 23:36:31`；`G:/skyrim_mod_develop/overwrite/SKSE/Plugins/DualPadTrace/field-debug-2026-06-14-rc20-live` 仍不存在。也就是说，还没有本轮 DLL / debug config 的真实进游戏样本。
- 当前已部署版本仍为 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll` SHA256 `54E8FEB436F779160FE564EE9942CA5D7438341B03507F62C6835EB79C7282BE`，且已部署 `DualPadDebug.ini` 指向 `trace_session=field-debug-2026-06-14-rc20-live` 并开启 `log_action_plan` / `log_native_injection` / `log_route_health` / `enable_trace_recording`。
- 阻塞原因：同一阻塞条件已连续多轮重复。没有新实机样本时，无法证明“进游戏手柄能操控 / glyph 已正确翻译”，也无法基于当前版本继续定位新的断点。下一步必须来自一次新的 Skyrim 进程实测；实测后运行 `python scripts/dev/summarize_dualpad_field_capture.py --log "C:/Users/xuany/Documents/My Games/Skyrim Special Edition/SKSE/DualPad.log" --trace-dir "G:/skyrim_mod_develop/overwrite/SKSE/Plugins/DualPadTrace/field-debug-2026-06-14-rc20-live"`。

## 2026-06-14 22:47:00 +08:00

- 用户新反馈：本轮部署后“还是不行”。这次已有新实机样本：`DualPad.log` LastWriteTime `2026/6/14 22:44:10`，长度 `7618618`；`G:/skyrim_mod_develop/overwrite/SKSE/Plugins/DualPadTrace/field-debug-2026-06-14-rc20-live` 已生成 trace CSV。
- 运行 `python scripts/dev/summarize_dualpad_field_capture.py --log "C:/Users/xuany/Documents/My Games/Skyrim Special Edition/SKSE/DualPad.log" --trace-dir "G:/skyrim_mod_develop/overwrite/SKSE/Plugins/DualPadTrace/field-debug-2026-06-14-rc20-live"`，输出 `first_breakpoint=native_commit_translate`。关键证据：`skyrim_compat_success=True`、`upstream_hook_installed=True`、`route_health=1075 active_fresh`、`input_nonzero_mask_lines=3236`、`runtime_plan=1133`、`runtime_plan_action=763`、`native_button_commit=43`、`expected_presentation_surface.csv rows=1641 gamepad_owner_rows=1641`。
- 本轮不再是旧的 presentation owner 断点：presentation 已全程 Gamepad；trace 中 `glyph_queries.csv` / `expected_glyph_results.csv` 行数仍为 0，说明本次没有触发 glyph API 查询。native commit 侧出现一次翻译失败：`[DualPad][NativeButtonCommit] translate_failed action=Menu.Confirm phase=Release contract=Pulse digitalPolicy=PulseMinDown outputCode=Menu.Confirm`。同时已有成功排队/提交样本：`Menu.ScrollDown`、`Menu.Confirm Press`、`Menu.Right` 均出现 `apply` / `queue queued=true` / `poll managed=true down=true`。
- 当前结论：本轮实机断点收窄到 native commit 的 release/translate 语义或后续虚拟手柄提交与 Skyrim 消费之间的边界，不能再按 presentation owner / hook install / route health 方向继续猜。

## 2026-06-14 23:47:21 +08:00

- DP5-RC20 hotfix PR3 已完成本地实现：`device_report` 级 sequence gap 现在只输出 `[DualPad][SequenceGap] ... recovery=SoftGap` 诊断，不再向 frame assembler 注入 runtime recovery transition；runtime snapshot seq gap 仍生成 soft transition，但不再 flush pulse edge、不再清 native/helper/sustained output，也不再清 projection sticky owner。`snapshot.coalesced` 不再触发 `ExplicitReset`，只保留 stable frame diagnostic health，保证 latest-wins 输入不制造 `HardResetOutputs` 风暴。
- 已补 focused 覆盖：`CoalescedHidReports_DoNotHardResetOutputs`、`SequenceGapWithoutDroppedDigitalEdges_IsSoftGap`、`RuntimeSnapshotSeqGap_WithoutBoundaryChange_DoesNotClearAuthoritativePoll`、`ContextEpochChange_HardResetsOnce`、`ManifestEpochChange_HardResetsOnce`、`TransitionFrame_DoesNotDispatchActions`、`OverflowDroppingDigitalEdges_HardResetsOutputs`（既有 overflow 覆盖保留）、`AxisOnlyCoalescing_DoesNotClearHeldButton`。
- 已运行并通过：`xmake build/run -y DualPadIngressTests`；`xmake build/run -y DualPadReplayTests`；`xmake build/run -y DualPadGameplayProjectionTests`；`xmake build/run -y DualPadInputV2Tests`；`git diff --check` exit 0（仅 Windows 行尾提示，无 whitespace error）。

## 2026-06-14 23:51:14 +08:00

- DP5-RC20 hotfix PR4 已完成本地实现：`ControlMapOverlay` 计数改为固定可信 device slot 集合，`mappingCount > 100000` 时记录 `[DualPad][ControlMapOverlay] impossible mapping count ... failClosed=true` 并返回失败，不再继续输出可信的 `Applied ... mappings=...`。这会阻止 `mappings=25173386696` 这类不可信计数被当成成功 overlay。
- `scripts/dev/summarize_dualpad_field_capture.py` 已扩展 smoke 字段：`hook_installed`、`hid_raw_buttons_seen`、`cross_raw_seen`、`menu_cancel_plan_seen`、`menu_confirm_release_translate_failed_count`、`sequence_gap_count`、`hard_reset_outputs_count`、`impossible_controlmap_mapping_count`。当前 `2026-06-14 22:44` 实机样本的 summary 现在 `first_breakpoint=menu_cancel_plan_missing`，并明确警告 Cross raw 没有 `RuntimePlanAction action=Menu.Cancel`、`Menu.Confirm phase=Release` translate_failed、`HardResetOutputs` 计数为 59、`impossible_controlmap_mapping_count=1`。
- 已运行并通过：`python -m py_compile scripts/dev/summarize_dualpad_field_capture.py`；非 strict field summary exit 0；strict field summary 按预期 exit 2；`xmake build -y DualPad` exit 0，并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，SHA256 `81F8E2AE9902CC04235BC8719DC36FA90C548E14E3A9B5CC2CE39D6ED65E3DE4`。

## 2026-06-14 23:56:41 +08:00

- DP5-RC20 hotfix 最终验证：已运行全部 `*Tests` target 并通过（`DualPadGameplayProjectionTests`、`DualPadReplayTests`、`DualPadPresentationProjectionTests`、`DualPadPromptSnapshotTests`、`DualPadManifestCompilerTests`、`DualPadPropertyTests`、`DualPadReplayHarnessTests`、`DualPadGlyphResolutionCompatTests`、`DualPadNativeButtonCommitTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadContextResolverTests`、`DualPadRouteHealthContractTests`、`DualPadFuzzRegressionTests`）。
- readiness / CI：`python scripts/ci/check_release_readiness.py`、`python scripts/ci/check_rc_readiness_closeout.py`、`python scripts/ci/check_config_prompt_menu_glyph_closure.py`、`python scripts/ci/check_reviewed_docs_consistency.py` 均通过；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` 重新通过；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1` 重新通过。第一次 Phase8/RC 失败点为 `docs/generated` manifest hash drift，已将 docgen 输出 `970e71eb9dea87e1` 纳入提交后复跑通过。
- before/after 日志摘要：已生成当前实机失败样本的 before 摘要到 `build/dp5-rc20-field-summary-before.txt` / `.json`，结论 `first_breakpoint=menu_cancel_plan_missing`，包含 Cross raw seen、Menu.Cancel plan missing、Confirm Release translate_failed、HardResetOutputs 59、impossible mapping count 1。当前没有新的 post-fix 实机日志，不能伪造 after 实机摘要；after 证据来自代码级测试、field summary smoke 和 CI。已部署构建：`G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll` SHA256 `81F8E2AE9902CC04235BC8719DC36FA90C548E14E3A9B5CC2CE39D6ED65E3DE4`；`G:/g/SkyrimSE/dinput8.dll` SHA256 `59667D5D9E2599C51F1F630F83D2B2E7CB08F925B3292F228B2D658D55F0409A`。

## 2026-06-15 00:16:00 +08:00

- 用户新反馈：上一版部署后进游戏仍无法操控。重新切分最新实机样本：`DualPad.log` LastWriteTime `2026/6/14 23:59:49`，summary 仍为 `first_breakpoint=menu_cancel_plan_missing`；关键证据是 `cross_raw_seen=True`、`menu_cancel_plan_seen=False`、`runtime_plan_actions={'Menu.LeftStick': 610, 'Menu.DownloadAll': 2}`、`native_commit_actions={'Menu.DownloadAll': 8}`、`sequence_gap_count=86`、`hard_reset_outputs_count=87`、`controlmap_mapping_counts=[304]`。
- 根因 1：`FrameAssembler::UpsertLatestSample` 对同一路径样本执行纯 latest-wins 覆盖，导致同一 stable window 内 `Cross Press` 后续 held 报告把 `pressed=true` 覆盖成 `pressed=false`。`pulseLedger` 虽保留边沿，但 `BuildKernelFrame` 传给 `InteractionEngine` 的是 `controlSamples`，因此 resolver 看见 `Cross down` 却没有 press edge，不会产出 `RuntimePlanAction action=Menu.Cancel`，且不会出现 `ResolveMiss`。
- 根因 2：没有 pending `DeviceMarker` 时，较新的 `SourceEvidence.deviceFamilyRevision` 被当成 `ExplicitReset` fatal mismatch。实机 trace 中 marker/evidence 高频队列恢复可出现 marker 缺失或错位，这会造成 `explicit_reset` / `HardResetOutputs` 风暴；真正 fatal 仍保留在 pending marker revision mismatch 路径。
- 修复：`FrameAssembler` 合并同路径 stable sample 时，状态/标量/最新 timestamp 仍取最新，但 `pressed` / `released` 用窗口内 OR 语义保留，`downAtUs` 保留非零最早值；无 pending marker 且 source evidence revision 向前时改为 `BoundaryKeyChanged` 软同步并应用 evidence，不再硬清输出。已补红灯再绿灯覆盖：`TestCoalescedHeldHidPressSampleTriggersInteractionEngine`、`RunRuntimeFrameEnvelopeUsesActiveConfigGraphForMenuCrossCancelTests` 的 Cross held 变体、`TestMissingDeviceMarkerSourceEvidenceSoftSyncsBoundary`。
- 验证：新增红灯先在旧实现失败（`FAIL: coalesced held HID press must retain the press edge`；`FAIL: missing device marker source evidence must not hard reset repeatedly`），修复后 `xmake build/run -y DualPadIngressTests` 与 `xmake build/run -y DualPadInputV2Tests` 均通过。完整门禁也已通过：`powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` exit 0；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1` exit 0，包含 replay diff、release artifact manifest、graphify rebuild（`1753 nodes, 3856 edges, 148 communities`）与 `git diff --check`。仅 PDB 部署提示目标文件被占用，DLL 本体部署成功。
- 新部署 DLL：`G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll` SHA256 `2CFA9532DA113F8D1BA16DCCB2F3D13BA0C1F2B7EAAB2536A4B9B44109828F1B`。当前仍没有 post-fix 实机日志；修复前 summary 仍按预期报告 Cross raw 有但 Menu.Cancel plan 缺失和 HardResetOutputs storm。

## 2026-06-15 00:27:00 +08:00

- 用户新反馈：上一版已经“有反应”，但按住时会连续不断重复触发。重新分析最新实机日志：`DualPad.log` LastWriteTime `2026/6/15 00:18:30`，summary 显示 `menu_cancel_plan_seen=True`、`native_commit_actions` 包含 `Menu.Cancel`，因此断点已从“没有计划动作”推进到“已发布边沿被后续 stable frame 重复消费”。
- 根因：上一轮为修复同一 stable window 内 coalesced HID press 被 held 样本覆盖的问题，引入了窗口内 `pressed/released` OR 语义，但 `FrameAssembler::FlushWindow` 仍把完整 `_window.facts` 带入 `_latestFacts`，`StartWindow` 又直接用 `_latestFacts` 播种下一窗口。结果已经发布过的 `pressed/released` 和 `pulseLedger` 被携带到后续 stable frame，导致 `Menu.Cancel phase=Press/Release` 在按住或松开后每帧重复生成。
- 修复：新增 durable carry 清洗语义，只把电平状态、标量和按住起点带入下一窗口；发布型边沿 `pressed/released`、`pulseLedger`、一次性 health 与 compaction diagnostic 在 stable frame 发布后清除。这样同一窗口内仍保留 coalesced press edge，但跨窗口不会重复提交一次性菜单动作。
- 已补红灯再绿灯覆盖：`TestPublishedPressEdgeDoesNotCarryIntoNextStableFrame`、`TestPublishedReleaseEdgeDoesNotCarryIntoNextStableFrame`、`MenuCross_HeldFrameDoesNotRepeatMenuCancel`。旧实现下 `DualPadIngressTests` 先失败于 `published press edge must not carry into the next stable frame`，修复后通过。
- 验证：`xmake build/run -y DualPadIngressTests` 通过；`xmake build/run -y DualPadInputV2Tests` 通过；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` exit 0；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1` exit 0。RC readiness 覆盖 Phase8、replay diff、release artifact manifest、graphify rebuild（`1756 nodes, 3884 edges, 147 communities`）与 `git diff --check`。仅 PDB 复制提示目标文件占用，DLL 本体部署成功。
- 新部署 DLL：`G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll` SHA256 `FECB909C507FB67D84EA896370A41546A410FB32883901FECA1D8DB7942A9C13`。当前仍需要一次 post-fix 进游戏实测确认真实手感；本次代码级回归已经覆盖按住 Cross 不应重复发 `Menu.Cancel` native transient。

## 2026-06-15 00:44:00 +08:00

- 用户新反馈：按三角后激活项往下走一个，这是旧问题复现。重新切最新实机日志：`DualPad.log` LastWriteTime `2026/6/15 00:31:59`，summary 显示 `runtime_plan_actions={'Menu.LeftStick': 2, 'Menu.Confirm': 2}`、`native_commit_actions={'Menu.Confirm': 8}`、`native_translate_failed=0`、`sequence_gap_count=5`、`hard_reset_outputs_count=1`、`controlmap_mapping_counts=[304]`。关键手工证据：三角 raw `mask=0x00000008` 后生成 `RuntimePlanAction action=Menu.Confirm phase=Press/Release`，native 输出为 `xinputButtons=0x1000`，没有 `Menu.ScrollDown` / DPadDown 计划或提交；但 `Menu.Confirm` 的可见 down 持续约 `0.049s`，跨多个 upstream poll。
- 根因：`Menu.Confirm` descriptor 已声明 `ActionLifecyclePolicy::DeferredPulse`，但 `GameplayProjectionFrame` 的 native command 没有携带 lifecycle；`DualPadRuntimeLive`、`FrameActionPlanner`、`ActionLifecycleCoordinator` 又都把 `ActionOutputContract::Pulse` 直接解析为 `PulseMinDown`，因此菜单确认被错误拉成长按窗口。这个问题不是 Triangle 被解析成 ScrollDown，也不是 raw mask/DPad nibble 混淆。
- 修复：新增统一 `NativeDigitalPolicyResolver`，按 `(backend, kind, contract, lifecyclePolicy)` 解析 native digital policy；新增 `NativeDigitalPolicyKind::DeferredPulse`，`DeferredPulse` 仍走 poll-owned pulse request，但 `minDownMs=0`，`MinDownWindowPulse` 才保留 `40ms`。`GameplayProjectionFrame` 的 transient/sustained native command 现在携带 `ActionLifecyclePolicy`，live runtime 构建 `PlannedAction` 时保留该字段；旧 planner/coordinator 也改用同一个 resolver。
- 已补红灯再绿灯覆盖：`Menu.Confirm must keep DeferredPulse lifecycle through gameplay projection` 旧实现先在 `DualPadGameplayProjectionTests` 失败；修复后新增/扩展 `MenuConfirm_DeferredPulse_DoesNotUseMinDownWindow`、`GameActivate_MinDownWindowPulse_UsesDefaultMinDown`、`RunGameplayActivateKeepsMinDownWindowLifecycleTests` 等覆盖，确认菜单确认不使用 min-down window，而 gameplay `Game.Activate` 继续使用 `PulseMinDown=40ms`。
- 验证：`xmake run -y DualPadGameplayProjectionTests` 先红后绿；`xmake run -y DualPadNativeButtonCommitTests` 通过；`xmake run -y DualPadInputV2Tests` 通过；`xmake build -y DualPad` 通过并部署 DLL（PDB 因目标占用跳过）；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` exit 0；field summary 对当前实机旧日志 exit 0，仍显示修复前 `Menu.Confirm` 样本。`powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest` 当前按预期因未提交工作树 dirty 失败，待本轮提交后复跑。
- close-out：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1752 nodes, 3881 edges, 147 communities`；`git diff --check` exit 0（仅 Windows 行尾提示，无 whitespace error）。新部署 DLL SHA256 `9E52E7584C7F0E959378B4F2B7146558CCB05D06147381A4D110441188661DC9`。当前仍需要一次 post-fix 进游戏实测确认三角确认不会再跨 poll 形成旧菜单下移副作用。

## 2026-06-15 01:02:07 +08:00

- 用户新反馈：上一版仍复发，并要求参考老链路文档和代码。重新对照 `0d2c93a` 旧链路：`AxisProjection.cpp` 直接把 `leftStickX.value -> moveX`、`leftStickY.value -> moveY`，`FrameActionPlanner::PlanAxisValue` 保留二维 valueX/valueY；当前实机日志中 raw `ls=(-0.012,0.012)` 后 runtime projection 却出现 `analog=move(-0.012,0.000)`，说明不是 Triangle 误解析，而是菜单左摇杆 Y 轴在新链路被错误降维。
- 根因：`NativeActionDescriptor.cpp` 已声明 `Menu.LeftStick` 为 `PlannedActionKind::NativeAxis2D` / `NativeAxisTarget::MoveStick`，但 `ActionManifest.cpp::InferActionValueKind` 没有从 native descriptor 派生 value kind，也没有列出 `Menu.LeftStick`，导致 checked-in `Axis:LeftStickX/Y=Menu.LeftStick` 不能按 Axis2D 合并，Y 轴被落到错误的 `ActionValueSnapshot.x` 投影形状，形成旧菜单“按确认后激活项下移”的复发条件。
- 修复：`ActionManifest` 现在优先查询 `FindNativeActionDescriptor(actionId)`，对 `NativeAxis2D` / `NativeAxis1D` 分别返回 `ActionValueKind::Axis2D` / `Axis1D`，非 native action 才继续走原有启发式。新增 manifest 测试锁定 `Menu.LeftStick` 必须是二维 native axis；新增 runtime envelope 测试用 checked-in config 发布 Menu context，注入 `leftStick.x=0.25f`、`leftStick.y=-0.75f`，断言 native projection 分别得到 `moveX=0.25f`、`moveY=-0.75f`。
- 已补红灯再绿灯覆盖：旧实现下 `DualPadManifestCompilerTests` 先失败于 `Menu.LeftStick must be a two-dimensional native axis action`，`DualPadInputV2Tests` 先失败于 `Menu.LeftStick X must project to native moveX from checked-in bindings`；修复后两者通过。
- 验证：`xmake build/run -y DualPadManifestCompilerTests` exit 0；`xmake build/run -y DualPadInputV2Tests` exit 0；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` exit 0，覆盖 `DualPad` build、Replay/InputV2/PresentationProjection/ManifestCompiler/Ingress/RouteHealthContract/NativeButtonCommit/PromptSnapshot/Property/FuzzRegression 测试、docgen 和 release readiness checks。`run_phase8_ci.ps1` 已重新部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；PDB 复制仍因目标占用被跳过，但 DLL 本体部署成功。
- close-out：`powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest` 已完整跑过 Phase8、RC closeout、dinput8 proxy build 和 release artifact manifest 生成，最后只因当前修复尚未提交导致 clean-tree gate 失败；本轮提交后会复跑该命令。`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1753 nodes, 3898 edges, 147 communities`。

## 2026-06-15 21:10:00 +08:00

- 用户新反馈：前两次补丁思路可能错误，要求先完全确定“第一次下移”的原因，不再继续止血式修复。重新复核最新实机日志 `DualPad.log`：首个 Triangle raw 为 `mask=0x00000008` / `dpadNibble=0x8`，随后只生成 `RuntimePlanAction action=Menu.Confirm phase=Press`，native 输出为一次 `xinputButtons=0x1000`，窗口内没有 `Menu.ScrollDown`、没有 `xinputButtons=0x0002`、没有 D-pad down raw，move 输出为 `(0.000,0.000)`。实机 `DualPadControlMap.txt` 与仓库 controlmap SHA256 一致，`Accept=0x1000`、`Down=0x0002`。
- 当前结论：日志已经能证明 DualPad 在第一次 Triangle 窗口没有发 Down；但还不能证明 UI 下移来自 A/Accept 下游、游戏/Scaleform 焦点副作用，还是非 DualPad 输出漏入。为做因果隔离，本轮只加入诊断开关 `suppress_menu_confirm_native_output_probe`：resolver 和 action plan 仍照常生成 `Menu.Confirm`，但打开 probe 时 `NativeButtonCommitBackend` 在 commit queue 前 suppress native A/Accept 输出，并打印 `[DualPad][MenuConfirmProbe] suppress_native_output ...`。
- 已按 TDD 补覆盖：先让 `DualPadNativeButtonCommitTests` 红灯失败于缺失 `ShouldSuppressNativeButtonCommitOutputForProbe`；实现后新增 `MenuConfirmProbe_EnabledSuppressesPressBeforeQueue`、`MenuConfirmProbe_DisabledKeepsPressQueueable`、`MenuConfirmProbe_DoesNotSuppressReleaseNoop`、`MenuConfirmProbe_DoesNotSuppressOtherMenuActions`，确认 probe 只影响 `Menu.Confirm` Press/Pulse，Release 仍保持 no-op，其他菜单动作不受影响。
- 验证与部署：`xmake build -y DualPadNativeButtonCommitTests` 和 `xmake run -y DualPadNativeButtonCommitTests` exit 0；`xmake build -y DualPad` exit 0 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，DLL LastWriteTime `2026/6/15 21:09:33`。`DualPadDebug.ini` 已部署为 `suppress_menu_confirm_native_output_probe = true`，供下一次进游戏 A/B：如果按 Triangle 仍第一次下移，原因在 DualPad native A 输出之外；如果不再下移，原因在 A/Accept 下游路径。

## 2026-06-15 21:23:26 +08:00

- 用户实测 suppress probe：按 Triangle 不再下移，但 Triangle 也无确认反应。最新日志确认启动配置 `menuConfirmSuppressProbe=true`，首个 Triangle 生成 `RuntimePlanAction action=Menu.Confirm phase=Press` 后被 `[DualPad][MenuConfirmProbe] suppress_native_output ...` 拦截，没有进入 native A commit。该实验把根因收窄到 `Menu.Confirm -> gamepad A/Accept(0x1000)` 下游消费路径本身；不是 raw mask、D-pad、Menu.ScrollDown、摇杆或未管控输出漏入。
- 对照 `0d2c93a` 旧链路：`Menu.Confirm` 当时也在 `NativeActionDescriptor` 中映射为 `NativeButtonCommit + VirtualPadButtonRoleCross`，并非 KeyboardHelper 或 Scaleform 直接调用。因此本轮继续做第二个 A/B，而不是把旧 native A 方案再补一遍。
- 新增诊断开关 `route_menu_confirm_keyboard_activate_probe`：开启时 `Menu.Confirm` 仍正常 resolve / log action plan，但不发 native A，而是通过现有 dinput8 keyboard bridge 发 PC Activate `DIK_E(0x12)` pulse。`KeyboardHelperBackend::TriggerRawScancodePulseForDiagnostics` 只用于诊断 raw scancode，不扩展公开 `VirtualKey.*` / `ModEvent` ABI。部署配置已改为 `suppress_menu_confirm_native_output_probe = false`、`route_menu_confirm_keyboard_activate_probe = true`。
- 验证与部署：`xmake build/run -y DualPadNativeButtonCommitTests` exit 0；`xmake build -y DualPad` exit 0 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，DLL LastWriteTime `2026/6/15 21:23:08`。dinput8 代理已在 `G:/g/SkyrimSE/DualPadDInput8.log` 中确认包住 keyboard device，且代理代码对 `DIK_E` 有 release 补偿。下一次实测判定：若 Triangle 能确认且不下移，根因就是 gamepad A/Accept 下游副作用；若仍无反应，则 PC Activate 不是该菜单确认入口，需要继续查 Scaleform / menu-specific confirm path。

## 2026-06-15 22:13:47 +08:00

- 用户实测 keyboard activate probe：确认仍无反应。复核日志确认 `Menu.Confirm` Press/Release 都生成，12 次 `[DualPad][MenuConfirmProbe] route_keyboard_activate ...` 与 12 次 `KeyboardHelper diagnostic pulse scancode=0x12` 均出现；`G:/g/SkyrimSE/DualPadDInput8.log` 也确认 dinput8 消费了 12 次 `DIK_E` press/release。因此 `DIK_E` 键盘桥不是该菜单确认入口，不能把 KeyboardHelper 路由当正式修法。
- 新增第三个隔离 probe `route_menu_confirm_input_event_accept_probe`：`Menu.Confirm` 仍正常 resolve / log action plan，但不发 native A current-state，也不走 dinput8，而是在 `BSInputEventQueue` 追加 `userEvent=Accept`、`device=kGamepad`、`id=0x1000` 的 ButtonEvent press/release。队列不足两个 button slot 时 fail closed 并记录 queue count，避免只追加 press 造成粘滞。
- 已按 TDD 补覆盖：先让 `DualPadNativeButtonCommitTests` 红灯失败于缺失 `ShouldRouteMenuConfirmToInputEventAcceptProbe` / `kMenuConfirmInputEventAcceptProbeGamepadId`，实现后新增 `MenuConfirmInputEventAcceptProbe_EnabledRoutesPressToAcceptEvent`、`DisabledKeepsNativeRoute`、`DoesNotRouteRelease`、`DoesNotRouteOtherMenuActions`，确认 probe 只影响 Menu.Confirm Press/Pulse。
- 验证与部署：`xmake build -y DualPadNativeButtonCommitTests`、`xmake run -y DualPadNativeButtonCommitTests`、`xmake build -y DualPad` 均 exit 0；DLL 已部署到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/15 22:13:27`。现场 `DualPadDebug.ini` 已确认 `route_menu_confirm_keyboard_activate_probe=false`、`route_menu_confirm_input_event_accept_probe=true`。`git diff --check` exit 0（仅 Windows 行尾提示，无 whitespace error）。下一次实测判定：若 Triangle 确认且不下移，断点是 XInput current-state Accept 消费语义；若仍无反应，则直接排入 input queue 的 `Accept` ButtonEvent 也不是该菜单确认入口，下一步应转向菜单/Scaleform 层确认入口而不是继续换 native 输出外壳。

## 2026-06-15 22:20:00 +08:00

- 用户反馈第三个 probe 闪退。最新 `DualPad.log` LastWriteTime `2026/6/15 22:17:01`，启动配置确认为 `menuConfirmInputEventAcceptProbe=true`；日志在 `[DualPad][NativeButtonCommit] apply action=Menu.Confirm phase=Press ... outputCode=Menu.Confirm` 后立刻截断，未打印 `route_input_event_accept` 成功/失败行。未在 `Documents/My Games/Skyrim Special Edition`、`G:/skyrim_mod_develop/overwrite` 或 `G:/g/SkyrimSE` 下找到新 crash dump。
- 回顾旧基线与文档：`0d2c93a` 的 `NativeActionDescriptor` 将 `Menu.Confirm` 定义为 `NativeButtonCommit + DeferredPulse + VirtualPadButtonRoleCross`；`docs/backend_routing_decisions.md` 与 `docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md` 明确当前正式主线是 `AuthoritativePollState -> XInputStateBridge -> Skyrim Poll`，不是 direct `ButtonEvent/InputEventQueue` 拼接，也不是 keyboard-native 主线。第三个 probe 与这个契约冲突，已判定为错误实验方向。
- 已撤销 direct `BSInputEventQueue` probe：从 `RuntimeConfig`、`NativeButtonCommitBackend`、`DualPadDebug.ini` 和 `DualPadNativeButtonCommitTests` 中移除 `route_menu_confirm_input_event_accept_probe` 相关代码；现场 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadDebug.ini` 已同步为只保留 `suppress_menu_confirm_native_output_probe=false`、`route_menu_confirm_keyboard_activate_probe=false`。
- 对“第一次下移”的结论更正：已证明的部分是，DualPad 在首个 Triangle 窗口没有发 `Menu.ScrollDown`、没有 D-pad Down、move=(0,0)，且 suppress native A 后下移消失；这只能证明下移依赖 `Menu.Confirm -> native A/Accept(0x1000)` 下游路径。尚未完全证明具体消费者是 Skyrim menu producer、Scaleform 焦点逻辑、ControlMap overlay 语义还是其它菜单态 handler。因此不能再说“完整找到了下移根因”；后续必须比较当前与 `0d2c93a` 在 poll-owned XInput current-state 的时序、context、controlmap overlay 与菜单模式身份上哪里不同。
- 继续对照旧基线时发现一个实际合同漂移：`NativeActionDescriptor` 中 `Menu.Confirm` 仍是 `ownsLifecycle=false` / 非 gate-aware 语义，但 `GameplayProjectionFrame` 对所有 transient native command 写死 `.gateAware=true`，导致实机 `NativeButtonCommit apply action=Menu.Confirm ... gateAware=true`。这不是已证明的下移根因，但它会污染菜单 native output 的恢复/诊断语义，也偏离旧 `FrameActionPlanner` 通过 `IsNativeDigitalGateAwareAction()` 决定 gate-aware 的合同。
- 修复合同漂移：`GameplayProjectionFrame` 现在用 `ResolveNativeDigitalPolicy()` + `IsNativeDigitalGateAwareAction()` 生成 transient command 的 `gateAware`，并补测试锁定 `Menu.Confirm` 在 menu projection 中必须 `gateAware=false`、`Game.Activate` 在 gameplay projection 中仍 `gateAware=true`。红灯先失败于 `Menu.Confirm must not be marked gate-aware in menu projection`，修复后通过。
- 验证与部署：`xmake build/run -y DualPadGameplayProjectionTests`、`xmake build/run -y DualPadInputV2Tests`、`xmake build/run -y DualPadNativeButtonCommitTests` 均 exit 0；`xmake build -y DualPad` exit 0 并部署 DLL 到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/15 22:25:24`，SHA256 `DAF0D69EDDCF446F28DB6B0F26AEA4A005328B22BE1DDC8CE18EC84A853A082C`。`git diff --check` exit 0（仅 Windows 行尾提示，无 whitespace error）。这版不包含 direct `BSInputEventQueue` probe，且所有 debug probes 当前关闭；它可用于下一次实机确认是否仍闪退、以及新日志中 `Menu.Confirm gateAware=false` 是否生效。

## 2026-06-15 22:41:00 +08:00

- 用户复测 gate-aware 版本后反馈仍会下移。重新复核 `DualPad.log` 的首个 Triangle 窗口：raw 为 `mask=0x00000008`，没有 `Menu.ScrollDown`、没有 `xinputButtons=0x0002`，但 `AuthoritativePoll` 在发 `xinputButtons=0x1000` 的同一 poll 中仍带 `move=(-0.004,-0.004)`，`UpstreamGamepad` 对应 `lx=-128 ly=-128`。因此这次下移的可疑输入不是 D-pad Down，也不是 Triangle 被错译，而是未 deadzone 的 `Menu.LeftStick` 中立量与 Confirm A 同 poll 输出。
- 根因修复：`CompiledActionGraph` 的 legacy `TriggerType::Axis` lowering 现在为 runtime binding 附带 `Deadzone(0.01)` modifier，并把 lowered modifiers 写入 `CompiledGraphBinding.modifiers`。这沿用现有 `InteractionEngine::ApplyModifiers` 合同，不硬编码 `Menu.Confirm` 或 `Menu.LeftStick`，也不改 runtime phase；阈值高于实机一档中立偏移 `1/255≈0.00392`，低于正常菜单摇杆输入。
- 已按 TDD 补红灯再绿灯：先新增 `DualPadInputV2Tests` 断言 Axis lowering 必须携带中立 deadzone，并断言 `LeftStickX/Y=±1/255` 不产生 Axis2D value / Value phase；旧实现先失败于 `Axis lowering must attach one neutral deadzone modifier`。实现后同一测试通过，并确认 `0.25/-0.25` 的有意菜单摇杆输入仍会产生 Axis2D value。
- 验证与部署：`xmake run -y DualPadInputV2Tests` exit 0；`xmake run -y DualPadGameplayProjectionTests` exit 0；`xmake run -y DualPadNativeButtonCommitTests` exit 0；`xmake build -y DualPad` exit 0 并部署 DLL 到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`。DLL LastWriteTime `2026/6/15 22:40:04`，SHA256 `DBEB7CA8960ACD6C325CC2E9B032D91A9EDBA614C0CD9CE5BD45FD8E7ACA0820`；PDB 仍因目标文件占用跳过。下一次实机日志重点看 Triangle confirm poll 是否变为 `xinputButtons=0x1000` 且 `lx=0 ly=0`。

## 2026-06-15 23:34:55 +08:00

- 用户启用 IDA Pro MCP 后，重新按旧链路和二进制证据定位 Favorites 首次下移问题。通过 IDA MCP 读取 Skyrim XInput button mask table，确认 `0x1000` 是 A/Accept，`0x0002` 是 DPadDown；最新实机日志中 Triangle raw 为 `mask=0x00000008`，DualPad 输出为 `Menu.Confirm -> xinputButtons=0x1000`，没有 `Menu.ScrollDown`、没有 `xinputButtons=0x0002`。因此这次下移不是 Cross/Triangle 被直接翻译成 Down，而是 Favorites 页面被落到通用 `Menu.Confirm`/A 接受路径后的下游页面副作用。
- 对照旧链路备份，Favorites 页面原语义不是 `Button:Triangle=Menu.Confirm`，而是 `Favorites.ToggleFocus`、`Favorites.GroupConfirm`、`Favorites.GroupToggle`、`Favorites.GroupUse`、`Favorites.SaveEquipState`、`Favorites.SetGroupIcon` 等页面级动作。当前 repo 没有 FavoritesMenu SWF workspace / page broker，不能继续用 native A/Accept 伪装这些页面动作。
- 修复：`config/DualPadBindings.ini` 恢复 Favorites 页面级绑定；`ActionManifest` 注册这些 Favorites 动作，并让 Favorites fallback 先填页面动作再填基础菜单动作，避免 fallback 把 Triangle 补成 `Menu.Confirm`；这些 Favorites 页面动作没有 native commit descriptor，防止再次从 Triangle 走 native A/Accept。新增启动 `BindingDump` 覆盖 FavoritesMenu Triangle/L1/R3/Square/Create/R2Button。
- 诊断：`ContextEventSink`、`UiMenuObserver`、`ContextRefreshTick`、`ContextResolver` 增加菜单事件、菜单栈 snapshot、context resolve 日志，用于下一次实机判断 UI 是否真的识别到 `FavoritesMenu`。期望看到 `[DualPad][MenuEvent] name=FavoritesMenu opening=true`、`[DualPad][MenuSnapshotNode] name=FavoritesMenu`、`[DualPad][ContextResolve] ... uiContext=FavoritesMenu legacyCtx=FavoritesMenu`。
- 测试与部署：`xmake build/run -y DualPadManifestCompilerTests` exit 0；`xmake build/run -y DualPadInputV2Tests` exit 0；`python scripts/ci/check_config_prompt_menu_glyph_closure.py` exit 0；`python scripts/ci/check_release_readiness.py` exit 0；`xmake build -y DualPad` exit 0 并部署 DLL 到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/15 23:30:45`。live `DualPadBindings.ini` SHA256 与 repo `config/DualPadBindings.ini` 一致。
- `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` 已跑过 build/test/docgen/readiness steps，最后只在 `git diff --exit-code -- docs/generated` clean-doc gate 失败；原因是本轮合法更新了 generated docs 且尚未提交，不是单测或编译失败。下一次实测重点：按 Triangle 时不应再出现 `NativeButtonCommit apply action=Menu.Confirm` / `xinputButtons=0x1000`；若不再下移但 Triangle 无页面功能，则确认剩余工作是恢复 FavoritesMenu SWF page broker/workspace。

## 2026-06-16 22:48:39 +08:00

- 用户更正：当前复现点是主菜单界面，不是 FavoritesMenu。此前把 `Menu.Confirm -> native A/Accept` 的下游副作用解释成 Favorites 页面级 broker 缺失，属于场景误判；后续主线回到 `Interface/startmenu.swf` / Main Menu 的 `Menu.Confirm` 消费路径。
- 重新复核主菜单证据：最新实机日志中 Triangle raw 为 `mask=0x00000008`，生成 `RuntimePlanAction action=Menu.Confirm phase=Press`，native 输出为 `xinputButtons=0x1000`，没有 `Menu.ScrollDown`、没有 `xinputButtons=0x0002`。因此“第一次下移”仍只能证明依赖主菜单对 A/Accept 的下游消费，不能说 DualPad 直接发了 Down。
- 新增主菜单 A/B 诊断探针 `route_menu_confirm_keyboard_gamepad_a_probe`：开启时 `Menu.Confirm` 仍正常 resolve / log action plan，但不进入 native A commit queue，而是通过现有 dinput8 keyboard bridge 发送 `DIK_NUMPAD0(0x52)` pulse；CLIK 的 `InputDelegate.as` 会把 NumPad0 映射为 `NavigationCode.GAMEPAD_A`。该探针用于区分 XInput current-state A 路径与 Scaleform/CLIK GAMEPAD_A 键码路径，不作为最终修法。
- 验证与部署：`xmake build -y DualPadNativeButtonCommitTests` exit 0；`xmake run -y DualPadNativeButtonCommitTests` exit 0；`xmake build -y DualPad` exit 0 并部署 DLL 到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/16 22:48:00`。现场 `DualPadDebug.ini` 已同步，`route_menu_confirm_keyboard_gamepad_a_probe=true`、`route_menu_confirm_keyboard_activate_probe=false`、`suppress_menu_confirm_native_output_probe=false`；`G:/g/SkyrimSE/dinput8.dll` 存在。下一次实测重点看日志是否出现 `[DualPad][MenuConfirmProbe] route_keyboard_gamepad_a ... scancode=0x52 routed=true`，且同一次 Triangle 不应再出现 `NativeButtonCommit queue action=Menu.Confirm` / `xinputButtons=0x1000`。

## 2026-06-16 22:56:22 +08:00

- 用户实测 `route_menu_confirm_keyboard_gamepad_a_probe`：Triangle 没反应。最新日志确认 `menuConfirmKeyboardGamepadAProbe=true`，6 次 `RuntimePlanAction action=Menu.Confirm` 后均出现 `[DualPad][MenuConfirmProbe] route_keyboard_gamepad_a ... scancode=0x52 routed=true`；`G:/g/SkyrimSE/DualPadDInput8.log` 同步确认 `0x52` press/release 被 Skyrim keyboard device `GetDeviceData` 取走。因此失败点不是 HID、resolver、native commit 短路或 dinput8 投递失败，而是当前主菜单不把 `DIK_NUMPAD0` / `GAMEPAD_A` 当确认入口。
- 复查旧导出的 `startmenu.swf` ActionScript：当前 Norden/dualpad startmenu 的 `NavigationCode.GAMEPAD_A` 是 `"gamepad_A"`，`NavigationCode.ENTER` 是 `"enter"`；`BSScrollingList.handleInput()` 只在 `details.navEquivalent == ENTER` 时调用 `onItemPress()`，`StartMenu.handleInput()` 也只在 `ENTER` 时调用 `onAcceptPress()`。这解释了 `DIK_NUMPAD0 -> GAMEPAD_A` 无反应，也解释了继续发 XInput A/current-state A 不是可靠的主菜单确认语义。
- 新增单变量验证探针 `route_menu_confirm_keyboard_enter_probe`：开启时 `Menu.Confirm` 仍正常 resolve / log action plan，但不发 native A/current-state，改经 dinput8 keyboard bridge 发 `DIK_RETURN(0x1C)`，用于验证 Flash `ENTER` 路径是否能触发主菜单确认且不下移。现场配置已切换为 `route_menu_confirm_keyboard_enter_probe=true`、`route_menu_confirm_keyboard_gamepad_a_probe=false`、`route_menu_confirm_keyboard_activate_probe=false`、`suppress_menu_confirm_native_output_probe=false`。
- 验证与部署：`xmake build -y DualPadNativeButtonCommitTests` exit 0；`xmake run -y DualPadNativeButtonCommitTests` exit 0；`xmake build -y DualPad` exit 0 并部署 DLL 到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/16 22:55:58`，SHA256 `1AECC8EF10A7E94AE9F1500DFE08F0B4918B9194D03772F6620353CD9E955C7A`。下一次实测重点看 `[DualPad][MenuConfirmProbe] route_keyboard_enter ... scancode=0x1C routed=true`，且同次 Triangle 不应出现 `NativeButtonCommit queue action=Menu.Confirm` / `xinputButtons=0x1000`。

## 2026-06-16 23:21:48 +08:00

- 用户确认 Enter probe 可执行功能且不下移，但认为它更像规避路径；本轮目标改为落实“主菜单首次 native XInput A 引发 presentation/focus 水合副作用”的推测，而不是继续更换确认路径。
- 新增显式采证开关 `trace_menu_confirm_focus_probe`。开启后 `Menu.Confirm` 仍走 native A/current-state 路径，不改变 runtime phase；日志统一使用 `[DualPad][MenuConfirmFocusProbe]`，覆盖 `plan_to_native`、`native_queue`、`poll_commit`、`xinput_fill`、`upstream_return`、`presentation_commit`、`is_using_gamepad_hook`、`gamepad_device_enabled_hook` 与 `should_refresh_menus`。这些点用于判断首次 Triangle 的 `xinputButtons=0x1000` 是否与 presentation owner / menu refresh 边界同帧或邻近发生。
- 现场配置已切换为采证模式：`route_menu_confirm_keyboard_enter_probe=false`、`route_menu_confirm_keyboard_gamepad_a_probe=false`、`route_menu_confirm_keyboard_activate_probe=false`、`suppress_menu_confirm_native_output_probe=false`、`trace_menu_confirm_focus_probe=true`。这版预期可能重新出现首次下移；如果下移出现，日志应能说明它发生在 native A 输出之前、同帧、还是之后。
- 验证与部署：先写 `DualPadNativeButtonCommitTests` 红灯，确认 `ShouldTraceMenuConfirmFocusProbe` 未实现时编译失败；实现后 `xmake build -y DualPadNativeButtonCommitTests` exit 0、`xmake run -y DualPadNativeButtonCommitTests` exit 0。`xmake build -y DualPadPresentationProjectionTests` exit 0、`xmake run -y DualPadPresentationProjectionTests` exit 0。`xmake build -y DualPadInputV2Tests` exit 0、`xmake run -y DualPadInputV2Tests` exit 0。`xmake build -y DualPad` exit 0 并部署 DLL 到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；PDB 因目标文件占用跳过。部署后的 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadDebug.ini` 已确认 `route_menu_confirm_keyboard_enter_probe=false`、`trace_menu_confirm_focus_probe=true`。
- 收尾验证：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，重建为 1796 nodes / 4071 edges / 145 communities；`git diff --check` exit 0，仅输出 Windows 行尾提示，无 whitespace error。

## 2026-06-16 23:31:20 +08:00

- 用户完成 `trace_menu_confirm_focus_probe` 采证测试。最新 `DualPad.log` 显示 presentation 已在 `23:26:37.449` 提前切到 `owner=Gamepad epoch=1 dirty=0x3F`，首个 `Menu.Confirm` native A 发生在 `23:26:38.768`；同一 Confirm poll 中 `xinputButtons=0x1000` 且 `move=(0.000,0.000)`、`lx=0 ly=0`，附近没有 `should_refresh_menus`。因此“首次 native A 触发 presentation/focus 水合导致下移”的推测被日志否定。
- 新根因证据：同一场日志在主菜单稳定帧持续出现 `Menu.LeftStick phase=Value`，随后 `AuthoritativePoll xinputButtons=0x0000 move=(0.000,-0.012)` 与 `UpstreamGamepad ... ly=-385`。首个 Triangle raw 仍是 `mask=0x00000008`，没有 `Menu.ScrollDown`、没有 D-pad down、没有 `xinputButtons=0x0002`；下移可由主菜单消费这类低幅 left-stick current-state 导航解释。
- 修复：将 legacy `TriggerType::Axis` lowering 注入的中立死区从 `0.01` 提升到 `0.02`，覆盖实机观测到的 `-0.012` DualSense 中立漂移；不硬编码 Triangle / Confirm / LeftStick，不改变 runtime phase，不移除菜单摇杆导航。新增测试锁定 `0.012` 漂移不得生成 Axis2D value / Value phase，同时 `0.05` 小幅真实菜单摇杆移动仍必须通过。
- 红灯到绿灯：新增测试后旧实现先失败于 `Axis neutral deadzone must suppress field-observed menu stick drift`；调整死区后 `xmake run -y DualPadInputV2Tests` exit 0，并覆盖一档量化漂移、字段漂移和真实小幅移动。
- 验证与部署：`xmake run -y DualPadInputV2Tests` exit 0；`xmake run -y DualPadNativeButtonCommitTests` exit 0；`xmake run -y DualPadGameplayProjectionTests` exit 0；`xmake run -y DualPadPresentationProjectionTests` exit 0；`xmake build -y DualPad` exit 0 并部署 DLL 到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，PDB 因目标占用跳过。现场 `DualPadDebug.ini` 保持 native A 路径和 `trace_menu_confirm_focus_probe=true`，下一次实测重点看是否还出现 `move=(0.000,-0.012)` / `ly=-385` 和主菜单首次下移。

## 2026-06-16 23:56:09 +08:00

- 用户复测后确认 deadzone 方向仍不解决主菜单首次下移，要求重新回看旧基线。重新对照历史提交与旧链路后修正结论：`trace_menu_confirm_focus_probe` 确实否定了“native A 同帧触发 presentation owner 翻转”，但没有否定“presentation owner/policy 变更后没有刷新已打开菜单平台状态”。最新日志中完全没有 `should_refresh_menus`，这与当前 HEAD 中 `SkyrimCompatibilitySurface::ShouldRefreshMenus()` 只有定义没有调用点一致。
- 旧基线差异已定位到 `261869b Complete PH8a runtime closeout`：该提交删除 `InputModalityTracker` 时，同时移除了旧链路中的 `_compatibilitySurface.ShouldRefreshMenus() -> RefreshMenus() -> SKSE UI task -> menu->RefreshPlatform() -> _root.DualPad_OnPresentationChanged()`。`input_v2` 保留了 `SkyrimCompatibilitySurface::ShouldRefreshMenus()`，但没有把它接回 `DualPadRuntime` 发布 stable presentation surface 的路径。
- 历史提交信息检索结果：没有发现专门写“主菜单首次下移”的提交；最接近的是 `367723c Refactor poll-owned input routing`，提交说明里提到 `fix Menu.Down repeat cleanup by honoring lifecycle releases and stable packet numbers`，这是 Menu.Down 重复清理，不是当前 `Menu.Confirm -> native A` 首次被 SWF 导航消费的问题。
- 本轮修复恢复的是旧基线语义，而不是换输出外壳：`DualPadRuntime::PublishStablePresentationSurface()` 在提交 `SkyrimCompatibilitySurface` 后调用 `RefreshMenusIfNeeded()`；`SkyrimCompatibilitySurface` 负责按 dirty owner/family/cursor/policy 和 epoch 去重，异步排 SKSE UI task 刷新当前 menu stack 的 `RefreshPlatform()`，并兼容调用 `_root.DualPad_OnPresentationChanged`。这样保证主菜单第一次 `Menu.Confirm -> XInput A` 前，SWF 已收到与旧链路一致的平台状态刷新。
- 覆盖与部署：新增 `PresentationProjectionTests` 覆盖 owner dirty 只触发一次 refresh、context-only dirty 不 refresh、policy dirty refresh；`InputV2Tests` 增加 stable presentation publish 必须消费 refresh request 的回归断言。已运行 `xmake run -y DualPadInputV2Tests`、`xmake run -y DualPadPresentationProjectionTests`、`xmake run -y DualPadNativeButtonCommitTests` 和 `xmake build -y DualPad`，均 exit 0，DLL 已部署到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`。`scripts/ci/run_phase8_ci.ps1` 的 build/test 阶段通过，最后停在 `git diff --exit-code -- docs/generated`，原因是本地 generated docs 已因当前 dirty config 发生合法漂移，尚未提交，不是编译或单测失败。

## 2026-06-17 00:12:45 +08:00

- 用户确认上一版主菜单手柄功能正常后，本轮将代码从采证补丁收敛为正式修复。删除菜单确认 A/B 实验面：`suppress_menu_confirm_native_output_probe`、`route_menu_confirm_keyboard_activate_probe`、`route_menu_confirm_keyboard_enter_probe`、`route_menu_confirm_keyboard_gamepad_a_probe`、`trace_menu_confirm_focus_probe` 及对应 `NativeButtonCommitBackend` 分支、`KeyboardHelperBackend::TriggerRawScancodePulseForDiagnostics` 和相关单测均已移除；`Menu.Confirm` 回到正式 native pulse / XInput A 路径。
- 保留正式根因修复：`DualPadRuntime::PublishStablePresentationSurface()` 在 stable presentation 发布后调用 `SkyrimCompatibilitySurface::RefreshMenusIfNeeded()`；`SkyrimCompatibilitySurface` 按 presentation dirty family/owner/cursor/policy 与 epoch 去重，通过 SKSE UI task 调 `menu->RefreshPlatform()` 并兼容 `_root.DualPad_OnPresentationChanged()`。刷新日志降为 `debug` 级，不再使用专门 probe tag。
- 将现场定位期间新增的 menu/context 诊断日志降为 `debug` 级，避免正式运行默认刷 `MenuEvent`、`MenuSnapshot`、`ContextResolve`；startup/effective `BindingDump` 保留为验收诊断，继续覆盖 Menu 与 FavoritesMenu 关键触发器。
- 验证与部署：`xmake run -y DualPadContextResolverTests`、`DualPadPresentationProjectionTests`、`DualPadInputV2Tests`、`DualPadGameplayProjectionTests`、`DualPadManifestCompilerTests`、`DualPadNativeButtonCommitTests` 全部 exit 0；`python scripts/ci/check_config_prompt_menu_glyph_closure.py` 与 `python scripts/ci/check_release_readiness.py` exit 0；`xmake build -y DualPad` exit 0 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/17 00:12:26`，SHA256 `00C39F38A6F20FB519F425C9939126E1A2E4CBF4BD7BBE74FCB68748DB4FE5FC`。`rg` 已确认 repo 源码、测试、配置和部署 `DualPadDebug.ini` 中无菜单确认 probe 残留；`git diff --check` exit 0（仅 Windows 行尾提示）。

## 2026-06-17 00:15:49 +08:00

- 正式提交前补跑完整 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`。第一次运行 build/test/readiness 均通过，但 `DualPadDocGen` 将 generated docs 的 manifest hash 从 `2a8a6ec08b2cb618` 更新为 `1d5e22d96aa42057`，导致最后 `git diff --exit-code -- docs/generated` 失败；已将生成文档 hash 更新 amend 进同一提交。
- 第二次完整 `scripts/ci/run_phase8_ci.ps1` exit 0：所有 build/run 目标、`generate_dualpad_docs.py`、reviewed docs consistency、legacy authority boundary、release readiness、config/prompt/menu/glyph closure 和 `docs/generated` clean gate 均通过。PDB copy 仍因目标文件占用被跳过，DLL 已正常部署。

## 2026-06-17 22:30:00 +08:00

- `[RC20][Hostile Hardening] Harden menu platform refresh hook and context refresh semantics` 基于 `4e4dc99` 收口为正式 hardening：
  - runtime health reason 拆分为 `UpstreamXInputRouteFailed`、`SkyrimCompatSurfaceHookFailed`、`SkyrimCompatSurfacePartialInstall`，并新增 `MenuObserverPartial`、`MenuObserverUnavailable`、`MenuIdentityDegraded`；Skyrim compat hook failure 默认只降级 presentation health，不再自动禁用 HID/action/native/prompt。
  - `SkyrimCompatibilitySurface` partial install 现在先禁用 hook thunks，逐站点保存 original target；只有所有 hook site 安装完成后才启用 thunks。partial/signature/unsupported 仍作为 install attempt failure 阻止继续 patch 和 silent retry，但不等同 runtime fail-closed。
  - menu platform refresh 改为按 presentation key 去重，并区分 queued/completed epoch；同 owner 下 top tracked menu/context/action-set/policy 变化仍会 queue refresh，队列不可用不会消费 pending epoch，prompt scope 先 publish 再执行 refresh callback。
  - observer partial/unavailable 不再静默落回 Gameplay；缺失 top name 时使用 `UnknownTrackedMenu` / weak event-name degraded sentinel，runtime health 显式记录 degraded observer 状态。
  - Favorites 页面级动作增加 executable availability metadata；在 page broker 缺失时不发布为 active prompt，保留 broker-independent Favorites native prompt。
- 代码清理中修正一处正式化漏洞：`IsHookInstallFailure()` 现在只表达 fail-closed 语义，因此 install gate 不能再用它判断签名失败；已新增独立 install-attempt failure 判断，避免 signature mismatch 继续进入 patch。
- Focused 验证：
  - `xmake run -y DualPadPresentationProjectionTests`、`DualPadContextResolverTests`、`DualPadManifestCompilerTests`、`DualPadPromptSnapshotTests`、`DualPadInputV2Tests`：exit 0。
  - 覆盖 required cases：`CompatSurfacePartialInstall_DoesNotForceKeyboardMode`、`CompatSurfaceUnsupportedRuntime_DegradesPresentationOnly`、`OpeningNewMenuWithSameOwner_RefreshesPlatformOnce`、`SwitchingMenuContextWithSameOwner_QueuesPlatformRefresh`、`RefreshQueueUnavailable_DoesNotConsumeEpoch`、`ObserverPartialMissingTopName_ResolvesUnknownTrackedMenuNotGameplay`、`ObserverUnavailable_DoesNotDispatchGameplayActions`、`FavoritesPageActions_AreHiddenWhenBrokerUnavailable`、`PromptStatePublishedBeforeRefreshCallback`。
- Close-out 验证：
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`：首次在 `docs/generated` clean gate 停止，因为 `DualPadDocGen` 将 manifest hash 更新为 `18f7f6228462fdb6`；暂存 generated docs 后重跑 exit 0。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest`：pre-commit 阶段按预期在 release artifact manifest `--expect-clean` 停止，因为本轮代码仍有 tracked diff。
  - `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1`：exit 0；覆盖内嵌 Phase8、replay harness、phase0 trace diff、builder JSON、reviewed docs consistency、legacy authority boundary、release readiness、config/prompt/menu/glyph closure、RC readiness closeout、`DualPadDInput8Proxy` build、release artifact manifest、graphify rebuild 与 `git diff --check`。
  - phase0 replay diff 10 个场景均为 `no diff`；graphify rebuild 输出 `1792 nodes / 4029 edges / 146 communities`。
  - `git diff --check` exit 0；仅有 Windows 行尾提示，无 whitespace error。
- 构建部署：
  - `xmake build -y DualPad` 已生成并部署 `DualPad.dll` 到本机 MO2 插件目录；`DualPad.pdb` 因目标文件占用跳过复制。
  - `xmake build -y DualPadDInput8Proxy` 已生成并部署 `dinput8.dll` 到本机 Skyrim 目录；`dinput8.pdb` 因目标文件占用跳过复制。

## 2026-06-17 22:53:00 +08:00

- 用户实机反馈：进游戏后右摇杆转动明显卡顿，按 DPadUp 打开 Favorites 菜单时 Skyrim 闪退。Windows WER 最新记录为 `SkyrimSE.exe` 在 `ucrtbase.dll` 触发 `0xc0000409`，WER archive 只有 `Report.wer`，没有 minidump。
- 复核最新 `DualPad.log`：raw HID 输入正常，高幅右摇杆 raw 样本充足，但 upstream authoritative poll 只有约 60Hz；日志中 `Input][Hex` 约 19884 行、`Input][State` 约 8898 行、`pending_max_before=271`，说明当前 playtest 仍开启高频 input/action/route trace，足以造成 HID reader 到 XInput poll 间的队列积压和体感卡顿。
- 同一日志中 DPadUp raw `mask=0x00010000` 生成 `RuntimePlanAction action=Game.Favorites`，随后 `NativeButtonCommit` 输出 `xinputButtons=0x0001`，日志很快停止且没有后续 `FavoritesMenu` snapshot。结合当前恢复的 `RefreshPlatform()` 异步菜单刷新，正式根因假设收敛为：Gameplay / degraded context dirty 状态也可能排队 menu platform refresh，延迟 UI task 在 FavoritesMenu 打开途中遍历半初始化 menu stack 并调用 `RefreshPlatform()`。
- 修复：`SkyrimCompatibilitySurface` 新增显式 tracked menu 白名单；`RefreshMenusIfNeeded()` 只允许稳定、已识别菜单上下文排队平台刷新，`Gameplay`、`UnknownTrackedMenu`、gameplay substates、`PassthroughOverlay` 都不再排队。UI task 执行时再次读取 committed state；若已不在稳定菜单上下文则清队列并 debug 记录 `menu_refresh_skipped`。遍历 menu stack 时跳过 `nullptr` 与 `uiMovie == nullptr` 的菜单，避免对未就绪 Scaleform movie 调 `RefreshPlatform()`。
- 为降低 playtest 输入延迟，将 `config/DualPadDebug.ini` 的高频默认采集关闭：input packet/hex/state、mapping、synthetic state、action plan、native/keyboard injection、route health、replay trace 与 glyph query trace 均默认 `false`；保留按需手动开启的注释。
- 覆盖与部署：`xmake run -y DualPadPresentationProjectionTests`、`DualPadContextResolverTests`、`DualPadGameplayProjectionTests`、`DualPadNativeButtonCommitTests`、`DualPadManifestCompilerTests` 均 exit 0；`xmake build -y DualPad` exit 0 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/17 22:52:30`。live `DualPadDebug.ini` SHA256 与 repo `config/DualPadDebug.ini` 一致，所有高频日志开关已确认 `false`。`DualPad.pdb` 仍因目标占用跳过复制，不影响 DLL 部署。
- 本轮为实机复测构建，暂未提交/推送；待用户确认右摇杆卡顿与 Favorites 打开闪退结果后再收口提交。

## 2026-06-17 23:02:00 +08:00

- 用户复测后主菜单首次下移回归，并指出上一轮闪退修法可能根本不对。复核代码与 catalog 后确认：上一轮把 `UiContextId::UnknownTrackedMenu` 误建模为 degraded unknown；但 `ContextCatalog` 明确把 `Main Menu`、`Credits Menu`、`Loading Menu` 等稳定 generic Menu 也映射到 `UnknownTrackedMenu`。因此将 `UnknownTrackedMenu` 整体排除出 menu platform refresh 会切断主菜单旧基线所需的 `RefreshPlatform()`。
- 修正模型：`UnknownTrackedMenu` 只是 generic `Menu` context id，不等同于 degraded。真正应阻止 refresh 的条件来自 `ResolvedContextSnapshot` 的 `menuObserverCompleteness != Complete` 或 `menuIdentityDegraded == true`。`PresentationProjection` 现在把这两个字段转发到 `PublishedPresentationState`，`SkyrimCompatibilitySurface` 用 `IsRefreshableMenuPresentation()` 判断是否允许 queue / execute platform refresh。
- 保留上一轮 crash hardening 中合理部分：UI task 执行时仍跳过 `nullptr` 和 `uiMovie == nullptr` 的半初始化 menu；如果 queued task 执行时 presentation 已变成 unstable/degraded，会清队列并 debug 记录 `menu_refresh_skipped reason=unstable_menu_context`。但稳定 `UnknownTrackedMenu` / Main Menu 会重新 queue refresh。
- 覆盖与部署：新增测试覆盖 `StableGenericMenu_QueuesPlatformRefresh`、degraded Unknown 不刷新，以及 observer completeness / degraded identity 从 `ContextResolver` 经 `PresentationProjection` 传到 compat surface。已运行 `xmake run -y DualPadPresentationProjectionTests`、`DualPadContextResolverTests`、`DualPadGameplayProjectionTests`、`DualPadNativeButtonCommitTests`、`DualPadManifestCompilerTests`、`DualPadInputV2Tests`，均 exit 0。`git diff --check` exit 0，仅 Windows 行尾提示。`xmake build -y DualPad` exit 0 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/17 23:00:58`；live `DualPadDebug.ini` SHA256 与 repo 一致。

## 2026-06-27 17:05:00 +08:00

- WebGPT 对 `76689a2` 的 RC20 裁决为单点 NACK：`DeferredNotReady` immediate retry 耗尽后会丢失同一个 menu refresh intent；如果后续 `uiMovie` 变 ready 但 presentation 没有新的 dirty publish，同一 key 可能永远不再执行 `RefreshPlatform()`。复核代码确认：旧实现第 4 次 deferred 后既不写 completed，也不保留 pending/held intent，后续 `dirty=None` 会被 `CaptureMenuRefreshIntentLocked()` 直接跳过。
- 修复：`SkyrimCompatibilitySurface` 新增 `_refreshDeferredHeld`。`DeferredNotReady` 在 bounded immediate retry 后进入 held 状态，不写 completed、不继续自旋；后续 stable `Commit()` 即使 `dirty=None`，只要 key 仍匹配且 eligibility 仍为 `EligibleStableMenu`，会把 held request 以新 serial 重新放回 pending。若 eligibility 变非 stable、key 变化、出现新 pending、或 request superseded，则清理旧 held。
- 同步修复 review 提到的 queue failure 边角：`QueueMenuRefreshTask()` 失败回滚时只在没有 newer pending 的情况下恢复旧 request，避免 task queue 失败窗口里用 K1 覆盖 K2。
- 回归测试：先将 `MenuRefresh_DeferredNotReady` 改成 `stableTickWithoutDirty.dirty=None` 后仍必须重新排队，同旧实现先红灯；实现后转绿。新增 `MenuRefresh_QueueFailureDoesNotOverwriteNewerPendingLatest`，模拟 queue failure 中间 commit K2，确认失败的 K1 不会覆盖 newer pending。
- 验证：`xmake run -y DualPadPresentationProjectionTests`、`xmake run -y DualPadInputV2Tests`、`xmake run -y DualPadReplayHarnessTests` 均 exit 0；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` exit 0；`powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1` exit 0。RC readiness 内嵌 Phase8、phase0 replay diff、release readiness、config/prompt/menu/glyph closure、RC closeout、release artifact manifest、graphify rebuild 与 `git diff --check` 均通过；phase0 10 个场景均 `no diff`；graphify rebuild 输出 `1800 nodes / 4056 edges / 145 communities`。
- 构建部署：`DualPad.dll` 已部署到 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；`dinput8.dll` 已部署到 `G:/g/SkyrimSE/dinput8.dll`。`DualPad.pdb` 与 `dinput8.pdb` 复制仍因目标文件占用跳过。WebGPT 要求的 fresh playtest evidence 仍需实机确认：Main Menu cold start first Triangle/Y 不下移，Favorites 连续打开/关闭含按住/连点 DPadUp 不崩，日志不能以 held/deferred 作为最后状态。

## 2026-06-27 21:55:52 +08:00

- 用户实机反馈：冷启动 Main Menu 首次 Triangle/Y 仍会下移，说明 `DeferredNotReady` held retry 修复没有闭环主菜单首次 focus transition 根因。当前现场证据仍显示首个 native 输出是 XInput A (`0x1000`) 而非 DPadDown (`0x0002`)，但默认日志缺少 menu refresh request/done 与首个 `Menu.Confirm` 输出之间的直接时间线。
- 本轮不继续叠加行为补丁，改为诊断构建：`SkyrimCompatibilitySurface` 将 menu refresh request / done / complete / deferred-held / skipped / stale 统一提升为 info 级 `[DualPad][MenuRefreshTrace]`；`NativeButtonCommitBackend` 对 `Menu.Confirm` 的 apply / translate / queue / poll 输出 info 级 `[DualPad][MenuConfirmTrace]`，记录当时的 presentation epoch、dirty、uiContext、eligibility、owner、navigationOwner、cursorOwner、resolver context、observer completeness、identity quality、menuStackRevision、topMenuInstance，以及 poll 输出 mask 和 XInput buttons。
- 验证与部署：`xmake build -y DualPad` exit 0，并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`；`DualPad.pdb` 因目标占用跳过复制。`xmake build -y DualPadNativeButtonCommitTests`、`xmake run -y DualPadNativeButtonCommitTests`、`xmake run -y DualPadPresentationProjectionTests` 均 exit 0。该版本用于下一次冷启动复测采证，暂不作为正式根因修复提交。

## 2026-06-27 22:09:46 +08:00

- 用户完成诊断版实测。最新 `DualPad.log` 证明首个 Main Menu `Menu.Confirm` 在 `21:57:05.549` 输出 `xinputButtons=0x1000`，但此前唯一一次 menu refresh request 在 `21:57:04.540` 被判 `result=Superseded`，没有任何 `result=Completed`。当时 presentation 仍是 stable Main Menu，问题不是按钮翻译或 DPad/axis，而是首个 `RefreshPlatform()` 没有完成就放出了 native confirm。
- 根因收敛：`MakeRefreshKey()` 包含 `gameplayPresentationRevision`，但 `PresentationProjection` 不把该字段变化计入 refresh-relevant dirty。冷启动时 gameplay presentation revision 从 1 变 2，key 改变但 dirty 为 `None`，in-flight request 被 UI task 判 stale，且没有 pending latest 重新排队。另一个现场问题是 `DeferredNotReady` held intent 会在相同 key 的每个 stable tick 被重新排队，形成 800+ 次 refresh storm。
- 修复：menu refresh key 移除 `gameplayPresentationRevision`，只保留会影响 refresh 资格/语义的 epoch、context、ui、eligibility、policy 和 action-set scope；`_lastDeferredHeldRequeueKey` 阻止同一 held intent 在相同 stable tick 上反复 requeue；UI task 中只要实际刷新了至少一个 ready menu，即使 stack 中有 skipped-not-ready 条目也视为 `Completed` 并保留 skipped 计数，避免被非目标/半关闭菜单拖成永久 Deferred。
- 回归测试：新增 `MenuRefresh_GameplayRevisionOnlyChange` key 合同测试，以及 held deferred 不在相同 stable tick 上自旋的断言。先跑红灯确认旧行为失败；实现后 `xmake run -y DualPadPresentationProjectionTests`、`xmake run -y DualPadInputV2Tests`、`xmake run -y DualPadNativeButtonCommitTests`、`xmake run -y DualPadReplayHarnessTests` 均 exit 0。
- 构建部署：`xmake build -y DualPad` exit 0，并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/27 22:09:33`；`DualPad.pdb` 因目标占用跳过复制。本轮仍保留 `MenuRefreshTrace` / `MenuConfirmTrace` 低频 info 探针，供下一次冷启动复测确认：首次 Triangle/Y 前应出现 `MenuRefreshTrace event=done result=Completed`，且不能再有同 key deferred storm。

## 2026-06-27 22:25:18 +08:00

- 用户复测确认冷启动 Main Menu 首次 Triangle/Y 不再下移，但打开 Favorites 菜单仍会闪退/退出。最新 `22:12` 现场没有新的 LocalDump / WER，`DualPad.log` 最后一条有效输入停在 upstream poll `buttons=0x0001`，且没有后续 `FavoritesMenu` prompt/menu observer 事件；旧 `21:57` BEX64 dump 属于上一版构建。当前证据只证明 `Game.Favorites` 最终 materialize 为 XInput DPadUp，尚不足以判断是否为错误 context、重复/未释放 pulse，还是游戏侧 Favorites 打开流程在菜单 observer 前退出。
- 本轮不改变行为，新增窄探针：`NativeButtonCommitBackend` 将 `MenuConfirmTrace` 泛化为 tracked native action trace，并为 `Game.Favorites` 增加 `GameFavoritesTrace` apply / translate / queue / slot / poll 日志；同时为 DPadUp 输出增加 `DpadUpTrace` poll 日志。日志记录 native slot 状态、token、pending、pressed/released/managed mask、XInput buttons、presentation committed state、resolver snapshot、observer completeness 与 menu identity quality。
- 验证与部署：`xmake build -y DualPadNativeButtonCommitTests`、`xmake run -y DualPadNativeButtonCommitTests`、`xmake run -y DualPadPresentationProjectionTests`、`xmake run -y DualPadInputV2Tests` 均 exit 0；`xmake build -y DualPad` exit 0，并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/27 22:25:06`，SHA256 `2F59EE65E5E27C06B1A1DC3BBFD8954E07EFBA9B22BB25BAED51561CECC212F4`。`DualPad.pdb` 仍因目标占用跳过复制。本轮是采证构建，暂不提交/推送。

## 2026-06-27 22:46:45 +08:00

- 用户完成 Favorites 采证版实测。最新 `DualPad.log` 显示崩溃/退出发生在 `Game.Favorites` 第一次输出 XInput `buttons=0x0001` 后，且没有后续 `FavoritesMenu` observer / prompt handler / menu refresh 事件；LocalDumps 和 WER 没有新的 `22:26` crash 记录。`GameFavoritesTrace` 进一步显示该帧 runtime 语义是 `Gameplay`，但 committed presentation owner / navigationOwner / cursorOwner 都仍为 `KeyboardMouse`，同时 `Game.Favorites` 不是 gate-aware action，因此 DPadUp 在 KBM presentation surface 下被 upstream 消费。
- 根因收敛：这不是 `RefreshPlatform()` 刷半初始化 FavoritesMenu，也不是 DPadUp 重复/未释放；断点位于 gameplay 数字菜单入口的 presentation handoff。`Game.Favorites` 是 gamepad native menu-entry action，但旧 projection 只用 analog owner 推动 `engineOwner/menuEntryOwner`，纯 DPadUp 打开菜单不会在 native pulse 前把 Skyrim compatibility surface 切到 Gamepad。
- 修复：`NativeActionDescriptor` 新增 `NativePresentationHandoff::GameplayMenuEntry` 元数据，并将 `Game.Favorites` 标记为该类型；`GameplayProjectionFrame` 将带该元数据的 press/pulse transient 视为 gamepad menu-entry presentation primary，发布 `engineOwner=Gamepad`、`menuEntryOwner=Gamepad` 和 `preOutputPresentationHandoff=true`；`PollOutputAdapter` 在 native transient 输出前执行 `ApplyPreOutputPresentationHandoff`；live executor 调用 `SkyrimCompatibilitySurface::CommitPreOutputGameplayPresentationHandoff()`，在同一 native pulse 被 upstream poll 消费前更新 owner / navigationOwner / cursorOwner。
- 回归测试：新增 `RunGameplayFavoritesPreOutputPresentationHandoffTests`，覆盖 `Game.Favorites -> FavoritesCombo` metadata、Gamepad owner/menuEntryOwner、pre-output handoff 标记，以及输出顺序 `ApplyGatePlan -> ApplyPreOutputPresentationHandoff -> ApplyTransientDigital -> PublishAnalogState -> CommitCleanRecoveryBaseline`。`DualPadGameplayProjectionTests` 先红灯，再实现后转绿。
- 验证与部署：`xmake run -y DualPadGameplayProjectionTests` exit 0；`xmake run -y DualPadInputV2Tests` exit 0；`git diff --check` exit 0，仅 Windows 行尾提示；`xmake run -y DualPadNativeButtonCommitTests` 首次与其他目标并行时因 MSVC PDB API 抢占失败，单独重跑 exit 0；`xmake build -y DualPad` exit 0，并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/6/27 22:46:15`，SHA256 `75BFD109CFE3699BBC3B14A6AADE3C70EE89F30D9927732CE64016D39436A80A`。`DualPad.pdb` 仍因目标占用跳过复制。本轮仍暂不提交/推送，等待实机复测 Favorites。

## 2026-07-10 22:05:00 +08:00

- 用户完成 pre-output handoff 版复测。最新 `DualPad.log` 证明主菜单路径已稳定：冷启动 Main Menu 前出现 `MenuRefreshTrace event=done result=Completed`，两次 `Menu.Confirm` 都输出 XInput Y/A-equivalent `xinputButtons=0x1000`，release 为 explicit noop，没有 DPadDown `0x0002`。Favorites 路径中 `PresentationHandoff event=pre_output_gameplay` 已在 `Game.Favorites` native pulse 前执行，committed owner / navigationOwner / cursorOwner 均为 `Gamepad`，随后输出 XInput DPadUp `buttons=0x0001`；日志仍没有后续 `FavoritesMenu` observer / prompt handler / menu refresh 事件。
- 复核 Windows WER / LocalDumps：唯一新 dump 是 `SkyrimSE.exe.53948.dmp`，WER 为 `ucrtbase.dll` `0xc0000409` subcode `7`，进程创建时间 `21:52:00`，早于当前 `DualPad.log` 的 `21:52:26` 启动记录。该 dump 不足以直接证明 Favorites 打开调用栈，但符号化显示异常线程栈中存在 `dynamic atexit destructor for 'g_thread'`，对应 `src/input/HidReader.cpp` 文件级 `std::thread` 静态对象析构；若 HID reader 仍 joinable，进程退出/卸载时会触发 CRT fast-fail，污染 crash 证据甚至造成二次崩溃。
- 修复：`HidReader` 不再持有文件级可析构 `std::thread` 对象，改为受 `g_threadMutex` 保护的显式 `std::thread*` 生命周期。`StartHidReader()` 创建线程；`StopHidReader()` 原子取走、join、delete。若进程异常退出且没有机会 Stop，模块静态析构阶段不再调用 `std::thread::~thread()` 触发 terminate；正常停止路径仍会 join 并关闭 HID transport。
- 验证与部署：`xmake build -y DualPad` exit 0 并部署 `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPad.dll`，LastWriteTime `2026/7/10 22:02:39`，SHA256 `6FC5B5100B9579E85D3CA9503F28C8FF671BA08C8A64D62C3FC08DB993EB57F0`；`xmake run -y DualPadInputV2Tests` exit 0；`git diff --check` exit 0，仅 Windows 行尾提示；`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` exit 0，输出 `1813 nodes / 4103 edges / 146 communities`。`DualPad.pdb` 仍因目标文件占用跳过复制。本轮构建用于验证退出 fast-fail 是否消失，并继续观察 Favorites 打开后是否产生新的真实 dump / `FavoritesMenu` observer。

## 2026-07-10 22:12:00 +08:00

- 用户复测 HID thread lifecycle 修复版后反馈：打开 Favorites 仍然闪退。因此 pre-output gameplay presentation handoff 与 HID reader 静态析构 hardening 都不能作为 Favorites 闪退根因闭环；当前可确定的是主菜单首次 Triangle/Y 下移已收敛，Favorites 仍停在 `Game.Favorites -> XInput DPadUp` 后、进入 `FavoritesMenu` observer 前。
- 本轮按用户要求提交当前代码给网页 GPT 审核。待审重点应放在：`Game.Favorites` 走 upstream XInput DPadUp 是否仍会触发 Skyrim/SWF Favorites 打开链的已知崩溃路径、是否需要恢复/审计 Favorites SWF workspace 或对 live `favoritesmenu.swf` 做 A/B、以及当前 `NativeButtonCommitBackend` 低频 trace 是否应在正式合入前降噪或拆成诊断开关。

## 2026-07-11 00:00:00 +08:00

- `S-DP5-RC20-HOTFIX` planning start：读取用户提供的 RC20 Final v2 计划包、authoritative baseline、builder memory、graphify report、当前代码、相关测试、runtime/menu/poll 文档与 repo-local learnings。
- 分支 / 基线：`codex/dp5-rc20-menu-native-hotfix @ 5f920307acd0fd01180fb37c9094bf2ac0d6600d`；开始规划时工作树 clean，跟踪 `origin/codex/dp5-rc20-menu-native-hotfix`。
- 当前代码事实：实际 high-water schedule 未调用 `ShouldForceTaskFallback()`；`DrainOnMainThread(maxSnapshots)` 仍全量 `IngressHub::Drain()`；snapshot/event/frame 单位混用；analog/source evidence 与 edge/boundary 共用有界 queue；Poll thunk 内执行 drain、runtime/context mutation 与 pulse commit；`AuthoritativePollState` 多 atomic 字段无法证明同代；`XInputStateBridge` 存在 reader-side 普通全局；`ContextResolver` 无同步返回内部动态对象 reference；menu refresh 仍遍历全 stack；compat hook fail-closed helper 恒为 false。
- 计划已写入：`docs/plans/2026-07-11-001-fix-rc20-runtime-determinism-plan.md`。执行拆为 10 个可审查 slice：事故隔离、fallback/bounded drain、latest state/ordered edges、runtime owner、immutable output、generation pulse、target refresh、transactional hook、stress/docs/CI、IDA/dump/real-game evidence。
- Builder sync：`DP5` 改为 `in_progress`，`current_sprint=S-DP5-RC20-HOTFIX`。该 slice 是 post-closeout field-readiness hotfix，不新增 runtime phase。
- 当前 release status：`NO-GO`。本条只完成规划和事实核验，尚未修改代码、运行新测试或完成 IDA/dump/实机验证；不得把既有静态/host 结果描述成 Favorites 已修复。

## 2026-07-11 00:30:00 +08:00

- `ce:plan` confidence deepening 与 mandatory document review 完成。审查依次覆盖 coherence、feasibility、scope-guardian、adversarial；按仓库规则在主线程顺序执行，未启动 subagent。
- 自动修正 5 类计划问题：bounded drain 下 latest analog / ordered digital 的双 cutoff 合同；Poll read-only 与 generation pulse 的一致性；E-OWNER/E-POLL/E-PATCH/E-CRASH 证据依赖顺序；event hard cap / deadline 单位；首次 owner tick、degraded 与 shutdown 的 neutral `PollOutputFrame`。
- 关键合同：LatestPadState 内部同代且 analog latest-wins；normal digital 只由已 drain edge reducer 推进，`currentDownMask` 仅用于 overflow recovery baseline。Poll diagnostics 不驱动 pulse；E-POLL 无法证明 owner/Poll ordering 时 native Favorites 继续 fail-closed。
- review 后无残留 P0/P1 judgment finding；计划仍保持 `NO-GO`，下一步按默认链进入 `ce:work`，从 Unit 1 的 failing tests 与事故隔离开始。

## 2026-07-11 00:45:00 +08:00

- `S-DP5-RC20-HOTFIX / Unit 1` 完成：`Game.Favorites` 在 `ActionBackendPolicy` 唯一路由决策处增加独立 `enable_native_favorites` gate，默认 false；未验证 fallback 时返回 `Backend::None` 与稳定 `native_favorites_disabled` reason，不改变其它 native action、binding 或 descriptor。
- 新增默认关闭的 `log_poll_diagnostics`。启用时 `PollDiagnosticLimiter` 以固定 256-call 容量原子记录 sequence / thread / in-flight / caller / result / pollSequence / contextEpoch / packet / buttons / axes / triggers / dropped；容量耗尽只计 dropped，不分配内存。删除原先默认开启且用非原子静态 budget 的 per-Poll result log。
- TDD 证据：默认 Favorites 用例先以 `Game.Favorites native output must fail closed by default` 红灯，gate 实现后绿灯；diagnostic limiter 先因缺少 `PollDiagnostics.h` 红灯，再实现后绿灯；config getter/reason 也分别先编译红灯再实现。
- 验证：`DualPadGameplayProjectionTests`、`DualPadRouteHealthContractTests`、`DualPadInputV2Tests`、`DualPadNativeButtonCommitTests` 均 exit 0；`xmake build -y DualPad` exit 0 并部署 DLL，PDB 因目标占用跳过复制。该结果只证明 host/build containment，不证明 Favorites crash 已修复；release status 仍为 `NO-GO`。

## 2026-07-11 00:55:00 +08:00

- `S-DP5-RC20-HOTFIX / Unit 2` 完成：新增唯一 `ShouldScheduleTaskFallback()` truth table。frame pump disabled 时 pending event 由 task fallback 接管；frame pump enabled 时仅 `pendingEvents >= highWatermarkEvents && routeState=active_stale` 排 task；`active_fresh`、route disabled/hook missing、manual replay 均不启动第二 consumer。
- dispatcher 的 pending/high-water/budget/drained telemetry 统一改为 event 单位；`SubmitSnapshot()` 实际路径调用同一 truth table。task 每次最多 drain 64 events，完成后释放 queued ownership，再按当前 pending/route 重评是否续排；TaskInterface 不可用时保留 pending 并清除 queued flag。
- `IngressHub` transport 改为 `deque`，新增 `Drain(maxEvents)`，以 O(k) pop-front 严格执行 hard cap；`Drain()` 只保留为显式 full-drain compat。overflow compaction 会清零已被 marker 代表的 pending legacy snapshot count，partial drain 只在实际消费 legacy PadSnapshot 时递减。
- TDD 证据：fallback truth table 先因 API 缺失编译红灯；17-event fixture 先因 `Drain(maxEvents)` 缺失编译红灯。实现后 `active_fresh` 不排 task、budget 0 不消费、budget 16 留下 seq 17 均转绿。
- replay fixture 的 dispatcher schedule 从 snapshot 单位迁移为 event 单位：普通 legacy snapshot 为 Ui + Pad 两个 events，overflow snapshot 额外包含 gap marker。验证 `DualPadRouteHealthContractTests`、`DualPadIngressTests`、`DualPadReplayHarnessTests`、`DualPadReplayTests` 与 `xmake build -y DualPad` 均 exit 0；PDB 仍因目标占用跳过复制。release status 仍为 `NO-GO`。

## 2026-07-11 01:16:00 +08:00

- `S-DP5-RC20-HOTFIX / Unit 3` 完成：新增 header-only `LatestPadState` / `LatestSourceEvidence` complete-generation publication，并将 `IngressHub` 拆为 latest slots 与有界 ordered event deque。`Capture(maxEvents)` 在同一 mutex 临界区内返回完整 latest generations、严格 cutoff 的 ordered events 与 remaining event count。
- HID producer 现在先在线程安全的 `LiveInputFactProducer::CollectGamepadSourceEvidence()` 中构造 source frame，再与同源 `PadEventSnapshot` 一次提交；正常 runtime 不再保留 legacy snapshot/continuous analog queue payload，manual replay 仍保留 compat payload。连续 axes/triggers 和重复 source snapshot 只覆盖 latest slot，不增加 ordered queue；press/release、device/context boundary、reset/overflow 继续按序。
- `FrameAssembler` 只用已 drain digital edge 推进 normal reducer；latest `currentDownMask` 不参与正常 transition。latest analog 可以领先同 context/device boundary 内的 digital cutoff，但 context/device marker 尚未 drain 时会延迟对应 latest generation，避免 future boundary 绕过 backlog。
- dual-cutoff 下 stable fact time 使用 durable monotonic max：例如 latest analog 时间 400 先发布、ordered release 时间 300 后到时，release edge 仍按序消费，但 stable frame 时间不倒退。Hub 同时记录 latest generation 是否已被 capture；frame pump disabled 时，纯轴 `pendingEvents=0` 仍会经唯一 fallback truth table 排一次 owner task，high-water 判定继续只使用真实 event count。
- overflow recovery 新增 edge-history-lost barrier：overflow marker 保留 latest analog/current physical mask，冻结所有 transient digital edge，直到观察到全键释放建立 clean baseline；clean release 自身不泄漏 stale release pulse，下一次新 press 才恢复 ordered delivery。
- TDD / stress 覆盖：1000 Hz pure analog 零 queue growth、press/release 与 latest analog 双 cutoff、overflow 不丢轴、clean-release barrier、source latest slot、10,000 次并发 HID submit/capture 无半事务、device/context boundary 不被 latest 绕过；property 512 reports 与 fuzz 2,048 reports 覆盖 generation 不倒退、queue bounded 和无 synthetic duplicate press/release。
- 验证：`DualPadIngressTests`、`DualPadInputV2Tests`、`DualPadReplayTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests`、`DualPadRouteHealthContractTests` 均 exit 0；`xmake build DualPad` exit 0 并部署 DLL，PDB 因目标占用跳过复制；最终 graphify rebuild exit 0，结果 `1846 nodes / 4260 edges / 145 communities`。这仍是 host/build 证据，Unit 4 单一 runtime owner、immutable Poll output、generation pulse 与真实 Favorites 证据尚未完成，release status 保持 `NO-GO`。

## 2026-07-11 01:38:00 +08:00

- `S-DP5-RC20-HOTFIX / Unit 4` code slice 完成：新增 `RuntimeOwnerGuard` RAII ticket。首次 owner tick 绑定 thread identity；frame token 与 runtime generation 单调推进；active tick 重入、已完成 token 重复、token 倒退、thread drift 或 owner stop 均拒绝 mutation、永久进入可观察 degraded，并只在首次故障记录 critical diagnostic。正常 bind、generation 1 与每 600 generation 记录 thread hash / frame token / cadence 证据。
- `BSInputDeviceManager` InputFramePump 现在是唯一首选 owner candidate；它为每个 input-pump frame 分配 token并调用 `DrainOnOwnerTick()`。SKSE task fallback 只能经过同一 guard；若实际落在不同线程会 fail-closed，而不会静默建立第二 writer。dispatcher ticket 内按顺序执行 context refresh、Ingress capture、FrameAssembler、DualPadRuntime、presentation/prompt/menu publication、owner-side unmanaged pulse expiry 与一次 native button commit。
- upstream Poll thunk 已删除 `StartHidReader()`、dispatcher drain、context/runtime/presentation/prompt mutation 与 `CommitPollState()`；它只记录 Poll activity、读取 guard health并序列化当前 compatibility output。guard degraded 时输出 neutral XInput state。`AuthoritativePollState::ReadSnapshot()` 也改为 const/read-only；原 unmanaged time expiry 显式移到 owner tick。`XInputStateBridge` 的 reader-side packet/cache 与多 atomic compatibility frame 仍属于 Unit 5 待替换项，因此本条不声称 immutable Poll publication 已完成。
- `ContextResolver` publication 改为 mutex-protected by-value snapshot；HID、KBM、runtime 与 native commit consumers 不再取得 singleton 内部动态引用。`DualPadRuntime` 的 published gameplay/projection/debug getters 同样返回值；production `ProcessAssembledFrame`、`ProcessGameplayFrame`、processor reset 与 context refresh 都验证 active owner ticket，测试/replay reset 使用显式 `ForTests` API。
- TDD：owner guard API 缺失先编译红灯；实现后覆盖同 tick reentry、重复 token、thread drift、健康 generation 1→2、stop 后禁止重绑。ContextResolver 先由 return-reference static assertion 红灯，再以 20,000 次 writer/read 并发验证完整 publication 无撕裂。replay backlog golden 的 pending event 5→4 修正来自 Unit 3 已落地的同 context UiSnapshot 去重，重新执行 dispatcher runtime replay后全 bundle 匹配。
- 验证：`DualPadInputV2Tests`、`DualPadContextResolverTests`、`DualPadReplayHarnessTests`、`DualPadReplayTests`、`DualPadRouteHealthContractTests`、`DualPadNativeButtonCommitTests` 均 exit 0；`xmake build DualPad` exit 0 并部署 DLL，PDB 因目标占用跳过复制；静态扫描 `UpstreamGamepadHook.cpp` 不再包含 runtime drain/process/commit/start 调用；graphify rebuild exit 0，结果 `1870 nodes / 4367 edges / 147 communities`。
- 证据边界：上述只证明 host contracts、静态调用图与诊断 instrumentation。真实 Skyrim 的 E-OWNER 仍需验证 InputFramePump thread identity、cadence、无重入与 lifecycle；E-POLL、immutable output、generation pulse、Favorites dump/循环也未完成。`enable_native_favorites=false` 保持 fail-closed，release status 仍为 `NO-GO`。

## 2026-07-11 01:49:55 +08:00

- `S-DP5-RC20-HOTFIX / Unit 5` 完成：新增完整 `PollOutputFrame` 与 `PollOutputPublication`。owner tick 在 native commit 后绑定 publication/runtime generation、manifest/context/presentation/action epoch、menu stack revision、source timestamp、已序列化 XInput buttons/axes/triggers、packet、route health 与 pulse token，再通过 `atomic<shared_ptr<const ...>>` 一次发布；slow reader 持有的旧对象在生命周期内保持不可变。
- plugin 在安装 upstream hook 前预构造 Initializing / OwnerDegraded / PublicationUnavailable / Shutdown 四类 neutral frame；Poll acquire 不返回 null。`RuntimeOwnerGuard` 用单独 atomic failure publication 让任意 Poll thread 在 owner drift、degraded 或 stop 后 fail-closed，而不读取 owner mutex snapshot。
- `UpstreamGamepadHook` 每次调用只 acquire 一个 frame，`XInputStateBridge` 只把该 frame 复制到 `XINPUT_STATE`。删除 `AuthoritativePollState` 二次读取、reader-side `g_lastSnapshot` / `g_hasLastSnapshot` / packet mutation；packet 由 owner 按已序列化 gamepad-visible payload变化递增，context-only publication 保持 packet 稳定。
- TDD / stress：先以缺失 `PollOutputFrame.h` 建立红灯；实现后覆盖首次 publication、slow reader、2 / 4 / 8 readers × 100,000 次 owner publish 无撕裂、context-only packet 稳定、payload packet 递增、serializer 单帧复制，以及 unavailable / degraded / shutdown neutral。
- 验证：`DualPadInputV2Tests` build/run、`DualPadReplayHarnessTests` build/run、`xmake build -y DualPad` 均 exit 0。DLL 已按本机 xmake deploy 配置更新，PDB 因目标占用跳过复制。上述仍是 host/build 证据；E-POLL、generation-based pulse、IDA/dump 与 Favorites 实机循环尚未完成，`enable_native_favorites=false` 和 release status `NO-GO` 保持不变。

## 2026-07-11 02:16:07 +08:00

- `S-DP5-RC20-HOTFIX / Unit 6` code slice 完成：`PollCommitCoordinator` 不再把 commit/Poll 调用次数当 pulse clock。每个 pulse/toggle token 记录 `downGeneration` / `upGeneration`；down 在 owner generation N 的 commit acknowledgement 成功时生效，release 必须位于严格后续 owner generation，并继续满足已有 `minDownMs` 下界。同 generation 重复 begin/tick/flush 不会缩短 pulse。
- context/epoch 变化会清除 stale pending，并对已可见 down 最多提交一次 up；recovery/overflow/device disconnect/route unavailable 使用带 generation 的 boundary cancellation 清除全部 managed native state，并记录 cancelled token 的逻辑 up generation。快速双击 `Game.Favorites` 只 coalesce 一个 pending pulse，host contract 证明得到两组不重叠 down/up token，不产生 overlap 或 stuck-down。
- `CommittedButtonState -> PollOutputFrame -> PollDiagnostic` 现在携带 pulse token、down generation、up generation；一次 output publication 的 button/pulse metadata 同代。`NativeButtonCommitBackend::CommitPollState(runtimeGeneration)` 只在 owner tick 被调用；Poll hook仍仅 acquire/serialize，不推进 token。
- TDD：4 参数 `BeginFrame`、`EmitRequest.runtimeGeneration`、boundary API 与 pulse record 缺失先产生编译红灯；实现后 green。测试首次使用 `RE::BSFixedString` 触发 repo 已知 standalone string-pool hang，进程已终止并清理；最终将 coordinator owner-side action key 改为 `std::string`，建立不依赖 Skyrim runtime 的纯 host seam，没有改变 action ID 文本或绑定语义。
- 验证：`DualPadInputV2Tests`、`DualPadNativeButtonCommitTests`、`DualPadGameplayProjectionTests`、`DualPadReplayHarnessTests` 均 exit 0；`xmake build -y DualPad` exit 0 并部署 DLL，PDB 仍因目标占用跳过复制。E-POLL / IDA ordering、真实 Skyrim pulse 可见性、Favorites dump 与循环门禁尚未完成；`enable_native_favorites=false` 与 release status `NO-GO` 不变。

## 2026-07-11 06:45:41 +08:00

- `S-DP5-RC20-HOTFIX / Unit 7` 完成：menu refresh request 现在从同一份 `PublishedPresentationState` 捕获 target menu name、stable instance ID、menu/movie pointer、menu stack revision、context revision、presentation epoch 与 requested dirty；scheduler 不跨线程读取 ContextResolver/MenuInstanceRegistry 拼接身份。
- UI task 不再遍历 `RE::UI::menuStack`。它只通过捕获名称获取一个 live target，并在调用前第二次读取 committed state，复验完整 target identity、refresh key、allowlist、movie 与 `_root` readiness。存在 `_root.DualPad_OnPresentationChanged` 时优先调用该 callback；否则只对显式 allowlisted 的同一 target 调用一次 `RefreshPlatform()`。
- Loading/Fader/MessageBox/Favorites 不在 allowlist；target 替换、revision/epoch 变化与 live pointer/movie mismatch 取消为 superseded；null movie/root 进入已有 bounded deferred/held retry。任何路径都不扫描或刷新其它 menu。
- TDD / evaluator：缺失 target fields 与 validation API 先编译红灯；实现后覆盖 target 传播、callback 优先、target-only fallback、null movie/root、instance 替换、stack revision 变化、denied menu 与 refresh key 身份变化。evaluator 发现 root readiness 与 callback 缺失被混淆后，增加独立 `_root` readiness 合同及调用前 committed-state 复验。
- 验证：`DualPadPresentationProjectionTests`、`DualPadContextResolverTests`、`DualPadInputV2Tests`、`DualPadReplayHarnessTests` build/run 均 exit 0；`xmake build -y -j 4 DualPad` exit 0 并部署 DLL，PDB 因目标占用跳过复制。静态扫描确认 `DoRefreshMenus()` 只剩一个 target `GetMenu()` 与一个 target `RefreshPlatform()`，无 `menuStack` 遍历；Graphify manual close-out exit 0，结果为 `1904 nodes / 4454 edges / 147 communities`。上述仍是 host/build 证据；真实 Skyrim menu lifecycle、E-PATCH、IDA/dump 与循环门禁未完成，release status 保持 `NO-GO`。

## 2026-07-11 07:05:57 +08:00

- `S-DP5-RC20-HOTFIX / Unit 8` code slice 完成：新增共享 `HookPatchTransaction`。所有 patch site 在首写前完成 exact preflight；每次 compare-write 后复验 replacement，失败后逆序以 expected-current 恢复 original 并再次复验。safe no-write、rolled-back、installed 与 `UnsafePartial` 分别映射 operational state / failure disposition，删除原先所有失败均 `failClosed=false` 的伪合同。
- upstream Poll hook 保存并解码原始 `CALL rel32`，为 thunk 生成近地址 absolute-jump stub，source call-site 只在原始 5 bytes 仍匹配时写入；transaction 成功后才启用 synthetic route。unsafe partial 时 route 保持 disabled，若 call 已指向本插件 thunk，该 thunk只走 original passthrough。install status/reason 通过原子状态和受锁原因副本形成一致 snapshot；即使 config disabled 也显式发布 `DisabledByConfig`。
- Skyrim compatibility surface 的两个函数入口不再使用会把普通 prologue 误解为 original target 的 `write_branch<5>`。新路径验证固定 prologue，覆盖两个完整指令（8 bytes），生成保存原始指令并跳回 `source+8` 的 callable gateway；两个 entry branch 与 gamepad-enabled vfunc 作为一个三站点 transaction 提交。失败且成功回滚时清除 gateway authority；unsafe residue 时 hook flag 保持 disabled，已落到本插件的 thunk 通过 gateway/vfunc original 安全透传。
- TDD / failure injection：缺失 transaction header 与 patch encoding API 先编译红灯；实现后覆盖全站点 preflight、第 2 / 3 site apply 失败恢复 exact original、rollback expected-current 外部篡改拒绝盲写、原始 `CALL rel32` 解码、完整指令 entry patch + NOP padding，以及 writer 写入后抛异常。最后一个 site post-write exception 首次暴露 `appliedSites == totalSites` 误判 Installed，已用独立 `applicationFailed` 修复并记录到 `.learnings/ERRORS.md`。
- 验证：`DualPadRouteHealthContractTests`、`DualPadPresentationProjectionTests`、`DualPadInputV2Tests`、`DualPadReplayHarnessTests` build/run 均 exit 0；`xmake build -y -j 4 DualPad` exit 0 并部署 DLL，PDB 因目标占用跳过复制；Graphify manual close-out exit 0，结果为 `1934 nodes / 4518 edges / 147 communities`。上述证明 host transaction/状态合同和 Windows build；真实 Skyrim 1.5.97 站点、启动期 quiescent window、gateway 执行与 rollback 仍需 E-PATCH/Unit 10 动态证据，release status 保持 `NO-GO`。

## 2026-07-11 07:20:08 +08:00

- `S-DP5-RC20-HOTFIX / Unit 9` 完成：`DualPadIngressTests` 增加 deterministic virtual-clock matrix，覆盖 producer 500/1000 Hz × owner 30/60/120 Hz × producer-first/owner-first tie ordering。测试证明 pure analog 不产生 ordered event amplification、latest generation 单调、owner 不读取 future state、semantic P99 不超过一个 producer period；20,000 次 axis publication wall-clock telemetry 本机本次 P50/P95/P99 均约 100 ns，仅作趋势样本，不作为跨机器硬阈值。
- 新增 `docs/runtime_concurrency_contract.md`、`docs/runtime_backpressure_contract.md`、`docs/testing/rc20_runtime_validation.md`，同步 README、ARCHITECTURE、current input/backend/menu truth、DOC index、U5 closeout、authoritative baseline、work-package registry、builder harness/spec 与四份 generated docs provenance hash。静态门禁现在同时维护 PH8b completed 与当前 `S-DP5-RC20-HOTFIX` active/non-phase/fail-closed/NO-GO 边界。
- 首轮 Phase 8 暴露两处治理漂移并完成修正：`check_release_readiness.py` 不再锁死旧 `_attemptedInstall = true` 拼写，改为要求 atomic install attempt、transaction、RolledBack 与 UnsafePartial；Unit 1 配置变化导致 generated manifest hash 漂移，已由 canonical DocGen 更新为 `f32ed22b59cd387e`。相关 learning 记录为 `ERR-20260711-006`。
- 最终验证：`scripts/ci/run_phase8_ci.ps1` exit 0；`scripts/ci/run_rc_readiness.ps1` exit 0；dispatcher replay 10 个 mandatory scenarios 全部 no diff；`DualPadDInput8Proxy` build、release artifact manifest、builder JSON、reviewed/generated docs、legacy/release/U4/U5 static gates、Graphify rebuild 与 `git diff --check` 均通过。Graphify 结果为 `1936 nodes / 4531 edges / 147 communities`。这些仍是 host/build/static 证据；matching IDA/dump、真实 owner/Poll/UI ordering、Favorites 1000 次循环、UI profile 循环与 2-hour soak 留给 Unit 10。`enable_native_favorites=false`，release status 保持 `NO-GO`。

## 2026-07-11 08:08:00 +08:00

- `S-DP5-RC20-HOTFIX / Unit 10` 阶段性动态证据：IDA 已确认 Skyrim SE 1.5.97 unpacked EXE SHA-256 `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD`、image base `0x140000000`、`BSWin32GamepadDevice::Poll` RVA `0xC1AB40`、XInput call-site RVA `0xC1AB9D`、bytes `E8 2C 17 00 00` 与 original thunk RVA `0xC1C2CE`。`BSInputDeviceManager::PollInputDevices` RVA `0xC150B0` 有 3 个直接 caller context，因此静态图不证明固定 thread 或每 rendered frame 恰好一次。调查已写入 `docs/research/skyrim_xinput_poll_callsite.md`。
- build `1b3ca5a2bc7a` 的实机主菜单/读档日志确认 runtime `1-5-97-0`、relocated Poll/call-site/original target、hook installed、`nativeFavorites=false` 和单 target Main Menu refresh。读档时 owner 在线程 `29128` 完成 token 1-359 后，由线程 `22420` 到达 token 360；旧 guard 永久 `thread_drift`。这证明 InputFramePump 的逻辑单 writer 不能等同于进程全生命周期固定 OS thread。
- 按 systematic debugging + TDD 修复：旧 sequential cross-thread fixture 先以 `a later monotonic tick may rebind` 正确红灯；新 `ownerThreadHandoffs` API 再以成员缺失编译红灯。实现后，只有前一 RAII ticket 已释放且 frame token 严格递增时允许 `event=rebound`；ticket active 时异线程仍 `ThreadDrift` 永久 fail-closed。`DualPadInputV2Tests`、`DualPadReplayHarnessTests` 与 `xmake build -y -j 4 DualPad` 均 exit 0，Graphify 为 `1936 nodes / 4531 edges / 147 communities`。
- owner handoff 修复已独立提交并推送为 `1edb1949ecc4`。提交后重新构建并部署 matching DLL/PDB；DLL SHA-256 `F377AF16E628BF6CE30F67BF79EC4F3F4BCD65B99AF4D4E15992E9CFA4A66D81`，PDB SHA-256 `F6097998AC2C7EF866F8108F3CFE1CD8FE5C3FB70DD2C621D13FF79969B6248B`，DLL 内嵌 commit 与 HEAD 一致。PDB copy warning 来自 target symbolfile 已位于部署目标后的 self-copy，目标 PDB 时间/hash 已更新。
- 待用户简版复测：读取存档后日志必须出现可接受的 `event=rebound` 并继续推进 generation，不能再出现 `failure=thread_drift`；同时记录摇杆是否卡顿以及 Favorites 在默认 gate off 下正常/无反应/闪退。matching crash dump、physical/synthetic DPadUp A/B、1000 次 vanilla、每 profile 200 次和 2-hour soak 均未完成；Unit 10 保持未完成，release status 保持 `NO-GO`。

## 2026-07-11 08:18:00 +08:00

- Unit 10 阶段性 evidence close-out 对 commit `18bf1faf4076` 完成 canonical 验证：`scripts/ci/run_phase8_ci.ps1` exit 0；`scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest` exit 0。Phase 8 覆盖全部 canonical runtime targets、public-surface support proofs、DocGen、reviewed/generated consistency 和 static gates；RC readiness 额外证明 dispatcher replay 10 个 mandatory scenarios 全部 no diff、builder JSON、`DualPadDInput8Proxy`、release artifact manifest、Graphify `1936 nodes / 4531 edges / 147 communities` 与 diff hygiene。
- 首轮 Phase 8 在 generated docs clean check 暴露 provenance drift：`xmake.lua` 是 DocGen hash input，构建来源日志 slice 修改它后，4 份 generated docs 需要更新到 manifest hash `7ab15ab062bdd968`。canonical DocGen 连续两次输出一致，修复记录为 `ERR-20260711-008`；提交生成物后 Phase 8 与 clean RC readiness 均通过。
- 该自动化 close-out 不替代用户读档复测、Favorites matching dump、physical/synthetic A/B、循环和 soak。当前结论仍为 `NO-GO`，native Favorites 默认关闭。

## 2026-07-11 08:29:00 +08:00

- build `71c57b30ae0a` 完成修复后实机安全 smoke：runtime `1-5-97-0`、official Poll call-site hook 与 `nativeFavorites=false` 均匹配；游戏完成启动、读档、菜单操作并正常退出，runtime generation 推进到 3000。日志记录 1519 次 serialized `event=rebound`，没有 `event=degraded failure=thread_drift`，证明 `1edb1949ecc4` 的 owner handoff 修复在实机生效。
- 同一日志没有设备 `[SequenceGap]`、`queue_overflow` 或 fallback task 调度，但 `RuntimeDebug` 出现 31 次 `transition=sequence_gap`。系统化排查确认 `FrameAssembler::HandleOrderingViolation()` 同时把 ingress `seq` 和独立 producer 的采集时间戳当作排序权威；菜单期间 producer 交错导致时间戳轻微倒退，虽 `seq` 连续仍制造 recovery transition。
- TDD 红灯以严格递增 `seq=1,2`、时间戳 `2000,1999` 的多 producer fixture 稳定复现 `serialized multi-producer timestamps must not manufacture a transition frame`。首次完全移除 timestamp regression 检查时，Phase 8 中既有 `DualPadIngressTests` 正确阻止了同 producer 倒退合同丢失；最终实现改为跨 producer 由 ingress `seq` 排序、每个 `IngressSource` 内仍检查 timestamp regression、帧评估时间只以全局 max 推进。
- 新增 `scripts/dev/check_rc20_live_log.py` 与 6 个 Python 单测，自动判定 matching build/runtime、native Favorites gate、Poll hook、owner lifecycle、queue/sequence health 和 target refresh。对本轮实机日志的结果为单一 `FAIL: overflow or sequence gap observed`；高频 handoff 不再因 600-generation tick 采样策略产生误报。修复尚待新 build 实机复测，matching dump 与 Favorites 循环未完成，release status 保持 `NO-GO`。
- focused `DualPadInputV2Tests` / `DualPadIngressTests` 均 exit 0；完整 `scripts/ci/run_phase8_ci.ps1` exit 0。首次 clean RC readiness 已执行完 Phase 8、replay、Python evaluator tests、静态检查和 proxy build，按设计在未提交 working tree 上被 artifact manifest `--expect-clean` 拒绝；本 slice 提交后须在 clean HEAD 上重跑。Graphify manual close-out 为 `1956 nodes / 4616 edges / 143 communities`。

## 2026-07-11 08:50:00 +08:00

- 用户补充 build `71c57b30ae0a` 的肉眼结果：摇杆仍卡顿；打开普通菜单后部分按键无响应；收藏菜单无法打开。最后一项与 `nativeFavorites=false` kill switch 的 fail-closed containment 一致，不作为 native path 通过证据；前两项继续构成 release blocker。
- 高频 1519 次 serialized owner handoff 进一步暴露 S3 UI authority 缺口：`ContextRefreshTick` 原先在任意 owner thread 上调用 `UiMenuObserver::Capture()`，直接遍历 live `RE::UI::menuStack`；`ScaleformPromptAdapter::OnMenuOpened()` 也在排 UI task 前直接 attach。该行为与“runtime owner 可迁移但 UI/Scaleform 只能在 verified UI task”合同冲突。
- TDD 红灯 1 证明 `MarkMenuEvent()` 未即时发布 fail-closed `Partial` snapshot；绿灯后 pending snapshot 保留上一稳定 nodes 并携带新 event metadata。红灯 2 先因缺少 `PublishCapturedSnapshot` 编译失败；实现后 stale event sequence 不能覆盖新 pending event 或清 dirty，matching capture 才原子发布。架构静态红灯同时捕获 owner-side `observer.Capture()`、缺失 `AddUITask` capture 和直接 Scaleform attach；修复后 3 项 static tests、`DualPadContextResolverTests` 与 `DualPadPromptSnapshotTests` 均 exit 0。
- 当前实现由 `ContextEventSink` 只发布 event facts 并请求 coalesced UI task；`UiMenuObserver` 只在 `AddUITask` 回调读取 `RE::UI`，capture 期间若有新事件则拒绝 stale publish 并续排；runtime owner 只消费 by-value snapshot。Phase 8 现在显式 build/run `DualPadContextResolverTests`，RC readiness 用 Python test discovery 覆盖 live-log 与 UI boundary suites。新 build 尚未完成 canonical close-out 与实机复测，release status 仍为 `NO-GO`。

## 2026-07-11 09:05:00 +08:00

- UI authority 审计补齐最后一处 owner-side 访问：`ActionExecutor` 原先在安排 HUD task 前直接调用 `RE::UI::GetSingleton()`，且使用普通 `AddTask`。新增 static test 先稳定红灯，随后移除 task 外 UI access，并将 HUD mutation 改为 `AddUITask`；owner tick、menu capture、Scaleform attach、HUD mutation 与 target refresh 现均按各自静态门约束在 UI task boundary 内。
- live-log evaluator 先以 6 个新增红灯证明旧检查器会误收缺少 UI capture、只有初始 `eventSeq=0` capture、缺少/零目标 refresh、运行不足 600 generation、UI task queue failure 和缺少 clean shutdown 的日志；实现后 evaluator 13 项单测与 UI boundary 4 项 static tests 全绿。实机 safe smoke 现在必须留下 matching menu-event UI snapshot、实际单 target refresh 和正常退出证据，同时继续拒绝 `thread_drift`、queue/sequence failure 与 native Favorites gate 漂移。
- 最终未提交状态验证：`scripts/ci/run_phase8_ci.ps1` exit 0；`scripts/ci/run_rc_readiness.ps1` exit 0；17 个 Python tests 全部通过；dispatcher replay 10 个 mandatory scenarios 全部 no diff；`DualPadDInput8Proxy`、release artifact manifest、reviewed/generated docs、builder JSON、static gates 与 `git diff --check` 均通过。Graphify manual close-out 为 `1969 nodes / 4655 edges / 146 communities`。
- 首次大范围编译 `DualPad` 与 `DualPadReplayHarness` 时，xmake 默认 34 jobs 两次触发本机 MSVC `C3859` / `C1076`、Windows 1455 pagefile limit；按既有 `ERR-20260711-004` 分别用 `-j 4` 预热后，原始 canonical 命令均完整通过。该资源故障不作为代码失败或通过证据。提交后仍需用新 HEAD 重建 matching DLL、执行 clean manifest gate 并部署；新 build 实机 smoke、matching dump、Favorites loops 与 soak 未完成，release status 保持 `NO-GO`。

## 2026-07-11 09:30:00 +08:00

- build `32617f1ed2fa` 的新实机日志通过 build/runtime、`nativeFavorites=false`、Poll hook、owner generation 2400、13 次 menu-event UI-task capture 与单 target refresh 证据；标准 crash/WER 目录在同一时间窗没有新增 Skyrim crash artifact。日志未出现 clean shutdown marker，因此退出方式仍待下轮明确，不能记为正常退出证据。
- 同一日志没有设备 `[SequenceGap]`、`queue_overflow`、UI task queue failure 或 runtime owner permanent degraded，但出现 429 次 `transition=sequence_gap`、121 次 `transition=explicit_reset` 与高频 device boundary/menu refresh。系统化排查确认 `IngressSource::DeviceFamilyPublisher` 同时承载 HID 与键鼠线程/时钟，旧 per-source timestamp watermark 会丢弃 seq 连续的 boundary marker；latest-wins source snapshot 又可能领先本轮 capture cutoff，旧逻辑会把这种正常超前误判为 marker mismatch。
- TDD 红灯 1 使用真实 `IngressHub::PublishSourceEvidenceFrame()` 构造 Gamepad(2,000,000) -> KeyboardMouse(1,999,000) -> Gamepad(2,001,000)，现代码稳定失败为 `independent HID and keyboard capture clocks must not manufacture sequence loss`。绿灯后移除 timestamp ordering authority，`IngressEvent.seq` 成为唯一 ordered authority，frame evaluation time 继续以全局 max 推进。
- TDD 红灯 2 连续发布 3 个 device-family marker、每个 owner tick 只 capture 1 个 event，同时携带 revision 3 的 latest snapshot；旧逻辑首个 tick 即 `ExplicitReset`。绿灯后 ahead-of-cutoff latest snapshot 延迟到匹配 marker 被消费，真正 ordered pair mismatch 仍保持 fail-closed。focused `DualPadIngressTests` 已 exit 0；canonical、Graphify、matching build 部署与新实机复测尚未执行，release status 保持 `NO-GO`。
- cutoff 安全复核又补出 2 个 TDD 红灯：未配对 marker 后的 `LatestPadState` 会绕过 source 延迟发布 stable，ordered `PadSnapshot` 也会提前发布。最初直接拒绝 pending 窗口内的 ordered pad facts，随后 Phase 8 的 live Menu D-pad fixture 正确拦截该过宽修法；最终实现允许 pending window 暂存同批 ordered facts，但 `FlushWindow()` 在配对完成前不发布，newer marker 会丢弃无法配对的旧窗口，`LatestPadState` 也延迟 generation。matching revision 到达后才一次释放 source、ordered facts 与 latest pad。
- 本轮前一版实现已通过 Phase 8 与未启用 clean-manifest 的 RC readiness，包含 17 个 Python tests、10 个 replay scenarios、完整 canonical targets 与 Graphify `1972 nodes / 4674 edges / 148 communities`。由于随后新增了上述 cutoff 安全修复，这组结果只作为中间证据；最终 canonical / Graphify 必须在最新工作树上重跑。
- 最终实现重新通过 `scripts/ci/run_phase8_ci.ps1` 与未启用 clean-manifest 的 `scripts/ci/run_rc_readiness.ps1`：完整 canonical targets 全绿，17 个 Python tests 通过，10 个 mandatory replay scenarios 全部 no diff，reviewed/generated docs、legacy/release/U4/U5 static gates、proxy build、artifact manifest 与 `git diff --check` 均通过。最终 Graphify 为 `1973 nodes / 4681 edges / 147 communities`。提交后仍需重建 matching DLL/PDB 并运行 `-ExpectCleanManifest`；实机复测未完成，release status 保持 `NO-GO`。

## 2026-07-11 10:00:00 +08:00

- 用户完成 matching build `b5899084bf6d` 复测并明确报告：摇杆依然卡顿，Journal 菜单扳机翻页依然无效。该主观结果优先于 live-log evaluator 的结构性 `PASS`，release status 继续为 `NO-GO`。
- matching log 记录 build/runtime 正确、`nativeFavorites=false`、generation 推进、898 次 serialized owner handoff、0 次 `sequence_gap`、1 次 `explicit_reset`、36 次普通 boundary transition、0 次 queue overflow / owner permanent degraded，并有正常 screenshot service shutdown。timestamp/cutoff 修复已消除旧 storm，但没有解决用户主症状。
- 根因反向追踪闭合为 `LatestPadState -> InteractionEngine -> GameplayProjectionFrame` 语义错配：latest 每帧提供完整 axes/triggers，`InteractionEngine` 却把 `ResolvedActionFrame.values` 与 `Value` phase 一起限制为 change-only；projection 每代从零构造，导致 held stick/trigger 在下一代回零。该断点同时解释摇杆跳变和全按下扳机只短暂存在一代、容易被 Skyrim Poll 错过。
- TDD 红灯在同一 runtime/context 连续输入两帧相同 `RightStickX=0.5`、`RightTrigger=1.0`，旧代码稳定失败为 `unchanged stick current-state must remain present in every complete projection frame`。最小修复后，非 neutral Axis1D/Axis2D absolute value 每个 stable frame 都进入 `values`，只有实际变化才进入 `changes`；neutral 缺省仍投影为零。
- focused `DualPadInputV2Tests` 已两次 exit 0；新增真实 checked-in `JournalMenu -> Axis:RightTrigger -> Journal.TabRight -> NativeAxisTarget::RightTrigger` 连续两帧回归也通过。canonical、Graphify、matching build 部署与新一轮实机复测尚未执行。

## 2026-07-11 10:08:38 +08:00

- 持续模拟量 current-state 修复已通过 focused 验证：`DualPadInputV2Tests`、`DualPadGameplayProjectionTests`、`DualPadPropertyTests`、`DualPadIngressTests` 均 exit 0；覆盖相同非 neutral stick/trigger 连续帧、真实 Journal `RightTrigger -> Journal.TabRight` 绑定以及 neutral release 回零。
- `scripts/ci/run_phase8_ci.ps1` 在当前工作树完整 exit 0：canonical targets、DocGen、reviewed/generated consistency、legacy/release/config/prompt/menu/glyph gates 与 generated docs clean check 均通过。首次扩大并行编译命中本机已知 MSVC `C3859` / `C1076`（Windows 1455 pagefile limit），按既有低并发策略 `-j 4` 重试后通过；该资源错误不计为产品测试失败。
- 以上仍是自动化证据。matching commit/DLL 部署与用户短复测尚未完成，release status 保持 `NO-GO`，`enable_native_favorites=false` 不变。

## 2026-07-11 10:12:32 +08:00

- 最终差异审查把 Axis1D 的内部 change-threshold 基准恢复为原有“实际变化时更新”，避免把 current-state 修复扩大成去抖语义变更；非 neutral `values` 仍逐 stable frame materialize，`changes` 仍只在变化时发射。收紧后四组 focused tests 均重新 exit 0。
- 最新工作树的 `scripts/ci/run_rc_readiness.ps1` 完整 exit 0，并内含最新 Phase 8：全部 canonical targets、17 个 Python tests、10 个 mandatory replay scenarios、reviewed/generated consistency、legacy/release/config/prompt/menu/glyph gates、`DualPadDInput8Proxy`、artifact manifest、builder JSON 与 `git diff --check` 均通过。Graphify manual close-out 为 `1974 nodes / 4690 edges / 148 communities`。
- 自动化证明当前实现与治理合同一致，但不替代原症状实机验证。下一步是提交/推送、按新 HEAD 构建部署 matching DLL，再由用户短测摇杆与 Journal L2/R2；在实测通过前 release status 继续为 `NO-GO`，native Favorites 继续默认关闭。

## 2026-07-11 10:26:50 +08:00

- 用户完成 matching build `4d9a43e845db` 复测：摇杆已不卡，证明逐 stable frame 保留非 neutral current-state 的修复在实机生效；Journal L2/R2 翻页仍无效，因此 release status 保持 `NO-GO`。
- live-log evaluator 确认 build/runtime 匹配、generation 到 1800、705 次 serialized handoff、`nativeFavorites=false`，但因缺少 clean shutdown marker 返回 `INCOMPLETE`。日志中的 live target 是 `Journal Menu`，refresh 记录 `uiContext=1`（`UnknownTrackedMenu`），interaction 记录 legacy `ctx=Menu`；该证据与 catalog 静态反向追踪一致。
- 配置本身正确：`[JournalMenu] Axis:LeftTrigger=Journal.TabLeft`、`Axis:RightTrigger=Journal.TabRight`，两项 native descriptor 分别写 `NativeAxisTarget::LeftTrigger/RightTrigger`。根因是 `ContextCatalog` 只把无空格 `JournalMenu` 放入 `menuNameIndex`；有空格的实机名称只存在于 alias index，而 `ResolveMenuName()` 按设计不读取 alias，导致 `JournalLayer` 未启用。
- TDD 红灯用 exact live name `Journal Menu` 经 `MenuInstanceRegistry -> ContextResolver` 稳定失败为 `live Skyrim 'Journal Menu' name must resolve to the Journal context`。最小修复只补齐该 entry 的 live menu name；绿灯后 `DualPadContextResolverTests` exit 0。`DualPadInputV2Tests` 的共享 Journal fixture 也改用 live name，包含真实 checked-in trigger binding 的端到端测试重新 exit 0。
- matching commit/DLL、canonical RC readiness、Graphify 与新一轮 Journal L2/R2 实机复测尚未完成；native Favorites 继续默认关闭。

## 2026-07-11 10:32:28 +08:00

- live `Journal Menu` context 修复已完成未提交 canonical close-out：`scripts/ci/run_phase8_ci.ps1` exit 0，随后 `scripts/ci/run_rc_readiness.ps1` exit 0。全部 canonical targets、17 个 Python tests、10 个 mandatory replay scenarios、reviewed/generated consistency、legacy/release/config/prompt/menu/glyph gates、proxy build、artifact manifest、builder JSON 与 diff hygiene 均通过；Graphify 为 `1974 nodes / 4690 edges / 148 communities`。
- 首轮 Phase 8 按设计在 generated-doc clean check 拦截 provenance drift；`ContextCatalog.cpp` 是 DocGen input，4 份 generated docs 的稳定 manifest hash 从 `7ab15ab062bdd968` 更新为 `c048fdb1677fa132`。连续生成一致后暂存生成物并重跑通过；`.learnings/ERRORS.md` 的既有 `ERR-20260711-008` 已补充 dirty-tree staging 规则。
- 上述仍是 host/build/static 证据。提交/推送后必须用新 HEAD 强制重建并部署 matching DLL/PDB，再让用户只复测 Journal L2/R2；在通过前 release status 保持 `NO-GO`，native Favorites 保持默认关闭。

## 2026-07-11 11:27:00 +08:00

- 用户完成 matching build `a7a75fac5281` 的 Journal 复测并明确确认 L2/R2 标签翻页有效。结合 build `4d9a43e845db` 已确认的摇杆不卡，native Favorites 之外的两条用户可见 blocker 均获得实机通过证据。
- `python scripts/dev/check_rc20_live_log.py --expect-commit a7a75fac5281 --json` exit 0，结果为 `PASS`、空 reasons、runtime `1-5-97-0`、latest generation 1200、506 次 serialized owner handoff、`native_favorites=false`。
- matching 日志中的 live target 为 `Journal Menu`；交互记录已显示 `presentationUiContext=5 / resolverUiContext=5 / legacyContext=JournalMenu`，不再回退到 `UnknownTrackedMenu / Menu`。用户观察证明 L2/R2 的 `Journal.TabLeft/TabRight` 投影已被游戏实际消费。
- `S-DP5-RC20-HOTFIX` 以 `GO WITH NATIVE FAVORITES DISABLED` 完成，`current_sprint` 清空。native Favorites 继续默认关闭；physical/synthetic DPadUp A/B、1000 次 open/close、各 UI profile 200 次、2-hour soak 与 matching crash dump 均未执行，因此不得标记完全 `GO`，DP5 后续验证仍保持 `in_progress`。

## 2026-07-11 11:34:00 +08:00

- 首轮条件 close-out 的 `scripts/ci/run_rc_readiness.ps1` 在全部已执行 runtime targets 通过后停于 reviewed-doc consistency：两个治理门禁仍硬编码 `NO-GO / current_sprint=S-DP5-RC20-HOTFIX / in_progress`。这是动态证据到达前的旧状态合同，不是产品回归。
- TDD 红灯新增 `tests/python/test_rc20_governance_state.py`，分别直接运行 reviewed-doc 与 RC closeout gate；旧检查器稳定失败。最小修复把 builder、authoritative baseline、README/索引、U5 closeout 与检查器原子升级到 `current_sprint=null / hotfix completed / GO WITH NATIVE FAVORITES DISABLED`，同时继续要求 native Favorites fail-closed、DP5 `in_progress/passes=false` 与完整 `GO` 禁止边界。
- focused 治理测试现为 2/2 通过；完整 RC readiness 仍需在更新后的工作树重新执行，当前不能用首轮失败前的部分输出宣称 canonical close-out 通过。

## 2026-07-11 11:35:00 +08:00

- 更新治理状态后的 `scripts/ci/run_rc_readiness.ps1` 完整 exit 0。Phase 8 canonical targets 全部通过，Python discovery 为 19/19，10 个 mandatory dispatcher replay scenarios 全部 no diff；reviewed/generated consistency、legacy/release/U4/U5 gates、builder JSON、DualPadDInput8Proxy、release artifact manifest 与 `git diff --check` 均通过。
- Graphify manual close-out 结果为 `1979 nodes / 4698 edges / 146 communities`。构建阶段仅有目标 PDB 正被占用而跳过同路径复制的 warning，DLL 与 proxy build 均明确 `build ok`，不改变门禁 exit 0 结论。
- 该 fresh canonical 结果与 build `a7a75fac5281` 的 matching safe-smoke `PASS` 共同支持 `GO WITH NATIVE FAVORITES DISABLED`；提交后仍需对新 HEAD 生成 matching artifact，并运行 clean-manifest gate。

## 2026-07-11 12:25:16 +08:00

- 用户补充实机结论：当前键鼠与手柄共同作用仍功能混乱、实际不可用。专项可行性审查确认仓库已有 per-channel gameplay ownership、sustained source aggregation、single menu presentation owner 和 cursor handoff 设计，但旧文档中的 `InputModalityTracker / GameplayOwnershipCoordinator / PadEventSnapshotProcessor` 挂点已被 PH8a 主线替换。
- 当前生产断点已静态闭合：`DualPadRuntime` 将 6 个 KBM gameplay policy facts 全部硬编码为 false；`InputFramePump` 只发布 presentation source evidence；`HidReader -> LiveInputFactProducer` 对每份 HID report 无条件记录 gamepad activity，持续刷新约 1.5 秒 lease 并清除 KBM evidence；`SyncExternalHeldContributors` 仍固定 `kbmSprintHeld=false`。因此现有 arbitration/gate consumer 存在，但 live KBM producer 与 meaningful gamepad activity 分类未闭合。
- 本轮通过 IDA MCP 重新反编译 matching 1.5.97 unpacked EXE：`0x140C150B0` 依次 Poll 四个 input device slots，`0x140C1AB40` 从 `XInputGetState` 消费完整 button/trigger/stick current-state，`0x140ECD970` 向菜单发布单一 `_root.SetPlatform`，`0x140ED2F90` 区分真实鼠标位置与手柄光标积分。SHA-256 已从磁盘复验为 `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD`。
- 结论与证据已写入 `docs/reviews/2026-07-11-mixed-input-feasibility-gpt-review-brief_zh.md`，包含可直接交给 GPT 的提示词、证据置信度、最小 `input_v2` 落点和实机矩阵。本条只完成调查与文档，不声称混合操作已修复；对该能力本身保持 `NO-GO`，是否迁移整体 RC 治理状态留待确认修复 slice 时原子处理。

## 2026-07-11 12:40:42 +08:00

- 为“让外部 GPT 基于源码与游戏事实制定更优且可执行方案”新增正式方案请求 `docs/reviews/2026-07-11-mixed-input-solution-plan-request_zh.md`。提示词要求先审计附件证据，再比较至少 4 种 engine mode 架构，并输出精确数据结构、线程 ownership、文件清单、逐任务 TDD 红/绿步骤、canonical 命令、IDA/debugger gate、实机矩阵、commit/rollback 和发布门禁；禁止只给模糊 phase 或直接实现代码。
- 补充 IDA 证据写入 `docs/research/skyrim_mixed_input_mode_queries_zh.md`：`0x140C15240` 已由 device slot 和 vtable `+0x38` 识别为 gamepad `IsEnabled()` 语义查询，共有 26 个 direct code xrefs；`0x140705AE0` 会据此在 gamepad response curve/deadzone/acceleration 与 KBM 时间步/灵敏度缩放之间分支，且由 gameplay/camera-like 与菜单路径共同调用。该证据修正了“engine mode 是纯 presentation 字段”的过度简化。
- 已生成定向附件 `DualPad_Mixed_Input_Solution_Plan_Bundle_2026-07-11.zip`：根目录包含 4 份按阅读顺序命名的提示词/证据，`repository/` 包含 72 个相关 source/doc/test/toolchain 文件；ZIP 为 260,003 bytes，SHA-256 `F2A40CBF7B75E086924BBFA9576F13FE09D00BC767C7C0F1CD782B9705A69CE2`。通过 .NET ZipArchive 验证 84 entries / 76 files、Unicode 文件名、无绝对/上跳路径，且 4 份根文档逐一与 repo 源文件 SHA-256 相同。
- 本轮只增强调查材料和外部方案约束，没有修改 runtime 代码，也不声称 mixed-input blocker 已修复。后续应先审查 GPT 输出与 repo/IDA 事实的一致性，再决定是否建立正式修复 slice。
## 2026-07-12 11:29:22 +08:00

- `S-DP5-MIXED-INPUT / WP2 completed`：
  - RED 先证明缺失 `KbmGameplayFactProducer`、Skyrim adapter boundary、映射后 current/ordered facts、稳定 `(device,idCode)` quarantine、精确 synthetic provenance、adapter failure fail-closed、生产 `Game.Look` mouse delta 身份及 held-repeat 去重。
  - 最小实现新增 core-only `KbmGameplayFactProducer` 和 `SkyrimKbmInputAdapter`：ControlMap snapshot/fingerprint、callback-local ordered event list、KBM current masks、物理 ledger/quarantine、mouse delta source activity 与精确 receipt suppression 合同均已落地；mouse delta 使用独立 `0xFFFFFFFF` 物理 ID，避免与鼠标左键 `idCode=0` 冲突。
  - `InputFramePump` 现在按 `BeginFrame -> CaptureBindingSnapshot -> ObserveEventList -> PublishOwnerKbmBatch -> DrainOnOwnerTick` 发布同一 owner callback 的 KBM facts；`eventListComplete=false` 时不调用 producer、不发布 batch，因此不推进 current-cycle-sensitive ledger。
  - `button->IsDown()` 是唯一 initial press 判据；held repeat 只保留 physical current-state，不制造 ordered press 或 meaningful activity。平台 raw provider 保持 `physicalOnlyProvenance=false / complete=false`，synthetic receipt 尚无 production route，因此 I-KBM 两项独立裁决均保持 NO-GO。
  - focused：`python tests/python/test_mixed_input_wp2_boundaries.py`、`xmake run -y DualPadIngressTests`、`xmake run -y DualPadInputV2Tests` 全部 exit 0。
  - adjacent：`xmake run -y DualPadContextResolverTests`、`xmake run -y DualPadPresentationProjectionTests`、`xmake build -y DualPad` 全部 exit 0；PDB 同路径占用仅产生已知 copy warning，DLL 明确 `build ok`。
  - 本 WP 只发布 coherent KBM ingress facts，未接入 gameplay gate、Sprint bridge、engine query override、cursor side effect 或其它 IDA-gated production capability。
## 2026-07-12 11:31:32 +08:00

- `S-DP5-MIXED-INPUT / WP3 start`：
  - 按批准顺序进入 causal ingress cutoff、global epoch、gamepad session 与 recovery scope；先补 partial drain、latest-only、empty capture、overflow、disconnect/session、revision-ahead 和 atomic first-batch 红灯。
  - 本切片不实现 WP4 channel arbitration、WP5 receipt/runtime mutation，也不启用任何 IDA-gated production capability。
## 2026-07-12 11:49:00 +08:00

- `S-DP5-MIXED-INPUT / WP3 completed`：
  - RED 覆盖 partial drain 越 cutoff 应用 latest、boundary payload 缺 control-map revision、首批 KBM event 前无 atomic marker、runtime dispatcher 丢弃完整 `IngressCapture`、HID 无 session tag、sequence gap 无 Hub reset receipt，以及 ControlMap adapter failure 仍可能推进 ledger。
  - Hub 现在是 `inputStateEpoch`、`gamepadSessionId`、`controlMapRevision` 与 binding generation transaction 的唯一 writer；latest-only 不分配 ordered seq，empty capture 保留累计 cutoff，old epoch/session/revision latest defer 或 drop。
  - `FrameAssembler` 按 causal tail/cumulative cutoff 应用 pad、connection、KBM latest；overflow/global reset 清除 durable gameplay carry，gamepad disconnect 只清 gamepad scope并保留 KBM Move/Sprint facts；ordered seq gap 同 tick产生 fail-closed transition并请求 Hub global reset receipt。
  - gamepad report 携带 producer 已确认 session；old-session 大 generation report 被明确拒绝且 HID reader 会重置 classifier/baseline并重新发布 connection。connectivity 不受 context/control-map epoch 误清，pad current-state则继承 Hub 当前 context/menu boundary。
  - owner KBM batch在容量检查通过后原子提交 boundary marker、new `IngressBoundaryKey`、binding generation、首个 ordered event与 latest；overflow只保留完整物理 current-state，标记 virtual-ineligible，不制造 ordered/semantic action，也不让旧 held在后续 KBM frame重附着。
  - focused 与相邻回归已通过：Ingress、InputV2、Replay、Property、Fuzz、两个 Python boundary tests及主 DLL build 均 exit 0；最终 fresh rerun 与 Graphify 在 commit 前执行。
  - WP4 channel arbitration、WP5 current-cycle receipt/runtime mutation和所有 IDA-gated capability仍未启用。
## 2026-07-12 11:51:43 +08:00

- `S-DP5-MIXED-INPUT / WP4 start`：
  - 按批准计划开始 Look、Move、Combat、TransientDigital 四份独立 next-Poll arbitration state；先写 mixed-channel、200 ms quiet/candidate、None owner 与 scoped reset 红灯。
  - 本切片只建立 next-Poll owner/gate/reason/state，不实现 WP5 current-cycle event mutation、Poll receipt 或任何 engine/menu/cursor patch。

## 2026-07-12 12:06:25 +08:00

- `S-DP5-MIXED-INPUT / WP4 completed`：
  - RED 先以缺失 `ChannelArbitration.h` 证明独立 per-channel 状态机尚不存在；第二个 RED 证明 production `BuildStableRuntimeInput` 尚无可验证的 KBM policy builder。随后相邻回归捕获 global transition 被重复应用到首个 stable frame、错误中和新鲜 RS 的边界问题，并修正为 global 只在 transition 原子清空一次。
  - 新增 `ChannelArbitration` pure decision：`ChannelOwner` 固定保留 `Gamepad=0 / KeyboardMouse=1` 并追加 `None=2`；Look、Move、Combat、TransientDigital 各自持有 owner、gamepad candidate 与 KBM quiet timestamp，低于 sustain 会清 candidate，双方 inactive 会进入 None 并中和 virtual channel。
  - Look 使用 200 ms owner-clock physical mouse quiet window；同帧 physical KBM 优先但会锁存已达 enter 的 gamepad candidate，quiet 到期或最后 move key release 时可在同 tick 按 sustain reclaim。Combat 只同时 gate LT/RT，不影响 Look/Move；通道之间不再共享 primary owner writer。
  - live runtime 已消费 coherent `LatestKbmGameplayFacts` 的 mouse、Move、Combat、TransientDigital 与 keyboard/mouse sustained 六类字段；只有 `current.complete && virtualGameplayEligible` 的 facts 可进入 policy，sustained 仍只作为 WP6 shadow contributor 输入。
  - gameplay arbitration 已从 engine/menu/cursor 字段中抽离；旧 `PrimaryPathArbitration` 双 writer 已删除。presentation plan 在仲裁之后独立投影，未增加 engine query、menu 或 cursor patch。
  - `GamepadSource` reset scope 现在从 `FrameAssembler -> IngressRecovery -> DualPadRuntime` 保真传递，只清 gamepad owner/candidate并保留 KBM owner/quiet；Global reset 才清全部四份 state。channel next state 仅在 `PollOutputAdapter` apply 成功后提交，失败帧不推进 candidate。
  - Focused GREEN：`xmake run -y DualPadGameplayProjectionTests`、`xmake run -y DualPadInputV2Tests` 均 exit 0；覆盖 mixed Look/Move、199/201 ms、latched/unlatched sustain、双 trigger gate、None neutral、scoped reset、production KBM builder 与 failed-apply rollback。
  - 相邻回归：`xmake run -y DualPadIngressTests`、`xmake run -y DualPadReplayTests` 均 exit 0；`xmake build -y DualPad` exit 0。构建仅出现目标 PDB 同路径占用的已知 copy warning，DLL 明确 `build ok`。
  - WP5 current-cycle receipt/prepare/apply/commit、event mutation、Sprint bridge、engine query、cursor side effect 与全部 IDA 动态门禁 capability 仍未启用；下一切片固定为 `WP5 shadow`。

## 2026-07-12 12:09:28 +08:00

- `S-DP5-MIXED-INPUT / WP5 shadow start`：
  - 从独立 WP4 commit `1c0390f` 开始 Poll materialization receipt、current-cycle pure gate plan、transient dedup disposition 与 prepared runtime commit。
  - current-cycle identity 固定只来自 verified serialize 后的 `PollMaterializationReceipt`，owner/Pump 只 exact consume-once；禁止重新 Acquire 最新 Poll frame 猜测本轮 materialized identity。
  - 本切片只运行 callback-local shadow adapter/audit，真实 event mutation 在 I-P 动态证据通过前保持关闭；任何 audit/adapter failure 都不得推进 channel、transient、Sprint contributor 或其它 current-cycle-sensitive ledger。

## 2026-07-12 12:33:00 +08:00

- `S-DP5-MIXED-INPUT / WP5 shadow completed`：
  - RED 先证明仓库缺失 `PollMaterializationReceipt` 与 Pump receipt consume wiring；随后新增事务断言准确捕获“shadow audit 成功但未实际 apply 时错误提交 sensitive ledger”的漏洞，并以 focused target 进程 exit 1 验证红灯原因。
  - verified XInput serialize 现在发布 bounded immutable receipt，冻结 publication/runtime/packet、epoch/session、context/control-map、cutoff 与 event batch identity；Pump 只按 callback thread consume-once，missing、ambiguous、already-consumed 与 thread mismatch 均返回精确 failure，且没有重新 `AcquireForPoll()` 猜测 materialized identity。
  - `CurrentCycleGatePlan` 独立计算 Look、Move、Combat、TransientDigital current-cycle disposition 与双时间视图 writer count；物理 release 不作为 activation，identity/physical/route/consumer/scratch 任一不满足均零 event mutation。
  - callback-local 顺序固定为 receipt consume -> KBM batch publish -> Prepare audit token -> shadow Apply -> Commit audit -> owner drain；prepared runtime state 仅在 Poll output 成功且 current-cycle audit commit-safe 后提交。mutation required 时，只有 `success && mutationApplied && !shadowOnly` 可推进 channel/transient/Sprint 等 sensitive ledger；否则 previous state 保持不变并只对受影响 next-Poll channel fail-closed。
  - `SkyrimCurrentCycleEventAdapter::ProductionMutationEnabled()` 仍编译期返回 false；当前只扫描 callback list、检查 cycle/scratch 并报告 would-mutate descriptor，不改写物理或 virtual event。I-P 未完成，production current-cycle capability 明确保持 NO-GO。
  - transient dedup 已按 action/token/context 生成 physical-first disposition；physical release 不会误杀新的 virtual press。receipt mailbox 在 Pump unregister 时清空，避免重载后消费陈旧 identity。
  - Focused GREEN：`python tests/python/test_mixed_input_wp5_shadow_wiring.py`、`xmake run -y DualPadGameplayProjectionTests`、`xmake run -y DualPadInputV2Tests` 全部 exit 0。
  - 相邻回归：`xmake run -y DualPadReplayTests`、`xmake run -y DualPadPresentationProjectionTests`、`xmake run -y DualPadNativeButtonCommitTests`、`xmake build -y DualPad` 全部 exit 0；PDB 同路径占用仅产生已知 copy warning，DLL 明确 `build ok`。
  - 剩余硬门禁：I-P 决定真实 event mutation；I-SPRINT 结果 B 才允许 SprintHandler guard；I-KBM raw reconcile 与 synthetic suppression 仍分别 NO-GO；I-CURSOR 和 I-0/I-1/I-2/I-MENU/I-5 均未提前启用。

## 2026-07-12 12:37:00 +08:00

- `S-DP5-MIXED-INPUT / WP6 start`：
  - 按批准计划进入 Sprint complete contributor、virtual bridge 与唯一 materialization authority；先覆盖 G/K/M 三来源 OR、joining/non-final disposition、final release one-shot、reset/disconnect 和 prepared rollback。
  - 当前已确认 production 断点仍存在：`SyncExternalHeldContributors` 固定 `kbmSprintHeld=false`，coordinator 只有 G/K 两位并保留故意 handoff gap，live sustained executor 把完整 source mask 压扁为单一 Hold/Release。
  - 本切片不新增 `SprintHandler` hook；I-SPRINT 未得到结果 B，因此 guard capability 保持不存在。需要 current-cycle mutation 的 joining/non-final suppression 只进入 pure/shadow plan，并继续服从 WP5 commit-safe rollback。

## 2026-07-12 12:53:00 +08:00

- `S-DP5-MIXED-INPUT / WP6 completed`：
  - RED 先以缺失 `SustainedContributorDecision.h` 证明 G/K/M contributor pure model 尚不存在；第二个 RED 精确列出 coordinator 缺少 Mouse 位、full-mask API 与 `virtualBridgeDesired`；后续事务 RED 捕获 shadow audit 回滚 runtime ledger、但 proposed K|G mask 仍提前写入 backend 的漏洞。
  - 新增 pure `SustainedContributorDecision`：G/K/M 三位 mask、aggregate hold、virtual materialized bridge、effective emitter、joining press / non-final release suppression、same-batch earliest physical ordinal 和 consumed-once final release token 均由 owner-frame 单次裁决。
  - 两条序列 `G -> G|K -> K -> 0` 与 `K -> K|G -> G -> 0` 均保持 aggregate 无 gap；K-only/M-only 不合成 virtual press，G bridge 在 coordinator 中各只 materialize 一次 Down 和一次最终 Up。
  - producer 保留 callback-local Sprint `eventOrdinal`，Hub latest 记录 K/M 最早 physical ordinal并传入 GameplayPolicy；production projection 不再只在 gamepad resolved edge 上拼 sustained mask，而是每 owner frame发布完整 Sprint mask/bridge/suppression/release metadata。
  - `PollCommitCoordinator::SyncHeldContributors` 原子替换完整三来源 mask；旧 `GameplayKbmFactTracker` include/read、固定 `kbmSprintHeld=false`、单 contributor side channel、`pendingGamepadHandoff` 和故意 gap 已删除。其它 sustained action仍走原路径。
  - Sprint 完整 state 已进入 WP5 `PreparedRuntimeInputCommit`。mutation required 且 shadow audit 未 apply 时，runtime、outbound command 与 backend 全部重放 previous committed mask/bridge/token；不会出现 ledger 回滚但 backend mask 先行。gamepad disconnect只清G并在K/M仍 held时保留已materialized bridge；global reset清全部mask/bridge/emitter/token。
  - Focused GREEN：`xmake run -y DualPadGameplayProjectionTests`、`xmake run -y DualPadNativeButtonCommitTests`、`xmake run -y DualPadIngressTests` 全部 exit 0。
  - 相邻回归：`xmake run -y DualPadInputV2Tests`、`xmake run -y DualPadReplayTests`、`xmake build -y DualPad` 全部 exit 0；PDB 同路径占用仅产生已知 copy warning，DLL 明确 `build ok`。
  - Gate 保持：真实 joining/non-final event suppression 继续等待 I-P；没有新增 `SprintHandler` hook，I-SPRINT 结果 B 未到前 guard 继续 NO-GO。Favorites/SWF/glyph/haptics/rumble/bindings 均未修改。
