# Phase 8B Slice：Governance Closeout / DocGen / Reviewed Docs / CI

> 本文承接 `09a_slice_phase8_runtime_closeout_zh.md`。  
> 本 slice 不再负责 runtime 选路与 legacy authority 删除，只负责把已稳定的主线变成可复现、可文档化、可 CI 守护的默认事实。

## 目标

- 把 generated docs、reviewed docs、测试 target 和默认 CI 全部收口到与 `09a` 一致的单一真相源。
- 冻结 `DualPadDocGen` 的 provenance，禁止机器私有 truth 混入 generated docs。
- 把 replay、input-v2 contract、ingress contract、prompt snapshot、property、fuzz 6 类验证正式接进默认 CI。
## 当前 repo reality 诚实边界

- 本页列出的 `DualPadDocGen`、`scripts/ci/run_phase8_ci.ps1`、`.github/workflows/dualpad-ci.yml` 与 6 个 canonical test targets，都是 `09b` 的 closeout deliverables 或前置输入，不是当前 baseline 已兑现的现状事实。
- 在 `09a -> 09b` handoff gate 满足前，repo 若尚未出现这些脚本、targets 或 generated docs，只能说明 `09a / 09b` 还未真正实施，不能反过来把缺口包装成“bundle 漏打”。
- `09b` 的 breach 判定边界固定为：
  - 若 `09b` 被宣称开始执行，但 `09a` handoff gate 仍未满足，则是 handoff breach
  - 若 `09b` 被宣称完成，但这些 closeout 入口仍未落地，则是 closeout breach
  - 若 bundle 或 reviewed docs 把这些入口描述成“当前 repo 已存在”，则是 authority honesty breach

## 冻结的设计决定

1. **`09b` 只做治理收口，不重开 runtime 决策。**
   - 不允许在本 slice 再讨论是否保留 legacy runtime 开关。
   - 不允许在本 slice 里为了让 CI 过关而把旧 authority 暂时接回来。

2. **replay 资产根路径统一固定为 `tests/replay/golden/`。**
   - `tests/golden/replay/` 从本轮起视为无效路径。
   - `09b` 若发现脚本、target、workflow、文档或测试仍引用旧路径，必须在同一轮中统一改回 `tests/replay/golden/`。
   - 若仓库中曾经临时生成过 `tests/golden/replay/`，必须在本 slice 中清理并停止继续引用。

3. **prompt snapshot / CI 契约固定消费 `Phase 6` 的 `PromptSnapshotRecord`。**
   - snapshot / CI 固定消费 `Phase 6` 发布的 `PromptSnapshotRecord`。
   - `PromptSnapshotRecord` 是 `PromptDescriptor` 的唯一派生投影，字段固定为：
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
   - `ok` 不单独进入 `PromptSnapshotRecord`；默认 CI 必须按 `status == Ok` 推导成功态，并直接覆盖失败状态矩阵。
   - candidate 级 provenance 统一叫 `source`。
   - 顶层不再出现 `origin`。
   - 顶层不再出现 `contextRevision`。
   - 若 CI 或 docgen 需要更多上下文，只允许从上述字段组合推导，不允许另起第二套 prompt truth 结构。

4. **`DualPadDocGen` 的 provenance 现在就写死。**
   - `DualPadDocGen` 只允许读取 repo 内 checked-in 输入，经当前 worktree 的编译/导出流程生成 compiled facts。
   - 允许读取：
     - repo 内 checked-in config
     - repo 内 checked-in legacy import fixture
     - repo 内 checked-in replay / prompt snapshot / property / fuzz 资产
     - 由当前 worktree 编译得到的 `ContextCatalog`、`ActionManifest`、prompt matrix、policy tables
   - 禁止读取：
     - 游戏安装目录
     - 用户部署目录
     - 机器私有 live capture 目录
     - 磁盘 `last-known-good` bundle
     - `activeBundle` 的机器外置缓存副本
   - generated docs 文件头必须至少带出：
     - source config root
     - manifest hash
     - trace schema version
     - generator version / command

5. **reviewed docs 只保留 narrative，不再手写事实表。**
   - `README.md`
   - `src/ARCHITECTURE.md`
   - `docs/DOC_INDEX_zh.md`
   - `docs/current_input_pipeline_zh.md`
   - `docs/authoritative-baseline/README.md`
   - 上述文档只能负责阅读入口、架构解释、状态说明；不得继续手写 context table、action table、prompt matrix、policy matrix 的第二份副本。

