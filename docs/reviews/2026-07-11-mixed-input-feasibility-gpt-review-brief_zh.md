# 键鼠与手柄混合操作可行性及证据（GPT 审查材料）

本文用于把 DualPad 的既有混合输入设计、当前 `input_v2` 实现、Skyrim SE 1.5.97 的 IDA 证据和尚未闭合的风险整理到同一处，方便交给外部 GPT 做第二轮架构审查。

调查基线：

- 日期：2026-07-11
- 分支：`codex/dp5-rc20-menu-native-hotfix`
- 调查时 HEAD：`ffcba3a03c5440a7b8cb7faea5ee6cc9d33c70ad`
- 支持 runtime：Skyrim SE 1.5.97.0
- 用户实机结论：摇杆和 Journal 扳机翻页已分别修复，但“手柄与键鼠共同作用”仍然功能混乱、实际不可用

## 可直接复制给 GPT 的提示词

```text
你现在作为 C++ / Skyrim SKSE / 输入运行时架构审查者，帮我审查 DualPad 的键鼠与手柄混合操作设计和可行性。

仓库：airoucat/dualpad
分支：codex/dp5-rc20-menu-native-hotfix
调查基线 commit：ffcba3a03c5440a7b8cb7faea5ee6cc9d33c70ad
目标 runtime：Skyrim SE 1.5.97.0

目标不是让 UI 同时显示两套平台，也不是允许键鼠和虚拟手柄同时写同一 gameplay 通道。目标是：
1. 不同 gameplay 通道可以由不同来源同时工作，例如鼠标控制 Look、手柄左摇杆控制 Move。
2. 同一通道发生冲突时有确定性仲裁，例如 mouse-look 活跃时将虚拟右摇杆 Look 归零。
3. Sprint 等持续动作按来源聚合，只要任一真实来源仍按住就保持 held。
4. 菜单 UI / SetPlatform 仍输出单一 presentation owner。
5. glyph 来源与 Skyrim 引擎 IsUsingGamepad 兼容状态不要被不必要地绑成同一个高频切换变量。

已有设计：
- docs/gameplay_input_ownership_investigation_and_plan_zh.md
- docs/gameplay_sustained_digital_and_cursor_handoff_plan_zh.md
- docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md
- docs/current_input_pipeline_zh.md

设计核心是：
- LookOwner / MoveOwner / CombatOwner / DigitalOwner 按通道仲裁。
- transient digital 由 owner/gate 防止双触发。
- sustained digital 不参与 DigitalOwner，改用 GamepadResolved | KeyboardPhysical | MousePhysical 的来源掩码聚合。
- UI/menu presentation 最终仍是单一 owner。

2026-07-11 重新执行的 IDA 静态证据：
- IDA 输入为 SkyrimSE.exe.unpacked.exe，34,586,624 bytes，SHA-256 DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD，image base 0x140000000。
- BSInputDeviceManager::PollInputDevices，VA 0x140C150B0：循环 4 个 device slots，对每个非空设备调用 virtual +0x10 Poll。CommonLib 1.5.97 布局把这 4 个槽定义为 Keyboard、Mouse、Gamepad、FlatVirtualKeyboard。
- BSWin32GamepadDevice::Poll，VA 0x140C1AB40：在 0x140C1AB9D 调 XInputGetState(userIndex, this+0x100)，成功后同步处理 14 个按钮、两扳机和左右摇杆 current-state。
- 菜单平台刷新，VA 0x140ECD970：检查 movie 是否支持 _root.SetPlatform，然后调用一次 _root.SetPlatform，并更新菜单平台状态。
- 菜单光标更新，VA 0x140ED2F90：鼠标路径读取 GetCursorPos；手柄路径从内部 cursorPos 按 gamepad cursor speed 积分。只切 owner 而不做坐标 handoff 会产生跳位风险。

当前代码断点：
1. src/input_v2/gameplay/DualPadRuntime.cpp 构造 GameplayPolicy 时，把 mouseLookActive、keyboardMoveActive、keyboardMouseCombatActive、keyboardMouseDigitalActive、keyboardPhysicalSustainedActive、mousePhysicalSustainedActive 全部硬编码为 false。
2. src/input/InputFramePump.cpp 只把键盘/鼠标事件发布为 presentation source evidence，没有把 gameplay held/move/look/combat/digital facts 送入 input_v2 runtime。
3. src/input/HidReader.cpp 对每份成功解析的 HID report 调 CollectGamepadSourceEvidence；LiveInputFactProducer.cpp 又无条件 RecordGamepadEvidence(true)。因此手柄即使完全空闲，也会持续声明新的 gamepad activity。
4. SourceEvidenceCollector::RecordGamepadEvidence(true) 会清 keyboard/mouse evidence，并刷新约 1.5 秒 lease。持续空闲 HID report 会让该 lease 永不过期。
5. GameplayProjectionFrame.cpp 的按通道仲裁和 gate 已存在；问题是 production policy 永远收不到 KBM facts。
6. sustained source mask 类型已存在，但上述 policy 仍为 false；NativeButtonCommitBackend::SyncExternalHeldContributors 还把 kbmSprintHeld 固定为 false。
7. legacy GameplayKbmFactTracker 仍有 ControlMap 映射逻辑，但 PH8a 删除了调用 ObserveButtonEvent / MarkMouseLookActivity 的 InputModalityTracker。git blame 显示硬编码 false 同样由 commit 261869b Complete PH8a runtime closeout 引入。
8. 当前 live-style 测试只验证 gamepad -> keyboard -> gamepad 等顺序切换，没有覆盖“空闲手柄持续 HID 上报，同时用户实际使用键鼠”的交错场景。

当前建议：
- 不恢复 InputModalityTracker 或 GameplayOwnershipCoordinator authority。
- 在 input_v2 ingress 增加独立的 complete-generation LatestKbmGameplayFacts，不把 gameplay facts 塞进 presentation SourceEvidenceSnapshot。
- InputFramePump 在 owner tick 前从原生 ButtonEvent / MouseMoveEvent 更新 keyboard move、combat、transient digital、sustained contributor 和 mouse-look activity facts。
- HidReader 将“设备已连接/有报文”与“有意义的手柄活动”分开；只有按键 edge、超过 deadzone/enter threshold 的轴或扳机、或明确的触控活动才能刷新 presentation activity。neutral/unchanged HID 不得抢 owner。
- owner tick 一次采样 LatestPadState、LatestKbmGameplayFacts、boundary/context/config，生成同代 GameplayPolicy，再调用现有按通道仲裁。
- Gameplay channel owner、Skyrim engine presentation owner、DualPad prompt device family 保持不同合同；菜单只消费单一 presentation owner。
- 对 Sprint 等 sustained action 使用每 action 的来源集合 OR 聚合，不使用 family-level owner。

请重点审查：
1. IDA 的四设备 Poll 循环与 XInput current-state 证据，是否足以支持“Skyrim 底层允许混合设备输入”的静态可行性判断？还需要追哪些 PlayerControls / handler 动态路径？
2. LatestKbmGameplayFacts 应放 ingress latest publication、runtime config envelope，还是 owner tick 内直接从事件列表构建？怎样保证与 context/boundary 同代？
3. meaningful gamepad activity 应如何定义，才能避免 idle HID 抢 owner，同时不让 held stick 在值不变时丢失 current-state？请区分 activity evidence 与 output current-state。
4. mouse-look + left-stick、WASD + right-stick 等不同通道同时工作时，按通道 gate 是否是正确模型？同通道冲突应采用 KBM 优先、last meaningful source、还是可配置策略？
5. Skyrim IsUsingGamepad / GamepadControlsCursor 是否应保持更稳定的兼容状态，而让 DualPad glyph 单独跟随最近有意义来源？有哪些 Skyrim handler 会依赖这些查询而影响 gameplay？
6. sustained source aggregation 是否应放 GameplayProjectionFrame、PollCommitCoordinator，还是独立 aggregator？如何处理 context change、disconnect、overflow 和丢 release？
7. 请指出该方案中最可能的竞态、stuck key、owner 抖动、菜单回归和测试假阳性，并给出最小可审查的实现切片与测试清单。

约束：
- src/input_v2 保持唯一 runtime mainline。
- legacy tracker/processor 只能做 adapter 或迁移参考，不恢复 authority。
- Poll hook 只读 immutable PollOutputFrame，不推进 runtime。
- 不通过延长 lease、扩大队列或加 sleep 掩盖问题。
- UI/menu SetPlatform 仍是单一 owner；gameplay 才按通道仲裁。
- 不改 Favorites gate，不在本次混入 SWF workspace 或 haptics。

请输出：
- 可行性结论与置信度。
- 对现有三层状态划分的评价：gameplay channel / engine presentation / prompt family。
- 推荐的数据合同与线程所有权。
- 最小实现顺序。
- 必须自动化和实机验证的矩阵。
- 任何需要继续 IDA 或调试器确认的具体函数/断点。
```

