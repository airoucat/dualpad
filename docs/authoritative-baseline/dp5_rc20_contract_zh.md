# DP5-RC20 合同

本文是 `DP5 post-closeout hardening & RC readiness` 的当前合同。它只负责 PH0-PH8b 之后的 hardening 与 RC readiness，不新增 runtime phase，不重开 `input_v2` 主线裁决。

## GitHub Issue 结构

- Meta：[#13](https://github.com/airoucat/dualpad/issues/13) `[DP5-RC20][Meta] Post-closeout hardening & RC readiness`
- U0：[#7](https://github.com/airoucat/dualpad/issues/7) Contract preflight and scope lock
- U1：[#8](https://github.com/airoucat/dualpad/issues/8) Runtime determinism hardening
- U2：[#9](https://github.com/airoucat/dualpad/issues/9) Legacy boundary collapse
- U3：[#10](https://github.com/airoucat/dualpad/issues/10) Product integration and release readiness
- U4：[#11](https://github.com/airoucat/dualpad/issues/11) Config / prompt / menu coverage closure
- U5：[#12](https://github.com/airoucat/dualpad/issues/12) Verification / observability / governance closeout

旧 issue 映射：

- #2、#3、#4、#6 迁移到 U2。
- #5 迁移到 U5。
- #2-#6 已标记为 `status:superseded`，并以 `not_planned` 关闭；这是 tracking 结构迁移，不表示旧验收项已经天然完成。

## 执行顺序

1. U0 先完成合同与范围锁定。
2. U1 才能开始 runtime determinism hardening。
3. U2-U4 在 U1 合同稳定后按依赖拆分推进。
4. U5 是 RC readiness closeout，不替代 Phase 8 CI，而是在它外层加 RC gate。

## Runtime Frame Baseline Invariant

同一帧进入 runtime 后，runtime resolve、presentation projection、prompt publish / resolve 必须绑定同一份不可变 baseline：

```text
bundle + graph + context + manifestEpoch + configGeneration
```

U1 的目标是把当前防守型 epoch skew guard 收口成 frame-bound `RuntimeConfigSnapshot` / `FrameRuntimeEnvelope`。在 envelope 绑定后，下游路径不得再为了同一帧重新读取 active epoch、active config 或 active context。

## Degraded Health Semantics

`runtimeHealthDegraded` 只能作为派生 helper。DP5-RC20 的正式健康语义必须能表达原因，例如：

- `GraphUnavailable`
- `ManifestEpochSkew`
- `ContextRevisionSkew`
- `QueueOverflow`
- `SequenceGap`
- `BoundaryMismatch`
- `PromptScopeFrozen`
- `HookInstallFailed`

degraded frame 必须 fail closed：不得输出 dirty action，不得用错误 graph 解析输入，不得生成错误 glyph。

## Prompt Freeze Semantics

Prompt 的目标不是追最新 graph，而是与当前 presentation 因果一致。degraded stable frame 可以继续 commit `SkyrimCompatibilitySurface` 的 public owner / cursor projection，但必须冻结或标记 prompt unavailable，不能推进到不匹配的 prompt scope。

`PromptRuntimeOwner::PublishPresentationState()` 后续必须接收 frame epoch / config generation；不得在发布瞬间重新读取 active epoch 来解释旧 presentation。

## Overflow Compaction Semantics

Ingress overflow 可以丢弃 volatile input backlog，但不能把最新边界事实一起吞掉。U1 / U2 后续实现 typed compaction 时应保留最新：

- `ManifestEpochChanged`
- `UiSnapshot`
- `DeviceFamilyChanged`
- `SourceEvidence`

然后注入 `QueueOverflow` transition marker。overflow 后允许 degraded / freeze / drop volatile input，但不得合成 stale press / release action。

## U1.9 Input Semantic Contracts

`Axis2D`、chord 与 primary path 属于 U1 runtime determinism hardening 的输入语义合同，不新增 runtime phase，也不改变 prompt / glyph public shape。

### Axis2D value domain

- 值域固定为 `[-1.0, 1.0]`，非有限值 fail-closed 到 `0.0`。
- neutral 固定为 `(0.0, 0.0)`；绝对值小于等于 `0.0001` 的分量归零。
- deadzone 仍由 binding modifier 显式声明；`InteractionEngine` 不额外发明 per-action deadzone。
- hysteresis 归属 gameplay owner enter / sustain threshold：例如 look / move / trigger 的 enter threshold 与 sustain threshold。`Axis2D` value 本身保持无状态 absolute value。
- `Axis2D` 由同一 action 下 X / Y 两条 stick axis binding 合并成一个 value snapshot；同一帧最多输出一条该 action 的 `Axis2D` value。
- 语义是 absolute，不是 delta。delta / pointer intent 只能在上游 evidence 或后续 consumer 中解释，不能污染 action value。
- timestamp / coalescing：若 frame 有 `monotonicUs`，coalesced value 与 `Value` phase 使用该 evaluation timestamp；否则使用本帧最新 component sample timestamp。

### Chord timestamp contract

- `firstEdgeUs` 是 chord 参与者中最早的 down edge timestamp。
- `lastEdgeUs` 是 chord 参与者中最晚的 down edge timestamp。
- `evaluationUs` 是当前 frame 的 evaluation timestamp，优先使用 frame `monotonicUs`。
- chord 是 edge-triggered pulse：只有 primary 或 required participant 出现新 edge，且 `lastEdgeUs - firstEdgeUs <= chordWindowUs` 时 fire。
- chord 的 held state 只维持 latch，不重复 pulse；任一参与者 release 后解除 latch。
- overflow / degraded stable frame 必须 invalidate chord latch，且不得输出 dirty chord pulse。下一次干净 frame 只有看到新的 chord edge 才能再次 fire。

### Primary path exclusivity

Primary path 仲裁表固定为内部 gameplay projection 合同：

| Surface | Keyboard / mouse signal | Gamepad signal | Non-gameplay / UI behavior |
| --- | --- | --- | --- |
| look | `mouseLookActive` 优先 | right stick enter / sustain | 非 gameplay 强制 KeyboardMouse |
| move | `keyboardMoveActive` 优先 | left stick enter / sustain | 非 gameplay 强制 KeyboardMouse |
| combat | `keyboardMouseCombatActive` 优先 | trigger enter / sustain | 非 gameplay 强制 KeyboardMouse |
| transient digital | `keyboardMouseDigitalActive` 优先 | resolved gamepad transient digital | 非 gameplay 强制 KeyboardMouse |
| UI / menu presentation | keyboard / mouse primary signal 优先 | gamepad analog primary signal 可 reclaim | 非 gameplay 使用 UI owner |
| menu cursor | 使用 published menu cursor owner | 使用 published menu cursor owner | 不从 gameplay channel 反推 |

仲裁结果必须是单一 primary path decision，再回填 channel owner、gate plan、presentation owner 与 cursor owner；不得继续在各 consumer 里复制 if-else 藤蔓。

## Hook Install Failure Semantics

Hook install 失败必须有显式状态和可诊断原因。不得半安装、不得静默重试。U3 负责把 product integration boundary 上的 failure policy、Skyrim runtime version gate、relocation signature gate 和 release packaging 验证补齐。

## Legacy Shim Authority Boundary

`src/input_v2/` 仍是唯一正式 runtime mainline。legacy-named shim / adapter 只允许作为 black-box compatibility、replay 或 public-surface consumer 存在，不得重新持有 runtime authority。

U2 负责吸收旧 #2-#6 的 follow-up：证明旧 tracker/coordinator/processor 路径不是隐藏 runtime authority，或把仍影响主线的路径迁到明确的 input_v2 fact/frame/envelope 边界。

### U2 legacy issue 迁移复核

| 旧 issue | U2 结论 | 当前归类 |
| --- | --- | --- |
| #2 `GameplayOwnershipCoordinator -> InputModalityTracker` reverse dependency | `GameplayOwnershipCoordinator.*` 与 `InputModalityTracker.*` 均已从 `src/` 删除；live runtime 不存在该反向依赖。 | U2 已关闭为 legacy authority deletion proof |
| #3 `InputModalityTracker` gameplay presentation adapter | `InputModalityTracker.*` 已删除；presentation 由 `SourceEvidenceCollector`、`PresentationProjection`、`SkyrimCompatibilitySurface`、`GameplayPresentationPublisher` 处理。 | U2 已关闭为 presentation authority migration proof |
| #4 duplicate `RefreshMenus` during menu platform handoff | 旧 tracker setter path 已删除；menu refresh 当前由 `SkyrimCompatibilitySurface` 基于 published presentation dirty/epoch 去重。 | U2 已关闭为 old path removed |
| #5 `GameplayKbmFactTracker` ControlMap lookup cleanup | `GameplayKbmFactTracker` 仍在 legacy KBM fact / diagnostic support surface；它不进入 `input_v2` core runtime。若后续要优化 hot-path lookup，归入 U5 profiling / observability，不是 U2 hidden authority blocker。 | 重新归类为 U5 / non-authority cleanup |
| #6 `PadEventSnapshotProcessor::Process()` degraded recovery split | `PadEventSnapshotProcessor` 已退为 ingress bridge；degraded recovery 归属 ingress transition / runtime recovery input。 | U2 已关闭为 processor bridge proof |

U2 的代码证明由 `DualPadIngressTests` 中的 kernel boundary test 与 `scripts/ci/check_legacy_authority_boundary.py` 共同承担：前者证明 `legacySnapshot` 不能覆盖 kernel facts，后者禁止旧 authority token 回流 core runtime 与 Phase8 CI。

## Config / Prompt / Menu Coverage

U4 负责关闭用户可见矩阵缺口：

- zero direct bindings 的 context 必须解释为 inherited、pass-through、unsupported、ignored 或 bug。
- `unknown_menu_policy` 与 ignored menu 行为必须可测试。
- `FavoritesMenu` 仍遵守当前 non-restore workspace 决策；若要恢复页面，先恢复 workspace。
- repeated triggers、ambiguous chords、axis/button collisions 必须有 conflict detection 或明确延后记录。
- prompt unavailable / frozen / degraded 行为必须 deterministic 且可观察。

## Glyph / Icon Contract Freeze

U4 只冻结 glyph/icon contract，不生产完整视觉资产。合同至少包括：

- glyph id
- platform id
- button semantic name
- fallback text
- asset lookup path
- missing icon behavior
- debug reason for missing / unavailable glyph

完整 visual icon artwork production 是非目标，除非单独 promotion 到新的 feature milestone。

## Release Blocker Criteria

以下任一项都属于 release blocker：

- reload、context change 或 degraded state 下输出 wrong glyph / wrong prompt。
- reload、overflow 或 transition frame 后输出 dirty action。
- hook 半安装或 silent retry loop。
- unsupported Skyrim runtime 被当作 supported runtime。
- legacy shim 静默持有 runtime decision authority。
- config / prompt / menu matrix ambiguity 影响用户可见行为。
- 缺少 canonical runtime targets、replay、property / fuzz、generated docs、graphify 或 builder JSON 的 RC 验证记录。

## Non-Goals

- 不新增 runtime phase。
- 不重开 `input_v2` runtime mainline、canonical target 名称、replay root 或旧 SWF 返回 shape。
- 不恢复旧 `FavoritesMenu` workspace、旧 SWF authority、`BindingManager` 或 trigger reverse lookup authority。
- 不在 DP5-RC20 内生产完整 visual icon artwork。
- 不把 DualSense haptics / vibration 纳入本 milestone；除非后续单独 promotion 为 feature milestone。