6. **默认 CI 只允许一个脚本入口和一个 workflow 入口。**
   - 唯一脚本入口：`scripts/ci/run_phase8_ci.ps1`
   - 唯一 workflow 入口：`.github/workflows/dualpad-ci.yml`
   - 不允许额外分裂出第二个 workflow 去重复执行同一套 closeout 测试。

## 前置依赖

- `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md`
- `docs/plans/dualpad_rearchitecture/09a_slice_phase8_runtime_closeout_zh.md`
- `09a` 的退出条件已经全部满足
- replay root `tests/replay/golden/` 已经稳定存在，且不再有第二套路径
- `PromptDescriptor` 已经按 `Phase 6` 合同稳定发布

## 涉及代码与文档

### 必须新增或修改的工具与 target

- `tests/ReplayTests.cpp`
- `tests/PropertyTests.cpp`
- `tests/FuzzRegressionTests.cpp`
- `tests/PromptSnapshotTests.cpp`
- `tests/replay/golden/`
- `tests/golden/prompt_snapshots/`
- `tests/fuzz_corpus/`
- `tests/property_seeds/`
- `tools/docgen/DualPadDocGenMain.cpp`
- `scripts/dev/generate_dualpad_docs.py`
- `scripts/ci/run_phase8_ci.ps1`
- `.github/workflows/dualpad-ci.yml`
- `xmake.lua`
  - 仅允许校准 `DualPadDocGen` 或 CI 入口引用；6 个 canonical prove-out test targets 必须已经由 `09a` 落地，`09b` 不负责首次创建它们

### 必须更新的 reviewed 文档

- `README.md`
- `src/ARCHITECTURE.md`
- `docs/DOC_INDEX_zh.md`
- `docs/current_input_pipeline_zh.md`
- `docs/authoritative-baseline/README.md`

### 必须生成的 generated 文档

- `docs/generated/context_catalog_zh.md`
- `docs/generated/action_sets_zh.md`
- `docs/generated/prompt_matrix_zh.md`
- `docs/generated/policies_zh.md`

## 实施步骤

1. **先统一 replay 路径和 snapshot 契约，再接 CI。**
   - 全仓搜索并移除 `tests/golden/replay/` 引用。
   - 统一改成 `tests/replay/golden/`。
   - prompt snapshot 的归一化字段统一改成 `PromptSnapshotRecord` 的 11 个字段：
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
   - `PromptSnapshotRecord.status` 必须直接进入 snapshot diff、black-box 比较与 CI 报告；不得再从空 token 或旧 API `failureReason` 二次归类。
   - 若旧脚本仍用 `origin` 或 `contextRevision`，必须在本 slice 一次性清除。

2. **实现 `DualPadDocGen`，并把 provenance 写进产物。**
   - `DualPadDocGen` 只允许从 repo 内 checked-in 输入生成 compiled facts。
   - 不允许读取机器私有 `last-known-good`、live capture 或部署目录。
   - `scripts/dev/generate_dualpad_docs.py` 只负责跑生成器、整理输出、校验产物完整性，不得补做第二套事实推导。

3. **把 reviewed docs 改成 narrative only。**
   - 按固定顺序更新：
     1. `src/ARCHITECTURE.md`
     2. `README.md`
     3. `docs/current_input_pipeline_zh.md`
     4. `docs/DOC_INDEX_zh.md`
     5. `docs/authoritative-baseline/README.md`
   - 删除手写事实表，统一改链到 `docs/generated/*.md`。

4. **把 `09a` 已落地的 6 类 canonical 验证接入治理与 CI。**
   - `target("DualPadReplayTests")`
     - Phase 0 的 `DualPadReplayHarnessTests` 断言与 namespace batch runner 必须已经在 `09a` 收口到这里；进入默认 CI 后不得再单独保留 harness-only gate
   - `target("DualPadInputV2Tests")`
     - 保留为 `Phase 6` 合同 gate，不得静默移出默认 CI
   - `target("DualPadIngressTests")`
     - 保留为 `Phase 7` ingress / resync 合同 gate，不得静默移出默认 CI
   - `target("DualPadPropertyTests")`
   - `target("DualPadFuzzRegressionTests")`
   - `target("DualPadPromptSnapshotTests")`
     - 必须覆盖 `PromptSnapshotRecord.status` 的成功态与失败态，不得只验证成功查询
   - `target("DualPadDocGen")`
   - 上述 6 个 canonical test targets 在进入 `09b` 前必须已经能在当前 repo reality 下单独构建、单独运行；`09b` 只复用同名 targets 接 CI，不得在本阶段临时发明新名字或补建平行 target。
   - 若 `09b` 发现 `xmake.lua` 中缺少这些同名 targets、或命令入口仍不可执行，说明 handoff gate 未满足，必须退回 `09a` 补齐，而不是在治理阶段解围。