## 结论摘要

| 问题 | 结论 | 置信度 |
| --- | --- | --- |
| 仓库是否已有混合输入设计 | 有，而且核心模型已经进入 Phase 5 正式设计 | 高 |
| 设计是否仍可直接照旧实现 | 概念可用，旧组件名和旧挂点已失效，必须按当前 `input_v2` 主线重接 | 高 |
| Skyrim 底层是否允许键鼠与手柄进入同一输入周期 | 静态证据支持：输入管理器逐个 Poll 四类设备，并在之后统一分发事件 | 高 |
| UI 是否也能采用多 owner | 不能；`_root.SetPlatform` 和菜单平台状态仍要求单一结论 | 高 |
| 当前 production 是否已经完成混合输入 | 没有；KBM gameplay facts 被硬编码为 false，空闲 HID 又持续抢 gamepad evidence | 高 |
| 是否需要推翻当前 runtime 架构 | 不需要；现有 `GameplayProjectionFrame`、gate、immutable output 可以保留 | 高 |
| 当前混合输入能力是否可视为可发布 | 不可。对“混合操作”这项能力本身应判 `NO-GO`，直到自动化和实机矩阵通过 | 高 |

最简判断是：**设计方向可行，游戏底层也没有显示出结构性阻碍；当前不可用主要是 PH8a 切换主线时丢失了 KBM facts producer，同时把“收到空闲 HID 报文”误当成“手柄正在活动”。**

