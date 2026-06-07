# DP5-RC20 U1.7 Hook Install Result 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 把 Skyrim compatibility hook 安装结果显式化，补 runtime version / relocation signature gate，并在 hook 失败时 fail closed。

**架构：** `HookInstallResult` 留在 `src/input_v2/presentation/SkyrimCompatibilitySurface.*` 的 compat/presentation boundary。`DualPadRuntime` 只读取抽象安装结果，把失败映射成既有 `RuntimeHealthReason::HookInstallFailed` 并阻止 native/prompt/action 输出，不把 Skyrim relocation 细节带入 input_v2 core。

**技术栈：** C++20、CommonLibSSE-NG `REL::Module` / `REL::verify_code` / SKSE trampoline、xmake tests、Phase 8 CI。

---

### 文件结构

- 修改：`src/input_v2/presentation/SkyrimCompatibilitySurface.h`
  - 定义 `HookInstallStatus`、`HookInstallResult`、signature gate helper、debug string 和测试 seam。
- 修改：`src/input_v2/presentation/SkyrimCompatibilitySurface.cpp`
  - 在安装前做 runtime version / signature gate；安装过程记录 success、unsupported runtime、signature mismatch、already installed、failed、partial install。
- 修改：`src/input_v2/gameplay/DualPadRuntime.cpp`
  - 在 envelope 绑定时读取 `SkyrimCompatibilitySurface` 的抽象结果；失败时添加 `HookInstallFailed`，并在 stable frame 上跳过 output executor / prompt publish。
- 修改：`tests/input_v2/PresentationProjectionTests.cpp`
  - 覆盖 install result 状态转换、signature gate、partial install、debug reason。
- 修改：`tests/input_v2/InputV2Tests.cpp`
  - 覆盖 hook failure reason 进入 runtime health，并且 executor 不收到 native/prompt/action 输出。
- 修改：`.dualpad-builder/progress.md`
  - 记录 U1.7 start / closeout / verification。
- 更新：GitHub issue `#8`
  - 勾选 U1.7 checklist 或追加 completion comment。

### 任务 1：结构化 hook install result

- [ ] **步骤 1：编写 presentation focused tests**

在 `PresentationProjectionTests.cpp` 现有 install state block 中新增：

```cpp
const auto success = presentation::detail::MakeHookInstallResult(
    presentation::HookInstallStatus::Success,
    "installed");
Require(success.installed, "success result must be installed");
Require(presentation::IsHookInstallFailure(success) == false, "success is not failure");

const auto mismatch = presentation::detail::MakeHookInstallResult(
    presentation::HookInstallStatus::SignatureMismatch,
    "isUsingGamepad");
Require(!mismatch.installed, "signature mismatch must not be installed");
Require(presentation::IsHookInstallFailure(mismatch), "signature mismatch is fail-closed");
```

- [ ] **步骤 2：实现最小结构**

在 `SkyrimCompatibilitySurface.h` 定义 enum/result 和 helper；`Install()` 返回 `HookInstallResult`，并保留 idempotent `AlreadyInstalled`。

- [ ] **步骤 3：运行 focused test**

运行：`xmake build -y DualPadPresentationProjectionTests && xmake run -y DualPadPresentationProjectionTests`

### 任务 2：runtime / signature gate 与 partial install

- [ ] **步骤 1：补 gate tests**

在 presentation test 中用纯 helper 验证：

```cpp
Require(
    presentation::detail::EvaluateHookInstallGate(true, true).status ==
        presentation::HookInstallStatus::Success,
    "matching runtime and signatures may install");
Require(
    presentation::detail::EvaluateHookInstallGate(false, true).status ==
        presentation::HookInstallStatus::UnsupportedRuntime,
    "unsupported runtime must stop before patching");
Require(
    presentation::detail::EvaluateHookInstallGate(true, false).status ==
        presentation::HookInstallStatus::SignatureMismatch,
    "signature mismatch must stop before patching");
```

- [ ] **步骤 2：实现 runtime gate 和 signature gate**

在 `SkyrimCompatibilitySurface.cpp` 中：

- `REL::Module::get().version() == SKSE::RUNTIME_SSE_1_5_97`
- 两个 call patch sites 用 `REL::verify_code(address, expected-call-pattern)` 校验。
- vfunc patch site 要求 vtable relocation base 非零。
- 任一 patch 已开始后失败记录为 `PartialInstall`；未开始失败记录为 `Failed`。

- [ ] **步骤 3：运行 focused test**

运行：`xmake build -y DualPadPresentationProjectionTests && xmake run -y DualPadPresentationProjectionTests`

### 任务 3：runtime fail-closed 与 debug surface

- [ ] **步骤 1：编写 runtime focused test**

在 `InputV2Tests.cpp` 新增测试：用 `SkyrimCompatibilitySurface::ForceInstallResultForTests()` 设置 `SignatureMismatch`，处理 stable frame 后断言：

- `RuntimeHealthReason::HookInstallFailed`
- executor steps 为空
- prompt scope revision 不变
- `outputApplySucceeded == false`

- [ ] **步骤 2：实现 runtime guard**

在 `DualPadRuntime::BindRuntimeEnvelope()` 添加 `HookInstallFailed` reason；在 `ProcessGameplayFrameWithExecutor()` 如果 reason 包含 `HookInstallFailed`，直接返回 degraded result，不调用 executor，不更新 `_lastProjectionFrame`。

- [ ] **步骤 3：运行 focused tests**

运行：

```powershell
xmake build -y DualPadInputV2Tests
xmake run -y DualPadInputV2Tests
xmake build -y DualPadPresentationProjectionTests
xmake run -y DualPadPresentationProjectionTests
```

### 任务 4：状态同步与 closeout

- [ ] **步骤 1：更新 progress**

向 `.dualpad-builder/progress.md` 追加 U1.7 开始、实现摘要、验证结果。`feature_list.json` / `sprint_plan.json` 保持 DP5 planned / current_sprint null，只在验证命令通过后记录，不把 DP5 变成 runtime phase。

- [ ] **步骤 2：更新 #8 checklist**

用 GitHub CLI 或 connector 更新 issue `#8` 的 U1.7 checklist / comment，说明 hook install result、signature gate、fail-closed、verification。

- [ ] **步骤 3：完整验证**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1
python -m json.tool .dualpad-builder/feature_list.json > $null
python -m json.tool .dualpad-builder/sprint_plan.json > $null
python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff
python scripts/ci/check_reviewed_docs_consistency.py
python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout
git diff --check
```

- [ ] **步骤 4：提交并推送**

提交本 slice，推送当前跟踪远端分支。
