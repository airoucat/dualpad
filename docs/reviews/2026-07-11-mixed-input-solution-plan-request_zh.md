# DualPad 键鼠与手柄混合操作正式方案制定请求

这份文档不是请 GPT 评价现有方案是否“还行”，而是要求它基于附件中的真实代码、测试、Git 历史结论和 Skyrim 1.5.97 静态证据，制定一份更优秀、能直接交给工程代理执行的正式计划。

使用方法：

1. 将 `DualPad_Mixed_Input_Solution_Plan_Bundle_2026-07-11.zip` 上传给 GPT。
2. 把下面代码块中的完整提示词发送给 GPT。
3. 要求 GPT 先读附件再回答，不要只根据聊天摘要生成通用方案。
4. GPT 输出后，把完整结果交回本仓库做代码事实复核，再决定是否执行。

## 给 GPT 的唯一主提示词

```text
你现在是 DualPad 的 C++ / Skyrim SKSE 输入运行时架构师。请读取我上传的整个 ZIP，基于其中的当前源码、测试、正式设计、历史设计和 IDA 证据，制定一份“更优秀且切实可行”的键鼠 + 手柄混合操作实现计划。

这不是泛泛的架构讨论，也不是请你认可附件里已有的建议。你必须审查附件中的候选方向，必要时推翻它，并给出更好的、能在当前仓库真正实施的方案。

## 一、任务目标

DualPad 当前通过 DualSense HID 生成 virtual XInput state，同时 Skyrim 原生键盘和鼠标仍在工作。我们希望实现：

1. 不同 gameplay 通道可以由不同来源同时工作：
   - mouse look + gamepad move；
   - gamepad look + keyboard move；
   - keyboard/mouse combat + 不冲突的 gamepad 通道。
2. 同一通道不得双写：
   - mouse 与 virtual right stick 同时写 Look 时，必须有确定性仲裁；
   - keyboard 与 virtual left stick 同时写 Move 时，必须有确定性仲裁；
   - KBM combat 与 virtual triggers 同时写 Combat 时，必须有确定性仲裁。
3. transient digital 防止双触发；sustained digital（首先是 Sprint）按真实来源聚合，只要任一来源仍 held 就不得中断。
4. 菜单 `_root.SetPlatform`、导航和 cursor 最终仍采用一份单一且稳定的 presentation 结论。
5. prompt/glyph 可以跟随最近有意义来源，但不得反过来破坏 gameplay、Skyrim 输入变换或菜单状态。
6. 手柄已连接但完全空闲时，持续 HID report 不得抢走 KBM activity/owner。
7. 设备断连、context change、ControlMap reload、overflow、丢 release 和 synthetic keyboard suppression 后不得出现 stuck owner、stuck held 或 stale gate。

用户当前实机结论：摇杆卡顿和 Journal L2/R2 已分别修复；键鼠与手柄共同作用仍功能混乱、实际不可用。

## 二、必须先核实的当前代码事实

不要只引用下面摘要。请打开 ZIP 中对应文件，给出 repo-relative 文件和行号：

1. `src/input_v2/gameplay/DualPadRuntime.cpp` 当前把 6 个 KBM gameplay policy facts 硬编码为 false。
2. `src/input/InputFramePump.cpp` 只发布 keyboard/mouse presentation source evidence，没有形成 input_v2-native gameplay facts snapshot。
3. `src/input/HidReader.cpp -> src/input_v2/ingress/LiveInputFactProducer.cpp` 对每份成功解析的 HID report 无条件记录 gamepad evidence。
4. `SourceEvidenceCollector::RecordGamepadEvidence(true)` 会清 keyboard/mouse evidence 并续 gamepad lease。
5. `GameplayProjectionFrame.cpp` 已有 Look/Move/Combat/Digital 的 per-channel owner 和 gate 逻辑。
6. sustained source mask 已存在，但 production policy 不会提供 keyboard/mouse contributor；`NativeButtonCommitBackend::SyncExternalHeldContributors` 仍固定 `kbmSprintHeld=false`。
7. legacy `GameplayKbmFactTracker` 保留 ControlMap 映射逻辑，但 PH8a 删除了 live producer；它不得重新成为 input_v2 authority。
8. 当前 live-style tests 主要验证顺序 takeover，没有覆盖 idle HID 与 KBM 事件持续交错。
9. `InputFramePump` 是 verified runtime owner tick；Poll hook 只能 acquire/serialize immutable `PollOutputFrame`。

Runtime 代码基线 commit：`93184697c0f021a5979c9a71113bcb2a96308231`。后续若只有 docs-only commit，不改变上述代码基线。

## 三、必须纳入方案的 Skyrim 游戏事实

请完整阅读：

- `docs/research/skyrim_xinput_poll_callsite.md`
- `docs/research/skyrim_mixed_input_mode_queries_zh.md`
- `lib/commonlibsse-ng/include/RE/B/BSInputDeviceManager.h`
- `lib/commonlibsse-ng/include/RE/B/BSIInputDevice.h`
- `lib/commonlibsse-ng/include/RE/I/InputDevices.h`

关键证据：

1. `0x140C150B0` 会在同一 input cycle 遍历 Keyboard、Mouse、Gamepad、FlatVirtualKeyboard 四个设备槽。
2. `0x140C1AB40` 通过 `XInputGetState` 消费完整 gamepad button/trigger/stick current-state。
3. `0x140ECD970` 最终只向菜单发布一次 `_root.SetPlatform`。
4. `0x140ED2F90` 的 mouse cursor 与 gamepad cursor 使用不同的位置来源，owner handoff 可能需要坐标同步。
5. 特别重要：`0x140C15240` 是 `devices[kGamepad]->IsEnabled()` 查询，并有 26 个 direct code xrefs；它不是纯 glyph/UI 字段。
6. `0x140705AE0` 会根据该全局 gamepad-enabled 查询选择两套完全不同的二维输入变换：gamepad 分支包含归一化、deadzone、response curve、指数和 acceleration；KBM 分支使用另一套时间步/灵敏度缩放。该函数同时被 gameplay/camera-like 路径和菜单路径调用。

因此，你不能未经论证就建议：

- Skyrim engine mode 永远固定为 Gamepad；
- Skyrim engine mode 永远固定为 KeyboardMouse；
- 任一 KBM 通道活动都立刻翻转全局 engine mode；
- `IsUsingGamepad / IsGamepadEnabled` 只影响图标；
- 只做 prompt family 分离就能解决混合输入。

如果现有 IDA 证据不足以决定 engine mode，请把追加 IDA / live debugger 工作作为实施前置 gate，写明地址、caller、断点条件、要采集的参数和退出标准。

## 四、不可违反的仓库约束

1. `src/input_v2/` 保持唯一正式 runtime mainline。
2. 不恢复 `InputModalityTracker`、`GameplayOwnershipCoordinator`、legacy processor 或 singleton tracker authority。
3. legacy 代码只能作为迁移参考、compat adapter 或 test seam。
4. `InputFramePump / RuntimeOwnerGuard` 保持唯一 runtime mutation owner；HID 和其它线程只发布 facts/state。
5. Poll hook 只能读取同一份 immutable `PollOutputFrame`，不得推进 runtime、pulse、context 或 presentation。
6. current-state、connectivity、meaningful activity 必须是不同语义；不得为解决 idle HID 抢 owner 而丢失 held analog current-state。
7. gameplay per-channel ownership、Skyrim engine mode、menu presentation owner、cursor owner、prompt device family 不得未经论证重新合并成一个布尔值。
8. 不能用更长 lease、更大 queue、sleep、硬编码 WASD/鼠标键或禁用一类输入来掩盖问题。
9. 不能修改 Favorites native gate、SWF workspace、haptics、图标视觉资源或无关绑定语义。
10. 所有代码切片必须先有能稳定失败的测试，再做最小实现，再执行 canonical 验证。

## 五、你必须比较而不是直接接受的候选架构

至少比较以下方向，也可以提出更好的第五种：

1. engine mode 在 gameplay 跟随 `LookOwner`，menu 中跟随 presentation owner；
2. engine mode 跟随 last meaningful primary source，但 gameplay 输出仍按通道 gate；
3. 对 `0x140705AE0` 或更上游输入变换做 source-aware 分流，减少对全局 mode 的依赖；
4. 保留 Skyrim 原生 engine mode，只让 DualPad 管 virtual XInput gate、menu owner 和 prompt family。

每个方案必须说明：

- 是否符合已证明的游戏行为；
- 需要哪些新增 hook / patch；
- 版本耦合与崩溃风险；
- mouse-look、right-stick、WASD、combat、menu、cursor 的行为；
- 线程与 publication 影响；
- rollback 难度；
- 自动化和实机可验证性。

最终选择一个推荐方案，并解释为什么它优于附件中的初步 `LatestKbmGameplayFacts + meaningful gamepad activity + per-channel gate` 建议。若该建议本身就是最佳方案，也必须补齐 engine mode 的证据和治理细节，不能只是复述。

## 六、输出必须是一份可直接执行的正式计划

请只输出一份完整 Markdown 计划，不要先问我澄清问题，不要开始实现代码。标题建议：

`# DualPad 键鼠与手柄混合输入正式实现计划`