## “混合操作”的准确边界

本报告中的混合操作不是“两个来源无条件相加”，而是下面三种不同合同：

1. 不同通道可以并行：
   - 鼠标提供 `Look`
   - 手柄左摇杆提供 `Move`
2. 同一通道只能有一个写入者：
   - 鼠标正在提供 `Look` 时，DualPad 虚拟右摇杆必须归零
   - 键盘正在提供 `Move` 时，DualPad 虚拟左摇杆必须归零
3. 持续动作按来源聚合：
   - `Sprint` 只要手柄、键盘或鼠标任一真实来源仍按住，就继续 held

菜单不采用这套多通道模型。菜单导航、光标和 `_root.SetPlatform` 最终仍需要一份单一 presentation 结论。

## 现有文档设计盘点

### 当前仍有效的正式设计

- [Phase 5 GameplayProjection 计划](../plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md)
  - 冻结了 `LookOwner / MoveOwner / CombatOwner / DigitalOwner`。
  - 冻结了 transient digital gate。
  - 冻结了 sustained digital 的 `GamepadResolved / KeyboardPhysical / MousePhysical` 来源掩码。
  - 冻结了 `GameplayProjectionFrame -> PollOutputAdapter` 的职责边界。
- [当前输入主链路](../current_input_pipeline_zh.md)
  - 确认 `src/input_v2/` 是唯一 runtime mainline。
  - 明确旧 `GameplayOwnershipCoordinator / InputModalityTracker` 已删除。
  - 明确 `GameplayKbmFactTracker` 不得成为 `input_v2` core runtime authority。
- [当前架构](../../src/ARCHITECTURE.md)
  - 已建立 latest state、ordered edge、single owner 和 immutable Poll output。

### 概念仍有效、实现挂点已经过时的历史设计

- [Gameplay 输入所有权调查与方案](../gameplay_input_ownership_investigation_and_plan_zh.md)
  - “UI 单一 owner、gameplay 按通道 owner”的判断仍成立。
  - 文中推荐的 `PadEventSnapshotProcessor / GameplayOwnershipCoordinator` 已不再是当前挂点。
- [持续态数字动作与光标交接计划](../gameplay_sustained_digital_and_cursor_handoff_plan_zh.md)
  - “PulseDigital 与 SustainedDigital 分离”“Sprint 采用来源 OR 聚合”“cursor handoff 需要位置同步”仍成立。
  - 文中对 `InputModalityTracker` 的实现描述已经过时。

