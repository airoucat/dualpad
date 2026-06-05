# DP5-RC20 U1 runtime frame envelope 首切片实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让 stable runtime frame、presentation projection 与 prompt publish/resolve 绑定同一份 frame baseline，并把 `runtimeHealthDegraded` 升级为可测试的 reason mask。

**架构：** 在 `DualPadRuntime` 进入 stable frame 时创建 `FrameRuntimeEnvelope`，一次性捕获 bundle、graph、context、manifest epoch 与 config generation。runtime resolve、presentation projection 和 prompt publish 都只消费该 envelope；`PromptRuntimeOwner` 存储 envelope baseline，后续 `Resolve()` 不再重新读取 active graph/config。

**技术栈：** C++23、xmake、Phase8 canonical tests、GitHub Actions。

---

## 文件结构

- 创建：`src/input_v2/gameplay/RuntimeFrameEnvelope.h`
  - 定义 `RuntimeHealthReason`、reason mask helper、`RuntimeConfigSnapshot`、`FrameRuntimeEnvelope` 与 prompt baseline 转换。
- 修改：`src/input_v2/gameplay/DualPadRuntime.h`
  - `DualPadRuntimeResult` 从 bool health 改为 reason mask 派生 helper。
  - `BuildStableRuntimeInput()` / `PublishStablePresentationSurface()` 接收 envelope。
- 修改：`src/input_v2/gameplay/DualPadRuntime.cpp`
  - stable frame 入口绑定 envelope。
  - graph/context/config mismatch 写入 reason mask，fail closed。
  - presentation projection 使用 envelope context。
  - prompt publish 使用 envelope baseline。
- 修改：`src/input_v2/prompt/PromptRuntimeOwner.h/.cpp`
  - 新增显式 baseline publish API。
  - `Resolve()` 使用已发布 baseline，不再读取 active bundle/graph。
  - 保留旧 publish API 作为非 runtime 兼容入口。
- 修改：`tests/input_v2/InputV2Tests.cpp`
  - 增加 runtime context interleaving 测试。
  - 将 health 断言改为 reason mask。
- 修改：`tests/input_v2/PromptSnapshotTests.cpp`
  - 增加 prompt explicit baseline 测试，验证 active graph 变化后 resolve 仍使用已发布 baseline。

## 任务

### 任务 1：写失败测试

- [ ] 在 `PromptSnapshotTests.cpp` 新增测试：
  - 构造 epoch 42 的 bundle/graph baseline。
  - 调用 `PublishPresentationState(JournalPresentation(), baseline)`。
  - 将 active graph 切到 epoch 4242。
  - `Resolve(Menu.Accept, JournalMenu)` 仍返回成功，并报告 manifest epoch 42。

- [ ] 在 `InputV2Tests.cpp` 新增测试：
  - 构造 stable frame 时 context 是 `JournalMenu`。
  - 使用 executor 在 output apply 期间把 active context 改成 gameplay。
  - frame 完成后 `SkyrimCompatibilitySurface` 与 prompt scope 仍使用 frame 绑定的 `Journal` context。

运行：

```powershell
xmake build -y DualPadInputV2Tests
xmake run -y DualPadInputV2Tests
xmake build -y DualPadPromptSnapshotTests
xmake run -y DualPadPromptSnapshotTests
```

预期：新增 API 尚不存在或断言失败。

### 任务 2：实现 envelope 与 health reason

- [ ] 创建 `RuntimeFrameEnvelope.h`。
- [ ] `DualPadRuntime` stable frame 入口调用 `BindRuntimeEnvelope(frame)`。
- [ ] graph unavailable / manifest skew / context revision skew / ingress health 写入 `RuntimeHealthReasonMask`。
- [ ] `DualPadRuntimeResult::RuntimeHealthDegraded()` 从 mask 派生。

运行：

```powershell
xmake build -y DualPadInputV2Tests
xmake run -y DualPadInputV2Tests
```

预期：runtime 测试通过或只剩 prompt baseline 相关失败。

### 任务 3：实现 prompt explicit baseline

- [ ] `PromptRuntimeOwner` 新增 `PublishPresentationState(presentation, baseline)`。
- [ ] `Resolve()` 使用 stored baseline 的 bundle/graph，不再读取 active bundle/graph。
- [ ] 旧 `PublishPresentationState(presentation)` 构造 active baseline 后转调新 API，仅作为兼容入口。

运行：

```powershell
xmake build -y DualPadPromptSnapshotTests
xmake run -y DualPadPromptSnapshotTests
```

预期：prompt 测试通过。

### 任务 4：收尾验证与 PR

- [ ] 运行 Phase8：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1
```

- [ ] 运行 replay diff：

```powershell
python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff
```

- [ ] 运行 graphify：

```powershell
python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout
```

- [ ] 校验 JSON 与 diff：

```powershell
python -m json.tool .dualpad-builder/feature_list.json > $null
python -m json.tool .dualpad-builder/sprint_plan.json > $null
git diff --check
```

- [ ] 更新 `.dualpad-builder/progress.md`。
- [ ] 提交、推送并打开 U1 PR，关联 issue #8。