计划必须依次包含以下内容：

### A. 证据审计

用表格列出：

- 事实；
- 来源文件/IDA 地址；
- 已证明 / 高概率推导 / 未验证；
- 对方案的约束；
- 若未验证，如何验证。

发现附件摘要与代码冲突时，以代码和原始 IDA 证据为准，并明确指出冲突。

### B. 目标行为合同

为以下场景给出确定结果：

- mouse look + gamepad move；
- gamepad look + keyboard move；
- mouse + right stick 同通道争夺；
- keyboard + left stick 同通道争夺；
- KBM combat + LT/RT；
- transient digital 双来源；
- Sprint 双来源交接；
- idle connected controller + KBM；
- menu entry、menu 内切换和 cursor handoff；
- disconnect、overflow、context boundary、ControlMap reload、丢 release。

不能使用“合理处理”“适当切换”等模糊措辞。

### C. 方案比较与正式裁决

比较至少 4 个候选方案，给出优缺点、风险、证据要求和选择结果。特别裁决：

- Skyrim engine mode 到底由什么驱动；
- engine mode 是否需要新 hook；
- prompt family 是否与 engine mode 分离；
- menu presentation 与 cursor 是否继续共用 owner；
- per-channel owner 的优先级、enter/sustain threshold、hysteresis 和 release 规则。