因此，不能把旧文档当成“没有设计”，也不能照搬旧组件。正确做法是保留它们的行为合同，把事实生产和仲裁接到当前 `input_v2` owner frame。

## 2026-07-11 重新执行的 IDA 证据

本节不是转述旧文档；以下地址已在本轮通过 IDA MCP 重新反编译。

### 二进制身份

| 项目 | 值 |
| --- | --- |
| IDA 输入 | `SkyrimSE.exe.unpacked.exe` |
| 大小 | 34,586,624 bytes |
| SHA-256 | `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD` |
| IDA image base | `0x140000000` |
| IDA 当前输入 MD5 | `0879EE0900C4A8B53E3D7B1B5CE298C0` |

SHA-256 已在本轮从磁盘重新计算，与 [Skyrim XInput Poll call-site 调查](../research/skyrim_xinput_poll_callsite.md) 一致。

### 输入管理器会轮询四个设备槽

`BSInputDeviceManager::PollInputDevices`：

- RVA：`0xC150B0`
- VA：`0x140C150B0`

反编译结构为：

```text
devices = this + 0x60
repeat 4 times:
    if (*devices != null):
        devices[i]->virtual_Poll()
afterwards:
    process control/input state
    dispatch the combined event source
```

CommonLib 1.5.97 的同一对象布局将四个槽定义为：

1. `Keyboard`
2. `Mouse`
3. `Gamepad`
4. `FlatVirtualKeyboard`

这证明低层设备采集不是“先选择一个平台，再只 Poll 那一个设备”。四个已创建的设备在同一输入轮询里依次获得机会。

### 手柄 gameplay 路径消费 XInput current-state

`BSWin32GamepadDevice::Poll`：

- RVA：`0xC1AB40`
- VA：`0x140C1AB40`
- XInput call-site：`0x140C1AB9D`

该函数先保存 previous state，再执行：

```text
XInputGetState(userIndex, this + 0x100)
```

成功后继续：

- 同步处理 14 个 button 位；
- 归一化两枚 trigger；
- 归一化左右 stick；
- 将 stick activity 提交给后续 producer。

这与 DualPad 当前“发布完整虚拟 XInput current-state，再让 Skyrim 自己产生原生事件”的主线一致。

### 菜单平台仍是单一结论

菜单平台刷新函数：

- VA：`0x140ECD970`

该函数：

1. 查询 gamepad device 的启用/平台状态；
2. 检查 movie 是否支持 `_root.SetPlatform`；
3. 调用一次 `_root.SetPlatform`；
4. 更新菜单内部平台状态位。

因此，不能把 gameplay 的四个通道 owner 原样暴露给菜单。菜单需要折叠成一个稳定、可解释的 presentation owner。

### 光标切换还需要坐标 handoff

菜单光标更新函数：

- VA：`0x140ED2F90`

该函数存在两条路径：

- 鼠标路径：`GetCursorPos -> GetForegroundWindow -> GetWindowInfo`，再写入内部坐标；
- 手柄路径：使用内部保存的 cursor position、输入方向和 gamepad cursor speed 做积分。

这支持旧设计中的判断：只切 `CursorOwner` 而不同步位置，可能出现光标先跳到真实鼠标位置、随后又被手柄缓存位置拉回的问题。

### IDA 证据的边界

已经证明：

- 四类设备会在同一 input manager Poll 中被依次轮询；
- gamepad gameplay 入口消费完整 XInput current-state；
- menu platform 最终是单一 `SetPlatform` 结论；
- mouse/gamepad cursor 使用不同的位置来源。

尚未仅靠静态证据证明：

- 所有 `PlayerControls` handler 在所有状态下都能无条件接受同帧多设备事件；
- `IsUsingGamepad` 的每一个下游查询都只影响 presentation，不影响某些特殊 handler；
- gameplay、paused、loading 各状态下完整的动态 event ordering；
- 特定第三方输入 mod 是否会额外屏蔽某一设备。

因此，结论是“静态可行且与当前架构吻合”，不是“无需实机验证”。

## 当前实现为什么实际不可用

关键源代码位置如下，行号基于调查时 HEAD：

