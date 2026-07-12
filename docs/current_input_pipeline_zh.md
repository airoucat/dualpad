# 当前输入主链路

本文是 reviewed narrative，只解释当前 runtime 结构和治理边界；可枚举事实不在本文手写维护。

Generated facts 固定由 `DualPadDocGen` 输出到：

- [generated/context_catalog_zh.md](generated/context_catalog_zh.md)
- [generated/action_sets_zh.md](generated/action_sets_zh.md)
- [generated/prompt_matrix_zh.md](generated/prompt_matrix_zh.md)
- [generated/policies_zh.md](generated/policies_zh.md)

## 一句话版本

`PH8a` 已完成 runtime closeout：默认执行路径以 `src/input_v2/` 为正式 mainline。当前 RC20 hardening 在该主线内固定为 `LatestPadState + OrderedEdgeQueue -> verified runtime owner -> immutable PollOutputFrame`；`PH8b` 不重新决定 runtime 主线归属。

## 主链解释

当前输入链从 HID / `PadState` 进入 legacy-named adapter，再汇入 input-v2 ingress、frame assembly、interaction、gameplay projection、poll output 和 presentation/prompt publish。

其中：

- axes/triggers/current physical mask latest-wins，不按 HID report 排队；
- interaction `values` 保留逐 stable frame 的非 neutral 绝对轴状态，`changes` 只保留实际变化；
- digital edge、boundary、reset/overflow 进入 bounded ordered queue；
- `InputFramePump` 经过 `RuntimeOwnerGuard` 后是唯一 mutation owner；
- owner 一次发布完整 `PollOutputFrame`，任意 Poll reader 只 acquire/serialize；
- pulse 以 owner generation 推进，不以 Poll 次数推进；
- menu refresh 绑定并复验单一 target，不遍历整个 menu stack。

mixed-input hardening 在这条主链内补齐：

- gamepad connection、完整 current-state 与 meaningful activity 分离；neutral/unchanged HID report 不生成 owner takeover。
- Skyrim KBM callback 以 immutable ControlMap snapshot 解析 gameplay facts，并以稳定 `(device,idCode)` 保存 raw/quarantine 身份。
- Look、Move、Combat、TransientDigital 分别仲裁 next-Poll；current-cycle 使用实际 materialized frame 的 consume-once receipt，不读取 later HID publication 猜测本轮 identity。
- Sprint 使用 Gamepad/Keyboard/Mouse 完整 contributor mask 和 virtual bridge；disconnect 只清 gamepad scope，K/M contributor 可继续保持 aggregate held。
- prompt、menu/navigation 与 cursor 独立投影；engine query 默认 original，不由 prompt、connectivity 或任一单通道 owner 全局翻转。

保留的 legacy-named 入口只承担兼容 adapter / shim 职责：

- `src/input/injection/PadEventSnapshotProcessor.*`
- `src/input/glyph/ScaleformGlyphBridge.*`

它们不再是独立 runtime authority，也不负责恢复旧 fallback。

## DP5-RC20 U2 legacy 边界结论

- `PadEventSnapshotProcessor` 只允许作为 compat/replay ingress bridge：`PadEventSnapshot -> IngressHub -> FrameAssembler -> DualPadRuntime`。它不得直接执行 interaction、gameplay projection、prompt publish、owner 仲裁或 degraded recovery 决策。
- `legacySnapshot` 只能作为 compat/replay/debug payload 保留在 ingress window 与 trace surface。`BuildKernelFrame(...)` 只从 `AssembledFactFrame` 的 boundary key、health、monotonic time 和 `controlSamples` 构造 kernel，不读取 `legacySnapshot`。
- `GameplayOwnershipCoordinator.*` 与 `InputModalityTracker.*` 已在 PH8a 物理删除；相关旧 issue 只保留为迁移历史，不再是 live source authority。
- `GameplayKbmFactTracker` 仍是 legacy KBM fact / diagnostic support surface；它不得进入 `src/input_v2/` core runtime 决策，ControlMap hot-path cleanup 不属于 U2 release-blocking authority debt。
- degraded recovery 当前归属 `IngressHub`、`FrameAssembler` transition / health markers 与 `DualPadRuntime` recovery input，不再由 `PadEventSnapshotProcessor::Process(...)` 拥有。
- `scripts/ci/check_legacy_authority_boundary.py` 已接入 Phase8 CI，用 static check 固化上述边界。

## 输出面

当前输出面分为：

- 原生手柄状态：经 input-v2 gameplay projection / poll output 写入 virtual XInput hardware state。
- Poll publication：`PollOutputFrame` 是 current immutable authority；`AuthoritativePollState` 只保留 legacy compatibility。
- Skyrim compatibility surface：提供 `IsUsingGamepad`、cursor owner、remap/menu enable 等黑盒观察面。
- Prompt / Scaleform compatibility：旧 SWF API 继续经 `ScaleformGlyphBridge` 转发到 prompt runtime owner / adapter，不改旧返回 shape。
- Keyboard helper：仍作为 helper backend / simulated keyboard route 使用，不是 Skyrim PC native event 默认主线。

其中 Skyrim compatibility surface 已改为 Original-first gateway：production caller manifest 仍有 26 个 release-relevant unknown caller，I-0/I-1/I-2/I-MENU/I-5 未闭合前不启用 engine/device/menu/transform override。current-cycle event mutation、cursor 坐标写入、raw reconcile 与 synthetic suppression 也分别保持 shadow 或禁用；详见 [research/skyrim_mixed_input_dynamic_evidence_zh.md](research/skyrim_mixed_input_dynamic_evidence_zh.md)。

## PH8b 治理边界

- replay root 固定为 `tests/replay/golden/`。
- canonical targets 名称保持不变。
- generated facts 只能放入 `docs/generated/`。
- reviewed docs 只能引用、解释和路由 generated facts。
- `PH8b` 不改 input-v2 runtime 合同，不恢复 legacy authority，不恢复 `FavoritesMenu` workspace。

## 推荐阅读

- [../src/ARCHITECTURE.md](../src/ARCHITECTURE.md)
- [authoritative-baseline/README.md](authoritative-baseline/README.md)
- [plans/dualpad_rearchitecture/09a_slice_phase8_runtime_closeout_zh.md](plans/dualpad_rearchitecture/09a_slice_phase8_runtime_closeout_zh.md)
- [plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md](plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md)
- [runtime_concurrency_contract.md](runtime_concurrency_contract.md)
- [runtime_backpressure_contract.md](runtime_backpressure_contract.md)
- [testing/rc20_runtime_validation.md](testing/rc20_runtime_validation.md)
