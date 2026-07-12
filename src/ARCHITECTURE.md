# DualPad Input Architecture

本文是 reviewed narrative。可枚举 generated facts 不在这里维护，统一查看：

- [../docs/generated/context_catalog_zh.md](../docs/generated/context_catalog_zh.md)
- [../docs/generated/action_sets_zh.md](../docs/generated/action_sets_zh.md)
- [../docs/generated/prompt_matrix_zh.md](../docs/generated/prompt_matrix_zh.md)
- [../docs/generated/policies_zh.md](../docs/generated/policies_zh.md)

## 当前默认主线

```text
HidReader -> PadState ----+
Skyrim KBM adapter -------+
Ui/context facts ---------+
                         |
  -> PadEventSnapshotDispatcher / PadEventSnapshotProcessor shim
  -> IngressHub (LatestPadState + OrderedEdgeQueue)
  -> RuntimeOwnerGuard / FrameAssembler
  -> DualPadRuntime (single owner mutation)
      -> InteractionEngine
      -> GameplayProjectionFrame
      -> PollOutputAdapter
      -> GameplayPresentationPublisher
      -> PromptRuntimeOwner
  -> SkyrimCompatibilitySurface / ScaleformPromptAdapter
  -> immutable PollOutputFrame publication
  -> UpstreamGamepadHook (acquire + serialize only)
      -> XInputStateBridge
      -> Skyrim Poll producer
      -> PollMaterializationReceipt (consume-once back to owner callback)
```

`src/input_v2/` 是当前 runtime mainline。`PH8a` 已完成 runtime closeout；`PH8b` 只处理 governance closeout。

## 模块分层

### HID / 协议 / 归一化

- `src/input/HidReader.*`
- `src/input/hid/*`
- `src/input/protocol/*`
- `src/input/state/*`

负责读取 DualSense HID 报文并归一化为 `PadState`。

### Ingress / frame assembly

- `src/input_v2/ingress/*`
- `src/input_v2/presentation/SourceEvidenceCollector.*`

负责将 legacy snapshot、live input facts、source evidence 和 boundary marker 组装成 input-v2 frame。

gamepad connectivity、完整 current-state 与 meaningful activity 分别发布。neutral/unchanged report 只保留连接和物理 current-state，不生成 source activity。KBM raw/current facts 由 callback-local producer 按稳定 `(device,idCode)` 维护；binding index 只在同一 immutable binding snapshot 内临时使用。

连续 axes/triggers/current physical mask 使用 complete-generation latest publication；digital edge、manifest/UI/device boundary、reset 和 overflow marker 使用 bounded ordered queue。normal digital reducer 不读取 future `currentDownMask` 猜造 edge。

`IngressHub` 同时拥有 `inputStateEpoch`、`gamepadSessionId`、ControlMap revision/fingerprint 与 causal cutoff transaction。overflow 可保留完整物理 analog/current-state，但该 recovery latest 必须标记 virtual gameplay ineligible，不能制造 ordered/semantic action。

ordered ingress 的唯一顺序权威是 hub 锁内分配的 `IngressEvent.seq`。producer 采集时间戳只以 max 推进 frame evaluation time，不能制造 `SequenceGap`；领先当前 capture cutoff 的 `LatestSourceEvidence` 必须等到匹配的 device-family marker 被消费后才能发布，配对期间不得让 latest/ordered pad facts 绕过边界形成 `Stable` frame。

### Action graph / interaction

- `src/input_v2/actions/*`

负责 compiled action graph、control samples、interaction state 和 resolved action frame。

`ResolvedActionFrame.values` 是面向 current-state materialization 的稀疏绝对快照：经过 neutral 归一化后仍非零的 axis/trigger 必须在每个 stable frame 中继续出现，即使数值没有变化；归零可以由显式 zero change 或缺省值表达。`ResolvedActionFrame.changes` 才是 phase/edge 增量，未出现新的 `Value` phase 不代表物理轴已经回零。

### Gameplay projection / poll output

- `src/input_v2/gameplay/*`
- `src/input/backend/NativeButtonCommitBackend.*`
- `src/input/backend/PollCommitCoordinator.*`
- `src/input_v2/gameplay/PollOutputFrame.*`

负责在唯一 owner generation 内将 resolved action frame materialize 成完整不可变 virtual XInput frame。`AuthoritativePollState` 仅保留 legacy compatibility，不是 current output authority；Poll reader 不推进 pulse 或 packet。

Look、Move、Combat、TransientDigital 各自维护 next-Poll owner/gate/candidate。current-cycle 不重新 acquire 最新 Poll frame，只消费实际 serialize 成功后发布的一次性 `PollMaterializationReceipt`，并严格执行 `Prepare -> Apply -> Commit`；adapter/audit 失败时 channel、transient、Sprint contributor 和 backend state 全部回放 previous committed snapshot。

Sprint 使用 G/K/M 完整 contributor mask 与 virtual bridge。aggregate mask 非零期间不得出现 inactive gap，最后一位 contributor 退出时只产生一次 release。`SprintHandler` guard 在 I-SPRINT 结果 B 前不存在。

### Presentation / prompt compatibility

- `src/input_v2/presentation/*`
- `src/input_v2/prompt/*`
- `src/input/glyph/ScaleformGlyphBridge.*`

负责 Skyrim compatibility surface、prompt snapshot/publish 和旧 Scaleform API shim。旧 SWF 返回 shape 不在 `PH8b` 修改。

prompt family、menu/navigation owner 与 cursor requested/committed owner 是独立投影。cursor 只经 `Plan -> UI side effect -> exact Ack -> owner commit` 推进；I-CURSOR 前 adapter 不写坐标。engine/device query 默认调用 original，Unknown/Remap/unscoped caller 不允许 override。

### Replay / generated governance

- `src/input_v2/telemetry/*`
- `tools/docgen/DualPadDocGenMain.cpp`
- `scripts/ci/run_phase8_ci.ps1`

负责 replay barrier、DocGen provenance 和默认 CI close-out。

## 关键契约

- runtime 单一主线归属由 `PH8a` 完成。
- `PH8b` 不恢复旧 runtime authority，也不迁移 replay root。
- `PromptSnapshotRecord` 的 generated / CI 事实见 [../docs/generated/prompt_matrix_zh.md](../docs/generated/prompt_matrix_zh.md)。
- canonical CI target 事实见 [../docs/generated/policies_zh.md](../docs/generated/policies_zh.md)。
- 并发/发布合同见 [../docs/runtime_concurrency_contract.md](../docs/runtime_concurrency_contract.md)。
- 背压/恢复合同见 [../docs/runtime_backpressure_contract.md](../docs/runtime_backpressure_contract.md)。
- RC20 验证边界见 [../docs/testing/rc20_runtime_validation.md](../docs/testing/rc20_runtime_validation.md)。
- mixed-input 动态门禁见 [../docs/research/skyrim_mixed_input_dynamic_evidence_zh.md](../docs/research/skyrim_mixed_input_dynamic_evidence_zh.md)。
