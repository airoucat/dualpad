# DP5-RC20 U5 Verification / Observability / Governance Closeout 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 U4 merge commit `356837d6b0c51c01671a4f7f273932347d435a91` 之后，为 DP5-RC20 建立 RC readiness outer gate，并关闭 U5 的验证、可观察性和治理记录。

**架构：** Phase8 继续作为 canonical base gate，U5 只在外层聚合 replay diff、builder JSON、graphify、release readiness、U4 gate、artifact manifest 和 hygiene checks。Reviewed docs 记录 QA matrix、performance budget 与 debug surface 覆盖；static guard 防止 U5 gate 漂移。

**技术栈：** PowerShell、Python static checks、xmake、graphify、GitHub CLI、中文 reviewed docs。

---

## 文件结构

- 创建：`scripts/ci/run_rc_readiness.ps1`，RC readiness outer gate。
- 创建：`scripts/ci/check_rc_readiness_closeout.py`，U5 静态治理检查。
- 创建：`docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md`，U5 reviewed closeout 文档。
- 修改：`scripts/ci/check_reviewed_docs_consistency.py`，要求 U5 gate 和文档存在。
- 修改：`scripts/dev/generate_release_artifact_manifest.py`，让 `--expect-clean` 使用 tracked content diff，避免 generated docs 行尾状态假失败。
- 修改：`docs/authoritative-baseline/dp5_rc20_contract_zh.md`，接入 U5 合同。
- 修改：`docs/authoritative-baseline/work-packages/README.md`，记录 U4 merge 与 U5 closeout。
- 修改：`docs/DOC_INDEX_zh.md`，索引 U5 文档。
- 修改：`.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json`、`.dualpad-builder/progress.md`，同步 builder memory。

### 任务 1：建立 RC outer gate

- [x] **步骤 1：创建 `scripts/ci/run_rc_readiness.ps1`**

```powershell
Invoke-Step powershell @("-ExecutionPolicy", "Bypass", "-File", "scripts/ci/run_phase8_ci.ps1")
Invoke-Step xmake @("build", "-y", "DualPadReplayHarness")
Invoke-Step python @("scripts/dev/dualpad_trace_diff.py", "--batch", "tests/replay/golden/phase0", "--actual-root", "build/replay", "--report-root", "build/replay-diff")
Invoke-Step python @("scripts/ci/check_rc_readiness_closeout.py")
Invoke-Step python3 @("scripts/dev/setup_graphify_local.py", "rebuild", "--reason", "manual-closeout")
```

- [x] **步骤 2：创建 `scripts/ci/check_rc_readiness_closeout.py`**

```python
require_tokens(failures, "scripts/ci/run_rc_readiness.ps1", ["scripts/ci/run_phase8_ci.ps1", "DualPadReplayHarness"])
```

### 任务 2：记录 U5 closeout truth

- [x] **步骤 1：创建 U5 reviewed 文档**

```markdown
## Gate hierarchy

`Phase8 是 canonical base gate`。
```

- [x] **步骤 2：接入 baseline 和文档索引**

运行：`python scripts/ci/check_rc_readiness_closeout.py`
预期：新增文档和索引 token 都能被 static guard 找到。

### 任务 3：同步 builder memory 与 GitHub governance

- [x] **步骤 1：更新 `.dualpad-builder/feature_list.json` 和 `.dualpad-builder/sprint_plan.json`**

预期：`DP5` / `S-DP5` 继续是 planned post-closeout backlog，但 acceptance / verification 记录 U5 closeout gate。

- [x] **步骤 2：追加 `.dualpad-builder/progress.md`**

预期：记录 PR #20 merge commit、U5 分支和 U5 start / implementation surface。

- [ ] **步骤 3：验证通过后更新 #12、#13，并同步 #8 closed status**

运行：`gh issue edit` / `gh issue close`
预期：#12 和 #13 记录 U5 gate 证据；#8 不再在 #13 中显示为未完成。

### 任务 4：验证与收口

- [ ] **步骤 1：运行 static / JSON checks**

运行：

```powershell
python scripts/ci/check_rc_readiness_closeout.py
python scripts/ci/check_reviewed_docs_consistency.py
python -m json.tool .dualpad-builder/feature_list.json
python -m json.tool .dualpad-builder/sprint_plan.json
```

预期：全部 exit 0。

- [ ] **步骤 2：运行 Phase8 和 RC readiness gate**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1
powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1
```

预期：全部 exit 0。最终提交后重跑 `run_rc_readiness.ps1 -ExpectCleanManifest`，让 artifact manifest 绑定 final source commit。