| 事实 | 文件与行号 |
| --- | --- |
| 每份成功解析的 HID report 都采集 gamepad source evidence | `src/input/HidReader.cpp:91-94` |
| gamepad evidence 被无条件记录为 `true` | `src/input_v2/ingress/LiveInputFactProducer.cpp:115-128` |
| gamepad evidence 清 KBM facts 并续 lease | `src/input_v2/presentation/SourceEvidenceCollector.cpp:142-156` |
| InputFramePump 只发布 KBM presentation evidence | `src/input/InputFramePump.cpp:27-68,112-119` |
| production GameplayPolicy 的六个 KBM 字段全为 false | `src/input_v2/gameplay/DualPadRuntime.cpp:391-402` |
| 按通道 owner、engine owner 和 gate 已实现 | `src/input_v2/gameplay/GameplayProjectionFrame.cpp:160-244,287-335` |
| Sprint 外部 KBM contributor 固定为 false | `src/input/backend/NativeButtonCommitBackend.cpp:896-908` |
| live-style 测试只做顺序 takeover | `tests/input_v2/InputV2Tests.cpp:1234-1329` |

### 1. KBM gameplay facts 在 production policy 中全部为 false

[DualPadRuntime.cpp](../../src/input_v2/gameplay/DualPadRuntime.cpp) 构造 `GameplayPolicy` 时，当前写死：

```cpp
.mouseLookActive = false,
.keyboardMoveActive = false,
.keyboardMouseCombatActive = false,
.keyboardMouseDigitalActive = false,
.keyboardPhysicalSustainedActive = false,
.mousePhysicalSustainedActive = false
```

而 [GameplayProjectionFrame.cpp](../../src/input_v2/gameplay/GameplayProjectionFrame.cpp) 已经完整实现：

- mouse-look 对 gamepad look 的优先权；
- keyboard move 对 gamepad move 的优先权；
- KBM combat 对 synthetic triggers 的优先权；
- KBM transient digital 对 gamepad transient digital 的 gate；
- `Look / Move / Combat / Digital` 各自独立的 sticky owner。

所以当前不是“没有仲裁算法”，而是 production 永远不给它真实 KBM 输入。

### 2. InputFramePump 只发布 presentation evidence

[InputFramePump.cpp](../../src/input/InputFramePump.cpp) 会遍历 `ButtonEvent / MouseMoveEvent`，但只调用：

- `PublishKeyboardSourceEvidence`
- `PublishMouseButtonSourceEvidence`
- `PublishMouseMoveSourceEvidence`

这些事实进入的是 presentation/source evidence 路径，没有形成 `keyboardMoveActive / mouseLookActive / combat / digital / sustained` 的 gameplay snapshot。

### 3. 每份空闲 HID 报文都会抢回 gamepad evidence

[HidReader.cpp](../../src/input/HidReader.cpp) 对每个成功解析的 report 调 `CollectGamepadSourceEvidence(...)`。

[LiveInputFactProducer.cpp](../../src/input_v2/ingress/LiveInputFactProducer.cpp) 随后无条件执行：

```cpp
_sourceEvidenceCollector.RecordGamepadEvidence(true, tick, 1'500'000);
```

[SourceEvidenceCollector.cpp](../../src/input_v2/presentation/SourceEvidenceCollector.cpp) 在 `active=true` 时又会：

- 清 keyboard evidence；
- 清 mouse button/move evidence；
- 把 gamepad lease 延长约 1.5 秒；
- 清 pointer signal。

DualSense HID 会持续报告，即使轴、扳机和按钮完全 neutral。于是“设备连接并上报”被误当成“用户刚刚使用了手柄”。只要 HID 持续到达，lease 就不会自然过期。

这是用户所说“键鼠手柄共同作用功能混乱”的一个直接、确定性原因。

### 4. sustained 结构存在，但外部来源没有接入

[GameplayProjectionFrame.cpp](../../src/input_v2/gameplay/GameplayProjectionFrame.cpp) 已能把 `KeyboardPhysical / MousePhysical` 加入 `activeSourceMask`，但 policy 字段仍为 false。

[NativeButtonCommitBackend.cpp](../../src/input/backend/NativeButtonCommitBackend.cpp) 的 `SyncExternalHeldContributors(...)` 还明确写着：

```cpp
constexpr bool kbmSprintHeld = false;
```

