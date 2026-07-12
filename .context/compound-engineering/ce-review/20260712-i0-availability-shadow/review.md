# I-0 availability shadow telemetry 代码审查

- Scope：当前 working tree 的 I-0 availability shadow telemetry 实装。
- Intent：在 compatibility patch 完全关闭时，只读记录 native Poll reachability、完整 virtual XInput current-state、connectivity、delegate readiness、menu/remap 与 session/context，为 A/B availability 裁决提供证据。
- Mode：interactive，主线程顺序 review（仓库规则禁止并发 subagent）。
- Reviewers：correctness、testing、maintainability、project-standards、agent-native、learnings、performance、reliability、api-contract、adversarial。

## Applied Fixes

- P1 safe-auto：fingerprint 不再使用 analog-noisy `packetNumber`；改为 buttons + LS/RS/LT/RT semantic state class，完整原始 state 仍按 5 秒 health interval 记录。
- P1 safe-auto：补齐计划 I.2 必需的 `connected` 与 `delegateReady`，分别来自 IngressHub authority 和 verified CommonLib handler accessor。
- P2 safe-auto：时钟回退改为与 last-observed Poll 比较，避免两个未记录样本之间的回退漏标。

## Coverage

- Focused：`DualPadRouteHealthContractTests`、`DualPadIngressTests`、`test_mixed_input_wp9_wiring.py`。
- Adjacent：`DualPadInputV2Tests`、`DualPadGameplayProjectionTests`、`DualPadPresentationProjectionTests`、主 DLL build。
- Canonical：Phase 8 全部 exit 0；Graphify `2378 nodes / 5670 edges / 172 communities`。
- Residual risk / testing gap：matching Skyrim SE 1.5.97 的 A/B availability live matrix 尚未执行，因此 I-0 仍为 NO-GO，production patch 保持关闭。

## Verdict

当前 shadow-only instrumentation 可进入 canonical verification 和候选 build 实机采样；不得据此启用任何 availability override。
