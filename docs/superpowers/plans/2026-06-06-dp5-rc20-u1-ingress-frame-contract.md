# DP5-RC20 U1 Ingress Frame Contract 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:test-driven-development 逐行为实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 收紧 `FrameAssembler` 的 seq/time 合同，并让 `IngressHub` overflow 使用 typed compaction 保留最新边界事实、丢弃 volatile input。

**架构：** `IngressHub` 在 overflow 时不再只发布裸 `QueueOverflow` marker，而是把最新 `ManifestEpochChanged`、`UiSnapshot`、`DeviceFamilyChanged`、`SourceEvidence` 事实打包进 `QueueOverflowPayload`。`FrameAssembler` 按到达顺序消费事件，不再排序；遇到 seq gap/regression 或 monotonic time regression 时发 transition 并 fail closed；遇到 typed overflow 时先发 `QueueOverflow` transition，再发布不含 control samples / pulse ledger / legacy snapshot 的 baseline stable frame。

**技术栈：** C++17、xmake、现有 `DualPadIngressTests` / `DualPadPropertyTests` / Phase8 CI。

---

## 文件职责

- 修改 `src/input_v2/ingress/IngressMarkers.h`：新增 `QueueOverflowPayload`，挂到 `IngressEvent`。
- 修改 `src/input_v2/ingress/IngressHub.h/.cpp`：在 overflow 时扫描当前 backlog 与 incoming events，保留 latest typed boundary facts，生成单个 overflow event。
- 修改 `src/input_v2/ingress/FrameAssembler.h/.cpp`：移除 Assemble 内排序；新增 seq/time watermark；处理 overflow payload 生成 transition 后 baseline stable frame。
- 修改 `tests/input_v2/IngressTests.cpp`：新增红灯测试覆盖 overflow typed compaction、out-of-order seq 不被排序修正、monotonic time regression fail-closed。
- 修改 `tests/input_v2/PropertyTests.cpp`：扩展 assembled frame invariant，确认 stable frame seq/time range 单调，transition 不 dispatch。
- 修改 `.dualpad-builder/progress.md`：记录切片范围与验证结果。

---

### 任务 1：Typed Overflow Compaction 红灯

- [ ] **步骤 1：在 `tests/input_v2/IngressTests.cpp` 添加测试**

新增 `TestHubOverflowCompactsBoundaryFactsAndDropsVolatileInput()`：

```cpp
ingress::IngressHub hub{ 4 };
Require(hub.PushEvent(Manifest(9)), "manifest marker must enqueue");
Require(hub.PushEvent(Ui(21, 22)), "ui snapshot must enqueue");
Require(hub.PushEvent(DeviceMarker(presentation::DeviceFamily::Gamepad, 7)), "device marker must enqueue");
Require(hub.PushEvent(PadSample(99, true, true, false)), "volatile pad sample must enqueue");
Require(!hub.PushEvent(SourceEvidence(7)), "source evidence should trigger overflow");

const auto drained = hub.Drain();
Require(drained.size() == 1, "overflow compaction emits one marker");
Require(drained[0].kind == ingress::IngressKind::QueueOverflow, "marker kind required");
Require(drained[0].overflow.hasManifest, "manifest retained");
Require(drained[0].overflow.hasUi, "ui retained");
Require(drained[0].overflow.hasDeviceFamily, "device retained");
Require(drained[0].overflow.hasSourceEvidence, "source evidence retained");
Require(drained[0].overflow.manifest.manifestEpoch == 9, "manifest epoch retained");
```

- [ ] **步骤 2：运行红灯**

运行：`xmake build -y DualPadIngressTests && xmake run -y DualPadIngressTests`

预期：编译失败，原因是 `IngressEvent` 还没有 `overflow` payload。

- [ ] **步骤 3：最小实现**

在 `IngressMarkers.h` 新增 payload；在 `IngressHub.cpp` 的 overflow 路径扫描 backlog + incoming events，写入 payload 后清空队列并 push 单个 marker。

- [ ] **步骤 4：运行绿灯**

运行同上，预期 `DualPadIngressTests passed`。

### 任务 2：FrameAssembler Strict Seq / Time 红灯

- [ ] **步骤 1：添加测试**

新增 `TestFrameAssemblerDoesNotSortOutOfOrderEvents()`：

```cpp
auto events = AssignSeq({ Manifest(1), PadSample(1, true, true, false), PadSample(1, false, false, true) });
std::swap(events[1], events[2]);
ingress::FrameAssembler assembler;
const auto frames = assembler.Assemble(events);
Require(FindTransition(frames, ingress::TransitionReason::SequenceGap) != nullptr, "out-of-order seq must fail closed");
```

新增 `TestFrameAssemblerRejectsMonotonicTimeRegression()`：构造 seq 递增但 `monotonicUs` 倒退的 pad event，断言出现 `SequenceGap` transition 且倒退事件不进入最后 stable 的 `pulseLedger`。

- [ ] **步骤 2：运行红灯**

运行：`xmake build -y DualPadIngressTests && xmake run -y DualPadIngressTests`

预期：当前 assembler 会排序或接受时间倒退，测试失败。

- [ ] **步骤 3：最小实现**

`FrameAssembler::Assemble()` 去掉排序，按输入顺序消费；新增 `_lastConsumedSeq` / `_lastMonotonicUs` watermark。seq gap 后发 `SequenceGap` transition 并继续消费当前新事件；seq regression 或 time regression 发 `SequenceGap` transition 并丢弃当前 volatile event。

- [ ] **步骤 4：运行绿灯**

运行同上，预期 `DualPadIngressTests passed`。

### 任务 3：Overflow Payload 到 Baseline Stable

- [ ] **步骤 1：添加测试**

新增 `TestFrameAssemblerOverflowPayloadBuildsBoundaryBaseline()`：把任务 1 drain 出来的 overflow event 交给 assembler，断言第一帧是 `QueueOverflow` transition，最后一帧是 stable，`boundaryKey` 使用 payload 中 manifest/ui/device/source facts，且 `controlSamples` 与 `pulseLedger` 为空。

- [ ] **步骤 2：运行红灯**

运行：`xmake build -y DualPadIngressTests && xmake run -y DualPadIngressTests`

预期：只有 transition，没有 payload baseline stable。

- [ ] **步骤 3：最小实现**

在 `FrameAssembler` 中新增 `ApplyOverflowCompaction()`：`EmitTransition(... QueueOverflow ...)` 后直接根据 payload 更新 `_currentKey` 与 `_latestFacts`，再发布一份无 volatile input 的 stable frame。

- [ ] **步骤 4：运行绿灯**

运行同上，预期 `DualPadIngressTests passed`。

### 任务 4：Property / Closeout

- [ ] **步骤 1：扩展 `PropertyTests.cpp`**

断言 transition frame 不 dispatch；stable frame `firstSeq <= lastSeq`，且有非零时间时 `facts.monotonicUs` 不小于上一份 stable 的时间。

- [ ] **步骤 2：运行 focused 验证**

运行：

```powershell
xmake build -y DualPadIngressTests; xmake run -y DualPadIngressTests
xmake build -y DualPadPropertyTests; xmake run -y DualPadPropertyTests
xmake build -y DualPadReplayTests; xmake run -y DualPadReplayTests
```

- [ ] **步骤 3：完整验证与记录**

运行 Phase8、replay diff、builder JSON lint、graphify rebuild、`git diff --check` / `git diff --cached --check`，并把结果写入 `.dualpad-builder/progress.md`。