这意味着旧文档中“任一来源仍 held 就继续 Sprint”的合同没有在 production 完整闭合。

### 5. PH8a cutover 删除了旧 producer，但留下了新 consumer

`git blame` 显示上述 `GameplayPolicy` 硬编码 false 来自：

- commit `261869b315505c44c1812a9f28818908773926d1`
- `Complete PH8a runtime closeout`

同一提交删除了旧 `InputModalityTracker`。被删除的代码原本调用：

- `GameplayKbmFactTracker::ObserveButtonEvent(...)`
- `GameplayKbmFactTracker::MarkMouseLookActivity()`

当前仓库虽然还保留 `GameplayKbmFactTracker`，但 production 中找不到上述两个写入口。它的读取也因此只能得到空事实。

这说明问题更像一次 cutover 接线遗漏，而不是设计被实机证伪。

### 6. 自动化测试覆盖了错误的时间模型

[InputV2Tests.cpp](../../tests/input_v2/InputV2Tests.cpp) 当前 live-style 测试按以下顺序手动驱动：

```text
gamepad evidence -> drive
keyboard evidence -> drive
gamepad evidence -> drive
mouse evidence -> drive
```

它能证明离散 takeover，但没有模拟真实情况：

```text
idle HID report (gamepad evidence)
keyboard/mouse event
idle HID report (再次 gamepad evidence)
idle HID report
...
```

所以测试通过并不能证明插着手柄时键鼠可稳定使用。

## 可行性审查

### Happy path

现有 `GameplayProjectionFrame` 已能表达：

- `mouse look + gamepad move`
- `gamepad look + keyboard move`
- `keyboard combat + 非冲突 gamepad 通道`
- 每个通道独立 gate

只要 owner tick 获得同代 KBM gameplay facts，这条路径无需推翻。

### Nil / idle path

“设备存在但用户没有输入”必须只表示 connectivity，不得表示 activity。

当前缺少这一层区分。修复后至少要分别保留：

- `gamepadConnected`
- `gamepadCurrentState`
- `meaningfulGamepadActivity`

前两者可以持续，第三者只能由真实 edge 或超过阈值的输入刷新。

### Neutral / unchanged path

neutral 或 unchanged HID report：

- 仍可覆盖 latest current-state generation；
- 不应刷新 presentation activity lease；
- 不应抢走 keyboard/mouse owner；
- held stick 若非 neutral，仍必须逐 stable frame 保留 output current-state。

“activity evidence”与“current-state materialization”必须是两份不同语义，不能为了防 idle 抢 owner，又把持续摇杆值错误丢掉。

### Error / boundary path

以下边界应清理或重建 KBM facts：

- gameplay -> menu / menu -> gameplay；
- device disconnect/reconnect；
- ingress overflow / hard reset；
- ControlMap reload/remap；
- 丢失 release 后的 clean baseline；
- synthetic keyboard suppression window。

KBM held mask 若跨边界残留，会造成 stuck sprint、持续 gate 或永久 KBM owner；直接用短 lease 代替 release/baseline 也不安全。

## 推荐的当前主线落点

```text
BSInputDeviceManager event list
  -> InputFramePump
      -> LatestKbmGameplayFacts publication
         keyboard move held mask
         keyboard/mouse combat held mask
         transient digital activity/edges
         sustained contributor mask
         mouse-look activity timestamp

DualSense HID
  -> LatestPadState publication
  -> MeaningfulGamepadActivity classifier
      connectivity != activity
      neutral/unchanged report does not refresh activity

verified runtime owner tick
  -> acquire context/config + LatestPadState + LatestKbmGameplayFacts
  -> build one coherent GameplayPolicy
  -> ResolveGameplayProjection
      per-channel owners and gates
      sustained source aggregation
  -> immutable PollOutputFrame

presentation/prompt
  -> menu PresentationOwner: single value
  -> engine compatibility owner: conservative, explicitly governed
  -> prompt device family: last meaningful source, not raw HID cadence
```

建议新增 `LatestKbmGameplayFacts`，而不是把字段塞进 `SourceEvidenceSnapshot`。原因是：

- source evidence 是 presentation 决策输入；
- KBM gameplay facts 是 gameplay gating 与 held aggregation 输入；
- 两者时间和恢复合同不同；
- 合并后容易重新制造“UI owner 决定 gameplay 写权限”的旧耦合。

