# DP5-RC20 U3 Product Integration and Release Readiness 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:verification-before-completion。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 把 DP5-RC20 从工程收口推进到 RC 可安装、可诊断、可回滚。

**架构：** 不新增 runtime phase，不改变 `input_v2` mainline。本 slice 只收口产品集成边界：Skyrim runtime gate、安装/回滚检查、config fail-closed、设备生命周期、release artifact manifest 与 release notes。

**技术栈：** C++ manifest compiler tests、Python release/readiness scripts、PowerShell Phase8 CI、GitHub issue checklist。

---

## 文件职责

- `tests/input_v2/AtomicConfigReloaderTests.cpp`：补 stale/bad LKG fail-closed 回归。
- `scripts/dev/generate_release_artifact_manifest.py`：从最终 HEAD、构建产物、配置、generated docs 与 reviewed release docs 生成 release artifact manifest。
- `scripts/ci/check_release_readiness.py`：静态校验 U3 产品集成边界没有漂移。
- `scripts/ci/run_phase8_ci.ps1`：接入 U3 release readiness static gate。
- `scripts/ci/check_reviewed_docs_consistency.py`：要求 Phase8 CI 保留 U3 static gate。
- `docs/releases/dp5_rc20_u3_release_notes_zh.md`：记录 RC 安装、诊断、回滚和 non-goals。
- `docs/authoritative-baseline/dp5_rc20_contract_zh.md`、`docs/authoritative-baseline/work-packages/README.md`：接入 U3 当前结论。
- `.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json`、`.dualpad-builder/progress.md`：同步 builder memory。

## 任务

- [x] **步骤 1：基线与分支**
  - 合并 PR #18。
  - 同步本地 `main` 到 merge commit `7c0183cfa64a2ce3918febb0ad6a70ae07ebe306`。
  - 创建 `codex/dp5-rc20-u3-release-readiness`。

- [x] **步骤 2：补 config fail-closed 回归**
  - 在 `AtomicConfigReloaderTests` 增加 stale LKG schema 回归。
  - 断言 bad startup config + stale LKG 不提升 active epoch。

- [x] **步骤 3：生成 release artifact manifest**
  - 新增 `scripts/dev/generate_release_artifact_manifest.py`。
  - manifest 输出到 `build/release/DP5-RC20-U3-release-artifact-manifest.{json,md}`。
  - manifest 记录 source commit、tracked dirty 状态、构建产物、配置、repo-owned SWF、generated docs、reviewed release docs、install checks、device lifecycle checks 与 non-goals。

- [x] **步骤 4：补 U3 static gate 与 release notes**
  - 新增 `scripts/ci/check_release_readiness.py`。
  - 新增 `docs/releases/dp5_rc20_u3_release_notes_zh.md`。
  - 接入 Phase8 CI 与 reviewed-doc consistency。

- [ ] **步骤 5：close-out 验证**
  - 运行 `xmake build/run DualPadManifestCompilerTests`。
  - 运行 `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`。
  - 构建 `DualPadReplayHarness`、生成 replay、执行 diff。
  - 构建 `DualPadDInput8Proxy` 并生成 release artifact manifest。
  - 运行 builder JSON、reviewed/generated docs consistency、legacy authority boundary、release readiness、graphify rebuild、`git diff --check`。

- [ ] **步骤 6：GitHub checklist 与提交**
  - 更新 U3 issue #10 checklist。
  - 更新总控 issue #13 checklist。
  - 提交并推送 U3 分支。
