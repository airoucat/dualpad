# Phase 8A Slice：Runtime Closeout / SingleAuthorityAssembly / PublicSurfaceSwap

> 本文承接 `09_slice_phase8_cutover_cleanup_and_ci_zh.md`。  
> 本 slice 只负责 runtime 收口，不负责 docgen provenance、reviewed docs 去重和 CI 接线。

## 目标

- 把 `src/input_v2/` 收口为仓库内唯一正式 runtime mainline。
- 把对外可观察黑盒接口全部切到新主线。
- 按固定顺序删除旧 authority，并把仅剩的 legacy-named 入口缩成无 authority shim。

## 冻结的设计决定

1. **`09a` 的执行顺序固定为 `SingleAuthorityAssembly -> PublicSurfaceSwap -> LegacyDeletion / ShimShrink`。**
   - 不允许先删旧 authority，再临时拼新的 published surface。
   - 不允许先切一半 public surface，再把另一半留到 `09b`。

2. **runtime cutover 不允许重新引入长期运行时开关。**
   - 不允许新增 `UseLegacyMainline=true/false`、`EnableInputV2=true/false` 之类的运行时配置。
   - 若必须使用过渡编译期开关，只允许一次性的 `DUALPAD_PHASE8_LEGACY_SHIM`，并且它必须在本 slice 退出前被删除。

3. **Phase 8A 之后只允许保留 2 组 legacy-named shim。**
   - `src/input/injection/PadEventSnapshotProcessor.*`
   - `src/input/glyph/ScaleformGlyphBridge.*`
   - 这 2 组文件即使仍保留，也只能承担参数打包、注册、转发与 error boundary；不得持有 authority、状态机、lookup table 或 fallback 决策。

4. **replay 验证仍以 `tests/replay/golden/` 为唯一根路径。**
   - `phase0/`、`phase6_prompt/`、`phase7_ingress/` 全都挂在同一 replay root 下。
   - `09a` 只消费这些 namespace，不得新建第二套 replay 目录。

5. **prompt 黑盒契约固定消费 `Phase 6` 的 `PromptSnapshotRecord`。**
   - prompt black-box contract 固定只消费 `Phase 6` 发布的 `PromptSnapshotRecord`。
   - `PromptSnapshotRecord` 固定包含：
     - `actionId`
     - `status`
     - `resolvedSet`
     - `resolvedContext`
     - `primary`
     - `alternates`
     - `resolutionSource`
     - `fallback`
     - `deviceProfile`
     - `promptScopeRevision`
     - `manifestEpoch`
   - `actionId` 固定从 `PromptQuery.actionId` 派生；其余字段固定从 `PromptDescriptor` 派生。
   - `ok` 不单独进入 `PromptSnapshotRecord`；`09a` 的 black-box cutover 统一按 `status == Ok` 判定成功，失败路径必须直接比较 `status`。
   - 不允许在 `09a` 临时发明顶层 `origin` 或 `contextRevision` 去驱动 black-box cutover 判断。

6. **runtime rollback 边界现在就写死。**
   - 本 slice 固定分两次落地：
     1. `SingleAuthorityAssembly + replay / contract prove-out`
     2. `PublicSurfaceSwap + LegacyDeletion / ShimShrink`
   - 若第 1 次落地失败，允许回退到旧 runtime 入口，但不得改写新主线合同。
   - 若第 2 次 live smoke 失败，允许整块回退第 2 次变更；禁止保留 mixed runtime。

7. **canonical prove-out targets 由 `09a` 先落地，再执行 prove-out。**
   - 若当前 repo reality 的 `xmake.lua` 还没有 `DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests` 这 6 个 target，`09a` 必须先把它们以同名 canonical definitions 落地。
   - `09a` 不得假定这些 targets 会由 `09b` 再补建；`09b` 只允许复用 `09a` 已落地的同名 target 接 CI。
   - 若 replay batch diff 依赖的 `scripts/dev/dualpad_trace_diff.py` 尚未存在，`09a` 也必须先补齐脚本或等价入口，再开始 prove-out。