5. **把所有 closeout 验证接进统一 CI。**
   - `scripts/ci/run_phase8_ci.ps1` 固定执行顺序：
     1. `xmake build DualPad`
     2. `xmake build DualPadReplayTests`
     3. `xmake build DualPadInputV2Tests`
     4. `xmake build DualPadIngressTests`
     5. `xmake build DualPadPromptSnapshotTests`
     6. `xmake build DualPadPropertyTests`
     7. `xmake build DualPadFuzzRegressionTests`
     8. `xmake build DualPadDocGen`
     9. `xmake run DualPadReplayTests`
     10. `xmake run DualPadInputV2Tests`
     11. `xmake run DualPadIngressTests`
     12. `xmake run DualPadPromptSnapshotTests`
     13. `xmake run DualPadPropertyTests`
     14. `xmake run DualPadFuzzRegressionTests`
     15. `python scripts/dev/generate_dualpad_docs.py`
     16. `git diff --exit-code -- docs/generated`
   - `.github/workflows/dualpad-ci.yml` 只能调用这个脚本。
   - `09b` 只负责把 `09a` 已验证通过的同名 canonical targets 接到这里；不得在 CI 脚本里换成别名、wrapper target 或另一套补丁入口。

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
xmake build DualPadDocGen
xmake run DualPadReplayTests
xmake run DualPadInputV2Tests
xmake run DualPadIngressTests
xmake run DualPadPromptSnapshotTests
xmake run DualPadPropertyTests
xmake run DualPadFuzzRegressionTests
python scripts/dev/generate_dualpad_docs.py
git diff --exit-code -- docs/generated
```

### 必须观测到的成功信号

- 仓库里不再出现 `tests/golden/replay/` 引用。
- `DualPadDocGen` 在不同机器上只要基于同一 worktree 和同一 checked-in 输入，就能生成同一组 `docs/generated/*.md`。
- prompt snapshot 不再依赖 `origin` 或 `contextRevision`。
- `PromptSnapshotRecord.status` 已进入 snapshot diff 与 CI 报告，失败语义可直接验证。
- replay、input-v2 contract、ingress contract、prompt snapshot、property、fuzz 6 类测试已经成为默认 CI gate。
- reviewed docs 已只保留 narrative，不再和 generated docs 争夺 authority。

### 必须保留的 explainability

- docgen 输入根路径
- manifest hash
- trace schema version
- prompt snapshot mismatch 的 `resolutionSource / fallback / deviceProfile`
- replay mismatch 的 namespace / scenario / file

## 退出条件

- `tests/replay/golden/` 是唯一 replay 根路径；`tests/golden/replay/` 不再被任何脚本、文档、workflow 或测试引用。
- `DualPadDocGen` 已按 `DocGen Provenance` 规则运行，并把 provenance 写入 generated docs。
- `docs/generated/context_catalog_zh.md`
- `docs/generated/action_sets_zh.md`
- `docs/generated/prompt_matrix_zh.md`
- `docs/generated/policies_zh.md`
- 上述 4 份 generated docs 已由 `DualPadDocGen` 自动生成，并被默认 CI 校验。
- replay、input-v2 contract、ingress contract、prompt snapshot、property、fuzz 6 类测试都已成为默认 CI gate。
- `README.md`、`src/ARCHITECTURE.md`、`docs/DOC_INDEX_zh.md`、`docs/current_input_pipeline_zh.md`、`docs/authoritative-baseline/README.md` 已去掉重复事实表并改链到 generated docs。
- 若仍需读取机器私有 bundle、`last-known-good` 或部署目录才能生成 docs，本 slice 不算完成。

## 交接给收尾阶段的合同

- runtime 单一真相已经由 `09a` 证明；收尾阶段不再负责运行时主线裁决。
- generated facts 固定在 `docs/generated/*.md`，narrative 固定在 reviewed docs。
- 默认 CI 只认 `scripts/ci/run_phase8_ci.ps1` 与 `.github/workflows/dualpad-ci.yml`。
- 后续若需要新增测试资产，只能挂到既有 canonical roots 之下，不能重开新的 replay / prompt / docgen 真相根目录。
