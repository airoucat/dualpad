# DualPad

Skyrim SE 1.5.97 / CommonLibSSE-NG 的 DualSense 输入重构项目。

## 当前状态

当前默认 runtime mainline 已由 `PH8a` 收口到 `src/input_v2/`。对外黑盒面继续通过 Skyrim 兼容层和保留的 legacy-named shim 暴露，但运行时归属不在 `PH8b` 重新裁决。

`PH8b` 当前只负责治理收口：DocGen provenance、`docs/generated/` generated facts、reviewed docs 去重、默认 CI canonical target 接线，以及 builder memory / baseline / graphify close-out 口径一致。

最近完成的 post-closeout `S-DP5-RC20-HOTFIX` 已收口连续输入背压、唯一 runtime owner、不可变 Poll 输出、generation-based pulse、target-bound menu refresh、事务化 hook，以及摇杆与 Journal L2/R2 的 matching 实机 smoke。该工作未新增 runtime phase；native Favorites 的 matching dump 与真实游戏循环仍未闭环，因此当前发布状态为 `GO WITH NATIVE FAVORITES DISABLED`，`enable_native_favorites=false` 保持默认。

`S-DP5-MIXED-INPUT` 继续作为 DP5 post-closeout hardening 落在同一 `input_v2` 主线内。已落地的自动化安全面包括 meaningful gamepad activity、原生 KBM facts、per-channel next-Poll 仲裁、Sprint G/K/M contributor OR、scope 化恢复、prompt/menu/cursor 独立投影和 engine query Original-first。需要动态证据的 current-cycle event mutation、cursor 坐标 side effect、raw reconcile、synthetic suppression、engine/device/menu/transform patch 与 Sprint guard 仍全部关闭；这些 mixed-input gated capability 保持 `NO-GO`，不改变上述 RC20 条件发布状态。

## 事实与叙述边界

Generated facts 只放在 `docs/generated/`，由 `DualPadDocGen` 基于 repo 内 checked-in 输入生成：

- [docs/generated/context_catalog_zh.md](docs/generated/context_catalog_zh.md)
- [docs/generated/action_sets_zh.md](docs/generated/action_sets_zh.md)
- [docs/generated/prompt_matrix_zh.md](docs/generated/prompt_matrix_zh.md)
- [docs/generated/policies_zh.md](docs/generated/policies_zh.md)

本 README、`src/ARCHITECTURE.md`、`docs/DOC_INDEX_zh.md`、`docs/current_input_pipeline_zh.md` 和 `docs/authoritative-baseline/README.md` 只保留入口、架构解释和状态说明，不再手写 context table、action table、prompt matrix 或 policy matrix 的第二份副本。

## 当前主线原则

- runtime 主线归属由 `PH8a` 完成，`PH8b` 不重开 runtime 决策。
- replay root 固定为 `tests/replay/golden/`。
- 默认 CI 直接引用同名 canonical runtime targets，不使用 wrapper target 替代；public-surface support proof 可额外运行，但不得替代 canonical targets。
- 旧 SWF 返回 shape、`FavoritesMenu` workspace、legacy authority 和 replay root 都不在 `PH8b` 范围内恢复或迁移。
- axes/triggers 走 complete-generation latest publication；ordered queue 只保存 edge/boundary/recovery facts。
- runtime mutation 只由 `RuntimeOwnerGuard` 验证的 owner tick 推进；Poll hook 只读取不可变 `PollOutputFrame`。
- connectivity、current-state 与 meaningful activity 是三份不同事实；neutral/unchanged HID report 不续 activity lease，也不抢走 KBM owner。
- next-Poll 由 Look、Move、Combat、TransientDigital 各自仲裁；current-cycle 只允许使用一次性 `PollMaterializationReceipt -> Prepare -> Apply -> Commit`，adapter 未成功时不推进 sensitive ledger。

## 文档入口

- [docs/authoritative-baseline/README.md](docs/authoritative-baseline/README.md)
  - workflow current truth 入口；先看默认阅读顺序、当前 Sprint 和 close-out 规则
- [docs/harness/dualpad-builder.md](docs/harness/dualpad-builder.md)
  - `harness + ce + graphify` 在本仓库的默认执行协议
- [docs/DOC_INDEX_zh.md](docs/DOC_INDEX_zh.md)
  - 当前文档总索引
- [docs/current_input_pipeline_zh.md](docs/current_input_pipeline_zh.md)
  - 当前 input_v2 运行时主链解释
- [src/ARCHITECTURE.md](src/ARCHITECTURE.md)
  - 当前代码模块与主链路总览
- [docs/runtime_concurrency_contract.md](docs/runtime_concurrency_contract.md)
  - RC20 线程所有权、不可变发布、generation pulse 与 hook/UI task 合同
- [docs/runtime_backpressure_contract.md](docs/runtime_backpressure_contract.md)
  - latest state / ordered edges、budget、overflow 与 rate matrix
- [docs/testing/rc20_runtime_validation.md](docs/testing/rc20_runtime_validation.md)
  - 自动化证据、动态门禁和当前发布判定
- [docs/research/skyrim_mixed_input_dynamic_evidence_zh.md](docs/research/skyrim_mixed_input_dynamic_evidence_zh.md)
  - mixed-input I 节动态证据台账与 fail-closed capability 状态

## 验证入口

`PH8b` 默认 CI 脚本入口固定为：

- [scripts/ci/run_phase8_ci.ps1](scripts/ci/run_phase8_ci.ps1)

该脚本直接构建和运行以下 canonical runtime targets：

- `DualPadReplayTests`
- `DualPadInputV2Tests`
- `DualPadIngressTests`
- `DualPadPromptSnapshotTests`
- `DualPadPropertyTests`
- `DualPadFuzzRegressionTests`

该脚本还直接构建和运行 public-surface support proof：

- `DualPadPresentationProjectionTests`
- `DualPadRouteHealthContractTests`
- `DualPadNativeButtonCommitTests`

该脚本还构建和运行 DocGen target：

- `DualPadDocGen`

DocGen 可单独执行：

```powershell
xmake build DualPadDocGen
xmake run DualPadDocGen
```

RC 外层验证入口为：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest
```