## 前置依赖

- `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md`
- `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`
- `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
- replay root `tests/replay/golden/` 下的 `phase0/` 已经可用；若 `phase6_prompt/`、`phase7_ingress/` 已存在，也必须一起参与验证。
- `src/input_v2/runtime/DualPadRuntime.*` 已存在并能独立串起新主线。

## 涉及代码与文档

### 必须改动或缩成 shim 的正式入口

- `src/main.cpp`
- `src/input_v2/runtime/DualPadRuntime.h`
- `src/input_v2/runtime/DualPadRuntime.cpp`
- `src/input_v2/compat/LegacyInputContextCompat.h`
- `src/input_v2/adapters/SkyrimCompatibilitySurface.h`
- `src/input_v2/adapters/SkyrimCompatibilitySurface.cpp`
- `src/input_v2/adapters/ScaleformPromptAdapter.h`
- `src/input_v2/adapters/ScaleformPromptAdapter.cpp`
- `src/input_v2/adapters/PollOutputAdapter.h`
- `src/input_v2/adapters/PollOutputAdapter.cpp`
- `src/input/injection/PadEventSnapshotProcessor.h`
- `src/input/injection/PadEventSnapshotProcessor.cpp`
- `src/input/glyph/ScaleformGlyphBridge.h`
- `src/input/glyph/ScaleformGlyphBridge.cpp`
- `xmake.lua`
- `scripts/dev/dualpad_trace_diff.py`

### 必须删除的 legacy authority

- `src/input/injection/GameplayOwnershipCoordinator.h`
- `src/input/injection/GameplayOwnershipCoordinator.cpp`
- `src/input/InputModalityTracker.h`
- `src/input/InputModalityTracker.cpp`
- `src/input/InputContext.h`
- `src/input/InputContext.cpp`
- `src/input/InputContextNames.h`
- `src/input/InputContextNames.cpp`
- `src/input/MenuContextPolicy.h`
- `src/input/MenuContextPolicy.cpp`
- `src/input/BindingManager.h`
- `src/input/BindingManager.cpp`
- `src/input/mapping/TriggerMapper.h`
- `src/input/mapping/TriggerMapper.cpp`
- `src/input/mapping/TapHoldEvaluator.h`
- `src/input/mapping/TapHoldEvaluator.cpp`
- `src/input/mapping/ComboEvaluator.h`
- `src/input/mapping/ComboEvaluator.cpp`
- `src/input/mapping/BindingResolver.h`
- `src/input/mapping/BindingResolver.cpp`

## 实施步骤

1. **先收口 `DualPadRuntime`，再切 public surface。**
   - 把 `src/input_v2/runtime/DualPadRuntime.*` 冻结成唯一正式 orchestration root。
   - `src/main.cpp` 中旧 singleton 的直接 `Register()` / `Install()` / `Load()` 调用全部改接 `DualPadRuntime` 初始化与 published surface 注册。
   - 在这一阶段，旧 runtime 只允许作为 shadow prove-out 的对照物，不允许继续参与默认执行路径。

2. **完成 `SingleAuthorityAssembly`。**
   - `PadEventSnapshotProcessor` 若仍保留，只允许把 snapshot 打包后单向转发到 `DualPadRuntime::ProcessFrame(...)`。
   - `FinishFramePlanning(...)`、owner / gate / fallback 决策、reverse lookup、legacy prompt 查询全部必须退出 live runtime path。
   - 若这一阶段还需要从旧 authority 回问状态，说明前序 slice 合同未接稳，必须先回前序 slice 返工。

3. **完成 `PublicSurfaceSwap`。**
   - `IsUsingGamepad`
   - `GamepadControlsCursor`
   - `RE::BSPCGamepadDeviceHandler::IsEnabled`
   - `DualPad_GetActionGlyphToken`
   - `DualPad_GetActionGlyph`
   - 上述 5 组黑盒接口全部改读新主线 published state 或 `PromptService` 结果。
   - 禁止“一边走新主线、一边在失败时回头读旧 authority”的混合实现。

4. **按固定顺序执行 `LegacyDeletion / ShimShrink`。**
   - 第 1 批：`GameplayOwnershipCoordinator.*`
   - 第 2 批：`InputModalityTracker.*`
   - 第 3 批：先把仍需保留的 legacy context 标签迁到 `src/input_v2/compat/LegacyInputContextCompat.h`，再删除 `InputContextNames.*`、`MenuContextPolicy.*`、`InputContext.*` 中的 `ContextManager`
   - 第 4 批：`BindingManager.*` 与 `src/input/mapping/*`
   - 第 5 批：收缩 `ScaleformGlyphBridge.*` 与 `PadEventSnapshotProcessor.*`
   - 不允许跳批次，也不允许先把第 4 批保留成 dormant authority 再宣称“后面顺手删”。
   - `LegacyInputContextCompat` 不属于 legacy authority 删除对象；它只允许作为 compatibility type 留存给 `GameplayProjectionFrame`、replay、日志和文档导出使用。

5. **以 replay / black-box contract 完成 runtime 证明。**
   - Phase 0 replay barrier 是第一层护栏。
   - 若 `phase6_prompt/`、`phase7_ingress/` 已落地，也必须跟随新主线一起重跑。
   - prompt black-box prove-out 必须直接消费 `PromptSnapshotRecord` 合同；`PromptDescriptor` 只作为上游 authority，不再作为 `09a` 的直接黑盒比较 schema，也不允许回退到 trigger reverse lookup。

6. **先把 canonical prove-out 入口落到当前 repo reality，再执行命令。**
   - 在开始本页“必须执行的命令”前，必须先修改 `xmake.lua`，把下列 6 个 prove-out targets 以同名 canonical definitions 落地到当前 worktree：
     - `DualPadReplayTests`
     - `DualPadInputV2Tests`
     - `DualPadIngressTests`
     - `DualPadPromptSnapshotTests`
     - `DualPadPropertyTests`
     - `DualPadFuzzRegressionTests`
   - `DualPadReplayHarnessTests` 的 bootstrap 断言与 namespace batch runner 必须在这一步并入 `DualPadReplayTests`，而不是留到 `09b` 或默认 CI 阶段再收口。
   - 若 `scripts/dev/dualpad_trace_diff.py` 尚未存在，或现有入口不能直接消费 `tests/replay/golden/`，也必须在这一步补齐。
   - 只有当 `xmake build <canonical target>` 与 replay diff 命令在当前 repo reality 下都有明确落脚点后，才允许进入 prove-out。

## 验证与观测

### 必须执行的命令

```powershell
xmake build DualPad
xmake build DualPadReplayTests
xmake build DualPadInputV2Tests
xmake build DualPadIngressTests
xmake build DualPadPromptSnapshotTests
xmake build DualPadPropertyTests
xmake build DualPadFuzzRegressionTests
xmake run DualPadReplayTests
xmake run DualPadInputV2Tests
xmake run DualPadIngressTests
xmake run DualPadPromptSnapshotTests
xmake run DualPadPropertyTests
xmake run DualPadFuzzRegressionTests
python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff
```

若 `tests/replay/golden/phase6_prompt/` 或 `tests/replay/golden/phase7_ingress/` 已存在，还必须对每个已存在 namespace 各跑一遍同等强度的 batch diff。

### 必须观测到的成功信号

- 默认运行路径已经只剩 `src/input_v2/` 主线。
- `IsUsingGamepad`、`GamepadControlsCursor`、`RE::BSPCGamepadDeviceHandler::IsEnabled` 的黑盒结果不再依赖旧 authority。
- `DualPad_GetActionGlyphToken` 与 `DualPad_GetActionGlyph` 已只依赖 `PromptProjection / PromptService`。
- `DualPadReplayTests`、`DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests` 都已经直接跑通；不存在只靠 `DualPadReplayHarnessTests` 过门的临时 prove-out。
- 在 `09a` prove-out 开始前，上述 6 个 canonical targets 的定义必须已经落到当前 repo reality 的 `xmake.lua`；若其中任一 target 仍不存在或不可直接 `build/run`，说明 `09a` 前置落地未完成，不得把缺口推给 `09b`。
- replay 没有新增 nondeterministic mismatch。
- live smoke 不再出现 mixed runtime、双执行或暗中 fallback 到旧 authority。

### 必须保留的 explainability

- 当前是否走 `DualPadRuntime`
- 当前 published revision / epoch
- 当前 prompt 查询使用的 `resolutionSource`
- 当前 prompt 查询使用的 `fallback`
- replay mismatch 的 namespace / scenario / surface

## 退出条件

- 仓库内不存在第二套 formal runtime mainline；默认执行路径只剩 `src/input_v2/`。
- `GameplayOwnershipCoordinator.*`、`InputModalityTracker.*`、`MenuContextPolicy.*`、`BindingManager.*` 及相关 legacy mapping authorities 已按固定顺序物理删除。
- `src/input/InputContext.*` 与 `src/input/InputContextNames.*` 已物理删除；若仓库内仍需保留 legacy context 标签，只允许通过 `src/input_v2/compat/LegacyInputContextCompat.h` 提供 compatibility type。
- `PadEventSnapshotProcessor.*` 与 `ScaleformGlyphBridge.*` 若仍保留，均已缩成无 authority shim。
- 所有对外黑盒接口都已经切到新主线。
- `tests/replay/golden/` 仍是唯一 replay 根路径，且验证覆盖 `phase0/` 以及所有已存在的扩展 namespace。
- 不存在 mixed runtime、长期 compile-time 兜底或 dormant fallback authority。

## 交接给下一 slice 的合同

交给 `09b_slice_phase8_governance_closeout_zh.md` 时，下面这些合同必须已经稳定：

- runtime closeout 已完成；`09b` 不再负责决定主线归属。
- replay root 固定为 `tests/replay/golden/`；`09b` 只能接线，不得迁移到第二根路径。
- prompt black-box contract 已固定使用 `PromptSnapshotRecord` 的 11 个字段：
  - `actionId`
  - `status`
  - `resolvedSet`
  - `resolvedContext`
  - `primary`
  - `alternates`
  - `resolutionSource`
  - `fallback`
  - `deviceProfile`
  - `promptScopeRevision`
  - `manifestEpoch`
- `DualPadReplayHarnessTests` 的 Phase 0 bootstrap 断言已经并入 `DualPadReplayTests`；`09b` 只允许继续接 canonical replay gate，不得把 harness-only target 重新拉回默认 CI。
- `DualPadInputV2Tests`、`DualPadIngressTests`、`DualPadPromptSnapshotTests` 已在 `09a` prove-out 中直接执行；`09b` 只能复用同名 canonical targets 接线到默认 CI。
- 6 个 canonical prove-out targets 与 `scripts/dev/dualpad_trace_diff.py` 的运行入口都必须在 `09a` prove-out 前落到当前 repo reality；若仍缺失，必须留在 `09a` 补齐，`09b` 不承担补建这些 prove-out definitions 的责任。
- `LegacyInputContextCompat` 已经替代 `src/input/InputContext.*` 成为唯一允许残留的 compatibility type；`09b` 不得为了脚本或文档便利恢复旧 owning files。
- `09b` 可以为 docgen、reviewed docs 和 CI 重写文本与脚本，但不得为了解围重新引入旧 authority 或 runtime fallback。