也不建议让 `DualPadRuntime` 直接读取 legacy singleton。owner tick 应消费一次明确、完整、带 generation/context 边界的 snapshot，保持 replay 和测试可重建。

## 最小实现切片

### Slice 1：先建立会失败的真实时间模型测试

- 空闲 HID 500/1000 Hz 与 keyboard/mouse event 交错，KBM owner 不被 neutral report 立即抢回。
- mouse-look 与左摇杆同时存在：Look 只保留 mouse，Move 保留 gamepad。
- keyboard move 与右摇杆同时存在：Move 只保留 keyboard，Look 保留 gamepad。
- production `DualPadRuntime` 不是只测纯函数，必须证明真实 fact producer 能填充 policy。

### Slice 2：新增 input_v2-native KBM facts publication

- 从 `InputFramePump` 的原生 event list 更新 held masks 和 mouse-look activity。
- 使用 gameplay `ControlMap` 映射语义键，不硬编码 WASD、鼠标左右键或 Sprint 键位。
- publication 为 complete-generation、by-value/immutable snapshot。
- 与 context、ControlMap reload、reset 和 clean baseline 建立明确边界。

### Slice 3：拆开 gamepad connectivity 与 meaningful activity

- 按钮 press/release edge 是强 activity。
- stick/trigger 超过 enter threshold 是 activity；回到 sustain threshold 以下后不续 activity。
- unchanged neutral report 不刷新 activity。
- held 非 neutral current-state 继续逐帧进入 output，但不必每个 report 都重新触发 presentation dirty。

### Slice 4：接通 policy、sustained 与 presentation 分层

- `DualPadRuntime` 从 snapshot 填充六个 KBM policy 字段。
- 删除 `kbmSprintHeld=false` 和无事实 singleton read。
- transient 继续按通道 gate；sustained 改为每 action 来源 OR。
- 明确 `engineOwner`、menu presentation owner、prompt device family 的切换规则，避免 glyph 需求驱动 Skyrim hook 高频抖动。

### Slice 5：实机矩阵

自动化通过后仍需用户手动验证：

| 场景 | 预期 |
| --- | --- |
| 手柄连接且完全空闲，只用键鼠 5 分钟 | 不自动抢回手柄 owner；键鼠功能稳定 |
| 鼠标持续看视角 + 左摇杆移动 | 视角不抖，移动连续 |
| 右摇杆看视角 + WASD 移动 | 两个不同通道同时有效 |
| 鼠标与右摇杆同时争夺 Look | 只保留一个 Look writer，切换无跳变 |
| WASD 与左摇杆同时争夺 Move | 只保留一个 Move writer，无速度叠加或停顿 |
| 键鼠攻击 + 非冲突手柄输入 | 战斗通道无双写，其它通道不被全局禁用 |
| 手柄 Sprint 按住后再按键盘 Sprint，依次松开 | 任一来源仍 held 时 Sprint 不断 |
| 键盘 Sprint 按住后再按手柄 Sprint，依次松开 | 同上 |
| gameplay 进入菜单 | gameplay gates 清理；菜单只显示一个平台 |
| 菜单内鼠标/手柄切换 | 图标、导航、光标一致；无坐标跳回 |
| 设备断连、重连、loading、remap | 无 stuck owner、stuck button 或 stale held mask |

建议每帧保留以下低频、可关闭证据：

- runtime generation；
- KBM facts generation 与 held masks；
- meaningful gamepad activity reason；
- Look/Move/Combat/Digital owner；
- analog/digital gate；
- sustained source mask；
- engine presentation owner 与 prompt device family。

## 最终判断

1. 仓库有设计，不需要从零发明。
2. 设计的行为模型与本轮 IDA 证据一致，具备实现可行性。
3. 历史文档中的旧类名和旧挂点已经失效，不能恢复旧 authority。
4. 当前 `input_v2` 消费端大体存在，但 KBM facts producer 和 meaningful gamepad activity 分类没有闭合。
5. 现有自动化测试没有覆盖真实的 idle-HID 交错模型，用户实机结论应优先。
6. 在上述事实链和实机矩阵闭合前，只能说“架构可行”，不能说“混合操作已实现”。
