# DualPad

Skyrim SE 1.5.97 / CommonLibSSE-NG 的 DualSense 输入重构项目。

## 当前状态

当前默认 runtime mainline 已由 `PH8a` 收口到 `src/input_v2/`。对外黑盒面继续通过 Skyrim 兼容层和保留的 legacy-named shim 暴露，但运行时归属不在 `PH8b` 重新裁决。

`PH8b` 当前只负责治理收口：DocGen provenance、`docs/generated/` generated facts、reviewed docs 去重、默认 CI canonical target 接线，以及 builder memory / baseline / graphify close-out 口径一致。

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
- 默认 CI 只接同名 canonical targets，不使用 wrapper target 替代。
- 旧 SWF 返回 shape、`FavoritesMenu` workspace、legacy authority 和 replay root 都不在 `PH8b` 范围内恢复或迁移。

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

## 验证入口

`PH8b` 默认 CI 脚本入口固定为：

- [scripts/ci/run_phase8_ci.ps1](scripts/ci/run_phase8_ci.ps1)

该脚本直接构建和运行以下 canonical targets：

- `DualPadReplayTests`
- `DualPadInputV2Tests`
- `DualPadIngressTests`
- `DualPadPromptSnapshotTests`
- `DualPadPropertyTests`
- `DualPadFuzzRegressionTests`
- `DualPadDocGen`

DocGen 可单独执行：

```powershell
xmake build DualPadDocGen
xmake run DualPadDocGen
```
