# DP5-RC20 U2 Legacy Boundary Collapse 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:verification-before-completion。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 证明 legacy-named shim / adapter 不再拥有 `input_v2` runtime authority，并收口旧 #2-#6 迁移后的边界债。

**架构：** 不新增 runtime phase，不改变 `input_v2` mainline。通过 focused ingress test 证明 `legacySnapshot` 不进入 kernel facts，通过 CI static check 固化 legacy authority 删除、processor bridge、core runtime token 禁入和 `legacySnapshot` allowlist。

**技术栈：** C++ focused tests、Python static check、PowerShell Phase8 CI、GitHub issue checklist。

---

## 文件职责

- `tests/input_v2/IngressTests.cpp`：新增 focused test，证明 `BuildKernelFrame(...)` 只消费 input_v2 boundary facts / control samples。
- `scripts/ci/check_legacy_authority_boundary.py`：新增 static check，防止 legacy authority token 回流 core runtime。
- `scripts/ci/run_phase8_ci.ps1`：把 static check 接入默认 Phase8 CI。
- `scripts/ci/check_reviewed_docs_consistency.py`：确认 Phase8 CI 不会漏掉 static check。
- `docs/current_input_pipeline_zh.md`：记录 U2 当前边界结论。
- `docs/authoritative-baseline/dp5_rc20_contract_zh.md`：记录旧 #2-#6 迁移复核结论。
- `.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json`、`.dualpad-builder/progress.md`：同步 builder memory。

## 任务

- [x] **步骤 1：写 static check 红灯**
  - 新增 `scripts/ci/check_legacy_authority_boundary.py`。
  - 运行 `python scripts/ci/check_legacy_authority_boundary.py`。
  - 预期：失败，提示 `scripts/ci/run_phase8_ci.ps1` 尚未调用该检查。

- [x] **步骤 2：接入 static check**
  - 修改 `scripts/ci/run_phase8_ci.ps1`，在 reviewed-doc consistency 后运行 `python scripts/ci/check_legacy_authority_boundary.py`。
  - 修改 `scripts/ci/check_reviewed_docs_consistency.py`，要求 Phase8 CI 保留该检查。
  - 运行 `python scripts/ci/check_legacy_authority_boundary.py`。
  - 预期：通过，输出 `legacy authority boundary check passed`。

- [x] **步骤 3：补 focused ingress test**
  - 在 `tests/input_v2/IngressTests.cpp` 新增 `TestLegacySnapshotCannotOverrideKernelFacts()`。
  - 断言 kernel facts 来自 `IngressBoundaryKey`，control samples 来自 `FactFrame.controlSamples`，不从 `legacySnapshot` 重建。
  - 运行 `xmake build -y DualPadIngressTests && xmake run -y DualPadIngressTests`。
  - 预期：通过，输出 `DualPadIngressTests passed`。

- [x] **步骤 4：文档与 issue checklist 收口**
  - 更新 reviewed docs 和 builder memory。
  - 更新 U2 issue #9 checklist，记录旧 #2-#6 复核结论。

- [x] **步骤 5：完整 close-out 验证**
  - 运行 Phase8 CI、replay diff、builder JSON、reviewed/generated docs consistency、graphify rebuild、`git diff --check`。
  - 若 DocGen 更新 `docs/generated/`，纳入本轮 diff 后重跑 Phase8 CI。
