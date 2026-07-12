# KBM ingress shadow boundary 代码审查

- Scope：当前 working tree 的 KBM ingress/runtime read-only telemetry。
- Intent：在所有 dynamic gate 与 production mutation 关闭时，区分 Skyrim event materialization、KBM producer/Hub publish 与 assembled runtime facts 三段边界。
- Mode：interactive，主线程顺序 review（仓库规则禁止并发 subagent）。
- Reviewers：correctness、testing、maintainability、project-standards、agent-native、learnings、performance、reliability、api-contract、adversarial。

## Findings

- 无需要修复的 correctness、reliability 或 authority finding。
- 热路径只执行固定字段 fingerprint 与短临界区；完整字符串格式化仅在首次、状态变化、拒绝、时钟回退或 5 秒健康采样时发生。
- `BuildGameplayPolicyFromFacts` 仍只执行一次；提前到返回前用于同帧只读日志，不改变 policy、ledger、event、receipt 或 projection。
- 日志合同固定携带 `productionMutationEnabled=false` 与 `enginePatchEnabled=false`；wiring test 同时约束 observe/produce/publish/shadow/drain 顺序和禁止的 gated token。

## Coverage

- Focused：`DualPadRouteHealthContractTests`、`test_mixed_input_kbm_shadow_wiring.py`。
- Adjacent：`DualPadIngressTests`、`DualPadInputV2Tests`、`DualPadGameplayProjectionTests`、`DualPadPresentationProjectionTests`，全部 exit 0。
- Canonical：Phase 8 exit 0；closeout contracts、dynamic evidence checker、good trace evaluator 全部 PASS；Graphify `2389 nodes / 5682 edges / 172 communities`。
- Residual risk / testing gap：host tests 会用非单调合成时间，因此 runtime sampler 可产生预期的 `clock_regressed` 诊断行；RC readiness 的 standalone ReplayHarness 因 host 无 `SkyrimSE.exe` module handle 失败并保留未通过；matching Skyrim SE 1.5.97 的短 W/鼠标样本尚未采集，当前只能批准 shadow candidate，不能宣称 KBM gameplay 已修复。

## Verdict

当前 shadow-only instrumentation 可进入 canonical verification 和 clean candidate build；I-0、I-1、I-P、I-KBM 及所有 production capability 继续 NO-GO。
