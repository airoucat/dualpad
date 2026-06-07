# DP5-RC20 U1.8 Debug Degraded Observability 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将 U1.1-U1.7 已产生的 runtime degraded reason、prompt freeze、overflow compaction 与 hook install failure 投影到 debug snapshot 和去重日志。

**架构：** 新增 `RuntimeDiagnostics` 作为 projection/helper 层，消费既有 `DualPadRuntimeResult`、`AssembledFactFrame`、`HookInstallResult` 和 overflow compaction debug payload。`DualPadRuntime` 只接线 snapshot/log，不改变 `input_v2` mainline、frame envelope、fail-closed 或 prompt/glyph 兼容返回 shape。

**技术栈：** C++23、CommonLibSSE-NG/SKSE logger、xmake focused tests、Phase8 CI。

---

## 文件职责

- 创建 `src/input_v2/gameplay/RuntimeDiagnostics.h`：定义 `RuntimeDebugSnapshot`、prompt debug state、reason name projection、去重日志 state API。
- 创建 `src/input_v2/gameplay/RuntimeDiagnostics.cpp`：实现 reason mask 文本化、hook/prompt/overflow debug projection 和 reason-transition 日志去重。
- 修改 `src/input_v2/ingress/FrameAssembler.h/.cpp`：给 typed overflow compaction stable frame 附加 compact summary，不改变 assembly 行为。
- 修改 `src/input_v2/gameplay/DualPadRuntime.h/.cpp/.Live.cpp`：在 stable degraded / hook failure / transition 后生成 snapshot，并只按 reason transition 打日志。
- 修改 `src/input_v2/telemetry/InputTraceRecorder.h/.cpp`：在 trace/debug session 下追加 `runtime_debug_snapshot.csv`，不加入 Phase0 golden required schema。
- 修改 `tests/input_v2/InputV2Tests.cpp`：覆盖 reason mask -> snapshot、hook status/debugReason、prompt freeze/unavailable、日志去重。
- 修改 `tests/input_v2/IngressTests.cpp`：覆盖 overflow transition 与 typed compaction summary。
- 修改 `xmake.lua`：将 `RuntimeDiagnostics.cpp` 纳入 runtime/test 文件组。
- 修改 `.dualpad-builder/progress.md`：记录 start/verification/closeout。
- 更新 GitHub #8 checklist：勾选 U1.8 并追加验证证据。

## 任务

### 任务 1：诊断 projection

- [ ] 新增 `RuntimeDiagnostics` 类型和 helper。
- [ ] `RuntimeHealthReasonNames(mask)` 必须输出 `GraphUnavailable`、`ManifestEpochSkew`、`ContextRevisionSkew`、`QueueOverflow`、`SequenceGap`、`BoundaryMismatch`、`PromptScopeFrozen`、`HookInstallFailed`。
- [ ] `ShouldEmitRuntimeDebugLog()` 对相同 snapshot key 返回 false，对 reason/debug transition 和 recovery 返回 true。

### 任务 2：runtime 接线

- [ ] stable degraded frame 增加 `PromptScopeFrozen` reason。
- [ ] hook failure early return 仍 fail closed，并带 hook status/debug reason 到 snapshot。
- [ ] transition frame 输出 `prompt_state=unavailable`、`prompt_reason=transition_frame`。
- [ ] `DualPadRuntime::GetLastDebugSnapshot()` 暴露最近一帧 debug snapshot。

### 任务 3：overflow 与 trace

- [ ] overflow typed compaction summary 记录 retained/dropped 字段。
- [ ] replay/live trace session 可追加 `runtime_debug_snapshot.csv`。
- [ ] 该 debug CSV 不进入 `Phase0TraceFiles()`，避免改 replay golden mandatory schema。

### 任务 4：focused tests

- [ ] `DualPadInputV2Tests` 覆盖 all-reasons mask projection、hook `signature_mismatch`、prompt frozen、transition unavailable、log dedupe。
- [ ] `DualPadIngressTests` 覆盖 overflow transition + typed compaction summary。
- [ ] 运行 `xmake build/run -y DualPadInputV2Tests` 与 `DualPadIngressTests`。

### 任务 5：close-out

- [ ] 运行 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`。
- [ ] 运行 replay generation + `dualpad_trace_diff.py`。
- [ ] 运行 builder JSON、reviewed/generated docs consistency、graphify rebuild、`git diff --check`。
- [ ] 更新 #8 checklist 和 `.dualpad-builder/progress.md`。