### D. 精确数据模型

给出建议的 C++ 类型和关键函数签名，不要只写概念名。至少覆盖：

- KBM gameplay current facts；
- KBM ordered edge / held mask 的边界；
- gamepad connectivity、current-state、meaningful activity；
- coherent generation / context revision；
- per-channel arbitration input/output；
- sustained action contributor state；
- engine mode、menu presentation owner、cursor owner、prompt family；
- reset/recovery reason。

说明哪些字段 latest-wins，哪些必须 ordered，哪些只在 runtime owner 内可变。

### E. 线程所有权和数据流

给出 writer/reader 表和一张文本数据流图。必须覆盖：

- HID worker；
- Skyrim input event producer / InputFramePump；
- IngressHub / latest publications；
- FrameAssembler；
- DualPadRuntime；
- GameplayProjection；
- PresentationProjection；
- PollOutputFrame；
- arbitrary Poll readers；
- UI task / cursor handoff。

解释如何避免 generation 撕裂、stale held、事件丢失和 producer clock-domain 误排序。

### F. 文件结构与改动清单

在任务分解前，先列出：

- 要创建的文件；
- 要修改的现有文件及精确职责；
- 明确不应修改的文件；
- 要删除或降级的 legacy 读取口。

必须使用附件中的实际路径，不要发明与仓库风格不一致的目录。

### G. TDD 实施任务

把计划拆成小而可审查、可独立 commit、可回退的任务。每个任务必须包含：

1. 精确文件路径和行号/函数；
2. 先新增的失败测试，写出 fixture、输入序列和关键断言；
3. 运行命令；
4. 预期红灯原因；
5. 最小实现的类型/函数/伪代码；
6. 运行 focused 测试确认绿灯；
7. 运行相邻回归测试；
8. commit 边界和建议 commit message；
9. rollback 条件。

至少包含这些红灯：

