# Phase 8 入口：Runtime Closeout 与 Governance Closeout 二拆说明

> 本文件不再直接承担整个 `Phase 8` 的执行合同。  
> `Phase 8` 自本轮起固定拆成两个连续 checkpoint：
> - `09a_slice_phase8_runtime_closeout_zh.md`
> - `09b_slice_phase8_governance_closeout_zh.md`

## 目标

- 把原来的单一 `Phase 8` 文档拆成两段连续执行的 closeout 文档，避免把 runtime cutover、legacy 删除、docgen、reviewed docs 和 CI 接线绑在同一个退出门槛上。
- 固定 `Phase 8` 的共享约束、顺序和 handoff gate，防止 `09a` 与 `09b` 互相重开设计会。
## 当前 repo reality 缺口与 breach 边界

- 在 `09a` 真正开工前，当前 repo 允许尚不存在下列 closeout deliverables：
  - `src/input_v2/`
  - `scripts/dev/dualpad_trace_diff.py`
  - `scripts/ci/run_phase8_ci.ps1`
  - `target("DualPadReplayTests")`
  - `target("DualPadInputV2Tests")`
  - `target("DualPadIngressTests")`
  - `target("DualPadPromptSnapshotTests")`
  - `target("DualPadPropertyTests")`
  - `target("DualPadFuzzRegressionTests")`
  - `target("DualPadDocGen")`
- 上述脚本、targets 与目录是 `09a / 09b` 要落地的 closeout surface，不是当前 baseline 已兑现的 repo reality。
- 审查时若当前 bundle 明确把这些缺口列成“repo 现实仍缺”，应判定为实现前缺口，而不是 bundle 不 faithful。
- 只有在下面任一情况发生时，缺失才构成正式 breach：
  - `09a` 或 `09b` 被宣称已经 done / promoted / 已满足 handoff gate
  - prove-out、默认 CI 或 docgen closeout 被宣称已经接通
  - 文档把这些入口写成“当前 repo reality 已存在的默认 gate”

## 固定执行顺序

1. 先读本文件，确认 `Phase 8` 的分段边界和共享约束。
2. 先执行 `09a_slice_phase8_runtime_closeout_zh.md`。
3. 只有 `09a` 的退出条件全部满足后，才允许进入 `09b_slice_phase8_governance_closeout_zh.md`。
4. `09b` 不得回头重开 `09a` 已冻结的 runtime 决策；如需修改，必须回到 `09a` 文档返工，而不是在 governance closeout 阶段临时修补。

## 两段职责边界

- `09a_slice_phase8_runtime_closeout_zh.md`
  - 只负责 runtime single-authority assembly、public surface swap、legacy authority 删除与 shim shrink。
  - 只要求用 replay / black-box contract / live smoke 证明“默认运行路径已经只有一条主线”。
- `09b_slice_phase8_governance_closeout_zh.md`
  - 只负责 docgen provenance、generated docs、reviewed docs 去重、测试 target 收口与默认 CI 接线。
  - 不再负责决定运行时到底走哪条主线，也不负责决定哪些 legacy authority 应该删除。

## 共享冻结约束

- 不允许重新引入长期 runtime 开关；`Phase 8` 只允许单向 adapter 与 test/debug-only parity helper。
- replay 资产唯一根路径固定为 `tests/replay/golden/`。
  - `phase0/`：第一层基线护栏
  - `phase6_prompt/`：prompt replay extension
  - `phase7_ingress/`：ingress / transition / recovery replay extension
- `PromptDescriptor` 的 snapshot / CI / black-box 合同必须以 `Phase 6` 的 `PromptSnapshotRecord` 为准：
  - `PromptSnapshotRecord` 是 `PromptDescriptor` 的唯一派生投影，不是第二套 prompt runtime truth
  - 字段预算固定为：
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
  - `actionId` 固定从 `PromptQuery.actionId` 派生，其余字段固定从 `PromptDescriptor` 派生
  - `ok` 不单独进入 `PromptSnapshotRecord`；`09a/09b` 的 black-box、snapshot、CI 都必须按 `status == Ok` 推导成功态
  - 禁止再引入顶层 `origin`
  - 禁止再引入顶层 `contextRevision`
- `GameplayProjectionFrame.context` 的 compatibility type 归宿必须与 `Phase 5` 对齐：
  - `09a` 删除 `src/input/InputContext.*` 前，必须先把剩余消费者迁到 `src/input_v2/compat/LegacyInputContextCompat.h`
  - `LegacyInputContextCompat` 只允许承担 replay / black-box / docgen 兼容标签，不得继续承载 runtime authority
- `Phase 8` 的最终测试 target 矩阵现在写死：
  - `DualPadReplayTests`
    - `DualPadReplayHarnessTests` 的断言与 namespace batch runner 必须由 `09a` 落地到 `xmake.lua` 的同名 canonical target 中；进入 `09a` prove-out 后，不再允许单独把 `DualPadReplayHarnessTests` 当默认 gate
  - `DualPadInputV2Tests`
    - 保留为 `Phase 6` 合同 gate，并继续进入默认 CI
  - `DualPadIngressTests`
    - 保留为 `Phase 7` ingress / resync 合同 gate，并继续进入默认 CI
  - `DualPadPromptSnapshotTests`
    - 负责消费 `PromptSnapshotRecord` 的成功 / 失败语义；不得只覆盖成功查询
  - `DualPadPropertyTests`
  - `DualPadFuzzRegressionTests`
  - 这 6 个 canonical targets 的定义、可运行命令与 prove-out 入口必须先由 `09a` 落地到当前 repo reality 的 `xmake.lua`
  - `09a` prove-out 与 `09b` 默认 CI 只能直接运行这 6 个 canonical targets，不得再跑另一套临时 target 名称
  - `09b` 只允许复用这 6 个同名 targets 接默认 CI / docgen / governance；若发现当前 code reality 中 target 缺失或命名漂移，必须退回 `09a` 修正，不能在 `09b` 临时发明新 target
- retired replay root spelling 不是合法根路径。若仓库内任何脚本、文档或 target 仍引用第二 replay 根路径，必须在 `09b` 中统一改回 `tests/replay/golden/`。

## 共享前置依赖

- `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
- `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
- `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
- `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
- `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
- `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
- `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
- `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md`

## 09a -> 09b Handoff Gate

只有同时满足下面这些条件，`09b` 才允许开工：

- 默认 runtime 路径已经只剩 `src/input_v2/` 主线。
- `IsUsingGamepad`、`GamepadControlsCursor`、`RE::BSPCGamepadDeviceHandler::IsEnabled`、`DualPad_GetActionGlyphToken`、`DualPad_GetActionGlyph` 都已经切到新主线。
- legacy authority 已按 `09a` 的固定顺序删除，或缩成无 authority 的 shim。
- replay 验证仍统一走 `tests/replay/golden/`，没有第二套 replay 根路径。
- 6 个 canonical targets 已由 `09a` 写入 `xmake.lua` 并在当前 repo reality 下可直接 `build/run`；`09b` 不再承担 target 定义补建责任。
- `09a` 没有留下 mixed runtime、临时 fallback authority 或长期 compile-time 兜底。

## 退出条件

- `README_zh.md` 和本目录结构已经能把 `Phase 8` 的阅读顺序、执行顺序与共享约束表达清楚。
- 团队不会再把 `Phase 8` 误读成“一个文档里顺手做完 runtime closeout + governance closeout”的大爆炸收尾包。
