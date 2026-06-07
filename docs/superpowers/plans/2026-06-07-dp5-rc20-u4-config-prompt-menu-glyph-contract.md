# DP5-RC20 U4 Config / Prompt / Menu / Glyph Contract Closure 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 冻结 U4 用户可见 config / prompt / menu / glyph 合同，关闭 generated facts 中的覆盖缺口，并保持 `input_v2` mainline 与旧 SWF token shape 不变。

**架构：** 在内部 prompt descriptor / snapshot candidate 上补齐 glyph/icon contract 字段；旧 `ResolveLegacyGlyphToken()` 仍只返回单 token，旧 descriptor 仅增加诊断字段。U4 文档解释 zero-direct contexts、unknown / ignored menu、FavoritesMenu non-restore、prompt fail-closed 和 icon contract；静态 CI gate 保证这些合同不漂移。

**技术栈：** C++20 input_v2 prompt/action graph、Python CI static gate、Markdown reviewed docs、xmake Phase 8 CI。

---

## 文件结构

- 修改：`src/input_v2/prompt/PromptSnapshotRecord.h`
  - 给 `PromptCandidate` 增加 glyph/icon contract 字段。
- 修改：`src/input_v2/prompt/PromptService.h`
  - 给 `PromptLegacyGlyphDescriptor` 增加 missing icon / debug reason 诊断字段。
- 修改：`src/input_v2/prompt/PromptService.cpp`
  - 从 display binding 构造 deterministic glyph contract。
  - fail-closed descriptor 填充 missing icon behavior 与 debug reason。
- 修改：`tests/input_v2/PromptSnapshotTests.cpp`
  - 覆盖 success、alternate、snapshot、legacy failure 的 contract 字段。
- 创建：`scripts/ci/check_config_prompt_menu_glyph_closure.py`
  - 静态检查 U4 文档、CI 接线、generated facts、prompt contract 字段。
- 修改：`scripts/ci/run_phase8_ci.ps1`
  - 接入 U4 static gate。
- 修改：`scripts/ci/check_reviewed_docs_consistency.py`
  - 要求 Phase 8 CI 保留 U4 gate。
- 创建：`docs/releases/dp5_rc20_u4_config_prompt_menu_glyph_contract_zh.md`
  - 中文 reviewed release contract 文档。
- 修改：`docs/authoritative-baseline/dp5_rc20_contract_zh.md`
  - 写入 U4 close-out 结论。
- 修改：`docs/authoritative-baseline/work-packages/README.md`
  - 更新 U4 状态。
- 修改：`docs/DOC_INDEX_zh.md`
  - 接入 U4 文档。
- 修改：`.dualpad-builder/feature_list.json`
  - 补 U4 acceptance。
- 修改：`.dualpad-builder/sprint_plan.json`
  - 补 U4 verification。
- 修改：`.dualpad-builder/progress.md`
  - 记录开始、实现、验证和边界。

## 任务

- [ ] **任务 1：补 prompt glyph/icon contract 字段**

  在 `PromptCandidate` 增加 `glyphId`、`platformId`、`buttonSemanticName`、`fallbackText`、`assetLookupPath`、`missingIconBehavior`、`debugReason`。在 `PromptService.cpp` 增加 helper：

```cpp
std::string GlyphAssetLookupPath(std::string_view deviceProfile, std::string_view glyphId);
PromptCandidate MakePromptCandidate(const CompiledGraphBinding& binding, const DisplayBindingRecord& display);
```

  预期：成功 resolve 时 contract 字段来自 display binding；failure descriptor 不发明 token。

- [ ] **任务 2：补 focused tests**

  在 `RunPromptServiceSuccessTests()` 断言 primary / alternate / snapshot 中的 contract 字段；在 `RunPromptServiceFailClosedTests()` 断言 legacy failure 的 `missingIconBehavior` 与 `debugReason`。

  运行：`xmake build -y DualPadPromptSnapshotTests && xmake run -y DualPadPromptSnapshotTests`

- [ ] **任务 3：补 U4 static gate**

  创建 `scripts/ci/check_config_prompt_menu_glyph_closure.py`，检查：

  - U4 reviewed doc 存在并含 zero direct binding classification。
  - `unknown_menu_policy=passthrough` 与 ignored menu 行为被记录。
  - `FavoritesMenu` non-restore workspace 决策被记录。
  - glyph/icon contract 七字段被记录。
  - `PromptCandidate` 与 `PromptLegacyGlyphDescriptor` 含所需 contract / diagnostic 字段。
  - Phase 8 CI 调用本脚本。

  运行：`python scripts/ci/check_config_prompt_menu_glyph_closure.py`

- [ ] **任务 4：补 reviewed docs 与 builder memory**

  创建 U4 release contract 文档；更新 baseline、work-packages、DOC index、builder JSON 与 progress。所有文档使用中文，代码符号保持英文。

- [ ] **任务 5：收尾验证、issue、commit/push**

  运行：

```powershell
xmake build -y DualPadPromptSnapshotTests
xmake run -y DualPadPromptSnapshotTests
xmake build -y DualPadManifestCompilerTests
xmake run -y DualPadManifestCompilerTests
python scripts/ci/check_config_prompt_menu_glyph_closure.py
python scripts/ci/check_reviewed_docs_consistency.py
powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1
xmake build -y DualPadReplayHarness
xmake run -y DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay
python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff
python -m json.tool .dualpad-builder/feature_list.json
python -m json.tool .dualpad-builder/sprint_plan.json
python scripts/ci/check_legacy_authority_boundary.py
python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout
git diff --check
```

  更新 GitHub #11 checklist、#13 总控 issue，提交并推送当前分支。