- 500/1000 Hz neutral HID 与 KBM 交错时，neutral report 不抢 activity；
- non-neutral held stick 在 unchanged frame 仍保留 current-state；
- mouse look + gamepad move 跨通道并行；
- gamepad look + keyboard move 跨通道并行；
- 同 Look/Move/Combat 通道只有一个 writer；
- Sprint gamepad -> keyboard -> 松 gamepad -> 松 keyboard；
- Sprint keyboard -> gamepad -> 松 keyboard -> 松 gamepad；
- context/reset/disconnect 清理 held masks；
- engine mode 的选定策略在 gameplay 与 menu caller 上不抖动；
- prompt family 改变不错误改变 gameplay gate；
- synthetic keyboard event 不被误认成物理 KBM takeover。

### H. 验证命令和退出标准

按实际仓库 target 给出 focused 与 canonical 命令，至少考虑：

- `xmake run -y DualPadIngressTests`
- `xmake run -y DualPadInputV2Tests`
- `xmake run -y DualPadGameplayProjectionTests`
- `xmake run -y DualPadPresentationProjectionTests`
- `xmake run -y DualPadNativeButtonCommitTests`
- `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1`
- `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1`
- 修改代码后的 Graphify close-out。

每项写明它证明什么、不证明什么。编译通过不得被描述成实机混合输入已修复。

### I. IDA / 调试器前置任务

若 engine mode 策略仍缺证据，给出精确调查计划：

- 地址；
- caller；
- 条件断点；
- 记录寄存器/参数/事件 device/source；
- A/B 输入动作；
- 可以解除哪个设计阻塞；
- 什么时候停止继续逆向。

### J. 实机验收矩阵

给出最短 smoke、完整矩阵和 soak 三层测试。每项包含：

- 操作步骤；
- 预期可观察结果；
- 必须记录的低频日志字段；
- pass/fail 标准；
- 失败时能定位到哪个合同。

人工测试应尽量简单，不要求用户读复杂日志；日志由 evaluator 脚本判定。

### K. 迁移、回退和发布门禁

说明：

- 如何从当前 hardcoded false / idle HID evidence 平滑迁移；
- 是否需要 shadow compare；
- 每个 commit 的回退边界；
- 如何保持 native Favorites gate 不变；
- 何时只能标记 mixed-input `NO-GO`；
- 需要哪些自动化 + 实机证据才能解除门禁。

### L. 自检

最后逐项检查：

- 所有需求都有任务；
- 没有 `TODO / TBD / 适当处理 / 后续补充`；
- 类型名和函数签名前后一致；
- 每个代码任务都有红灯、绿灯和 commit；
- 没有恢复 legacy authority；
- 没有把 engine mode 当成未经证明的纯 presentation 字段；
- 没有把 current-state 与 activity evidence 混为一谈；
- 没有把 host tests 描述成实机通过。

## 七、质量要求

- 中文输出。
- 使用 repo-relative 路径。
- 方案应优先复用当前 `input_v2` 结构，除非证据证明需要更深的 hook。
- 追求边界清晰、可测试和可回退，不追求“大重写”。
- 不要只给 phase 名称；必须给数据结构、函数签名、测试序列、命令、预期结果和 commit 边界。
- 不确定就明确标注，并转化为有退出标准的证据任务。
- 不要实施代码，只输出计划。
```

## 附件推荐阅读顺序

GPT 应按以下顺序阅读 ZIP：

1. `00_请先阅读_混合输入方案请求.md`
2. `01_现状与可行性证据.md`
3. `02_Skyrim_IDA_混合输入证据.md`
4. `repository/docs/current_input_pipeline_zh.md`
5. `repository/src/ARCHITECTURE.md`
6. `repository/docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
7. `repository/src/input_v2/gameplay/*`
8. `repository/src/input_v2/ingress/*`
9. `repository/src/input/InputFramePump.cpp`、`HidReader.cpp` 和 backend 文件
10. `repository/src/input_v2/presentation/*`
11. `repository/tests/input_v2/*`
12. `repository/lib/commonlibsse-ng/include/RE/*` 中随包提供的输入/光标/held-state 头文件

## 方案包范围

方案包只包含与本问题直接相关的代码和事实，不是完整仓库快照。它刻意不包含：

- Favorites native route 实现和 crash dump；
- SWF workspace；
- haptics；
- 图标视觉资产；
- release package 二进制；
- 用户本机私有路径和日志。

如果 GPT 认为缺少某个文件或游戏事实才能裁决，必须列出精确路径/地址和用途，不得用猜测补齐。
