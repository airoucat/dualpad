# DualPad 键鼠与手柄混合输入正式实现计划

> 版本：仓库修订版 R1，2026-07-12
> 文档性质：正式实施计划，不包含代码实现。
> 外部原件 SHA-256：`B94417C03C01A89DA12D81C33FD73042E3E4414E6348B55311D03394629C7473`。
> 代码审计快照：`93184697c0f021a5979c9a71113bcb2a96308231`。已在真实 checkout 核实该 commit 存在，且截至 `1159c80378be5775f0152c7a01ebd7541f2d6c6b`，`src/`、`tests/`、`xmake.lua` 与 `scripts/ci/` 相对该快照无代码差异。该快照只用于一次性证据审计，不是最终 close-out 的零差异门禁。
> 对比输入 A：`DualPad_Mixed_Input_Formal_Implementation_Plan_2026-07-11_FINAL_zh(1).md`，SHA-256 `4fc3866a2eb5d96eb940edc0a02396dbc9640cea5b5c4e344935a04594decd85`。
> 对比输入 B：`DualPad_键鼠与手柄混合输入正式实现计划(1).md`，SHA-256 `4f6da23a5b613cb7f89085adf20de562ac982e2e410f546379b1bac61c67c448`。
> 差异审计：`DualPad_混合输入两案差异审计与综合裁决_2026-07-11_zh.md`，SHA-256 `abde1f8a7134c00b4c4e6661c8824f3157fe20122d6e31e192560f92b7dfca1f`。
> Skyrim 目标二进制：SE 1.5.97.0，SHA-256 `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD`。
> 当前发布状态：mixed-input 保持 **NO-GO**。只有 H 节自动化、I 节动态证据和 J 节实机矩阵全部通过后，才能解除门禁。
> 工作流状态：本文是待批准的 post-closeout hardening 计划，不创建 `PH9`，也不代表当前已有活跃 Sprint。开始实施前必须在 `.dualpad-builder/` 中登记独立工作包和 Sprint。

## 仓库修订说明

本版在外部 FINAL 方案基础上完成可实施性修正，以下条款具有规范性，正文已同步改写：

1. 固定审计快照只作一次性 preflight；实施时另行冻结 `implementationBaseCommit`，最终 close-out 不再要求相对旧快照零代码差异。
2. 新增 WP0.5 ingress batch scaffold，使 WP1/WP2 使用的 API 在各自开工前已存在并可独立编译。
3. current-cycle 改为 `Prepare -> Apply -> Commit` 同步事务；adapter 失败时不得推进 transient、sustained 或 current-cycle-sensitive ledger。
4. XInput hook 必须发布一次性 `PollMaterializationReceipt`；`InputFramePump` 不得通过再次 acquire 最新帧来猜测本轮实际 materialized identity。
5. KBM raw ledger 与 quarantine 改用稳定 `(device, idCode)` 身份，不再使用可随 ControlMap 重排的 binding index。
6. `ContextResolver` 保持 context authority；ControlMap fingerprint 与现有 `IngressBoundaryKey` 在同一个 owner-tick boundary transaction 中推进。
7. cursor ack 使用显式 `CursorHandoffAckMailbox` 回交 owner tick；gamepad disconnect 使用 source-scoped reset，不能清除 KBM-owned channel。
8. HID classifier 只生产 raw control facts；`GameplayChannel` 在 runtime 读取 context/action graph 后解析。
9. `orderedCutoffSeq` 是累计的 `lastConsumedOrderedSeq`；空 capture 不得回退到 0。
10. overflow 继续保留完整物理 current-state/analog 诊断事实，同时拒绝 ordered-derived semantic/action 并让 virtual output fail-closed，以保持现有 backpressure 合同。
11. synthetic provenance 若无法证明，则禁用冲突 helper route 或把事件按物理输入处理；不得只凭时间窗/token 吞掉真实同 scancode 输入。
12. pointer 达到距离阈值后由 owner tick 检查 120 ms deadline；即使没有后续 mouse event，也能且只能 promotion 一次。

## 总体裁决

采用以下统一架构：

> **`input_v2` 原生 KBM facts + gamepad connectivity/current-state/activity 三分离 + causal ingress cutoff/epoch/session + gameplay 按通道仲裁 + current-cycle 与 next-Poll 双时间视图 + sustained contributor OR 与 virtual bridge + Skyrim engine query 原生链优先、按证据最小作用域覆盖 + 单一 menu presentation 快照 + 两阶段 cursor handoff。**

这份裁决同时吸收两份方案中已经证明有效的部分，并明确否决四类高风险做法：

1. 不把 Skyrim engine mode 永久固定为 Gamepad 或 KeyboardMouse。
2. 不让任意 KBM 活动、prompt family、connectivity、MoveOwner 或 CombatOwner全局翻转 `IsGamepadEnabled()`。
3. 不只修下一份 `PollOutputFrame`，而忽略本次设备 Poll 已经 materialize 的 virtual event。
4. 不把 complete raw-state provider、SprintHandler hook、cursor 坐标写入或 transform router当作无条件前提。它们全部受动态证据门控。

综合方案的关键取舍如下：

- engine/device availability 使用原生链优先。caller 分类、TLS scope 和 router先作为 shadow框架，只有动态 A/B 证明原生链对某个已分类 caller稳定选错分支时，才逐 caller启用覆盖。
- current-cycle gate 采用独立时间视图。它绑定 XInput hook 成功序列化后发布的一次性 `PollMaterializationReceipt`、callback-local 物理事实和 event batch，不复用 next-Poll suppress 布尔值，也不通过重新 acquire 最新帧猜测 identity。
- ingress采用 `causalOrderedTailSeq <= orderedCutoffSeq`、`inputStateEpoch` 和 `gamepadSessionId`，防止 partial drain、overflow和纯手柄断连造成事实撕裂。
- Look第一版保留仓库既有 200 ms mouse quiet语义；不同时引入新的release threshold或2 tick deadzone策略。
- lost release以 physical quarantine和fresh initial-press epoch为保底；经I-KBM证明的complete raw-state provider只作为更快的reconcile路径。
- Sprint采用complete source mask和virtual bridge。只有真实handler证据证明bridge与edge suppression仍不能保持held时，才允许窄范围guard。
- pointer-only movement 只改变 cursor 和 prompt，不改变 `_root.SetPlatform`。第一版复用现有测试语义：累计 10 px 且窗口达到 120 ms 才 promotion；deadline 由 owner tick 检查，不要求第 120 ms 后再出现一条 mouse event。

---

## A. 证据审计

### A.1 审计方法、基线与证据等级

本计划基于两份正式计划、外部 bundle 中的源码/测试/设计/历史设计、CommonLib 头文件、给定 IDA 证据以及当前真实 checkout 重新裁决。代码锚点以当前 checkout 为准；实现切片开始前必须重新核对受影响文件，而不是把旧行号当作永久合同。

计划审计时执行一次以下命令：

```powershell
git rev-parse HEAD
git cat-file -e 93184697c0f021a5979c9a71113bcb2a96308231^{commit}
git diff --name-status 93184697c0f021a5979c9a71113bcb2a96308231..HEAD -- src tests xmake.lua scripts/ci
git cat-file -e 93184697c0f021a5979c9a71113bcb2a96308231:tests/NativeButtonCommitTests.cpp
```

判定规则：

- 固定 commit 只证明外部方案引用的代码事实是否仍成立。若代码有差异，先更新证据表和任务锚点。
- 完成证据审计后，把当时的 `HEAD` 记录为 `implementationBaseCommit`。后续切片必须是它的后代，并在 evidence manifest 中记录实际变更；最终 close-out 不要求相对固定审计快照或 `implementationBaseCommit` 零差异。
- `tests/NativeButtonCommitTests.cpp` 已确认在审计快照和当前 checkout 中存在；需要补的是 Sprint contributor/source-mask 行为覆盖，不能再把“文件可能不存在”作为红灯。

证据等级：

- **已证明**：当前源码、测试、CommonLib布局或给定二进制静态证据直接成立。
- **高概率推导**：多个已证明事实共同支持，但缺少完整动态调用顺序、参数或实机A/B。
- **未验证**：附件不足以安全决定，必须由I节的有退出标准任务闭合。

### A.2 当前仓库与 Skyrim 事实表

| 事实 | 来源文件 / IDA 地址 | 证据等级 | 对方案的约束 | 验证或补强方式 |
|---|---|---:|---|---|
| `src/input_v2/` 是唯一正式 runtime mainline，legacy 只能作为compat、迁移参考或test seam | `AGENTS.md:23-41`；`src/ARCHITECTURE.md:12-29`；`docs/current_input_pipeline_zh.md:12-44` | 已证明 | 所有新事实、仲裁、恢复和publication必须进入`input_v2`；不得恢复旧tracker/coordinator authority | CI扩展`check_legacy_authority_boundary.py`，禁止production新增legacy singleton读取 |
| `DualPadRuntime` 把6个KBM gameplay policy fact硬编码为false | `src/input_v2/gameplay/DualPadRuntime.cpp:391-409`，具体为`394-401` | 已证明 | 当前production没有KBM gameplay输入，不能把现状描述为已支持mixed-input | WP4先用coherent facts让live-style测试红灯，再替换常量 |
| `InputFramePump`只发布keyboard/mouse presentation evidence，没有input_v2-native gameplay current facts | `src/input/InputFramePump.cpp:27-68,112-148` | 已证明 | KBM producer应在owner tick前发布完整facts；不能让runtime旁读legacy singleton | WP2新增平台adapter和纯producer |
| `InputFramePump`是prepend sink并执行owner drain，但Keyboard/Mouse/Gamepad Poll及event materialization已在dispatch前发生 | `src/input/InputFramePump.cpp:77-93,112-150`；`0x140C150B0`；`docs/research/skyrim_xinput_poll_callsite.md:39-57` | Poll-before-dispatch已证明；后续consumer顺序未验证 | 只修改下一份Poll不能证明首次争夺cycle无双写 | I-P闭合Poll、event emit、InputFramePump和下游consumer的phase顺序 |
| 每份成功HID report都进入gamepad evidence路径 | `src/input/HidReader.cpp:75-109` | 已证明 | report到达不等于activity；必须拆connectivity/current-state/activity | WP1的500/1000 Hz neutral交错红灯 |
| live producer无条件调用`RecordGamepadEvidence(true)` | `src/input_v2/ingress/LiveInputFactProducer.cpp:108-129`，调用点`120` | 已证明 | neutral/unchanged report不能续owner lease或清KBM evidence | 只有classifier返回meaningful draft时才发布source activity |
| `RecordGamepadEvidence(true)`会清keyboard/mouse evidence并续gamepad lease | `src/input_v2/presentation/SourceEvidenceCollector.cpp:142-157` | 已证明 | idle controller会持续打败真实KBM；不能通过延长lease掩盖 | ordered meaningful activity替代report cadence |
| mouse pointer accumulator已有10 px、120 ms test seam | `src/input_v2/presentation/SourceEvidenceCollector.cpp:232-244`；`tests/input_v2/PresentationProjectionTests.cpp:109-120` | 已证明现有接口与测试语义 | 第一版pointer-only promotion复用10 px/120 ms，不新增另一组魔数 | WP8冻结路由测试；该阈值只影响cursor/prompt，不影响menu owner |
| Look/Move/Combat/Digital已有per-channel owner和gate | `src/input_v2/gameplay/GameplayProjectionFrame.h:25-42,158-232`；`.cpp:160-245,280-395` | 已证明 | 复用现有projection方向，不重建全局gameplay owner | WP4抽纯仲裁并增加candidate latch |
| 当前native KBM在同通道判定中优先于gamepad | `src/input_v2/gameplay/GameplayProjectionFrame.cpp:184-222` | 已证明 | 物理KBM无法由DualPad可靠撤销，因此同通道冲突时仍必须优先 | current-cycle和next-Poll都遵守该规则 |
| 当前Look/Move enter/sustain为`0.25/0.15`，Combat trigger为`0.15/0.08` | `src/input_v2/gameplay/GameplayProjectionFrame.h:217-224` | 已证明 | 第一版沿用既有阈值，避免接线修复同时改变手感 | 手感改动必须另立独立切片和实机A/B |
| legacy mouse-look quiet窗口为200 ms | `src/input/GameplayKbmFactTracker.cpp:12,200-225` | 已证明 | 第一版沿用200 ms，但用owner monotonic time实现；它不是presentation lease | WP4纯函数和实机阈值测试 |
| 当前`engineOwner`是多个primary channel折叠结果，不等于LookOwner | `src/input_v2/gameplay/GameplayProjectionFrame.cpp:224-243` | 已证明 | 不能继续把它喂给所有`IsUsingGamepad` caller | WP9把旧字段降级为shadow观测值 |
| sustained source mask类型已经存在 | `src/input_v2/gameplay/GameplayProjectionFrame.h:74-80,117-147` | 已证明 | 不创建第二套持续态概念，扩展现有mask和backend materialization | WP6使用complete source mask |
| production不会提供keyboard/mouse sustained contributor | `src/input_v2/gameplay/DualPadRuntime.cpp:394-401`；`GameplayProjectionFrame.cpp:374-395` | 已证明 | 当前Sprint无法按真实来源OR | WP6从KBM facts生成per-action contributor |
| backend把KBM Sprint固定为false | `src/input/backend/NativeButtonCommitBackend.cpp:896-909` | 已证明 | sustained交接在materialization端未闭合 | 删除常量和legacy读取，消费complete snapshot |
| Sprint当前为single-emitter hold，并包含KeyboardMouse到Gamepad的故意gap | `src/input/backend/PollCommitCoordinator.cpp:503-579,795-857` | 已证明 | 该gap直接违反“任一来源held不得中断” | WP6删除gap并引入virtual bridge |
| `SprintHandler`是held-state handler | `lib/commonlibsse-ng/include/RE/S/SprintHandler.h:7-18`；`RE/H/HeldStateHandler.h:15-23` | 结构已证明；多来源release行为未验证 | 不能假定两个raw来源天然OR，也不能预先加hook | I-SPRINT动态watchpoint决定是否需要guard |
| legacy`GameplayKbmFactTracker`含ControlMap映射逻辑，但PH8a已删除live producer | `src/input/GameplayKbmFactTracker.cpp:44-55,96-198,228-317`；`docs/current_input_pipeline_zh.md:37-44` | 已证明 | 只迁移映射语义，不恢复singleton authority | WP2纯producer不include legacy tracker |
| 当前live-style tests主要验证顺序takeover | `tests/input_v2/IngressTests.cpp:1001-1105`；`tests/input_v2/InputV2Tests.cpp:1188-1330` | 已证明 | 现有绿灯不能覆盖neutral HID与KBM持续交错 | WP1/WP10增加rate matrix |
| 现有500/1000 Hz测试只验证latest analog state，不验证activity owner | `tests/input_v2/IngressTests.cpp:1541-1611` | 已证明 | current-state retention和activity generation必须同测 | WP1扩展同一fixture |
| Poll hook 每次只 acquire 一份 immutable `PollOutputFrame` 并同步 serialize，但当前没有回传“实际成功 materialize 的帧”回执 | `src/input/injection/UpstreamGamepadHook.cpp:77-168`；`PollOutputFrame.cpp:37-80` | 已证明 | Poll reader不得推进runtime、context、pulse或presentation；Pump 也不能再次 acquire 最新帧代替回执 | WP5 新增一次性 `PollMaterializationReceipt`，I-P 证明 pairing |
| `InputFramePump / RuntimeOwnerGuard`是唯一runtime mutation owner，允许串行thread migration | `RuntimeOwnerGuard.h:69-74`；`.cpp:56-116` | 已证明 | HID、hook、UI和Poll只能发布或读取事实/快照 | E节writer/reader表冻结 |
| `IngressHub::Capture(maxEvents)`会drain ordered prefix，却返回全局latest slots | `IngressHub.cpp:402-414`；`FrameAssembler.cpp:140-220,476-526` | 代码行为已证明；因果领先风险高概率 | 新latest不能只凭generation应用，必须带causal tail | WP3新增partial-drain红灯 |
| `PushPadSnapshot`在容量检查前可能先更新latest | `IngressHub.cpp:177-203,267-287` | 已证明代码顺序 | overflow不能留下incoming latest半提交，否则stale held会重附着 | WP3改为事务容量检查后提交 |
| latest-only publication不能消耗ordered seq | `IngressHub.cpp:90-142`；`FrameAssembler.cpp:140-168` | 当前seq规则已证明 | neutral report不得制造虚假SequenceGap | `causalOrderedTailSeq`复用最近ordered tail |
| producer timestamp只作诊断，跨producer顺序由ingress seq决定 | `FrameAssembler.cpp:245-275` | 已证明 | HID与SKSE clock不能直接比较大小决定owner | source activity统一由Hub赋seq |
| Skyrim同一input cycle遍历Keyboard、Mouse、Gamepad、FlatVirtualKeyboard | `0x140C150B0`；CommonLib `BSInputDeviceManager.h:59-76`；`InputDevices.h:7-14` | 已证明 | 跨设备、跨通道并行在底层可行 | 实机仍需验证具体handler和transform |
| gamepad Poll消费完整XInput current-state | `0x140C1AB40`，callsite`0x140C1AB9D` | 已证明 | unchanged held stick必须持续保留；activity稀疏化不能稀疏current-state | WP1每stable frame断言0.60不丢 |
| `0x140C15240`是`devices[kGamepad]->IsEnabled()`语义查询，有26个direct xrefs | `docs/research/skyrim_mixed_input_mode_queries_zh.md:85-109`；`BSIInputDevice.h:18-23` | 已证明 | 该状态不是glyph-only，不能由presentation owner全局控制 | I-1分类全部caller |
| `0x140705AE0`按gamepad-enabled选择两套二维变换 | 研究文档`111-135`；已知caller`0x140707110`、`0x140888190`、`0x1408E1560`、`0x1408E1620` | 行为已证明；完整业务身份未验证 | global bool无法天然服务多通道；也不能预先假定LookOwner router正确 | I-2恢复ABI、caller和A/B结果 |
| `_root.SetPlatform`最终每次菜单刷新只发布一次 | `0x140ECD970`；研究文档`137-146` | 已证明 | menu必须折叠为一份稳定owner | WP8/WP9每epoch最多一次DualPad refresh |
| mouse与gamepad cursor使用不同位置来源 | `0x140ED2F90`；`MenuCursor.h:13-31` | 已证明两条路径；具体实例/坐标单位未验证 | cursor owner不能只翻布尔，也不能在未验证时盲写坐标 | I-CURSOR按方向A/B裁决 |
| 当前compat surface同时patch顶层query、cursor query和一个gamepad handler vfunc | `SkyrimCompatibilitySurface.cpp:19-29,446-545` | 已证明 | device availability、engine transform、menu和cursor职责必须拆开 | WP9拆patch group和original gateway |
| 当前top-level query直接返回`PublishedPresentationState.owner` | `SkyrimCompatibilitySurface.cpp:631-637,1141-1148` | 已证明 | 这是跨层耦合，可能污染26个caller和二维变换 | 默认无scope调用original |
| 当前名为device IsEnabled的hook在非remap时返回true | `SkyrimCompatibilitySurface.cpp:647-656,1159-1184` | 函数体已证明；vtable slot身份未验证 | 不能把connectivity或route operational当作全局mode；身份未闭合前不能安装 | I-0解析REL/vtable/slot/original target |
| CommonLib把`BSIInputDevice::IsEnabled`标为逻辑slot 07，而当前代码patch handler index 0x8 | `BSIInputDevice.h:14-24`；`SkyrimCompatibilitySurface.cpp:19-23,476-510` | 类层级关系未验证 | 不能直接判off-by-one，也不能把源码命名当证据 | I-0 dump handler 0-9项并跟踪delegate |
| `DualPadRuntime`已有frame-bound config/prompt baseline | `DualPadRuntime.cpp:273-335,432-457`；`PromptRuntimeOwner.cpp:34-78` | 已证明 | 不新增第二套config/prompt authority | 综合runtime快照只装配已裁决结果 |
| `DualPadGameplayProjectionTests` target存在，但Phase8脚本没有build/run它 | `xmake.lua:304-327`；`scripts/ci/run_phase8_ci.ps1:20-44` | 已证明 | canonical gate当前会漏掉核心projection红灯 | WP0立即修复并加script-content红灯；WP10只防回归 |
| `DualPadNativeButtonCommitTests` target 与 `tests/NativeButtonCommitTests.cpp` 均真实存在，但当前正文没有 Sprint contributor/source-mask 覆盖 | `xmake.lua:417-429`；`tests/NativeButtonCommitTests.cpp` | 已证明 | 不得把 target/file 存在等同于 Sprint coverage | WP0 做内容级预检，WP6 增加行为 fixture |

### A.3 两份方案差异与正式冲突裁决

| 议题 | 方案A | 方案B | 综合裁决 |
|---|---|---|---|
| engine默认策略 | original-first，scoped override证据触发 | caller-scoped router作为推荐主路径 | 保留caller class/causality框架，但所有class默认`Original`；逐caller A/B通过后才enable |
| device availability | 默认Native，只有原生Poll停止才按connectivity覆盖 | `DeviceOperational`可由route operational直接true | 独立`GamepadDeviceAvailabilityDecision`；无证据时entry和device vfunc都调用original |
| current-cycle gate | 同token按event ordinal materialize | receipt-backed materialized Poll identity，使用callback-local事实独立决定writer | 采用B的双时间视图，但增加hook receipt与同步prepared commit；保留A的物理KBM安全优先 |
| ingress coherence | generation/marker pairing | causal tail/cutoff、global epoch、gamepad session | 采用B的因果模型 |
| KBM producer | producer直接接触`RE::InputEvent` | Skyrim adapter与纯producer分层 | 采用平台adapter分层，保留A的raw ledger/quarantine |
| mouse Look hysteresis | 200 ms | 2 owner ticks | 第一版采用200 ms，不把帧率引入手感 |
| analog release策略 | 既有enter/sustain | 新增0.10/0.05和2 tick确认 | 第一版不新增release阈值；deadzone优化另立切片 |
| KBM release后pad reclaim | 通常重新达到enter | current-state达到sustain即可同tick接管 | 增加`gamepadCandidateLatched`：曾跨enter或takeover前已是Gamepad时可按sustain接管，否则要求enter |
| lost release | quarantine + real release/fresh press | complete raw provider reconcile | quarantine为保底；provider仅在I-KBM通过后加速reconcile |
| Sprint | virtual bridge + edge suppression | raw overlap，必要时handler guard | 采用virtual bridge；handler guard降为最后条件分支 |
| activity | ordered family event | context-neutral raw activity后runtime route | 采用B，避免HID线程理解menu/gameplay |
| menu pointer | 未完整冻结参数 | pointer-only只改cursor，参数4 px/2 tick | 分流语义保留，参数改用仓库现有10 px/120 ms测试语义 |
| cursor | 强调双向同步 | 两阶段plan/ack，主要描述KBM到Gamepad | 采用两阶段事务；两个方向分别由I-CURSOR A/B裁决 |
| publication | 多份decision靠revision匹配 | 单一runtime shared snapshot | runtime owner一次构建一次发布综合快照；`PollOutputFrame`仍独立但共享identity |
| CI | canonical命令已列出 | 明确发现Phase8漏跑GameplayProjection target | WP0接入，WP10 closeout防回归 |

以下附件初步方向只作为必要骨架，不作为完整答案：

```text
LatestKbmGameplayFacts
+ meaningful gamepad activity
+ per-channel gate
```

它没有单独解决 current-cycle首帧漏斗、partial-drain因果领先、overflow半提交、global epoch与pad session、Skyrim engine transform caller治理、Sprint bridge、cursor事务和canonical gate缺口。综合方案逐项补齐这些空洞。

---

## B. 目标行为合同

### B.1 全局不变量

1. `Look / Move / Combat / TransientDigital` 每个通道在一个时间视图内最多一个writer。current-cycle和next-Poll分别计算writer count，两者都必须`<=1`。
2. 物理KBM在同通道冲突时优先，因为DualPad能可靠gate自己的virtual XInput，却不能可靠撤销Skyrim已接收的物理输入。
3. 跨通道并行。一个通道的KBM活动只gate同一通道，不禁用其它virtual通道。
4. current-cycle 与 next-Poll 是两个不同决策：
   - next-Poll消费owner tick组装出的latest facts；
   - current-cycle 消费本 callback 的物理 current/activation、上一 owner 状态，以及一次性 `PollMaterializationReceipt` 证明已经 materialize 的 virtual event。
5. Poll之后才到达的HID latest不得回溯删除一个已经materialize、且没有物理竞争者的virtual event。
6. connectivity、current-state、device availability、meaningful activity是四种不同语义，不能由一个bool承载。
7. neutral/unchanged HID可以推进pad current-state generation，但不得推进activity generation、prompt family、menu owner、cursor owner或gameplay takeover。
8. unchanged non-neutral pad state必须在每个stable frame保留完整current-state，并可维持已有Gamepad owner。
9. prompt family只追随Hub排序后的有意义来源，不得写gameplay gate、engine decision、menu owner或cursor owner。
10. menu presentation owner与navigation owner共享单一稳定结论；cursor owner独立，但与menu/prompt一起原子发布。
11. gameplay不使用长lease。Look只使用200 ms physical mouse quiet hysteresis；该窗口不由neutral HID续期。
12. 只有具备已验证 provenance 的 synthetic keyboard helper event 才能从 physical ledger、semantic masks、ordered physical edge、source activity 和 current-cycle 竞争中排除。若同 scancode 的 synthetic/physical 来源不可区分，必须禁用冲突 helper route 或按 physical event 处理，绝不能仅凭时间窗或普通 token 吞事件。
13. global reset由`inputStateEpoch`隔离；纯gamepad connect/disconnect由`gamepadSessionId`隔离。手柄断连不得使完整KBM held失效。
14. transition frame只发布neutral/frozen virtual output，不dispatch新的DualPad synthetic action；Skyrim原生物理KBM继续流转。
15. Poll reader只Acquire和serialize同一份immutable`PollOutputFrame`，不得推进runtime、pulse、context、presentation或recovery。

### B.2 固定仲裁参数

| 通道 | Gamepad enter | Gamepad sustain | KBM enter / sustain | release与reclaim |
|---|---:|---:|---|---|
| Look | right-stick magnitude `>=0.25` | previous/candidate Gamepad且`>=0.15` | 任一非零physical mouse delta立即取得；最后mouse delta后200,000 us内保持 | quiet结束后，latched candidate且`>=0.15`可同tick接管；未latched要求`>=0.25`；双方inactive为`None` |
| Move | left-stick magnitude `>=0.25` | previous/candidate Gamepad且`>=0.15` | 任一mapped move key held立即取得，直到complete move mask归零 | 最后KBM release时按candidate规则接管；双方inactive为`None` |
| Combat | `max(LT,RT)>=0.15` | previous/candidate Gamepad且`>=0.08` | 任一mapped attack/block keyboard或mouse held立即取得 | 最后KBM release时按candidate规则接管；第一版整个LT/RT为单一Combat通道 |
| TransientDigital | virtual semantic press | 不适用 | 同一`ActionId`、context、epoch、owner tick内physical press优先 | transaction完成即结束；pending virtual遇physical press先cancel |
| SustainedDigital | contributor进入source mask | 任一source仍held | keyboard/mouse/gamepad分别占独立bit | mask从非0变0才release；不使用owner lease |

`gamepadCandidateLatched`规则：

- KBM占用期间，gamepad曾跨过enter threshold，则置true。
- takeover前该通道已由Gamepad拥有，且被KBM临时抢占时pad仍不低于sustain，也置true。
- pad低于sustain、hard reset、context/revision变化或session变化时清false。
- 最后一个KBM contributor release时，latched candidate可按sustain接管；未latched必须重新达到enter。

### B.3 场景结果表

| 场景 | 确定结果 |
|---|---|
| mouse look + gamepad move | `LookOwner=KeyboardMouse`。若本cycle已有virtual right-stick event且存在physical mouse竞争，current-cycle gate中和它；下一份Poll同时令RX/RY为0。`MoveOwner=Gamepad`，LX/LY保持current-state。engine默认Original；只有已分类并通过A/B的look caller使用KBM scope。 |
| gamepad look + keyboard move | `LookOwner=Gamepad`，right stick保持。`MoveOwner=KeyboardMouse`，本cycle已materialize的left-stick输家event被中和，下一Poll LX/LY=0。keyboard move不能全局翻转look transform。 |
| mouse + right stick同Look争夺 | 任一非零physical mouse delta立即赢。mouse quiet 200 ms内仍为KBM。quiet结束后，latched candidate且RS `>=0.15`可接管；未latched要求`>=0.25`。同cycle冲突只保留mouse writer。 |
| keyboard + left stick同Move争夺 | 任一mapped move key held时KBM赢，current-cycle和next-Poll都gate virtual LS。最后key release后按candidate规则恢复。若winner event未在本cycle materialize，不合成Skyrim event，下一次qualifying Poll恢复，最大延迟1个input cycle。 |
| KBM combat + LT/RT | 任一physical combat held时整个Combat通道由KBM写，virtual LT/RT本cycle和下一Poll都neutral。全部physical combat release后按candidate规则恢复。Look、Move和不冲突digital不受影响。 |
| transient digital双来源 | 仲裁键为`(inputStateEpoch, contextRevision, ActionId, ownerTickToken)`。同键physical press优先，virtual press不启动。若virtual pulse已down-visible，本tickphysical press先触发一次cancel/up，再禁止新virtual press。 |
| Sprint G->K->松G->松K | source mask为`G -> G∪K -> K -> 0`。virtual一旦由G materialize，在aggregate仍held时保持bridge。K joining press和G non-final release按已验证current-cycle disposition抑制；全程无inactive frame，只在0时一次final release。 |
| Sprint K->G->松K->松G | mask为`K -> K∪G -> G -> 0`。K-only阶段不合成virtual press；G加入时可建立virtual current-state，但其duplicate press被抑制。K non-final release不结束Sprint；G最终release一次结束。 |
| keyboard/mouse-only Sprint | 不合成新的virtual press。Skyrim原生KBM负责held；physical contributor仍进入aggregate snapshot，用于后续gamepad加入或恢复判断。 |
| idle connected controller + KBM | connectivity=Connected，pad state generation持续增长；neutral report不产生meaningful activity，不清KBM evidence，不改变任何owner、prompt、menu、cursor或engine scope。 |
| non-neutral held stick unchanged | unchanged report不刷新presentation activity，但每个stable frame继续输出完整stick current-state；无同通道KBM竞争且达到sustain时Gamepad owner不丢失。 |
| menu entry | 先执行context boundary recovery并取消旧transient/stale gate。menu owner优先采用触发entry action的有意义来源；无明确source时采用进入前最近strong source。`_root.SetPlatform`只消费该单一owner。cursor必须走handoff transaction。 |
| menu内keyboard/gamepad导航 | keyboard character/button、mouse button/wheel、gamepad button/D-pad、navigation stick跨enter均为strong event，立即切menu/navigation owner；neutral HID不切换。每个owner/context epoch最多一次DualPad refresh。 |
| menu内pointer-only | mouse 累计 Manhattan 距离达到 10 px 后保留 pending pointer candidate；owner tick 在窗口达到 120 ms 时进行一次性 promotion，即使期间没有新 mouse event。它只把 cursor 和 prompt 切为 KBM，不改变 menu owner、navigation owner 或 `_root.SetPlatform`；若 candidate 之后出现更高 `ingressSeq` 的 strong source，则旧 candidate 失效。 |
| cursor handoff | 使用`Plan -> UI side effect -> Ack -> next owner tick commit`。UI task重新取得live menu/movie并核对instance、pointer identity、contextRevision和presentationEpoch。未成功ack时保持旧cursor owner。两个方向是否需坐标同步由I-CURSOR分别裁决。 |
| prompt/glyph | 跟随最高`ingressSeq`的合格`RoutedSourceActivity`。family变化不得改变gameplay、engine、menu或cursor字段。gamepad disconnect且family为Gamepad时回落KBM；其它global recovery冻结上一family并记录reason。 |
| gamepad disconnect | Hub在同一事务递增`gamepadSessionId`，不递增`inputStateEpoch`；gamepad connection/current/activity归neutral，清gamepad-owned channel、pulse、contributor和旧session gate。完整KBM held、KBM-owned channel和physical Sprint contributor保留。 |
| context boundary | 递增`inputStateEpoch`，取消全部virtual output、transient、bridge、stale gate和pending cursor transaction；semantic KBM mask清零，当前raw down进入quarantine。 |
| ControlMap reload | owner tick 把 `ContextResolver` snapshot 与 ControlMap fingerprint 放入同一个 boundary transaction。Hub 先推进现有 `IngressBoundaryKey`/epoch，再原子提交 new binding snapshot 和本 callback KBM batch。旧 revision semantic mask 清零，旧 mapping 的稳定 `(device,idCode)` down codes 进入 quarantine；旧 edge 拒绝。 |
| overflow / sequence gap | Hub 原子发布 global recovery marker。触发 overflow 的 ordered edge/activity 与其派生 semantic action 不提交；完整物理 current-state/analog 可作为新 epoch 的 recovery-stamped latest 保留，供诊断并避免 analog freeze，但在 post-marker coherent facts 到达前不得驱动 virtual gameplay。旧 epoch latest 不得重附着。 |
| lost release | 不用timeout猜测。若I-KBM批准complete provider，则facts held但raw up时产生一次`ReconciledRelease`；否则code进入quarantine，真实release或同码fresh initial-press epoch才能rearm。 |
| synthetic keyboard suppression | 只有 I-KBM 证明 event provenance，或使用经验证不与 physical input 冲突的 reserved control 时，精确 injection receipt/token 才能过滤 helper event。仅匹配 scancode、phase、contextRevision、output generation 和时间窗不足以证明来源；证据不足时按 physical event 处理或禁用该 helper route。 |

---

## C. 方案比较与正式裁决

### C.1 五个候选方案比较

| 方案 | 已证明行为符合度 | 新hook/patch | 风险 | gameplay表现 | menu/cursor | publication与线程 | 回退与验证 | 裁决 |
|---|---|---|---|---|---|---|---|---|
| 1. gameplay engine mode跟随LookOwner，menu跟随presentation owner | 只对部分camera-like caller有直觉依据，无法解释其余25个xref | 可复用当前global query hook | 中到高，Look会污染movement/menu/remap/unknown caller | mouse/RS可能改善，WASD/combat可能走错数学；current-cycle空窗仍需另解 | menu可稳定，cursor仍需事务 | 简单但把channel提升为global state | host可测owner，不能证明26 caller | 不采用为全局策略 |
| 2. engine mode跟随last meaningful primary source | 接近传统单设备模式，但与跨通道并行目标冲突 | 当前hook即可 | 高，真实跨源活动会持续抖动 | mouse+LS、RS+W必有通道使用错误transform | menu直觉化，pointer/nav仍抖 | activity流变成全局控制面 | 可测抖动，无法给每caller正确答案 | 拒绝 |
| 3. 重写`0x140705AE0`或上游数学 | 理论最精确 | 替换函数或多个callsite | 很高，ABI、acceleration状态和版本耦合最大 | 可完全source-aware，但复制数学易改变手感 | menu caller也需覆盖 | hook保存状态复杂 | rollback可做，实机矩阵巨大 | 只作最后备选，不进入首版 |
| 4. 完整保留Skyrim原生engine mode，只管virtual gate/menu/prompt | 侵入最小，尊重全部caller | 移除当前presentation-owner/global true行为patch | 低到中 | 若native chain正确则最佳；若错误则mouse/RS可能走错数学 | menu/cursor仍需单独治理 | 最清晰 | I-0/I-2可直接证明 | 作为默认底座 |
| 5. **原生链优先 + causal facts + current-cycle/next-Poll双gate + scoped menu + 证据触发caller override** | 同时满足多设备Poll、global query非纯UI、菜单单一结论和首次takeover单writer | verified original gateway；current-cycle adapter；menu RAII scope；availability/transform/direct-callsite均条件启用 | 默认低到中，可选patch独立回退 | 按通道混合；输家在两个时间视图分别gate；transform只在已证明domain覆盖 | menu单一，cursor事务，prompt独立 | owner tick一次发布；hook/adapter只读或materialize | host、IDA、实机三层均有明确pass/fail | **正式采用** |

### C.2 Skyrim engine mode正式裁决

1. **默认驱动者是Skyrim original chain**。`0x140C15240`对全部caller的无scope结果由原函数决定。
2. `REL::ID 67320`、`REL::ID 560029`、handler vtable基址、slot `0x7/0x8`和original target必须在I-0唯一闭合。任何身份不一致都使compatibility behavior group fail-closed。
3. `GamepadDeviceAvailabilityDecision`独立回答virtual route是否在经证明的Poll/init域可用，不表达最近activity、prompt、menu或channel owner。
4. 恢复original后若`0x140C1AB9D`持续消费virtual XInput，则availability policy=`Native`，删除default vfunc override。
5. 只有I-0证明original会稳定阻断virtual Poll/初始化，且caller域封闭时，才在该域按`connected && delegateReady`覆盖。
6. 菜单平台首选不patch`0x140ECD970`。在现有`DoRefreshMenus`/`RefreshPlatform`调用外压入RAII `MenuSetPlatform` scope，verified gateway只在该scope返回menu owner。
7. 只有I-MENU证明外层scope无法覆盖必要初次发布，且内部query callsite可独立签名化时，才允许`MenuPlatformDirectCallsite`条件patch。
8. shared 2D transform默认Original。I-2若证明某个已分类caller在native chain下稳定走错分支，优先在更上游source-specific handler压入scope；找不到更窄入口时才考虑`0x140705AE0`入口router。
9. LookOwner只能驱动已证明的look/camera domain；MoveOwner只能驱动已证明的movement domain。Combat、prompt、connectivity和menu owner不得翻转gameplay transform。
10. Unknown、Remap、causality未证明、结果可能写入跨caller全局缓存或snapshot stale时，一律Original。
11. router先shadow后逐caller enable，不存在全局一次性开关。

### C.3 是否需要新hook或adapter

| 能力 | 默认状态 | 启用条件 | 失败回退 |
|---|---|---|---|
| verified engine query gateway | 仅在menu scope或shadow需要时安装；无scope调用original | I-0 identity与transaction全部通过 | 完整卸载，所有query original |
| device availability scope | 禁用 | I-0动态结果证明native Poll停止且域封闭 | 回Native，mixed保持NO-GO |
| current-cycle event adapter | shadow-only | I-P证明receipt唯一配对、pure decision已prepare、所有相关consumer尚未读取、event可安全原地中和，且adapter audit失败不会推进sensitive ledger | 关闭adapter，对应通道NO-GO |
| menu refresh RAII scope | 纯C++行为，默认候选 | I-MENU证明每epoch一次refresh可稳定覆盖 | 回原生menu平台，menu mixed NO-GO |
| menu direct callsite patch | 禁用 | I-MENU结果B | 独立卸载，回外层scope |
| shared transform scope/router | 禁用 | I-2结果B且ABI/caller/caching闭合 | 单caller关闭或全部Original |
| SprintHeldStateGuard | 不创建 | I-SPRINT证明bridge+edge suppression仍被原生release清held | 独立卸载，Sprint mixed NO-GO |
| cursor coordinate side effect | shadow plan/ack | I-CURSOR对具体方向证明实例、坐标换算和read-back | 保持旧cursor owner |
| KBM raw-state reconcile | 禁用 | I-KBM证明complete physical-only provider | 继续quarantine/fresh-press恢复 |

### C.4 prompt、menu、cursor和per-channel裁决

- prompt family与engine mode永久分离。
- menu presentation owner与navigation owner继续共享一份稳定结论。
- cursor owner独立，但和menu/prompt同代原子发布。
- pointer-only达到10 px/120 ms后只改cursor/prompt，不改menu owner。
- physical KBM active优先于virtual gamepad。
- Gamepad enter/sustain使用既有阈值；第一版不新增0.10/0.05 release threshold。
- Look用200 ms quiet窗口，不用2 owner-tick或presentation lease。
- competitor release时，latched gamepad current-state可按sustain同tick接管；未latched要求enter。
- current-cycle gate不合成缺失winner event，只中和已materialize输家event。


---

## D. 精确数据模型

### D.1 Reset、因果标识与公共source类型

新增 `src/input_v2/ingress/InputResetReason.h`：

```cpp
namespace dualpad::input_v2::ingress
{
    enum class InputResetReason : std::uint32_t
    {
        None = 0,
        DeviceDisconnected = 1u << 0,
        ContextBoundary = 1u << 1,
        ControlMapReload = 1u << 2,
        QueueOverflow = 1u << 3,
        SequenceGap = 1u << 4,
        FocusLost = 1u << 5,
        LostReleaseReconciled = 1u << 6,
        SyntheticSuppressionReset = 1u << 7,
        ExplicitReset = 1u << 8,
        RuntimeOwnerFailure = 1u << 9
    };

    using InputResetReasonMask = std::uint32_t;

    enum class InputResetScope : std::uint8_t
    {
        GamepadSource = 0,
        KeyboardMouseSource,
        GlobalInputState
    };

    struct InputResetMarker
    {
        InputResetReasonMask reasons{ 0 };
        InputResetScope scope{ InputResetScope::GlobalInputState };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    struct CausalLatestHeader
    {
        std::uint64_t generation{ 0 };
        std::uint64_t causalOrderedTailSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    enum class PhysicalInputSource : std::uint8_t
    {
        None = 0,
        Keyboard,
        Mouse,
        Gamepad
    };

    enum class GameplayChannel : std::uint8_t
    {
        Look = 0,
        Move,
        Combat,
        TransientDigital
    };
}
```

规则：

- `inputStateEpoch`只由`IngressHub`的`GlobalInputState` reset事务递增。
- `gamepadSessionId`只由`IngressHub`的connect/disconnect事务变化。
- producer不得自行选择epoch/session，也不得把自己的timestamp当作跨producer顺序。
- `LostReleaseReconciled`和`SyntheticSuppressionReset`不递增global epoch，它们只清具体KBM位或suppression bookkeeping。

### D.2 KBM binding、raw ledger、quarantine、current facts与ordered edge

新增 `src/input_v2/ingress/KbmGameplayFacts.h`。该头文件显式包含 `src/input_v2/ingress/MeaningfulSourceActivity.h`，因此batch类型不会依赖未完成类型：

```cpp
namespace dualpad::input_v2::ingress
{
    enum class KbmPhysicalDevice : std::uint8_t
    {
        Keyboard = 0,
        Mouse
    };

    struct KbmPhysicalCode
    {
        KbmPhysicalDevice device{ KbmPhysicalDevice::Keyboard };
        std::uint32_t idCode{ 0 };

        friend bool operator==(const KbmPhysicalCode&, const KbmPhysicalCode&) = default;
    };

    inline constexpr std::size_t kMaxKbmPhysicalCodes = 128;

    // Sorted unique set keyed by stable (device, idCode), never by binding index.
    struct KbmPhysicalCodeSet
    {
        std::array<KbmPhysicalCode, kMaxKbmPhysicalCodes> values{};
        std::size_t count{ 0 };

        [[nodiscard]] bool Contains(KbmPhysicalCode code) const noexcept;
        bool Insert(KbmPhysicalCode code) noexcept;
        bool Erase(KbmPhysicalCode code) noexcept;
    };

    enum class KbmGameplayClass : std::uint8_t
    {
        Look = 0,
        Move,
        Combat,
        TransientDigital,
        SustainedDigital
    };

    enum class KbmEdgePhase : std::uint8_t
    {
        Press = 0,
        Release,
        MouseDelta,
        ReconciledRelease
    };

    enum class KbmEdgeOrigin : std::uint8_t
    {
        Physical = 0,
        SyntheticSuppressed,
        Reconciled
    };

    enum class KbmBaselineState : std::uint8_t
    {
        Clean = 0,
        AwaitPhysicalRearm,
        MappingRearmRequired,
        ProviderIncomplete
    };

    struct KbmBindingEntry
    {
        KbmPhysicalCode physical{};
        actions::ActionId actionId{};
        KbmGameplayClass gameplayClass{ KbmGameplayClass::TransientDigital };
        std::uint32_t semanticBit{ 0 };
    };

    struct KbmBindingSnapshot
    {
        std::uint64_t generation{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::vector<KbmBindingEntry> entries;
    };

    struct KbmRawCurrentState
    {
        std::uint64_t providerGeneration{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        KbmPhysicalCodeSet downCodes{};
        bool physicalOnlyProvenance{ false };
        bool complete{ false };
    };

    class IKbmCurrentStateProvider
    {
    public:
        virtual ~IKbmCurrentStateProvider() = default;
        virtual KbmRawCurrentState ReadComplete(
            const KbmBindingSnapshot& bindings) const noexcept = 0;
    };

    struct KbmObservedEventDraft
    {
        std::uint32_t eventOrdinal{ 0 };
        std::uint64_t producerTimestampUs{ 0 };
        KbmPhysicalCode physical{};
        KbmEdgePhase phase{ KbmEdgePhase::Press };
        KbmEdgeOrigin origin{ KbmEdgeOrigin::Physical };
        std::int32_t deltaX{ 0 };
        std::int32_t deltaY{ 0 };
    };

    struct KbmGameplayEdgeDraft
    {
        std::uint64_t producerTimestampUs{ 0 };
        KbmPhysicalCode physical{};
        KbmGameplayClass gameplayClass{ KbmGameplayClass::TransientDigital };
        actions::ActionId actionId{};
        KbmEdgePhase phase{ KbmEdgePhase::Press };
        KbmEdgeOrigin origin{ KbmEdgeOrigin::Physical };
        std::int32_t deltaX{ 0 };
        std::int32_t deltaY{ 0 };
    };

    struct KbmGameplayEdge : KbmGameplayEdgeDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    struct KbmGameplayCurrentFacts
    {
        std::uint32_t keyboardMoveHeldMask{ 0 };
        std::uint32_t keyboardCombatHeldMask{ 0 };
        std::uint32_t mouseCombatHeldMask{ 0 };
        std::uint32_t keyboardTransientHeldMask{ 0 };
        std::uint32_t mouseTransientHeldMask{ 0 };
        std::uint32_t keyboardSustainedHeldMask{ 0 };
        std::uint32_t mouseSustainedHeldMask{ 0 };
        bool complete{ false };
    };

    struct KbmPhysicalLedger
    {
        KbmPhysicalCodeSet downCodes{};
        KbmPhysicalCodeSet quarantineCodes{};
        std::uint64_t physicalEpoch{ 0 };
        bool complete{ false };

        [[nodiscard]] bool QuarantineDrained() const noexcept;
    };

    struct LatestKbmGameplayFacts
    {
        CausalLatestHeader causal{};
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        KbmGameplayCurrentFacts current{};
        KbmPhysicalLedger physical{};
        std::uint64_t lastPhysicalMouseMoveOwnerUs{ 0 };
        bool physicalMouseMoveThisFrame{ false };
        KbmBaselineState baseline{ KbmBaselineState::Clean };
        InputResetReasonMask resetReasons{ 0 };
    };

    struct KbmGameplayIngressBatchDraft
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        std::uint64_t bindingGeneration{ 0 };
        std::uint64_t controlMapFingerprint{ 0 };
        std::vector<KbmGameplayEdgeDraft> orderedEdges;
        std::vector<MeaningfulSourceActivityDraft> sourceActivities;
        KbmGameplayCurrentFacts completeCurrent{};
        KbmPhysicalLedger physical{};
        std::uint64_t lastPhysicalMouseMoveOwnerUs{ 0 };
        KbmBaselineState baseline{ KbmBaselineState::Clean };
    };

    enum class SyntheticProvenanceMode : std::uint8_t
    {
        Unproven = 0,
        VerifiedPhysicalOnlyProvider,
        ReservedNonCollidingControl
    };

    struct SyntheticKeyboardSuppressionToken
    {
        std::uint64_t token{ 0 };
        std::uint8_t scancode{ 0 };
        KbmEdgePhase expectedPhase{ KbmEdgePhase::Press };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t originatingOutputGeneration{ 0 };
        std::uint64_t helperInjectionSequence{ 0 };
        SyntheticProvenanceMode provenanceMode{ SyntheticProvenanceMode::Unproven };
        std::uint8_t remainingMatches{ 0 };
        std::uint64_t expiresAtOwnerUs{ 0 };
    };
}
```

平台层和纯producer分开。平台中立的observed batch定义在`input_v2`，Skyrim adapter只负责填充它，避免core反向依赖`src/input/`类型：

```cpp
namespace dualpad::input_v2::ingress
{
    struct KbmObservedBatch
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        std::vector<KbmObservedEventDraft> events;
        KbmRawCurrentState rawCurrent{};
        bool eventListComplete{ false };
    };
}

namespace dualpad::input
{
    class SkyrimKbmInputAdapter
    {
    public:
        input_v2::ingress::KbmObservedBatch ObserveEventList(
            RE::InputEvent* const* events,
            const input_v2::ingress::KbmBindingSnapshot& bindings,
            std::uint64_t ownerTickToken,
            std::uint64_t eventBatchToken,
            std::uint64_t ownerNowUs);
    };
}

namespace dualpad::input_v2::ingress
{
    class KbmGameplayFactProducer
    {
    public:
        KbmGameplayIngressBatchDraft BuildIngressBatch(
            const KbmObservedBatch& observed,
            const KbmBindingSnapshot& bindings,
            const context::ResolvedContextSnapshot& context,
            std::uint64_t ownerNowUs);

        void RegisterSyntheticSuppression(
            const SyntheticKeyboardSuppressionToken& token);

        void EnterQuarantine(
            InputResetReasonMask reasons,
            std::uint32_t contextRevision,
            std::uint32_t controlMapRevision);

        void ResetSyntheticSuppression(InputResetReasonMask reasons) noexcept;
    };
}

```

边界规则：

- `SkyrimKbmInputAdapter`是唯一接触`RE::InputEvent`和Skyrim raw current-state API的组件。
- `KbmGameplayFactProducer`不include `RE::*`，也不依赖`src/input/`命名空间中的平台类型；它不持有owner、gate、presentation、engine或output authority。
- `KbmGameplayCurrentFacts` 和 `KbmPhysicalLedger` 是 complete latest-wins snapshot；press/release/mouse delta 是 ordered。raw down 与 quarantine 始终按稳定 `KbmPhysicalCode(device,idCode)` 保存，binding index 只能用于同一 immutable snapshot 内的临时查表。
- ControlMap revision变化时重建immutable `KbmBindingSnapshot`，event热路径不反复调用`ControlMap::GetMappedKey`，也不硬编码WASD或鼠标键。
- 只有 `provenanceMode != Unproven` 且 I-KBM 对应证据通过的 synthetic event 才能被 suppression；否则按 physical event 处理，或关闭冲突 helper route。
- global reset 或 mapping reload 把当前稳定 physical down codes 并入 quarantine，再清 semantic masks。替换 binding snapshot 后仍保留旧 physical codes，直到其 release/fresh initial-press 证据出现。
- quarantine 中的 held-repeat 不重新 armed；对应 `(device,idCode)` release 清位。若 release 丢失，同码 fresh initial-press epoch 先证明中间发生过 up，再清 quarantine 并把该 press 作为新周期处理。
- 经I-KBM证明的provider可在`physicalOnlyProvenance=true && complete=true`时生成`ReconciledRelease`。provider不可用或不完整时，继续使用quarantine，不以时间自动解封。

### D.3 Context-neutral meaningful source activity与runtime routing

新增 `src/input_v2/ingress/MeaningfulSourceActivity.h`：

```cpp
namespace dualpad::input_v2::ingress
{
    enum class SourceActivityKind : std::uint8_t
    {
        KeyboardPress = 0,
        KeyboardCharacter,
        MouseButtonPress,
        MouseWheel,
        MouseDelta,
        GamepadButtonPress,
        GamepadAnalogEnter,
        GamepadAnalogChange,
        GamepadTouchPress,
        ExplicitResync
    };

    enum class SourceActivityDomain : std::uint8_t
    {
        Unclassified = 0,
        Gameplay,
        MenuNavigation,
        MenuPointer
    };

    struct MeaningfulSourceActivityDraft
    {
        PhysicalInputSource source{ PhysicalInputSource::None };
        SourceActivityKind kind{ SourceActivityKind::KeyboardPress };
        std::uint32_t controlCode{ 0 };
        std::int32_t deltaX{ 0 };
        std::int32_t deltaY{ 0 };
        std::uint64_t producerTimestampUs{ 0 };
    };

    struct MeaningfulSourceActivity : MeaningfulSourceActivityDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };

    struct SourceActivityRoutingState
    {
        std::int32_t mouseAccumulatedManhattan{ 0 };
        std::uint64_t mouseWindowStartOwnerMs{ 0 };
        std::uint64_t lastAcceptedPointerSeq{ 0 };
        std::uint64_t lastCompetingStrongSeq{ 0 };
        std::optional<MeaningfulSourceActivity> pendingPointerActivity;
        bool pointerPromotionEmitted{ false };
    };

    struct RoutedSourceActivity
    {
        MeaningfulSourceActivity activity{};
        SourceActivityDomain domain{ SourceActivityDomain::Unclassified };
        std::optional<GameplayChannel> gameplayChannel;
        bool qualifiesForPrompt{ false };
        bool strongForMenuOwner{ false };
        bool qualifiesForCursor{ false };
        bool qualifiedByOwnerTimer{ false };
    };

    struct RoutedSourceActivityFrame
    {
        std::vector<RoutedSourceActivity> activities;
        SourceActivityRoutingState next{};
    };

    RoutedSourceActivityFrame RouteSourceActivities(
        std::span<const MeaningfulSourceActivity> orderedActivities,
        const SourceActivityRoutingState& previous,
        const context::ResolvedContextSnapshot& context,
        const actions::ResolvedActionFrame& resolvedActions,
        std::uint64_t ownerNowMs);
}
```

资格规则：

- HID worker和KBM producer只生成context-neutral draft，Hub统一赋`ingressSeq`。
- release-only、unchanged held、neutral report、reconciled release、synthetic-suppressed event不进入source activity流。
- gameplay中的任意非零physical mouse delta立即路由为Look活动，并可改变prompt。
- menu 中的 pointer-only mouse delta 先进入 owner-local accumulator。累计 Manhattan 距离达到 `>=10 px` 后保存最后一条 pointer activity 作为 candidate；每个 owner tick 都检查 `ownerNowMs >= windowStart + 120 ms`。达到 deadline 时可用 candidate 原始 `ingressSeq` 生成一次 `qualifiedByOwnerTimer=true` 的 routed record，因此不要求再来一条 mouse event。
- candidate 之后若出现更高 `ingressSeq` 的 strong source、context/boundary reset 或 candidate 已 promotion，则清除/消费 pending 状态；同一 candidate 不得重复 promotion。
- pointer-only永远`strongForMenuOwner=false`。
- keyboard character/button、mouse button/wheel、gamepad button/D-pad和navigation stick跨enter可`strongForMenuOwner=true`。
- 同一owner tick有多条合格activity时，各projection选择最高`ingressSeq`，不比较producer timestamp。
- `MeaningfulSourceActivity.gamepadSessionId`只对`source=Gamepad`有约束意义；Keyboard/Mouse activity写0，纯gamepad session变化不得使已提交的KBM activity或KBM held失效。

### D.4 Gamepad connectivity、current-state、digital edge与meaningful activity

新增 `src/input_v2/ingress/GamepadActivityClassifier.h`，扩展现有唯一`LatestPadState`，不创建平行current-state authority：

```cpp
namespace dualpad::input_v2::ingress
{
    enum class GamepadConnectivity : std::uint8_t
    {
        Disconnected = 0,
        Connected
    };

    struct GamepadConnectionDraft
    {
        GamepadConnectivity connectivity{ GamepadConnectivity::Disconnected };
    };

    struct GamepadConnectionFacts
    {
        CausalLatestHeader causal{};
        GamepadConnectivity connectivity{ GamepadConnectivity::Disconnected };
        std::uint64_t gamepadSessionId{ 0 };
    };

    struct GamepadCurrentStateDraft
    {
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        input::PadState state{};
        std::uint32_t currentDownMask{ 0 };
    };

    struct LatestPadState
    {
        std::uint64_t generation{ 0 };
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        input::InputContext context{ input::InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t currentDownMask{ 0 };
        input::PadState state{};
        std::uint64_t causalOrderedTailSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        bool virtualGameplayEligible{ true };
        InputResetReasonMask recoveryReasons{ 0 };
    };

    enum class GamepadDigitalEdgePhase : std::uint8_t
    {
        Press = 0,
        Release
    };

    struct GamepadDigitalEdgeDraft
    {
        GamepadDigitalEdgePhase phase{ GamepadDigitalEdgePhase::Press };
        std::uint32_t controlCode{ 0 };
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
    };

    struct GamepadDigitalEdge : GamepadDigitalEdgeDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };

    enum class GamepadActivityReason : std::uint8_t
    {
        None = 0,
        DigitalPress,
        LeftStickEntered,
        LeftStickChanged,
        RightStickEntered,
        RightStickChanged,
        LeftTriggerEntered,
        LeftTriggerChanged,
        RightTriggerEntered,
        RightTriggerChanged,
        TouchpadActivity
    };

    struct GamepadActivityDraft
    {
        GamepadActivityReason reason{ GamepadActivityReason::None };
        std::uint32_t controlCode{ 0 };
        float magnitude{ 0.0F };
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
    };

    struct GamepadMeaningfulActivity : GamepadActivityDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };

    struct ClassifiedGamepadReportDraft
    {
        GamepadCurrentStateDraft current{};
        std::vector<GamepadDigitalEdgeDraft> orderedDigitalEdges;
        std::vector<GamepadActivityDraft> meaningfulActivities;
        std::vector<MeaningfulSourceActivityDraft> sourceActivities;
    };

    class GamepadActivityClassifier
    {
    public:
        ClassifiedGamepadReportDraft Classify(
            const input::PadState& previous,
            const input::PadState& current,
            std::uint64_t sourceSequence,
            std::uint64_t sourceTimestampUs) const;

        void Reset(InputResetReasonMask reasons) noexcept;
    };
}
```

分类规则：

- 每份成功report更新`LatestPadState`。
- connect/disconnect只更新`GamepadConnectionFacts`和session，不生成presentation activity。
- digital press进入ordered edge并生成activity；release只进入ordered edge，不生成activity。
- analog从neutral跨enter产生activity。
- 已active analog只有向量L1变化`>=0.12`，或方向变化`>=12°`且magnitude`>=0.25`时生成下一条activity。
- trigger已active时绝对变化`>=0.08`才生成下一条activity。
- unchanged held、回中、release和neutral都不生成activity。
- activity阈值只控制presentation/prompt事件频率，不控制gameplay current-state和owner sustain。
- HID classifier 不写 `GameplayChannel`、`ActionId`、context 或 menu 语义。`GamepadDigitalEdgeDraft` 与 `GamepadActivityDraft` 只携带 raw `controlCode/reason/phase`；Hub 排序后由 `RouteSourceActivities`/resolved action graph 在 owner tick 映射到 gameplay channel 和 action。

### D.5 Ingress transactional batch、capture cutoff与stable frame

扩展 `src/input_v2/ingress/IngressHub.h`：

```cpp
namespace dualpad::input_v2::ingress
{
    struct PublishedIngressBatchReceipt
    {
        bool accepted{ false };
        InputResetReasonMask publishedResetReasons{ 0 };
        InputResetScope publishedResetScope{ InputResetScope::GlobalInputState };
        std::uint64_t firstOrderedSeq{ 0 };
        std::uint64_t causalOrderedTailSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    struct IngressBoundaryObservation
    {
        // contextRevision/menuStackRevision come only from ContextResolver.
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint64_t controlMapFingerprint{ 0 };
        std::uint64_t bindingGeneration{ 0 };
    };

    struct OwnerKbmIngressDraft
    {
        IngressBoundaryObservation boundary{};
        KbmGameplayIngressBatchDraft kbm{};
    };

    struct IngressCapture
    {
        std::uint64_t generation{ 0 };
        std::vector<IngressEvent> events;
        std::optional<LatestPadState> latestPadState;
        std::optional<LatestSourceEvidence> latestSourceEvidence;
        std::size_t remainingEvents{ 0 };
        std::uint64_t orderedCutoffSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::optional<GamepadConnectionFacts> latestGamepadConnection;
        std::optional<LatestKbmGameplayFacts> latestKbmGameplay;
    };

    class IngressHub
    {
    public:
        PublishedIngressBatchReceipt PublishOwnerKbmBatch(
            OwnerKbmIngressDraft batch);

        PublishedIngressBatchReceipt PublishGamepadBatch(
            ClassifiedGamepadReportDraft report,
            std::optional<GamepadConnectionDraft> connection);

        PublishedIngressBatchReceipt PublishGlobalReset(
            InputResetReasonMask reasons,
            InputResetScope scope);

        PublishedIngressBatchReceipt PublishGamepadDisconnect();
        IngressCapture Capture(std::size_t maxEvents);
    };
}
```

事务合同：

1. Hub 在一个 mutex 临界区完成 boundary reconciliation、容量检查、ordered seq 分配、latest generation 分配和 latest commit。
2. `ContextResolver` 是 `contextRevision/menuStackRevision` 的唯一来源；Hub 只把其 published snapshot 合并进现有 `IngressBoundaryKey`。`controlMapRevision` 由 Hub 在同一事务中根据 fingerprint 变化单调推进，并写入扩展后的 `IngressBoundaryKey`，不得创建平行 context authority。
3. `PublishOwnerKbmBatch` 同时接收 boundary observation 与 KBM draft。若 context 或 ControlMap fingerprint 变化，它先在同一临界区发布 boundary/reset marker、推进 epoch/revision，再用新 key 给 binding generation、ordered edges 和 latest facts 盖章。第一批新映射事件不会先按旧 revision 发布再被拒绝。
4. 有 ordered records 时，latest 的 `causalOrderedTailSeq` 等于 batch 最后一条 seq。latest-only report 不分配新 ordered seq，tail 复用 Hub 最近一条已分配 ordered seq。
5. Hub 保存累计 `_lastConsumedOrderedSeq`。`Capture` drain 到事件时推进它；空 capture 保持原值。返回的 `orderedCutoffSeq` 永远等于该累计值，不能因本轮未 drain 而回退到 0。
6. `FrameAssembler` 只应用 `causalOrderedTailSeq <= orderedCutoffSeq` 且 epoch/session/revision 匹配的 latest。ahead-of-cutoff latest 被 defer，不触发 reset，也不提前应用。
7. overflow 先做容量检查。容量不足时原子发布 `QueueOverflow + GlobalInputState` recovery marker；触发批次的 ordered edge/activity 及其 semantic latest 不提交。若批次含完整物理 pad current-state/analog，则可将其作为新 epoch、`virtualGameplayEligible=false` 的 recovery-stamped latest 保存，避免 analog/current-state 冻结；在 post-marker coherent facts 到达前不得驱动 virtual action。
8. gamepad disconnect 在同一事务递增 session 并清 gamepad connection/current/activity slots，不递增 global epoch，也不清 KBM latest。
9. context、ControlMap、focus、sequence gap、explicit reset 和 runtime owner failure 使用 global reset 并递增 epoch；ControlMap boundary 必须通过 `PublishOwnerKbmBatch` 的联合事务，而不是先单独 reset 再另发首批事件。
10. Hub 根据 boundary observation 验证/stamp revision；producer 不自行选择 epoch/session/controlMapRevision。receipt 返回 Hub 实际 epoch/session/revision 供诊断。
11. `docs/runtime_backpressure_contract.md` 仍是 backpressure current truth：overflow 不制造 ordered action，也不把旧 held 重新附着，但必须保留可安全保存的最新物理 analog/current-state。

扩展 `FrameAssembler` stable frame：

```cpp
struct InputFactCoherenceKey
{
    std::uint64_t captureGeneration{ 0 };
    std::uint64_t orderedCutoffSeq{ 0 };
    std::uint64_t inputStateEpoch{ 0 };
    std::uint64_t gamepadSessionId{ 0 };
    std::uint64_t manifestEpoch{ 0 };
    std::uint32_t contextRevision{ 0 };
    std::uint32_t menuStackRevision{ 0 };
    std::uint32_t controlMapRevision{ 0 };
};
```

KBM facts进入stable frame的条件：

- causal tail不晚于cutoff。
- epoch/context/control-map revision匹配。
- `current.complete=true`。
- baseline=`Clean`。
- quarantine已drained。
- 若I-KBM provider启用，raw current还必须`physicalOnlyProvenance=true && complete=true`。

条件不满足时stable frame仍携带诊断facts，但virtual gameplay fail-closed，不旁读旧singleton补洞。

### D.6 Per-channel next-Poll仲裁

新增 `src/input_v2/gameplay/ChannelArbitration.h`：

```cpp
namespace dualpad::input_v2::gameplay
{
    enum class ChannelOwner : std::uint8_t
    {
        Gamepad = 0,
        KeyboardMouse = 1,
        None = 2
    };

    enum class ChannelDecisionReason : std::uint8_t
    {
        None = 0,
        NativeActive,
        NativeUnsuppressiblePrecedence,
        NativeQuietHysteresis,
        GamepadEntered,
        GamepadSustained,
        GamepadCandidateReclaimed,
        BothInactive,
        GamepadSourceReset,
        HardReset
    };

    enum class ChannelResetMode : std::uint8_t
    {
        None = 0,
        GamepadSource,
        Global
    };

    struct ChannelThresholds
    {
        float enter{ 0.25F };
        float sustain{ 0.15F };
    };

    struct ChannelArbitrationPolicy
    {
        ChannelThresholds gamepad{};
        std::uint64_t nativeQuietUs{ 0 };
        bool physicalNativeWinsWhileActive{ true };
    };

    struct ChannelSignal
    {
        bool currentlyActive{ false };
        bool physicalHeld{ false };
        bool activationEdge{ false };
        float magnitude{ 0.0F };
        std::uint64_t latestOrderedSeq{ 0 };
    };

    struct ChannelArbitrationState
    {
        ChannelOwner owner{ ChannelOwner::None };
        bool gamepadCandidateLatched{ false };
        std::uint64_t mouseQuietUntilOwnerUs{ 0 };
        std::uint64_t ownerSinceSeq{ 0 };
    };

    struct ChannelArbitrationInput
    {
        ingress::GameplayChannel channel{ ingress::GameplayChannel::Look };
        ChannelArbitrationState previous{};
        ChannelSignal keyboardMouse{};
        ChannelSignal gamepad{};
        std::uint64_t ownerNowUs{ 0 };
        ChannelResetMode resetMode{ ChannelResetMode::None };
    };

    struct ChannelArbitrationDecision
    {
        ChannelOwner owner{ ChannelOwner::None };
        AnalogGateMode analogGate{ AnalogGateMode::Open };
        DigitalGateMode digitalGate{ DigitalGateMode::Open };
        ChannelDecisionReason reason{ ChannelDecisionReason::None };
        ChannelArbitrationState next{};
    };

    ChannelArbitrationPolicy LookArbitrationPolicy() noexcept;
    ChannelArbitrationPolicy MoveArbitrationPolicy() noexcept;
    ChannelArbitrationPolicy CombatArbitrationPolicy() noexcept;

    ChannelArbitrationDecision ResolveChannel(
        const ChannelArbitrationInput& input,
        const ChannelArbitrationPolicy& policy) noexcept;
}
```

固定策略：

```cpp
Look:   enter 0.25F, sustain 0.15F, nativeQuietUs 200'000
Move:   enter 0.25F, sustain 0.15F, nativeQuietUs 0
Combat: enter 0.15F, sustain 0.08F, nativeQuietUs 0
```

状态规则：

- `Global` reset 返回 `None` 并清 candidate/quiet state。
- `GamepadSource` reset 清 gamepad candidate 和 virtual gate；若 previous owner 是 Gamepad，则返回 `None`，若 previous owner 是 KeyboardMouse 且 physical facts 仍 complete，则保留 KBM owner/quiet state。它不得清除 KBM-owned channel。
- physical active立即KBM获胜。
- physical active期间，gamepad跨enter或原owner为Gamepad且仍`>=sustain`时latch candidate。
- physical release时，latched且`>=sustain`可同tickGamepad reclaim；未latched要求enter。
- Look的physical mouse delta刷新`mouseQuietUntilOwnerUs=ownerNowUs+200'000`。quiet未到时保持KBM。
- 双方inactive返回`None`并neutral virtual通道。
- activity event不参与sustain；完整current-state参与。

### D.7 Current-cycle独立时间视图、Poll receipt与materialized identity

新增 `src/input_v2/gameplay/CurrentCycleGatePlan.h`：

```cpp
namespace dualpad::input_v2::gameplay
{
    struct MaterializedPollIdentity
    {
        std::uint64_t pollCallSequence{ 0 };
        std::uint32_t pollThreadId{ 0 };
        std::uint64_t publicationGeneration{ 0 };
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t packetNumber{ 0 };
    };

    struct PollMaterializationReceipt
    {
        MaterializedPollIdentity identity{};
        std::uintptr_t verifiedCallsite{ 0 };
        std::uint32_t xinputResult{ 0 };
        bool dualPadRouteSelected{ false };
        bool serializationSucceeded{ false };
    };

    enum class CurrentCycleSemanticPhase : std::uint8_t
    {
        StateSample = 0,
        Press,
        Release,
        Repeat
    };

    enum class CurrentCycleEventDisposition : std::uint8_t
    {
        Keep = 0,
        SuppressVirtualValue,
        SuppressPress,
        SuppressRelease,
        CancelSyntheticDown
    };

    enum class CurrentCycleWriterReason : std::uint8_t
    {
        None = 0,
        PhysicalCurrentActive,
        PhysicalActivationEdge,
        NativeQuietHysteresis,
        MaterializedVirtualActive,
        HardBoundary,
        NoMaterializedWriter
    };

    struct CurrentCycleEventDescriptor
    {
        std::uint32_t eventOrdinal{ 0 };
        ingress::PhysicalInputSource source{ ingress::PhysicalInputSource::None };
        ingress::GameplayChannel channel{ ingress::GameplayChannel::TransientDigital };
        actions::ActionId actionId{};
        std::uint32_t controlCode{ 0 };
        CurrentCycleSemanticPhase phase{ CurrentCycleSemanticPhase::StateSample };
        float materializedMagnitude{ 0.0F };
        bool activationEdge{ false };
        bool fromDualPadVirtualRoute{ false };
    };

    struct CurrentCycleChannelInput
    {
        ingress::GameplayChannel channel{ ingress::GameplayChannel::Look };
        ChannelArbitrationState previous{};
        ChannelArbitrationDecision nextPollDecision{};
        bool physicalCurrentActive{ false };
        bool physicalActivationEdge{ false };
        bool materializedVirtualActive{ false };
        bool hardBoundary{ false };
    };

    struct CurrentCycleChannelDecision
    {
        ingress::GameplayChannel channel{ ingress::GameplayChannel::Look };
        ChannelOwner writer{ ChannelOwner::None };
        bool suppressMaterializedVirtual{ false };
        CurrentCycleWriterReason reason{ CurrentCycleWriterReason::None };
    };

    CurrentCycleChannelDecision ResolveCurrentCycleWriter(
        const CurrentCycleChannelInput& input) noexcept;

    struct CurrentCycleGatePlan
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        MaterializedPollIdentity materializedPoll{};
        std::uint64_t sourceBatchTailSeq{ 0 };
        std::uint64_t assembledOrderedCutoffSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        bool routeOperational{ false };
        bool physicalFactsComplete{ false };
        std::array<CurrentCycleChannelDecision, 3> channelDecisions{};
        std::array<actions::ActionId, 32> suppressTransientActions{};
        std::size_t suppressTransientCount{ 0 };
        std::array<actions::ActionId, 8> suppressSustainedPressActions{};
        std::size_t suppressSustainedPressCount{ 0 };
        std::array<actions::ActionId, 8> suppressSustainedReleaseActions{};
        std::size_t suppressSustainedReleaseCount{ 0 };
    };

    enum class CurrentCycleGateFailure : std::uint8_t
    {
        None = 0,
        TokenMismatch,
        EventBatchMismatch,
        PollFrameIdentityUnproven,
        PollReceiptMissing,
        PollReceiptAmbiguous,
        PollReceiptAlreadyConsumed,
        PollThreadMismatch,
        PollFrameMismatch,
        EpochMismatch,
        GamepadSessionMismatch,
        ContextMismatch,
        ControlMapMismatch,
        BatchNotAssembled,
        PhysicalFactsIncomplete,
        RouteNotOperational,
        PreDispatchConsumerDetected,
        UnsafeEventType,
        AdapterUnavailable,
        DispositionBufferMismatch
    };

    struct CurrentCycleGateObservation
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        PollMaterializationReceipt pollReceipt{};
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        bool materializedPollIdentityProven{ false };
        bool pollReceiptConsumedExactlyOnce{ false };
        bool routeOperationalNow{ false };
        bool adapterOperational{ false };
        bool physicalFactsComplete{ false };
        bool preDispatchConsumerObserved{ false };
    };

    struct CurrentCycleGateAudit
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t eventBatchToken{ 0 };
        bool mutationRequired{ false };
        bool applied{ false };
        bool commitCurrentCycleSensitiveState{ false };
        std::uint32_t suppressedCount{ 0 };
        CurrentCycleGateFailure failure{ CurrentCycleGateFailure::None };
    };
}
```

平台adapter：

```cpp
namespace dualpad::input
{
    struct SkyrimCurrentCycleEventBinding
    {
        std::uint32_t eventOrdinal{ 0 };
        RE::InputEvent* event{ nullptr }; // callback-local only
    };

    class SkyrimCurrentCycleEventAdapter
    {
    public:
        input_v2::gameplay::CurrentCycleGateAudit ApplyVerifiedDispositions(
            std::span<const SkyrimCurrentCycleEventBinding> bindings,
            std::span<const input_v2::gameplay::CurrentCycleEventDisposition> dispositions,
            const input_v2::gameplay::CurrentCycleGatePlan& plan,
            const input_v2::gameplay::CurrentCycleGateObservation& observation) noexcept;
    };
}
```

硬规则：

1. `UpstreamGamepadHook` 在已验证的 Skyrim `0x140C1AB9D` XInput callsite 成功序列化后，发布 immutable `PollMaterializationReceipt`。receipt 至少包含 call sequence、poll thread、publication/runtime/epoch/session identity、packet、route 结果和 serialization 结果。
2. `InputFramePump` 在任何 owner mutation 前，从有界 receipt mailbox 消费与当前 callback 同线程、尚未消费且唯一的一条 receipt；不得再次 `AcquireForPoll()` 并把“当前最新帧”冒充实际 materialized 帧。缺失、多条、重复消费或线程不匹配都返回明确 failure，并保持 current-cycle capability `NO-GO`。
3. current-cycle decision 只使用 callback-local physical current/activation、上一 channel state、receipt 证明的 materialized virtual event、明确的 200 ms Look quiet 和 hard boundary。
4. next-Poll decision 可使用 owner tick 期间组装出的最新 HID state；current-cycle 不得被这些 later facts 倒灌改写。
5. physical release 不是 activation edge，不得阻止本 cycle 已经 materialize 的 gamepad winner 接管。
6. adapter 不合成缺失 event，不 unlink head，不跨 callback 保存 pointer。
7. 只修改 `fromDualPadVirtualRoute=true` 且被 plan 明确判输的 event。物理 KBM、其它 gamepad 通道和 original fallback 永远 Keep。
8. token、receipt、epoch、session、revision、tail/cutoff、route、adapter 和 consumer 顺序任一不匹配，零修改并返回明确 failure。
9. runtime 必须执行同步 `Prepare -> Apply -> Commit`：先生成 provisional next state、next Poll frame 和 callback-local gate plan，但不推进 channel/transient/sustained/current-cycle-sensitive ledger，也不发布这些 provisional 结果；adapter 在同一 `InputFramePump` 调用栈返回 audit 后，runtime 才调用 `CommitPreparedFrame(audit)`。
10. 当 mutation required 且 audit 未成功 apply 时，`commitCurrentCycleSensitiveState=false`：保留上一份 transient/sustained/current-cycle ledger，下一 Poll 对受影响通道 fail-closed，并记录 recovery/capability fault。不得出现“adapter 失败但状态已推进”。无需 mutation 的 plan 可显式返回 commit-safe。
11. shadow 阶段可提交与 current-cycle 无关的 latest/current-state 诊断，但不得把依赖 event mutation 的 transient/Sprint 状态提升为 production authority。
12. normal arbitration 要求 complete physical facts。global boundary 依赖 epoch 前进，gamepad disconnect 依赖 session 前进，两者仍必须证明旧 materialized route identity。
13. production adapter 只有在 I-P 通过后编入 enable 路径；此前只生成 shadow decision 和 audit。

### D.8 Transient action gate

新增 `src/input_v2/gameplay/TransientActionGate.h`：

```cpp
namespace dualpad::input_v2::gameplay
{
    struct TransientDedupKey
    {
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        actions::ActionId actionId{};
        std::uint64_t ownerTickToken{ 0 };
    };

    struct TransientActionGateState
    {
        actions::ActionId actionId{};
        std::uint64_t activeSyntheticPulseToken{ 0 };
        bool syntheticDownPublished{ false };
        TransientDedupKey lastApplied{};
    };

    struct TransientActionGateInput
    {
        TransientDedupKey key{};
        bool physicalPressPresent{ false };
        bool gamepadPressPresent{ false };
        TransientActionGateState previous{};
    };

    struct TransientActionGateDecision
    {
        bool emitGamepadPulse{ false };
        bool cancelSyntheticPulse{ false };
        bool suppressVirtualInputEvent{ false };
        TransientActionGateState next{};
    };

    TransientActionGateDecision ResolveTransientAction(
        const TransientActionGateInput& input) noexcept;
}
```

规则：

- 同key physical press优先。
- virtual pulse已down-visible且physical press到达时，先输出一次cancel/up，再禁止新virtual press。
- release-only不当作physical activation竞争者。
- dedup state 只在 runtime owner 内可变；`ResolveTransientAction` 产生的 `next` 先进入 prepared commit，只有 current-cycle audit 为 commit-safe 后才替换正式 state。adapter 只 materialize 已裁决 disposition。
- context、epoch或hard reset立即清state和pending pulse。

### D.9 Sustained contributor、virtual bridge与唯一backend materialization

新增 `src/input_v2/gameplay/SustainedContributorDecision.h`：

```cpp
namespace dualpad::input_v2::gameplay
{
    enum class SustainedContributorBit : std::uint8_t
    {
        None = 0,
        GamepadResolved = 1 << 0,
        KeyboardPhysical = 1 << 1,
        MousePhysical = 1 << 2
    };

    enum class SustainedEdgeEmitter : std::uint8_t
    {
        None = 0,
        GamepadVirtual,
        KeyboardPhysical,
        MousePhysical
    };

    struct SustainedContributorInput
    {
        actions::ActionId actionId{};
        std::uint8_t previousSourceMask{ 0 };
        std::uint8_t activeSourceMask{ 0 };
        bool previousVirtualMaterialized{ false };
        SustainedEdgeEmitter previousEffectiveEmitter{ SustainedEdgeEmitter::None };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
    };

    struct SustainedContributorDecision
    {
        actions::ActionId actionId{};
        std::uint8_t previousSourceMask{ 0 };
        std::uint8_t activeSourceMask{ 0 };
        std::uint8_t joiningSourceMask{ 0 };
        std::uint8_t leavingSourceMask{ 0 };
        std::uint8_t suppressCurrentPressMask{ 0 };
        std::uint8_t suppressCurrentReleaseMask{ 0 };
        bool aggregateHeld{ false };
        bool aggregatePressed{ false };
        bool aggregateReleased{ false };
        bool virtualBridgeDesired{ false };
        bool armOneShotVirtualReleaseSuppression{ false };
        SustainedEdgeEmitter effectiveEmitter{ SustainedEdgeEmitter::None };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
    };

    SustainedContributorDecision ResolveSustainedContributor(
        const SustainedContributorInput& input) noexcept;
}
```

固定逻辑：

```cpp
const bool previousAggregateHeld = input.previousSourceMask != 0;
const bool aggregateHeld = input.activeSourceMask != 0;
const bool gamepadHeld = HasSource(input.activeSourceMask,
                                   SustainedContributorBit::GamepadResolved);
const bool physicalHeld =
    HasSource(input.activeSourceMask, SustainedContributorBit::KeyboardPhysical) ||
    HasSource(input.activeSourceMask, SustainedContributorBit::MousePhysical);
const bool virtualBridgeDesired =
    gamepadHeld || (input.previousVirtualMaterialized && physicalHeld);
```

`SustainedContributorDecision` 的 `next source mask`、virtual materialized 标志和 one-shot release token 同样属于 current-cycle-sensitive prepared state。若 joining/non-final edge disposition 需要修改当前 event 而 adapter 未成功，上一份 contributor state 必须保留，受影响 virtual output 进入 fail-closed recovery，不能先推进 mask 再报告失败。

规则：

- keyboard/mouse-only从0开始不制造virtual press。
- aggregate从0到非0只允许一个initial press。若同batch多source press，优先保留最早physical press；无physical时保留gamepad press。
- aggregate已held时，新source joining press不再次触发Sprint handler。
- 任一source release而remaining mask非0时，不允许结束aggregate。
- virtual一旦因真实gamepad contributor materialize，只要physical contributor仍held就保持bridge。
- mask从非0到0只允许一次final release。
- one-shot virtual release suppression token绑定`ActionId + inputStateEpoch + contextRevision + runtimeGeneration + expected phase`，匹配一次即消费，boundary/reset清除。
- `PollCommitCoordinator::PollCommitSlot`继续是唯一mutable backend materialization authority，不新建parallel singleton。

backend接口：

```cpp
struct SustainedMaterializationResult
{
    std::uint8_t activeSourceMask{ 0 };
    HeldEmitterSource activeEmitter{ HeldEmitterSource::None };
    bool virtualMaterialized{ false };
    bool virtualSet{ false };
    bool virtualClear{ false };
};

SustainedMaterializationResult PollCommitCoordinator::SyncHeldContributors(
    std::string_view actionId,
    std::uint8_t activeSourceMask,
    bool virtualBridgeDesired,
    std::uint64_t inputStateEpoch,
    std::uint32_t contextRevision,
    std::uint64_t runtimeGeneration);
```

必须一次性替换完整mask，禁止按三个source依次调用制造中间态。

### D.10 Presentation原子快照、cursor transaction与prompt family

新增/重构 `src/input_v2/presentation/PresentationProjection.h`：

```cpp
namespace dualpad::input_v2::presentation
{
    enum class PromptFamilyDecisionReason : std::uint8_t
    {
        CarryPrevious = 0,
        KeyboardMouseMeaningfulActivity,
        GamepadMeaningfulActivity,
        GamepadUnavailableFallback,
        RecoveryFrozen,
        ExplicitResync
    };

    enum class MenuOwnerDecisionReason : std::uint8_t
    {
        CarryPrevious = 0,
        GameplayMenuEntryOwner,
        KeyboardMouseStrongActivity,
        GamepadStrongActivity,
        RecoveryFrozen
    };

    enum class CursorOwnerDecisionReason : std::uint8_t
    {
        CarryPrevious = 0,
        MousePointerThreshold,
        MenuOwnerFallback,
        HandoffPending,
        HandoffCommitted,
        RecoveryFrozen
    };

    struct PromptFamilyDecision
    {
        DeviceFamily family{ DeviceFamily::KeyboardMouse };
        std::uint32_t revision{ 0 };
        std::uint64_t acceptedActivitySeq{ 0 };
        PromptFamilyDecisionReason reason{ PromptFamilyDecisionReason::CarryPrevious };
    };

    struct MenuPresentationDecision
    {
        PresentationOwner owner{ PresentationOwner::KeyboardMouse };
        NavigationOwner navigationOwner{ NavigationOwner::KeyboardMouse };
        std::uint64_t acceptedActivitySeq{ 0 };
        MenuOwnerDecisionReason reason{ MenuOwnerDecisionReason::CarryPrevious };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t epoch{ 0 };
    };

    struct CursorDecision
    {
        CursorOwner requestedOwner{ CursorOwner::KeyboardMouse };
        CursorOwner committedOwner{ CursorOwner::KeyboardMouse };
        std::uint64_t acceptedActivitySeq{ 0 };
        CursorOwnerDecisionReason reason{ CursorOwnerDecisionReason::CarryPrevious };
        bool positionSyncRequired{ false };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t epoch{ 0 };
    };

    struct PublishedPresentationState
    {
        PromptFamilyDecision prompt{};
        MenuPresentationDecision menu{};
        CursorDecision cursor{};
        ingress::SourceActivityRoutingState activityRouting{};
        std::uint32_t presentationEpoch{ 0 };
    };

    struct CursorHandoffPlan
    {
        std::uint64_t token{ 0 };
        CursorOwner from{ CursorOwner::KeyboardMouse };
        CursorOwner to{ CursorOwner::KeyboardMouse };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t presentationEpoch{ 0 };
        std::uint64_t targetMenuInstanceId{ 0 };
        std::uintptr_t targetMenuPtr{ 0 };
        std::uintptr_t targetMoviePtr{ 0 };
    };

    enum class CursorHandoffFailure : std::uint8_t
    {
        None = 0,
        MenuIdentityMismatch,
        MovieIdentityMismatch,
        MappingUnverified,
        PositionUnavailable,
        CoordinateOutOfRange,
        WriteVerificationFailed
    };

    struct CursorHandoffAck
    {
        std::uint64_t token{ 0 };
        std::uint64_t targetMenuInstanceId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t presentationEpoch{ 0 };
        bool positionSynchronized{ false };
        float appliedNativeX{ 0.0F };
        float appliedNativeY{ 0.0F };
        CursorHandoffFailure failure{ CursorHandoffFailure::None };
    };

    struct PresentationProjectionInput
    {
        const PublishedPresentationState& previous;
        const context::ResolvedContextSnapshot& context;
        PresentationOwner gameplayMenuEntryOwner{ PresentationOwner::KeyboardMouse };
        std::span<const ingress::RoutedSourceActivity> routedActivities;
        std::optional<CursorHandoffAck> cursorAck;
        ingress::InputResetReasonMask resetReasons{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t ownerTickToken{ 0 };
    };

    struct PresentationProjectionDecision
    {
        PublishedPresentationState state{};
        std::optional<CursorHandoffPlan> cursorPlan;
        bool refreshTargetMenu{ false };
    };

    PresentationProjectionDecision ProjectPresentation(
        const PresentationProjectionInput& input) noexcept;
}
```

平台side-effect adapter：

```cpp
namespace dualpad::input
{
    class SkyrimCursorHandoffAdapter
    {
    public:
        input_v2::presentation::CursorHandoffAck ExecuteVerifiedHandoff(
            const input_v2::presentation::CursorHandoffPlan& plan) noexcept;
    };
}
```

Ack 回交 mailbox：

```cpp
namespace dualpad::input_v2::presentation
{
    struct CursorHandoffAckEnvelope
    {
        std::uint64_t ackSequence{ 0 };
        CursorHandoffAck ack{};
    };

    class CursorHandoffAckMailbox
    {
    public:
        void PublishFromUiTask(CursorHandoffAck ack);
        std::optional<CursorHandoffAckEnvelope> ConsumeExactOnOwnerTick(
            std::uint64_t token,
            std::uint32_t contextRevision,
            std::uint32_t presentationEpoch,
            std::uint64_t targetMenuInstanceId);

    private:
        // Bounded, monotonic and consume-once; UI task never mutates owner state.
    };
}
```

规则：

- prompt/menu/cursor读取同一`RoutedSourceActivityFrame`，但使用不同资格位。
- prompt family变化不能回写其它projection。
- cursor handoff plan中的pointer只作identity比较，UI task必须重新取得live对象。
- UI task 只做坐标 side effect，并把 ack 发布到 `CursorHandoffAckMailbox`，不能直接 commit owner，也不把 UI ack 混入 raw input queue。
- 下一 owner tick 从 mailbox consume-once，验证 ack sequence、token、menu/movie identity、context revision 和 presentation epoch 后，才把 ack 放入 `PresentationProjectionInput` 并 commit cursor。stale/duplicate ack 丢弃并记录原因。
- 每个方向是否需要同步、同步哪一侧坐标由I-CURSOR裁决；证据未闭合时保持旧owner。

### D.11 Device availability、engine query、caller规则与runtime publication

新增 `src/input_v2/runtime/EngineModeDecision.h`：

```cpp
namespace dualpad::input_v2::runtime
{
    enum class GamepadDeviceAvailabilityPolicy : std::uint8_t
    {
        Native = 0,
        ScopedConnectivity
    };

    enum class GamepadAvailabilityDomain : std::uint8_t
    {
        None = 0,
        VerifiedPollOrInitialization,
        Remap
    };

    struct GamepadDeviceAvailabilityDecision
    {
        GamepadDeviceAvailabilityPolicy policy{
            GamepadDeviceAvailabilityPolicy::Native };
        GamepadAvailabilityDomain allowedDomain{
            GamepadAvailabilityDomain::None };
        bool connected{ false };
        bool delegateReady{ false };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };
}
```

新增 `src/input_v2/gameplay/EngineModeProjection.h`：

```cpp
namespace dualpad::input_v2::gameplay
{
    enum class EngineInputMode : std::uint8_t
    {
        Original = 0,
        KeyboardMouse,
        Gamepad
    };

    enum class EngineQueryDomain : std::uint8_t
    {
        Unknown = 0,
        DeviceOperational,
        GameplayLookTransform,
        GameplayMoveTransform,
        GameplayOtherTransform,
        MenuPointerTransform,
        MenuNavigationTransform,
        MenuSetPlatform,
        Remap
    };

    enum class EngineDecisionCausality : std::uint8_t
    {
        AfterOwnerPublication = 0,
        EventLocalSource,
        OriginalOnly
    };

    struct EngineCallerRule
    {
        std::uintptr_t callerRva{ 0 };
        EngineQueryDomain domain{ EngineQueryDomain::Unknown };
        std::optional<ingress::GameplayChannel> channel;
        EngineDecisionCausality causality{ EngineDecisionCausality::OriginalOnly };
        bool overrideEnabled{ false };
    };

    struct EngineModeDecision
    {
        EngineQueryDomain domain{ EngineQueryDomain::Unknown };
        EngineInputMode mode{ EngineInputMode::Original };
        EngineDecisionCausality causality{ EngineDecisionCausality::OriginalOnly };
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
    };

    struct EngineModeDecisionSnapshot
    {
        std::uint64_t generation{ 0 };
        std::array<EngineModeDecision, 9> byDomain{};
    };
}
```

新增/扩展 `src/input_v2/runtime/RuntimeInputPublication.h`：

```cpp
namespace dualpad::input_v2::runtime
{
    struct PublishedRuntimeInputSnapshot
    {
        std::uint64_t runtimeGeneration{ 0 };
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        gameplay::GameplayProjectionFrame gameplay{};
        gameplay::EngineModeDecisionSnapshot engineModes{};
        GamepadDeviceAvailabilityDecision deviceAvailability{};
        presentation::PublishedPresentationState presentation{};
        std::array<gameplay::SustainedContributorDecision, 8> sustained{};
        std::optional<presentation::CursorHandoffPlan> pendingCursorHandoff;
    };

    class RuntimeInputPublication
    {
    public:
        void PublishCommittedOnOwnerTick(PublishedRuntimeInputSnapshot snapshot);
        std::shared_ptr<const PublishedRuntimeInputSnapshot> Acquire() const noexcept;

    private:
        std::atomic<std::shared_ptr<const PublishedRuntimeInputSnapshot>> published_;
    };

    struct PreparedRuntimeInputCommit
    {
        PublishedRuntimeInputSnapshot provisionalSnapshot{};
        gameplay::PollOutputFrame provisionalNextPoll{};
        gameplay::CurrentCycleGatePlan currentCyclePlan{};
        bool currentCycleSensitiveMutationPending{ false };
    };

    enum class PreparedCommitDisposition : std::uint8_t
    {
        Committed = 0,
        CommittedWithoutCurrentCycleAuthority,
        AbortedToFailClosedRecovery
    };

    class RuntimeInputCommitCoordinator
    {
    public:
        PreparedRuntimeInputCommit PrepareOnOwnerTick(/* one assembled frame */);
        PreparedCommitDisposition CommitAfterCurrentCycleAudit(
            PreparedRuntimeInputCommit prepared,
            const gameplay::CurrentCycleGateAudit& audit);
    };
}
```

`PreparedRuntimeInputCommit` 只在当前 `InputFramePump` 调用栈中存活，不通过 atomic publication 交给 adapter。`CommitAfterCurrentCycleAudit` 是唯一可以同时推进正式 runtime state、发布 next `PollOutputFrame` 与发布 `PublishedRuntimeInputSnapshot` 的入口；它必须验证 prepared token 只消费一次。

`SkyrimEngineModeRouter`：

```cpp
namespace dualpad::input_v2::presentation
{
    class EngineQueryScope
    {
    public:
        EngineQueryScope() noexcept = default;
        EngineQueryScope(const EngineQueryScope&) = delete;
        EngineQueryScope& operator=(const EngineQueryScope&) = delete;
        EngineQueryScope(EngineQueryScope&&) noexcept;
        EngineQueryScope& operator=(EngineQueryScope&&) noexcept;
        ~EngineQueryScope();
    };

    class SkyrimEngineModeRouter
    {
    public:
        bool Decide(
            std::uintptr_t returnAddress,
            const runtime::PublishedRuntimeInputSnapshot& snapshot,
            bool originalValue) const noexcept;

        gameplay::EngineQueryDomain ClassifyDirectCaller(
            std::uintptr_t returnAddress) const noexcept;

        EngineQueryScope EnterScopedOverride(
            gameplay::EngineQueryDomain domain,
            gameplay::EngineInputMode mode,
            std::uint64_t ownerTickToken,
            std::uint32_t contextRevision) noexcept;
    };
}
```

规则：

- runtime快照不是新authority，只是把同owner tick已裁决的结果装进一个immutable shared snapshot。
- `SkyrimCompatibilitySurface::_committed`不再保留独立可演进owner state；兼容测试读取同一shared snapshot中的presentation副本。
- `PollOutputFrame`继续独立atomic publication，但增加`runtimeGeneration/ownerTickToken/inputStateEpoch/gamepadSessionId`诊断字段，并与runtime快照由同一owner tick生成。
- engine query hook每次只Acquire一份runtime快照。
- caller rule `overrideEnabled=false`为默认。
- 无scope、Unknown、Remap、stale generation或causality未证明时调用original。
- menu RAII scope只在`MenuSetPlatform`栈内生效，离开后立即恢复Original。

### D.12 Recovery request与清理矩阵

新增 `src/input_v2/runtime/InputRecovery.h`：

```cpp
namespace dualpad::input_v2::runtime
{
    enum class RecoveryDomain : std::uint32_t
    {
        None = 0,
        PadCurrentState = 1u << 0,
        PadActivity = 1u << 1,
        KbmSemanticFacts = 1u << 2,
        KbmPhysicalQuarantine = 1u << 3,
        ChannelOwners = 1u << 4,
        TransientLedger = 1u << 5,
        SustainedMaterialization = 1u << 6,
        SourceActivity = 1u << 7,
        CurrentCycleGate = 1u << 8,
        CursorHandoff = 1u << 9,
        PollOutput = 1u << 10
    };

    struct InputRecoveryRequest
    {
        ingress::InputResetReasonMask reasons{ 0 };
        ingress::InputResetScope scope{ ingress::InputResetScope::GlobalInputState };
        RecoveryDomain domains{ RecoveryDomain::None };
        std::uint64_t observedInputStateEpoch{ 0 };
        std::uint64_t observedGamepadSessionId{ 0 };
        bool requireEpochAdvance{ false };
        bool requireSessionAdvance{ false };
        bool preserveKbmCompleteFacts{ false };
        bool enterKbmQuarantine{ false };
        bool publishNeutralPollFrame{ false };
        bool fallbackGamepadPresentationToKeyboardMouse{ false };
    };

    InputRecoveryRequest BuildRecoveryRequest(
        const ingress::InputResetMarker& marker,
        std::uint64_t previousAppliedInputStateEpoch,
        std::uint64_t previousGamepadSessionId) noexcept;
}
```

清理矩阵：

| reason/scope | pad current/activity | KBM facts | channel/transient | sustained | epoch/session | baseline |
|---|---|---|---|---|---|---|
| `DeviceDisconnected / GamepadSource` | gamepad current neutral，activity清空 | 完整KBM semantic/raw保留 | 清gamepad-owned channel、gamepad pulse、旧session gate | 清G bit；physical bit保留，已materialize bridge按aggregate继续 | session前进，epoch不变 | pad等新session完整state；KBM baseline不变 |
| `ContextBoundary / GlobalInputState` | virtual输出neutral | semantic清零，raw down进quarantine | 全清 | 全清 | epoch前进 | quarantine排空后Clean |
| `ControlMapReload / GlobalInputState` | pad current保留但virtual gate关闭 | semantic清零，旧mapping下的稳定 `(device,idCode)` down codes 进quarantine，binding index不持久化 | 全清 | 清受影响action | epoch前进，revision更新 | new binding稳定且physical-code quarantine排空 |
| `QueueOverflow/SequenceGap / GlobalInputState` | 完整物理 current/analog 以新 epoch recovery-stamped latest 保留，`virtualGameplayEligible=false`；输出 fail-closed | ordered-derived semantic 清零，稳定 raw down codes 进 quarantine | 全清 | 全清 | epoch前进 | post-marker 完整 coherent facts 到达后按 quarantine 恢复；不得冻结 analog 诊断事实 |
| `FocusLost/ExplicitReset/RuntimeOwnerFailure / GlobalInputState` | neutral | semantic清零，raw provider标incomplete并quarantine | 全清 | 全清 | epoch前进 | 新完整publication且quarantine排空 |
| `LostReleaseReconciled / KeyboardMouseSource` | 不改 | 只清指定bit | 仅重算受影响channel/action | 只更新受影响source bit | epoch/session不变 | 保持Clean |
| `SyntheticSuppressionReset / KeyboardMouseSource` | 不改 | 只清token bookkeeping | 不改owner | 不改 | epoch/session不变 | 不变 |

### D.13 latest-wins、ordered与owner-only mutable总表

| 数据/状态 | 合同 | writer | reader |
|---|---|---|---|
| `LatestPadState` | latest-wins complete current-state | HID draft经Hub事务 | FrameAssembler/owner tick |
| `GamepadConnectionFacts` | latest-wins，session单调 | HID lifecycle经Hub | runtime/telemetry |
| `GamepadMeaningfulActivity` | ordered activity | classifier draft经Hub | routed activity/presentation |
| KBM semantic current facts | latest-wins complete | InputFramePump producer经Hub | FrameAssembler/gameplay |
| KBM raw ledger/quarantine | latest-wins complete | KBM producer | recovery/gameplay health |
| KBM/gamepad press/release | ordered | producer draft经Hub | assembler/transient/sustained |
| source activity | ordered，context-neutral | producers draft经Hub | runtime route |
| `orderedCutoffSeq` | 累计 `lastConsumedOrderedSeq`，单调不减 | Hub capture | FrameAssembler/trace |
| `inputStateEpoch` | global reset fence | Hub only | all mixed publications |
| `gamepadSessionId` | connection-session fence | Hub only | pad/runtime/current-cycle |
| published `controlMapRevision` | fingerprint change fence，扩展现有 `IngressBoundaryKey` | Hub owner boundary transaction | KBM facts/assembler/runtime |
| `PollMaterializationReceipt` | verified callsite成功serialize后immutable、consume-once | XInput hook | 当前InputFramePump callback |
| channel state/candidate/quiet deadline | owner-only mutable | DualPadRuntime | gameplay projection/debug |
| transient dedup ledger | owner-only mutable | DualPadRuntime | transient projection |
| sustained logical decision | owner-frame pure result | gameplay projection | current-cycle/backend |
| sustained materialization | owner-only mutable，唯一authority为PollCommitSlot | PollCommitCoordinator | backend/debug |
| current-cycle prepared plan/audit | 当前callback栈内prepare/apply/commit，不跨callback publish | DualPadRuntime + adapter audit | commit coordinator/trace |
| cursor handoff ack | bounded monotonic、consume-once | UI task mailbox | next owner tick |
| presentation/engine/availability | immutable同代publication | runtime owner | hooks/UI/prompt |
| `PollOutputFrame` | immutable latest publication | runtime owner | arbitrary Poll readers |


---

## E. 线程所有权和数据流

### E.1 writer / reader表

| 组件 | 可写状态 | 可读状态 | 明确禁止 |
|---|---|---|---|
| HID worker | producer-local previous report；提交connection/current/activity drafts | HID bytes、transport lifecycle | 修改channel owner、presentation、engine、runtime generation、pulse、cursor、PollOutputFrame |
| verified XInput Poll hook | 成功serialize后发布immutable `PollMaterializationReceipt` | 单份immutable `PollOutputFrame`、verified callsite/thread/result | 推进runtime/context/pulse；在serialize失败时伪造成功receipt |
| Skyrim input event producer / `InputFramePump` | consume exact receipt；分配owner/event batch token；联合提交boundary+KBM batch；进入owner ticket；执行prepare/apply/audit commit | 原始event list、binding/context snapshot、consume-once receipt、同callback prepared plan | 重新Acquire最新Poll代替receipt；使用legacy tracker作为authority；在token/revision不匹配时改event；adapter失败后仍commit sensitive state |
| `SkyrimKbmInputAdapter` | callback-local observed draft和raw snapshot | `RE::InputEvent`、verified raw state API、binding snapshot | 保存event pointer到callback外；解释gameplay owner或presentation |
| `KbmGameplayFactProducer` | producer-local physical ledger、quarantine和suppression token；提交facts draft | observed batch、binding、context | 修改runtime owner/gate/output；include legacy singleton |
| `GamepadActivityClassifier` | producer-local previous state；提交classified draft | previous/current normalized pad state | 生成presentation owner或比较跨producer时钟 |
| `IngressHub` | global ordered seq、累计cutoff、latest generations、epoch、session、已发布controlMapRevision、扩展后的现有boundary key、latest slots、queue | producer drafts、ContextResolver boundary observation | 创建第二个context authority；gameplay arbitration、owner lease、cross-clock比较、output side effect |
| `FrameAssembler` | assembly window、boundary/coherence health | 一次`IngressCapture` | 旁读capture外latest singleton；调用Skyrim hook；推进presentation |
| `DualPadRuntime` / commit coordinator | provisional与committed channel state、candidate、quiet、dedup、recovery、sustained、presentation routing state | assembled stable frame、config/context envelope、callback-local receipt/materialized view、adapter audit | audit前发布provisional state；owner ticket外执行；让Poll/UI/HID推进state |
| Gameplay projection | owner内纯计算 | complete facts、previous owner state、policy | 读legacy tracker、prompt family或global engine bool决定gate |
| `SkyrimCurrentCycleEventAdapter` | I-P通过后仅修改当前callback中plan明确列出的virtual event payload | exact plan、observation、callback-local binding | 保存指针、修改physical KBM、改其它channel、推进runtime或合成event |
| Presentation projection | owner内纯计算 | routed activity、context、menu entry intent、cursor ack | 改gameplay owner、engine caller规则或Poll payload |
| Runtime/Poll publication | audit commit后原子publish immutable shared snapshot | committed owner frame最终结果 | 发布provisional/可变对象；让reader各算一份 |
| arbitrary Poll reader | 无 | 一份`shared_ptr<const PollOutputFrame>` | Capture ingress、推进pulse/context、写backend或缓存caller buffer |
| engine/menu/cursor compatibility hooks | 无长期runtime mutable；仅TLS scope栈和安装状态 | 一份runtime snapshot、original gateway、caller rule | 调用DualPadRuntime；让scope泄漏；由prompt/connectivity推断global mode |
| UI cursor task | verified坐标side effect；向 `CursorHandoffAckMailbox` 发布ack | immutable handoff plan、重新取得的live menu/movie | 直接commit cursor owner；使用stale pointer；改gameplay/engine或raw ingress |
| `PollCommitCoordinator` | 唯一backend held materialization state | sustained decision、native command | 旁读legacy KBM tracker；存在第二个mutable sustained singleton |

### E.2 文本数据流图

```text
DualSense HID worker
  -> parse / normalize complete report
  -> GamepadActivityClassifier
       -> GamepadConnectionDraft             [lifecycle]
       -> GamepadCurrentStateDraft            [latest, every valid report]
       -> GamepadDigitalEdgeDraft[]           [ordered press/release]
       -> GamepadActivityDraft[]              [only meaningful]
       -> MeaningfulSourceActivityDraft[]     [context-neutral]
  -> IngressHub::PublishGamepadBatch()
       -> capacity check first
       -> assign ordered seq + causal tail under one mutex
       -> stamp inputStateEpoch + gamepadSessionId + current revisions

Skyrim BSInputDeviceManager input cycle
  -> Keyboard Poll
  -> Mouse Poll
  -> Gamepad Poll
       -> XInput hook
       -> acquire one immutable PollOutputFrame
       -> serialize only
       -> on successful serialization publish one PollMaterializationReceipt
  -> event list materialized
  -> InputFramePump, prepended sink
       -> consume exactly one matching PollMaterializationReceipt before owner mutation
       -> ContextRefreshTick::BeginFrame()
       -> assign ownerTickToken + eventBatchToken
       -> SkyrimKbmInputAdapter::ObserveEventList()
            -> callback-local physical facts
            -> callback-local CurrentCycleEventDescriptor[]
            -> optional verified raw current snapshot
       -> KbmGameplayFactProducer::BuildIngressBatch()
            -> complete semantic facts
            -> physical ledger/quarantine
            -> ordered KBM edges
            -> context-neutral source activity drafts
       -> capture ControlMap fingerprint + binding generation
       -> IngressHub::PublishOwnerKbmBatch(boundary observation + KBM draft)
            -> reconcile ContextResolver revision + existing IngressBoundaryKey
            -> advance controlMapRevision/epoch and stamp first new batch atomically
       -> RuntimeOwnerGuard::Enter(ownerTickToken)
       -> one IngressCapture
       -> FrameAssembler
            -> causal tail <= ordered cutoff
            -> epoch/session/revision pairing
            -> transition or stable frame
       -> DualPadRuntime
            -> RouteSourceActivities
            -> ResolveChannel x Look/Move/Combat/Digital       [next-Poll view]
            -> ResolveCurrentCycleWriter x Look/Move/Combat    [materialized view]
            -> ResolveTransientAction
            -> ResolveSustainedContributor
            -> ProjectPresentation
            -> ProjectEngineModes, all overrides disabled by default
            -> BuildRecoveryRequest when boundary exists
            -> PrepareOnOwnerTick()
                 -> provisional runtime state + next PollOutputFrame
                 -> callback-local CurrentCycleGatePlan
       -> verify receipt + token + epoch + session + revision + cutoff
       -> SkyrimCurrentCycleEventAdapter::ApplyVerifiedDispositions()
       -> CommitAfterCurrentCycleAudit()
            -> success/no-mutation: commit ledgers and publish runtime + next Poll
            -> failure: preserve current-cycle-sensitive ledgers and publish fail-closed recovery
       -> return to all later Skyrim sinks

Arbitrary XInput Poll reader
  -> acquire same PollOutputFrame
  -> serialize only

0x140C15240 / menu refresh / scoped transform hook
  -> acquire one PublishedRuntimeInputSnapshot
  -> verified TLS/caller rule, otherwise original

Cursor UI task
  -> reacquire live menu/movie
  -> verify identity / revision / epoch
  -> perform direction-specific verified coordinate sync
  -> read back
  -> publish CursorHandoffAck to CursorHandoffAckMailbox
Next owner tick
  -> consume-once and verify ack
  -> commit CursorOwner
```

### E.3 generation撕裂防护

1. `InputFramePump` 先消费一次性 Poll receipt，再刷新 context 并分配 token，然后扫描 event 和构建 KBM draft。receipt 不得由第二次 Poll frame acquire 替代。
2. Hub owner batch 在同一 mutex 内完成 boundary、capacity、seq、latest 和 epoch/session/revision stamping。
3. `FrameAssembler`只消费本capture内的facts，不旁读“更晚”的global latest。
4. latest的causal tail晚于capture cutoff时defer，不触发reset，也不提前应用。
5. global reset后的旧epochfacts无论generation多大都拒绝；pad disconnect后的旧sessionfacts同理。
6. provisional runtime snapshot、next `PollOutputFrame` 和 current-cycle plan 由同一 owner tick prepare，并携带相同 runtime generation、owner token、epoch 和 session；只有 adapter audit 为 commit-safe 后才一起成为正式状态/publication。
7. hook或UI task发现snapshot revision过期时调用original或保留旧owner，不使用stale状态。
8. current-cycle adapter 只消费当前调用栈的 exact prepared plan，不能跨 callback publication 或复用；prepared token 与 Poll receipt 都只能消费一次。

### E.4 stale held、丢事件与clock-domain防护

- held真相来自complete latest snapshot，不依赖每个hold repeat都留在queue。
- press/release、source activity、reset marker必须ordered；不能用latest替代。
- HID timestamp与SKSE timestamp不比较大小；跨source先后只看Hub `ingressSeq`。
- current-cycle已经materialize的event只看其Poll identity和callback-local物理事实，不被later HID state倒灌。
- overflow 不提交触发批次的 ordered edge/activity 或派生 semantic action；允许保留 recovery-stamped 完整物理 analog/current-state，但 `virtualGameplayEligible=false`，直到 post-marker coherent facts 恢复。
- sequence gap立即在同owner tick发布global reset receipt并使用新epoch完成recovery/neutral publication。
- lost release不靠timeout。verified provider可reconcile；否则quarantine等待release或fresh initial-press epoch。
- synthetic token只按精确identity消费，过期仅回收token，不清physical held。
- current-cycle unknown event shape、提前consumer或identity mismatch时不猜测改写，记录failure并保持capability `NO-GO`。

---

## F. 文件结构与改动清单

### F.1 要创建的文件

| 路径 | 精确职责 |
|---|---|
| `src/input_v2/ingress/InputResetReason.h` | reset reason、scope、causal header公共类型 |
| `src/input_v2/ingress/MeaningfulSourceActivity.h` | context-neutral source activity、runtime routing类型与纯函数 |
| `src/input_v2/ingress/KbmGameplayFacts.h` | binding、raw current、physical ledger/quarantine、semantic current、ordered edge、batch draft |
| `src/input_v2/ingress/KbmGameplayFactProducer.h/.cpp` | 平台中立KBM producer；不include `RE::*`，不拥有runtime authority |
| `src/input/injection/SkyrimKbmInputAdapter.h/.cpp` | 唯一Skyrim KBM event/raw-state适配层 |
| `src/input_v2/ingress/GamepadActivityClassifier.h/.cpp` | connectivity/current-state/digital edge/activity分类 |
| `src/input/injection/PollMaterializationReceipt.h/.cpp` | verified XInput callsite成功序列化后的bounded consume-once receipt mailbox |
| `src/input_v2/gameplay/ChannelArbitration.h/.cpp` | per-channel next-Poll状态机、200 ms Look quiet和candidate latch |
| `src/input_v2/gameplay/CurrentCycleGatePlan.h/.cpp` | receipt-backed materialized Poll identity、callback-local writer纯决策和disposition |
| `src/input/injection/SkyrimCurrentCycleEventAdapter.h/.cpp` | I-P通过后在首个共同mutable点中和输家virtual event |
| `src/input_v2/gameplay/TransientActionGate.h/.cpp` | action级dedup、cancel和pulse决策 |
| `src/input_v2/gameplay/SustainedContributorDecision.h/.cpp` | source mask、virtual bridge、joining/non-final edge suppression纯函数 |
| `src/input_v2/runtime/InputRecovery.h/.cpp` | reset scope、epoch/session和清理域纯策略 |
| `src/input_v2/runtime/EngineModeDecision.h/.cpp` | 独立device availability决策 |
| `src/input_v2/gameplay/EngineModeProjection.h/.cpp` | original-first engine domain snapshot |
| `src/input_v2/runtime/RuntimeInputPublication.h/.cpp` | committed runtime综合快照与 `Prepare -> Apply -> Commit` 协调；不发布跨callback current-cycle pointer/plan |
| `src/input_v2/presentation/SkyrimEngineModeRouter.h/.cpp` | caller table、causality、TLS scope、Original fallback、shadow compare |
| `src/input_v2/presentation/CursorHandoffCoordinator.h/.cpp` | plan/ack/owner commit事务 |
| `src/input_v2/presentation/CursorHandoffAckMailbox.h/.cpp` | UI task到owner tick的bounded monotonic consume-once ack回交通道 |
| `src/input/injection/SkyrimCursorHandoffAdapter.h/.cpp` | I-CURSOR批准后的坐标side-effect适配器 |
| `scripts/ci/evaluate_mixed_input_trace.py` | 低频JSONL合同判定器 |
| `tests/python/test_evaluate_mixed_input_trace.py` | evaluator正反例和退出码测试 |
| `tests/input_v2/MixedInputFixtures.h` | rate matrix、fake event list、Poll receipt pairing、prepared commit、downstream observer共享fixture |
| `tests/fixtures/mixed_input/good.jsonl` | evaluator完整正例 |
| `tests/fixtures/mixed_input/neutral_hid_takeover.jsonl` | neutral report错误抢owner反例 |
| `tests/fixtures/mixed_input/double_writer.jsonl` | current-cycle或next-Poll双writer反例 |
| `tests/fixtures/mixed_input/stuck_sprint.jsonl` | contributor归零后stuck held反例 |
| `tests/fixtures/mixed_input/stale_epoch_session.jsonl` | stale epoch/session apply反例 |
| `tests/fixtures/mixed_input/engine_scope_leak.jsonl` | unknown/unscoped engine override反例 |
| `tests/fixtures/mixed_input/cursor_commit_without_ack.jsonl` | 未验证ack却commit cursor反例 |
| `docs/research/skyrim_mixed_input_dynamic_evidence_zh.md` | 统一记录identity/current-cycle/engine/menu/cursor/Sprint/KBM raw-state证据 |

条件创建：

- `src/input_v2/gameplay/SprintHeldStateGuard.h/.cpp`，仅I-SPRINT结果B时创建。
- menu direct-callsite patch site定义，仅I-MENU结果B时加入。
- shared transform callsite表，仅I-2结果B时加入production enable表。

### F.2 要修改的现有文件

| 路径 / 基线行 | 精确职责 |
|---|---|
| `src/input/HidReader.cpp:42-66,75-109` | lifecycle/current/activity draft分离；移除每report gamepad evidence；HID线程不读取owner-domain context |
| `src/input/InputFramePump.cpp:27-68,77-93,112-150` | 先consume exact Poll receipt；联合提交boundary+KBM batch；在同一调用栈执行prepare、adapter apply和audit commit |
| `src/input/injection/UpstreamGamepadHook.h/.cpp` | 保持exact XInput callsite单帧Acquire/serialize；成功后发布immutable `PollMaterializationReceipt`，不推进runtime |
| `src/input_v2/ingress/LiveInputFactProducer.h:15-62`、`.cpp:66-229` | 降为薄publication facade；neutral report不再调用`RecordGamepadEvidence(true)`；保留synthetic token adapter |
| `src/input_v2/ingress/LatestPadState.h:15-140` | 在现有唯一pad latest上加入causal tail、epoch、session字段 |
| `src/input_v2/ingress/IngressBoundaryKey.h/.cpp`、`IngressHub.h/.cpp` | 扩展现有boundary key的ControlMap revision；owner联合batch、累计cutoff、epoch/session、recovery-stamped physical latest |
| `src/input_v2/ingress/FrameAssembler.h:47-137`、`.cpp:140-555` | cutoff/epoch/session/revision pairing；defer ahead latest；sequence gap同tickrecovery |
| `src/input_v2/gameplay/DualPadRuntime.h:18-97`、`.cpp:273-568`、`DualPadRuntimeLive.cpp:118-337` | 移除6个false；持有channel/dedup/recovery/sustained/routing state；先prepare，收到adapter audit后才commit并发布immutable surface |
| `src/input_v2/gameplay/GameplayProjectionFrame.h:25-232`、`.cpp:160-419` | `ChannelOwner::None`、纯仲裁、complete sustained输出；旧engineOwner降为shadow字段 |
| `src/input_v2/gameplay/PollOutputFrame.h:21-77`、`.cpp:37-95` | 增加runtime generation、owner token、epoch、session诊断identity，不改变immutable读合同 |
| `src/input_v2/gameplay/PollOutputAdapter.h/.cpp` | 按Look/Move/Combat gate序列化current-state；Sprint读取complete contributor decision |
| `src/input_v2/presentation/SourceEvidenceCollector.h:63-155`、`.cpp:95-260` | 降级旧互斥lease authority；转发context-neutral activity；保留10px/120ms兼容测试直到新router接管 |
| `src/input_v2/presentation/PresentationProjection.h:80-129`、`.cpp:86-165` | menu/nav、cursor、prompt三类decision和routing state原子发布 |
| `src/input_v2/gameplay/GameplayPresentationPublisher.h/.cpp` | engineOwner迁移为menu-entry intent和shadow字段，不再驱动global query |
| `src/input_v2/presentation/GameplayPresentationAdapter.h/.cpp` | 只适配menu-entry intent，不生成engine owner |
| `src/input_v2/presentation/SkyrimCompatibilitySurface.h:151-273`、`.cpp:19-29,417-680,1080-1293` | identity gate、original gateway、patch group拆分、menu RAII scope、同一runtime shared snapshot、cursor task |
| `src/input/backend/NativeButtonCommitBackend.h:150-238`、`.cpp:1-6,573-604,669-679,896-909` | 删除legacy include/read和fixed false；消费complete sustained decision |
| `src/input/backend/PollCommitCoordinator.h:36-49,211-315`、`.cpp:503-579,642-870` | 三source mask、原子同步、virtual bridge、one-shot release token；删除handoff gap |
| `src/input_v2/prompt/PromptRuntimeOwner.h/.cpp` | 增加runtime generation/epoch invariant，不创建第二个family authority |
| `tests/input_v2/IngressTests.cpp:289-360,1001-1105,1541-1700` | neutral/unchanged、batch scaffold、累计cutoff、boundary/epoch/session、overflow、synthetic、稳定physical-code quarantine |
| `tests/input_v2/InputV2Tests.cpp:1188-1330,2056-2600` | producer到runtime、exact-token current-cycle、prompt隔离、recovery、engine shadow |
| `tests/input_v2/GameplayProjectionTests.cpp:192-251,431-532,622-727` | cross-channel、candidate latch、200ms quiet、current-cycle writer、transient、Sprint |
| `tests/input_v2/PresentationProjectionTests.cpp:80-220,308-450,734-834` | source routing、pointer 10px/120ms、menu/cursor/prompt、original gateway、TLS scope |
| `tests/NativeButtonCommitTests.cpp` | 实际repo preflight后加入Sprint bridge、source mask、reset和one-shot suppression测试 |
| `xmake.lua:257-429` | 纳入新源；确保runtime和canonical tests编译同一实现 |
| `scripts/ci/run_phase8_ci.ps1:20-51` | 明确build/run `DualPadGameplayProjectionTests`；保留canonical target名 |
| `scripts/ci/run_rc_readiness.ps1:27-79` | mixed trace evaluator、dynamic evidence close-out、signature manifest、Graphify和diff gate |
| `src/ARCHITECTURE.md:12-96`、`docs/current_input_pipeline_zh.md:14-54` | 更新唯一mainline、双时间视图和六个控制面 |
| `docs/runtime_concurrency_contract.md:7-42` | writer/reader、exact callback gate、UI ack、hook Original fallback |
| `docs/runtime_backpressure_contract.md:5-66` | latest/ordered/causal tail/overflow事务合同 |
| `docs/backend_routing_decisions.md:5-60` | physical priority、candidate latch、virtual bridge、original-first engine裁决 |
| `docs/menu_context_policy_current_status_zh.md:5-70` | menu/nav单一owner、pointer-only分流和cursor transaction |

### F.3 明确不应修改

- `Interface/**`、任何SWF workspace和glyph视觉资源。
- haptics、rumble、DualSense output report。
- Favorites native gate、`enable_native_favorites`默认值和现有native route语义。
- 无关action/binding语义。
- CommonLib头文件。
- `0x140705AE0`的Bethesda deadzone、curve、exponent和acceleration数学。
- `UpstreamGamepadHook` 的单次 acquire immutable frame + synchronous serialize 职责不得改变；只允许在成功返回路径追加 receipt publication，receipt 不得触发 runtime mutation。
- 不创建或恢复`InputModalityTracker`、`GameplayOwnershipCoordinator`、legacy processor authority。

### F.4 删除或降级的legacy读取口

1. 删除`src/input/backend/NativeButtonCommitBackend.cpp:595-603`对`GameplayKbmFactTracker::GetFacts()`的production读取。
2. 删除production backend对`input/GameplayKbmFactTracker.h`的include。
3. 不新增任何live writer到`ObserveButtonEvent()`或`MarkMouseLookActivity()`。
4. `GameplayKbmFactTracker`只允许映射参考、host fixture和bounded shadow compare，不能回流core runtime。
5. 旧`PublishedGameplayPresentation.engineOwner`只允许一个迁移shadow周期，不能进入engine hook。
6. `SourceEvidenceCollector`旧lease只服务legacy replay兼容；live path由ordered source activity接管。
7. `SkyrimCompatibilitySurface::_committed`不再是独立presentation authority，只持有同一runtime shared snapshot或refresh key。


---

## G. TDD实施任务

所有work package遵循固定顺序：

1. 先提交或暂存能稳定失败的测试。
2. 运行focused target，确认红灯原因与合同缺口一致。
3. 只做该package列出的最小实现。
4. focused绿灯后运行相邻回归。
5. 一个package形成一个或两个可独立回退commit。
6. 不在同一commit同时改变输入语义、手感阈值、Skyrim hook和无关文档。

### WP0 基线、canonical gate与测试正文预检

1. **文件/锚点**
   - `scripts/ci/run_phase8_ci.ps1:20-44`
   - `xmake.lua:304-327,417-429`
   - 真实repo中的`tests/NativeButtonCommitTests.cpp`
   - 新增`tests/python/test_mixed_input_closeout_contracts.py`

2. **先新增失败测试**
   - script-content fixture读取`run_phase8_ci.ps1`，断言同时包含：
     - `xmake build -y DualPadGameplayProjectionTests`
     - `xmake run -y DualPadGameplayProjectionTests`
   - preflight fixture 验证审计 commit 与 `tests/NativeButtonCommitTests.cpp` 均存在，并把当前 `HEAD` 记录为 `implementationBaseCommit`；该检查只允许以 `--phase preflight` 运行。
   - NativeButton content audit 记录“target/file 存在、Sprint contributor/source-mask fixture 尚缺失”。缺失项由 WP6 负责，WP10 的 `--phase closeout` 才把它升级为硬失败。
   - source-list fixture断言后续新增`.cpp`同时进入runtime target和对应focused target，禁止主插件与测试编译不同实现。
   - 禁止创建“固定审计快照到最终 HEAD 必须零 diff”的 fixture。

3. **运行命令**

```powershell
git diff --name-status 93184697c0f021a5979c9a71113bcb2a96308231..HEAD -- src tests xmake.lua scripts/ci
git cat-file -e 93184697c0f021a5979c9a71113bcb2a96308231:tests/NativeButtonCommitTests.cpp
python tests/python/test_mixed_input_closeout_contracts.py --phase preflight
```

4. **预期红灯原因**
   - 当前Phase8未build/run`DualPadGameplayProjectionTests`。
   - NativeButton test file 存在，但内容审计明确显示缺少 Sprint contributor/source-mask 覆盖；preflight 只报告，不伪装为已有覆盖。

5. **最小实现**
   - 加入 phase-aware preflight/closeout 检查，并立即把 `DualPadGameplayProjectionTests` 的 build/run 接入 Phase8；不修改 runtime 行为。
   - evidence manifest 记录 `auditSnapshot`、`implementationBaseCommit`、当前代码 diff 摘要和 NativeButton content audit。后续 close-out 只验证实现提交是 base 的后代、manifest 完整和实际合同测试存在，不要求零代码 diff。

6. **focused绿灯**

```powershell
python tests/python/test_mixed_input_closeout_contracts.py --phase preflight
```

7. **相邻回归**

```powershell
xmake build -y DualPadGameplayProjectionTests
xmake build -y DualPadNativeButtonCommitTests
```

8. **commit边界**
   - `test(ci): freeze mixed-input implementation base and canonical gates`

9. **rollback条件**
   - 该commit只增加测试/检查。若检查错误匹配或阻断docs-only commit，修正fixture，不回避真实缺口。

### WP0.5 建立可独立编译的 ingress batch scaffold

1. **文件/锚点**
   - 新建 `InputResetReason.h`、`MeaningfulSourceActivity.h`、`KbmGameplayFacts.h` 的公共 transport 类型。
   - 新建 `GamepadActivityClassifier.h` 的 raw draft/result 类型；classifier 行为留给 WP1。
   - 修改 `IngressHub.h/.cpp`、`IngressBoundaryKey.h/.cpp`、`FrameAssembler.h/.cpp`。
   - 修改 `xmake.lua`，确保 runtime 与 focused targets 编译同一 transport/scaffold 源。

2. **先新增失败测试**
   - `BatchApiCompileFixture`：`PublishGamepadBatch`、`PublishOwnerKbmBatch`、receipt 与 capture slots 在没有 WP1/WP2 producer 实现时即可编译链接。
   - `BatchCapacityAtomicityFixture`：容量不足时 ordered records 与 semantic latest 不得半提交。
   - `CumulativeEmptyCaptureFixture`：drain 到 seq 7 后连续空 capture，`orderedCutoffSeq` 仍为 7。
   - `SourceListParityFixture`：插件与 Ingress focused target 使用相同 scaffold `.cpp`。

3. **运行命令**

```powershell
xmake run -y DualPadIngressTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - 当前没有 batch API、KBM/gamepad transport slots、累计 cutoff 或联合 owner boundary 入口。

5. **最小实现**
   - 只建立 transport、bounded queue transaction、optional latest slots、累计 `_lastConsumedOrderedSeq` 与联合 owner batch API。
   - 所有新 facts 保持 shadow，不接管 gameplay/presentation authority。
   - WP1/WP2 只填 producer/classifier 语义，不再创建或改名 batch API；WP3 在 scaffold 上补齐 epoch/session/boundary/recovery 规则。

6. **focused 绿灯**
   - 第 3 项两个 target 全部通过。

7. **相邻回归**

```powershell
xmake run -y DualPadReplayTests
xmake run -y DualPadPresentationProjectionTests
```

8. **commit 边界**
   - `feat(input-v2): scaffold causal mixed-input ingress batches`

9. **rollback 条件**
   - API 只能依赖尚未实现的 producer 才能编译；空 capture cutoff 回退；runtime/tests source list 分叉；scaffold 意外成为 authority，任一发生即回退。

### WP1 拆分gamepad connectivity、current-state与meaningful activity

1. **文件/锚点**
   - 完成 WP0.5 已建立的 `src/input_v2/ingress/GamepadActivityClassifier.h`，新增 `.cpp`
   - 修改`src/input/HidReader.cpp:75-109`
   - 修改`src/input_v2/ingress/LiveInputFactProducer.cpp:108-129`
   - 修改`src/input_v2/presentation/SourceEvidenceCollector.cpp:142-157`
   - 测试`tests/input_v2/IngressTests.cpp:1001-1105,1541-1700`

2. **先新增失败测试**
   - `NeutralHidInterleaveFixture`：producer rate分别500/1000 Hz，owner rate分别30/60/120 Hz。先注入physical keyboard takeover，然后2秒持续neutral HID并交错keyboard/mouse press/release与mouse delta。
   - 断言：`LatestPadState.generation` 持续增长；ordered gamepad meaningful-activity count/activity generation 不增长；KBM source activity 按 Hub seq 出现；menu/prompt/四通道 owner 不被 neutral report 抢走。
   - `HeldStickUnchangedFixture`：RS从0跨到0.60，随后120个完全相同report。断言首report产生`RightStickEntered`，其余不产activity，但每stable frame RS仍为0.60。
   - `ReleaseDoesNotTakeoverFixture`：gamepad button held，physical KBM takeover，随后gamepad release与stick回中。断言current-state正确清除，但不产生Gamepad source activity。
   - `ConnectivityOnlyFixture`：connect/disconnect只改变connection/session，不改变prompt或engine mode。
   - `ContextNeutralGamepadDraftFixture`：同一 raw control 在 gameplay/menu 两种 context 下，HID classifier 输出完全相同；只有 owner-tick router/action graph 解析出的 channel/action 不同。

3. **运行命令**

```powershell
xmake run -y DualPadIngressTests
```

4. **预期红灯原因**
   - `HidReader.cpp:90-94`对每份report调用gamepad evidence。
   - `LiveInputFactProducer.cpp:120`无条件`RecordGamepadEvidence(true)`。
   - collector每次清KBM evidence并续lease。

5. **最小实现**

```cpp
const auto classified = classifier.Classify(
    previous.state,
    current.state,
    current.sourceSequence,
    current.sourceTimestampUs);
IngressHub::GetSingleton().PublishGamepadBatch(
    classified,
    connectionChange);
```

   - 每份report仍提交complete current-state。
   - 只有classifier产生的activity draft进入source activity流。
   - classifier 只输出 raw `controlCode/reason/phase`，不写 `GameplayChannel` 或 `ActionId`；映射发生在 owner-tick routing。
   - connect/disconnect独立提交connection draft。
   - live HID入口不再调用旧`RecordGamepadEvidence(true)`。

6. **focused绿灯**

```powershell
xmake run -y DualPadIngressTests
```

7. **相邻回归**

```powershell
xmake run -y DualPadPresentationProjectionTests
xmake run -y DualPadInputV2Tests
```

8. **commit边界**
   - `fix(input-v2): separate pad current state from meaningful activity`

9. **rollback条件**
   - unchanged non-neutral current-state丢失；digital press漏activity；neutral/release仍抢owner；disconnect后state不neutral，任一发生即回退。

### WP2 建立Skyrim KBM adapter与input_v2纯producer

1. **文件/锚点**
   - 完成 WP0.5 已建立的 `src/input_v2/ingress/KbmGameplayFacts.h` transport 类型
   - 新建`src/input_v2/ingress/KbmGameplayFactProducer.h/.cpp`
   - 新建`src/input/injection/SkyrimKbmInputAdapter.h/.cpp`
   - 修改`src/input/InputFramePump.cpp:27-68,112-150`
   - 参考但不调用`src/input/GameplayKbmFactTracker.cpp:44-55,96-198,228-317`
   - 测试`tests/input_v2/IngressTests.cpp`和`InputV2Tests.cpp:1188-1330`

2. **先新增失败测试**
   - `FakeKbmBindingSnapshot`把非WASD、非默认mouse codes映射为Move、Combat、Jump、Activate、Sprint。
   - `FakeSkyrimKbmAdapter`输出callback-local observed drafts和complete raw bits。
   - 序列：move press、mouse combat press、Sprint press、mouse delta、对应release。
   - 断言：complete current masks逐步精确变化；press/release/mouse delta ordered；context/control-map revision一致；producer重建后同raw state仍得到相同facts。
   - synthetic fixture 分 3 路：verified physical-only provider、reserved non-colliding control、unproven provenance。前两路只有 exact injection receipt 命中时不进入 facts；第三路必须按 physical event 处理或禁用 helper route，不能仅因 scancode/token/time 匹配而吞掉。
   - mapping fixture：held 时 fingerprint 变化，断言 semantic 清零、稳定 `(device,idCode)` 进入 quarantine、baseline=`MappingRearmRequired`；binding entry 重排不改变 quarantine 身份，held-repeat 不 rearm，release 或 fresh initial-press epoch 清位。
   - adapter boundary fixture：core producer头文件不得include `RE/`路径；平台adapter是唯一命中点。

3. **运行命令**

```powershell
xmake run -y DualPadIngressTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - InputFramePump只发布presentation evidence。
   - IngressCapture没有KBM latest。
   - production没有KBM producer，六个policy字段仍为false。

5. **最小实现**

```cpp
const auto frameToken = ContextRefreshTick::GetSingleton().BeginFrame();
const auto eventBatchToken = NextEventBatchToken();
const auto context = ContextResolver::GetSingleton().GetPublishedSnapshot();
const auto bindings = _skyrimKbmAdapter.CaptureBindingDraftAndFingerprint(context);
const auto observed = _skyrimKbmAdapter.ObserveEventList(
    events, bindings, frameToken, eventBatchToken, NowOwnerUs());
const auto batch = _kbmProducer.BuildIngressBatch(
    observed, bindings, context, NowOwnerUs());
const auto receipt = IngressHub::GetSingleton().PublishOwnerKbmBatch({
    .boundary = BuildBoundaryObservation(context, bindings),
    .kbm = batch
});
DrainOnOwnerTick(..., frameToken);
```

   - `BeginFrame()`移到event扫描前。
   - ControlMap fingerprint 变化时不先单独 reset。`PublishOwnerKbmBatch` 在一个 Hub transaction 中推进现有 `IngressBoundaryKey`/epoch/controlMapRevision，并给 new binding generation 与本 callback batch 盖同一新 key。
   - 本package只建立facts，不启用gameplay gate。

6. **focused绿灯**

```powershell
xmake run -y DualPadIngressTests
xmake run -y DualPadInputV2Tests
```

7. **相邻回归**

```powershell
xmake run -y DualPadContextResolverTests
xmake run -y DualPadPresentationProjectionTests
```

8. **commit边界**
   - `feat(input-v2): publish coherent control-map-aware kbm facts`

9. **rollback条件**
   - hardcoded键位；event热路径反复查ControlMap；unproven synthetic 被吞；quarantine 用 binding index；mapping change后旧held继续；core producer include RE；第一批new mapping event被旧revision拒绝，任一发生即回退。

### WP3 实现causal ingress cutoff、global epoch与gamepad session

1. **文件/锚点**
   - 修改`src/input_v2/ingress/IngressHub.h:17-80`
   - 修改`IngressHub.cpp:90-175,177-352,402-422`
   - 修改`FrameAssembler.h:47-137`
   - 修改`FrameAssembler.cpp:140-220,476-555`
   - 测试`IngressTests.cpp`、`InputV2Tests.cpp`、property/fuzz tests

2. **先新增失败测试**
   - `PartialDrainCausalTailFixture`：latest tail=10，capture cutoff=9。断言latest defer；下一capture到10才应用。
   - `LatestOnlyNoFakeSeqFixture`：1000 neutral current reports后压入ordered press。断言press seq与上一真实ordered seq连续，无SequenceGap。
   - `EmptyCaptureKeepsCutoffFixture`：先 drain 到 seq 10，随后两次空 capture，cutoff 仍为 10；tail=10 的 latest 可应用，不能被永久 defer。
   - `OverflowNoHalfCommitFixture`：incoming report 同时含新 analog/current-state 与 ordered press，但容量不足。断言 recovery marker/new epoch 原子提交；ordered press 与 semantic action 不提交；完整物理 analog/current-state 以 `virtualGameplayEligible=false` 保留；当前与下一 capture 均不把旧 held 重附着，virtual output 保持 neutral。
   - `DisconnectScopeFixture`：KBM Move/Sprint held，gamepad disconnect。断言session变化、epoch不变、KBM latest仍可用。
   - `OldSessionDropFixture`：disconnect后旧session pad activity和current-state即使generation更大也拒绝。
   - `RevisionAheadFixture`：旧context latest的tail晚于boundary prefix，断言不得跨到new context。
   - `AtomicBoundaryFirstBatchFixture`：context/control-map 同时变化且首个 callback 含 press，断言 marker、new `IngressBoundaryKey`、binding generation 与 press 处于同一事务/epoch；不存在先拒绝首个 press 的窗口。

3. **运行命令**

```powershell
xmake run -y DualPadIngressTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - Capture返回ordered prefix和全局latest，latest无causal tail。
   - `PushPadSnapshot`可能在capacity check前更新latest。
   - 当前没有global epoch和pad session分离。

5. **最小实现**

```cpp
std::scoped_lock lock(_mutex);
if (!HasCapacityFor(batch.OrderedRecordCount())) {
    return PublishOverflowRecoveryLocked(batch.PhysicalCurrentStateIfComplete());
}
const auto tail = AssignOrderedSeqLocked(batch);
CommitLatestLocked(batch, tail.value_or(_lastAssignedOrderedSeq));
```

   - Hub 是 epoch/session 与已发布 controlMapRevision 的唯一 writer；contextRevision 只来自 `ContextResolver` published snapshot，并被镜像进现有 `IngressBoundaryKey`。
   - latest-only不分配seq。
   - assembler对ahead latest执行defer，对old epoch/session/revision执行drop。
   - sequence gap在同owner tick触发Hub global reset receipt并立即neutral publish。

6. **focused绿灯**

```powershell
xmake run -y DualPadIngressTests
xmake run -y DualPadInputV2Tests
```

7. **相邻回归**

```powershell
xmake run -y DualPadReplayTests
xmake run -y DualPadPropertyTests
xmake run -y DualPadFuzzRegressionTests
```

8. **commit边界**
   - `fix(input-v2): bind latest facts to ordered cutoffs and reset scopes`

9. **rollback条件**
   - latest越过cutoff；空capture把cutoff重置；latest-only制造seq gap；overflow制造ordered action或冻结完整analog；disconnect误增global epoch；旧session/epoch重附着；首个new mapping event被旧revision拒绝，任一发生即回退整组。

### WP4 抽出per-channel next-Poll仲裁并启用跨通道并行

1. **文件/锚点**
   - 新建`src/input_v2/gameplay/ChannelArbitration.h/.cpp`
   - 修改`GameplayProjectionFrame.h:25-42,158-232`
   - 修改`GameplayProjectionFrame.cpp:160-245,280-343`
   - 修改`DualPadRuntime.cpp:391-409`
   - 测试`GameplayProjectionTests.cpp:192-251,431-505`和`InputV2Tests.cpp`

2. **先新增失败测试**
   - mouse delta + LS 0.70 + RS 0.65：Look=KBM、RS gate；Move=Gamepad、LS pass。
   - RS 0.70 + mapped move held + LS 0.80：Look=Gamepad、Move=KBM。
   - mouse+RS同frame：physical优先，candidate latched。
   - mouse停止199 ms：Look仍KBM；201 ms且RS 0.16、candidate=true：同tickGamepad reclaim。
   - mouse停止201 ms且candidate=false、RS 0.24：owner=None；RS 0.26才enter。
   - keyboard+LS：最后key release时latched LS 0.16同tickGamepad；未latched LS 0.16不enter。
   - Combat physical held + LT/RT：两个trigger都gate，Look/Move不受影响。
   - 双方inactive：owner=None，virtual通道neutral。
   - `GamepadSourceResetFixture`：Look=Gamepad、Move=KeyboardMouse 时断开手柄；Look 清为 None，Move/KBM quiet state 保留，gamepad candidate 全清。`Global` reset 才允许全部 channel 清空。
   - production builder断言六个KBM字段不再全部false，其中sustained字段可先进入shadow供WP6使用。

3. **运行命令**

```powershell
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - production六字段hardcoded false。
   - 现有projection没有`None`、candidate latch和明确200 ms state。
   - engine/menu/cursor仍混在primary decision中。

5. **最小实现**
   - `ChannelOwner`追加`None=2`，保留现有Gamepad=0、KeyboardMouse=1，并加`static_assert`和replay兼容测试。
   - runtime持有Look/Move/Combat/Digital四份`ChannelArbitrationState`。
   - projection只输出owner、gate、reason和next state。
   - `ChannelArbitrationInput.resetMode` 区分 `GamepadSource` 与 `Global`；disconnect 不得复用 global hard reset。
   - gameplay arbitration不读取prompt/menu/engine字段。

6. **focused绿灯**

```powershell
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadInputV2Tests
```

7. **相邻回归**

```powershell
xmake run -y DualPadIngressTests
xmake run -y DualPadReplayTests
```

8. **commit边界**
   - `feat(gameplay): arbitrate mixed input per channel`

9. **rollback条件**
   - 跨通道互相归零；gamepad disconnect 清除 KBM owner；candidate在低于sustain时不清；quiet由neutral HID续期；deadzone边缘逐frame抖动；任何通道双writer，任一发生即回退。

### WP5 建立current-cycle独立决策、transient去重和shadow adapter

1. **文件/锚点**
   - 新建`src/input_v2/gameplay/CurrentCycleGatePlan.h/.cpp`
   - 新建`src/input_v2/gameplay/TransientActionGate.h/.cpp`
   - 新建`src/input/injection/PollMaterializationReceipt.h/.cpp`
   - 新建`src/input/injection/SkyrimCurrentCycleEventAdapter.h/.cpp`
   - 修改`src/input/injection/UpstreamGamepadHook.h/.cpp`，在verified callsite成功serialize后发布receipt
   - 修改`DualPadRuntime.cpp:337-568`
   - 修改`RuntimeInputPublication.h/.cpp`，实现prepared commit而不是跨callback gate publication
   - 修改`PollOutputFrame.h/.cpp`加入identity
   - 修改`InputFramePump.cpp:112-150`
   - 测试`GameplayProjectionTests.cpp`、`InputV2Tests.cpp`、`MixedInputFixtures.h`

2. **先新增失败测试**
   - `ReceiptPairingFixture`：verified hook call sequence 41/thread A 成功 serialize frame 41/packet 9，Pump 同线程 consume-once 后才能建立 current-cycle identity；再次 acquire 最新 frame 42 不得改变 identity。
   - missing、ambiguous、already-consumed、thread mismatch receipt 分别返回精确 failure，零 event 修改。
   - Look：receipt证明frame 41/packet 9产生RS event，同callback有physical mouse。current-cycle中和RS，next-Poll也gate；不冲突LS保持。
   - Clock-domain A：41/9产生RS event，无physical竞争；随后owner tick看到later neutral HID并生成frame42。current-cycle必须Keep 41/9 event，next-Poll42可neutral。
   - Clock-domain B：41/9产生RS event，later HID方向改变。current-cycle保持旧event，next-Poll使用新方向；observation伪造成42/10时零修改并`PollFrameMismatch`。
   - Move：mapped key press与LS event同batch，current-cycle抑制LS；physical release不是activation，若LS event已materialize则Keep，若缺失不得合成。
   - Combat：physical attack press与LT/RT event同batch，抑制trigger；release后trigger event已存在则Keep。
   - Transient：physical Jump与virtual Jump同key只一次；virtual pulse已down-visible时physical press产生一次cancel；physical release不误杀new gamepad press。
   - synthetic helper命中token时不形成physical竞争者。
   - boundary：global epoch或pad session已前进，旧route event在identity可证明时中和；identity不明时零修改并NO-GO。
   - failure table：token、batch、Poll identity、epoch、session、context、mapping、cutoff、physical complete、route、adapter、consumer、scratch长度任一不匹配均零修改并返回对应failure。
   - `PreparedCommitRollbackFixture`：为每个 adapter failure 注入 provisional transient pulse、Sprint mask/bridge 和 channel next state；断言正式 ledger 与 previous 完全相同、prepared token 只消费一次、受影响 next Poll fail-closed，不能出现“audit失败但state前进”。
   - `NoMutationCommitFixture`：plan 无需 event mutation 时 audit 明确 `commitCurrentCycleSensitiveState=true`，可正常提交 next state。
   - head/middle/tail fake list全部覆盖。
   - 分别计算`currentEventWriterCount`和`nextPollWriterCount`，都必须`<=1`，不要求两个时间视图owner相同。

3. **运行命令**

```powershell
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - 当前只能影响下一份Poll，已materialize event仍进入后续sink。
   - 当前 hook 没有 materialization receipt，Pump 无法证明 callback 使用的精确 Poll frame。
   - 当前 runtime 没有 prepared commit/adapter audit 回交，无法在 mutation 失败时回滚 current-cycle-sensitive state。
   - transient只按粗粒度digital active处理，没有action/token dedup。

5. **最小实现**
   - hook 在 verified XInput callsite 成功 serialize 后发布 bounded immutable receipt；InputFramePump 只 consume exact receipt，不重新 acquire 猜测 identity。
   - adapter扫描原始list生成callback-local descriptors和bindings，不跨callback保存pointer。
   - runtime 保存 previous channel/transient/sustained state，分别计算 next-Poll 和 current-cycle decision，结果先放入 `PreparedRuntimeInputCommit`。
   - 第一 commit 只运行 shadow adapter/audit；不创建跨 callback 的 gate-plan publication，plan 只在 Pump 当前调用栈中传递。
   - transient gate 按 `TransientDedupKey` 生成 disposition，但 `next` 在 `CommitAfterCurrentCycleAudit` 之前不落入正式 state。
   - audit failure 保留 previous sensitive ledgers，并对受影响通道发布 fail-closed recovery；success/no-mutation 才原子发布 runtime snapshot 与 next Poll frame。
   - 在I-P前不启用真实event mutation。

6. **focused绿灯**

```powershell
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadInputV2Tests
```

7. **相邻回归**

```powershell
xmake run -y DualPadReplayTests
xmake run -y DualPadPresentationProjectionTests
xmake run -y DualPadNativeButtonCommitTests
```

8. **commit边界**
   - Receipt/shadow：`feat(gameplay): prove materialized polls and prepare current-cycle commits`
   - I-P通过后的enable commit：`fix(skyrim): suppress losing virtual events before downstream consumers`

9. **rollback条件**
   - physical input被改；其它channel被改；release误当activation；later HID倒灌；receipt缺失仍猜最新frame；adapter失败仍推进ledger；prepared token重复commit；head/list ownership不安全；identity mismatch仍修改；任一consumer先于gate，立即关闭enable commit并保持对应capability NO-GO。


### WP6 实现Sprint complete contributor、virtual bridge与唯一materialization authority

1. **文件/锚点**
   - 新建`src/input_v2/gameplay/SustainedContributorDecision.h/.cpp`
   - 修改`GameplayProjectionFrame.cpp:374-395`
   - 修改`NativeButtonCommitBackend.cpp:573-604,669-679,896-909`
   - 修改`PollCommitCoordinator.h:36-49,211-315`
   - 修改`PollCommitCoordinator.cpp:503-579,642-870`
   - 修改`CurrentCycleGatePlan`和adapter的sustained disposition
   - 测试`GameplayProjectionTests.cpp:431-532,622-727`和真实`NativeButtonCommitTests.cpp`

2. **先新增失败测试**
   - `SprintContributorFixture`逐owner frame记录previous/active mask、aggregate、virtual bridge、backend slot和XInput Sprint bit。
   - 序列A：G press -> K press -> G release -> K release。断言mask `G -> G|K -> K -> 0`，aggregate始终`1 -> 1 -> 1 -> 0`。
   - 序列B：K press -> G press -> K release -> G release。断言mask `K -> K|G -> G -> 0`；K-only不合成virtual press。
   - Mouse替代Keyboard重复两组，证明K/M是独立bit。
   - joining press在aggregate已held时被suppress；non-final release在remaining mask非0时被suppress。
   - 同batch多source press优先最早physical event ordinal；无physical时才允许gamepad press。
   - final release只允许一条；下一Poll冗余virtual release由精确one-shot token消费一次。
   - joining/non-final suppression 需要 current-cycle mutation 时，adapter failure 不得提交新的 source mask、bridge、effective emitter 或 one-shot token；恢复后从 previous committed state 重算。
   - context/reset/disconnect插入每个阶段：
     - global reset清全部mask、bridge、token和virtual held；
     - gamepad disconnect仅清G bit，若K/M仍held且bridge已materialize则不中断。
   - pure decision与`PollCommitCoordinator` materialized result逐frame shadow-check一致。

3. **运行命令**

```powershell
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadNativeButtonCommitTests
```

4. **预期红灯原因**
   - production KBM contributor为false。
   - backend固定`kbmSprintHeld=false`并读取legacy diagnostics。
   - coordinator存在single-emitter和故意handoff gap。
   - current-cycle没有joining/non-final edge suppression。

5. **最小实现**

```cpp
const auto decision = ResolveSustainedContributor({
    .actionId = actions::Sprint,
    .previousSourceMask = previous.activeSourceMask,
    .activeSourceMask = command.activeSourceMask,
    .previousVirtualMaterialized = previous.virtualMaterialized,
    .previousEffectiveEmitter = previous.effectiveEmitter,
    .inputStateEpoch = frame.inputStateEpoch,
    .contextRevision = frame.contextRevision,
    .runtimeGeneration = runtimeGeneration
});

const auto materialized = pollCommit.SyncHeldContributors(
    actions::Sprint,
    decision.activeSourceMask,
    decision.virtualBridgeDesired,
    decision.inputStateEpoch,
    decision.contextRevision,
    decision.runtimeGeneration);
```

   - `decision` 与 backend materialization 先写入 WP5 的 `PreparedRuntimeInputCommit`。只有 current-cycle audit commit-safe 后，`activeSourceMask/virtualBridge/effectiveEmitter/one-shot token` 才一起成为正式 state。

   - `SyncHeldContributors`原子替换完整三source mask。
   - 删除legacy include/read、fixed false和handoff gap。
   - current-cycle plan只materialize已裁决press/release suppression，不重新算mask。
   - 不在本package新增SprintHandler hook。

6. **focused绿灯**

```powershell
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadNativeButtonCommitTests
```

7. **相邻回归**

```powershell
xmake run -y DualPadInputV2Tests
xmake run -y DualPadReplayTests
```

8. **commit边界**
   - `feat(gameplay): aggregate sprint contributors with a virtual bridge`

9. **rollback条件**
   - mask非0出现inactive；adapter失败后mask仍前进；duplicate handler press；final release多于一次；keyboard/mouse-only制造virtual press；reset后bridge/token残留；改动影响Jump/Activate，任一发生即回退。

### WP7 统一recovery、quarantine、optional reconcile与disconnect scope

1. **文件/锚点**
   - 新建`src/input_v2/runtime/InputRecovery.h/.cpp`
   - 修改`KbmGameplayFactProducer` quarantine/rearm
   - 修改`IngressHub.cpp` reset事务
   - 修改`FrameAssembler.cpp:504-555`
   - 修改`DualPadRuntime.cpp:412-568`
   - 修改backend reset路径
   - 测试Ingress/InputV2/GameplayProjection/NativeButton targets

2. **先新增失败测试**
   - `GamepadSourceResetFixture`：KBM Move和K Sprint held，同时gamepad stick/trigger/G Sprint active，然后disconnect。断言session前进、epoch不变；D.6 `GamepadSource` reset只清gamepad current/activity/owner/pulse/G bit；KBM Move owner、quiet state和K bit保留；Sprint无inactive。
   - `GlobalContextResetFixture`：held Move/Look/Sprint和pending transient进入menu。断言epoch前进、全部virtual输出neutral、semantic清零、raw down进quarantine、旧event batch失效。
   - `ControlMapReloadFixture`：held key跨reload并故意重排 binding entries。旧semantic清零、稳定old `(device,idCode)` 进quarantine；index重用不能改变身份；held-repeat不rearm；release后new mapping可用。再丢release并发送fresh initial-press epoch，断言不会永久stuck。
   - `OverflowAndGapFixture`：press/release之间overflow或gap。断言无ordered/semantic partial apply、所有virtual held释放、old latest不重附着；完整物理analog/current-state仍以recovery-stamped latest可见但不能驱动virtual output。
   - `LostReleaseProviderFixture`：provider disabled时进入quarantine；provider enabled且complete/physical-only时生成一次ReconciledRelease；provider incomplete时不能猜all-up。
   - `SyntheticSuppressionResetFixture`：global reset清token，真实后续event不被吞。
   - `SessionVsEpochFixture`：旧session current-cycle plan返回GamepadSessionMismatch；旧epoch plan返回EpochMismatch。

3. **运行命令**

```powershell
xmake run -y DualPadIngressTests
xmake run -y DualPadInputV2Tests
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadNativeButtonCommitTests
```

4. **预期红灯原因**
   - 当前没有KBM semantic/raw/quarantine三层baseline。
   - reset没有覆盖新channel/current-cycle/sustained/presentation state。
   - disconnect与global reset尚未分scope。

5. **最小实现**

```cpp
const auto request = BuildRecoveryRequest(
    marker,
    _lastAppliedInputStateEpoch,
    _lastGamepadSessionId);
ApplyRecovery(request);
```

   - `GamepadSource`只清gamepad域并保留complete KBM facts。
   - `GlobalInputState`要求epoch前进并全清virtual状态。
   - KBM producer 按 reason 把稳定 physical codes 放入 quarantine；不保存 binding index，不使用时间 lease 自动解封。
   - verified provider只作为I-KBM批准的optional reconcile。
   - sequence gap在同owner tick发布Hub reset并立即neutral，不延迟到下一tick。

6. **focused绿灯**

运行第3项四个命令并全部通过。

7. **相邻回归**

```powershell
xmake run -y DualPadPropertyTests
xmake run -y DualPadFuzzRegressionTests
xmake run -y DualPadReplayTests
```

8. **commit边界**
   - `fix(input-v2): recover mixed held state by scoped epochs and quarantine`

9. **rollback条件**
   - disconnect误清KBM；global reset后残留owner/gate/held；binding重排后quarantine指向错误物理键；quarantine永远无法drain；provider误清真实held；physical KBM在quarantine被DualPad吞掉，任一发生即回退整组并要求重启session。

### WP8 分离prompt、menu、cursor并实现原子presentation transaction

1. **文件/锚点**
   - 修改`PresentationProjection.h:80-129`、`.cpp:86-165`
   - 新建`CursorHandoffCoordinator.h/.cpp`
   - 新建`CursorHandoffAckMailbox.h/.cpp`
   - 新建`SkyrimCursorHandoffAdapter.h/.cpp`，I-CURSOR前只编译shadow seam
   - 修改`GameplayPresentationPublisher`和`GameplayPresentationAdapter`
   - 修改`SkyrimCompatibilitySurface.cpp:586-680,1186-1293`
   - 测试`PresentationProjectionTests.cpp:80-220,308-450`和`InputV2Tests.cpp`

2. **先新增失败测试**
   - `OrderedActivityRoutingFixture`：同tick gamepad source seq50、keyboard seq51，prompt选KBM；交换seq后选Gamepad，producer timestamp反序不改变结果。
   - neutral、release-only、unchanged held、synthetic-suppressed不改变prompt。
   - pointer 9 px/119 ms不promotion；累计10 px在50 ms完成后停止移动，owner tick到120 ms必须一次性切cursor/prompt，不切menu/nav；不要求新mouse event。candidate后若有更高seq strong gamepad activity，则旧timer不得反抢。mouse click切menu/nav/cursor。
   - gamepad strong navigation切menu/nav，neutral HID不能抢回。
   - `PromptOnlyMutationFixture`：只改prompt family，gameplay/engine/menu/cursor/Poll hashes逐字段不变。
   - `CursorPlanAckFixture`：KBM->Gamepad请求只发布pending plan并保持旧owner；success ack经mailbox下一owner tick consume-once后commit。duplicate ack、wrong token、MappingUnverified、write read-back失败、stale instance/context/epoch ack均不commit。
   - Gamepad->KBM方向按I-CURSOR裁决配置：无需sync时直接commit；需sync时必须有success ack。
   - Favorites pre-output只发布menu-entry intent，不强制cursor或改变native gate。
   - `SkyrimCompatibilitySurface::GetCommittedState()`与同代runtime snapshot presentation逐字段一致，无独立epoch。

3. **运行命令**

```powershell
xmake run -y DualPadPresentationProjectionTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - current projection把gameplay engineOwner当通用owner。
   - current pre-output handoff同时强制owner/navigation/cursor。
   - any mouse move可能清gamepad evidence。
   - 没有plan/ack/owner commit事务。

5. **最小实现**

```cpp
const auto routed = RouteSourceActivities(
    rawActivities,
    previous.activityRouting,
    context,
    resolvedActions,
    ownerNowMs);
const auto presentation = ProjectPresentation({
    .previous = previous,
    .context = context,
    .gameplayMenuEntryOwner = menuEntryOwner,
    .routedActivities = routed.activities,
    .cursorAck = cursorAck,
    .resetReasons = resetReasons,
    .inputStateEpoch = inputStateEpoch,
    .ownerTickToken = ownerTickToken
});
```

   - 一次发布prompt/menu/cursor和routing state。
   - `RouteSourceActivities` 每个 owner tick 都推进 pending pointer deadline；mailbox ack 在 projection 前 consume，且只有 exact envelope 可进入 `PresentationProjectionInput`。
   - gameplay只发布menu-entry intent。
   - UI task只执行side effect并向 `CursorHandoffAckMailbox` 发布ack。
   - I-CURSOR未闭合时adapter返回MappingUnverified并保持旧owner。

6. **focused绿灯**

```powershell
xmake run -y DualPadPresentationProjectionTests
xmake run -y DualPadInputV2Tests
```

7. **相邻回归**

```powershell
xmake run -y DualPadPromptSnapshotTests
xmake run -y DualPadGameplayProjectionTests
```

8. **commit边界**
   - `refactor(presentation): publish prompt menu and cursor decisions atomically`

9. **rollback条件**
   - pointer-only翻SetPlatform；10 px candidate因无后续event永不promotion；旧pointer timer反抢更新strong source；prompt改gameplay/engine；cursor无ack commit；duplicate/stale ack被重复消费；stale UI task写入；同epoch重复refresh；Favorites gate变化，任一发生即回退。

### WP9 恢复original-first engine/device链并建立shadow router

> 该package先做identity和Original gateway。任何行为override都必须等待I节对应出口。

1. **文件/锚点**
   - 新建`runtime/EngineModeDecision.h/.cpp`
   - 新建`gameplay/EngineModeProjection.h/.cpp`
   - 新建`presentation/SkyrimEngineModeRouter.h/.cpp`
   - 修改`SkyrimCompatibilitySurface.h:151-273`
   - 修改`SkyrimCompatibilitySurface.cpp:19-29,244-264,417-573,631-680,1080-1184`
   - 测试`PresentationProjectionTests.cpp:734-834`和`InputV2Tests.cpp`

2. **先新增失败测试**
   - `Relocation67320Identity`：fixture要求REL解析VA和I-0记录前32 bytes完全匹配，不匹配install gate失败。
   - `HandlerVtableSlotIdentity`：slot0x7/0x8只有与I-0 original target一致者可patch，另一个拒绝。
   - `UnscopedEntryCallsOriginal`：prompt/menu/LookOwner/connectivity任意变化，无scope时entry original调用一次。
   - `UnscopedDeviceVfuncCallsOriginal`：非remap也不得默认true。
   - `DeviceAvailabilityNativeByDefault`：policy=Native时connectivity不覆盖original。
   - `MenuRefreshScope`：仅在RAII MenuSetPlatform scope返回menu owner，退出后Original；同owner/context epoch最多一次refresh。
   - `UnknownAndRemapOriginal`：未知caller、remap、stale token、causality未证明全部Original。
   - `NestedTlsScope`：外层Menu、内层Look，内层结束恢复Menu，无泄漏。
   - `CallerShadowTable`：26个caller都有class或OriginalOnly记录；release-relevant unknown使test失败。
   - `PartialInstallRollback`：任一entry/vfunc/optional site signature失败，不留下部分patch。
   - prompt-only变化不改变engine shadow snapshot。

3. **运行命令**

```powershell
xmake run -y DualPadPresentationProjectionTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - current top-level query返回presentation owner。
   - currentdevice hook非remap固定true。
   - 没有REL到VA、vtable slot语义或caller causality验证。
   - patch transaction把不同职责绑在一起。

5. **最小实现**

```cpp
bool StaticIsUsingGamepadHook(void* self)
{
    if (const auto scoped = SkyrimEngineModeRouter::CurrentScope()) {
        return scoped->value;
    }
    return CallOriginalIsUsingGamepad(self);
}
```

   - 先实现identity manifest和original gateway。
   - default device availability=`Native`。
   - runtime projection生成全部`Original`的shadow snapshot。
   - caller router只记录建议结果和causality，不改变production返回。
   - menu refresh RAII scope可在I-MENU通过后独立enable。
   - availability、direct menu callsite、transform caller override都为独立条件commit。

6. **focused绿灯**

```powershell
xmake run -y DualPadPresentationProjectionTests
xmake run -y DualPadInputV2Tests
```

7. **相邻回归**

```powershell
xmake run -y DualPadGameplayProjectionTests
xmake run -y DualPadPromptSnapshotTests
powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1
```

8. **commit边界**
   - A：`fix(skyrim): verify engine query and gamepad vtable identities`
   - B：`refactor(skyrim): make engine query routing original by default`
   - C：`feat(skyrim): shadow caller-scoped engine decisions`
   - D，仅I-MENU A：`feat(skyrim): scope menu platform refresh to menu owner`
   - E，仅I-0 B：`feat(skyrim): scope gamepad availability to verified poll domain`
   - F，仅I-MENU B：`feat(skyrim): patch verified menu platform query callsite`
   - G，仅I-2 B：`feat(skyrim): enable verified source-scoped input transform`

9. **rollback条件**
   - identity不稳定；unscoped不调用original；scope泄漏；connectivity影响transform/prompt/menu；unknown caller被覆盖；partial install残留；实机Poll停止、数值异常或崩溃。每个条件commit独立回退，不能回到global presentation-owner或fixed true作为发布路径。

### WP10 建立trace evaluator、canonical CI、文档与Graphify close-out

1. **文件/锚点**
   - 新建`scripts/ci/evaluate_mixed_input_trace.py`
   - 新建Python tests和JSONL fixtures
   - 修改`run_phase8_ci.ps1:20-51`
   - 修改`run_rc_readiness.ps1:27-79`
   - 修改架构/并发/backpressure/backend/menu文档
   - 更新xmake source lists和Graphify

2. **先新增失败测试**
   - evaluator对good fixture退出0。
   - neutral takeover fixture必须指出合同`B.1-7`。
   - double writer fixture分别识别current-cycle和next-Poll。
   - stuck Sprint fixture识别mask非0却held=false或mask0后无release。
   - stale epoch/session fixture识别old plan apply。
   - engine scope leak fixture识别Unknown/unscoped override。
   - cursor commit without ack fixture识别owner commit违规。
   - poll receipt missing/ambiguous/reused 与 adapter-failure-ledger-advance fixtures 必须被识别。
   - synthetic takeover fixture识别helper event进入physical/source activity。
   - closeout contract 以 `--phase closeout` 运行：验证 `HEAD` 是 evidence manifest 中 `implementationBaseCommit` 的后代、各 WP evidence/测试存在；不得要求相对固定审计快照或 implementation base 零代码 diff。
   - script-content test在Phase8缺GameplayProjection build/run时失败；这条门禁应已由WP0接入，WP10只防回归。
   - legacy boundary fixture在production新增`GameplayKbmFactTracker::GetSingleton()`读取时失败。
   - reviewed docs类型名与代码不一致时失败。

3. **运行命令**

```powershell
python tests/python/test_evaluate_mixed_input_trace.py
python tests/python/test_mixed_input_closeout_contracts.py --phase closeout
xmake run -y DualPadIngressTests
xmake run -y DualPadInputV2Tests
```

4. **预期红灯原因**
   - 当前没有evaluator、schema和反例fixture。
   - evaluator/schema、prepared commit/receipt反例和文档/Graphify/legacy boundary尚未认识新类型。

5. **最小实现**
   - 日志只在owner/gate/contributor/reset/presentation/engine decision变化或每10秒health摘要时写一行，不按1000 Hz report输出。
   - 保持WP0已加入的GameplayProjection build/run并由script-content test防回归。
   - RC readiness调用Phase8、evaluator、dynamic evidence checker、artifact manifest、Graphify和diff check。
   - 更新reviewed docs，不手写第二份generated事实表。

6. **focused绿灯**

```powershell
python tests/python/test_evaluate_mixed_input_trace.py
python tests/python/test_mixed_input_closeout_contracts.py --phase closeout
python scripts/ci/evaluate_mixed_input_trace.py tests/fixtures/mixed_input/good.jsonl
```

7. **相邻回归**

```powershell
powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1
powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest
python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout
git diff --check
```

8. **commit边界**
   - `test(ci): gate mixed input on causal runtime and live evidence`
   - `docs(ci): close mixed-input architecture and graph contracts`

9. **rollback条件**
   - 日志量随HID report线性增长；evaluator依赖机器路径；wrapper替代canonical target；缺实机证据仍误报GO；Graphify成功但target未真实运行；docs声称实机通过而无artifact，任一发生即回退close-out commit。

---

## H. 验证命令和退出标准

### H.1 focused、canonical与close-out矩阵

| 命令 | 它证明什么 | 它不证明什么 | 通过标准 |
|---|---|---|---|
| `xmake build -y DualPad` | 主插件和新增源在当前toolchain/CommonLib下编译链接 | 不证明hook ABI、runtime行为或实机稳定 | 退出0，runtime和tests使用同一实现源 |
| `xmake run -y DualPadIngressTests` | neutral/current/activity分离；KBM facts；causal cutoff；epoch/session；overflow/quarantine | 不证明Skyrim真实event list和camera/menu行为 | 500/1000 Hz neutral activity=0；held state连续；partial drain不提前应用 |
| `xmake run -y DualPadInputV2Tests` | Ingress->Assembler->Runtime->immutable publication；exact token；recovery；prompt隔离；engine shadow | 不证明1.5.97具体callsite或真实cursor坐标 | stable frame coherence一致；transition无action；mismatch零修改 |
| `xmake run -y DualPadGameplayProjectionTests` | cross-channel、candidate latch、200ms quiet、next-Poll/current-cycle纯决策、transient、Sprint | 不证明真实event payload可安全改写 | 每个时间视图每通道writer<=1；两种Sprint序列无gap |
| `xmake run -y DualPadPresentationProjectionTests` | routed activity、owner-timer 10px/120ms pointer、menu/nav/cursor/prompt分离、ack mailbox、Original gateway/TLS seam | 不证明真实Scaleform和OS/native坐标 | 无后续mouse event也按deadline一次promotion；无exact ack不commit；unscoped original |
| `xmake run -y DualPadNativeButtonCommitTests` | complete contributor、virtual bridge、one-shot release、backend唯一authority | 不证明真实SprintHandler全部语义 | 两序列一次press/一次final release；mask非0无clear；reset后0 |
| `xmake run -y DualPadPromptSnapshotTests` | prompt generation和其它surface解耦 | 不证明视觉glyph资源 | family-only change不改gameplay/engine/menu/cursor |
| `xmake run -y DualPadReplayTests` | 同trace确定性、enum兼容和snapshot generation | 不证明真实硬件时序 | golden无非预期diff；同seed摘要一致 |
| `xmake run -y DualPadPropertyTests` | 随机edge/reset/revision组合下的不变量 | 不证明用户手感或ABI | 固定/随机seed无反例；失败seed固化 |
| `xmake run -y DualPadFuzzRegressionTests` | overflow、乱序、重复edge、boundary交错不crash不stuck | 不证明支持外二进制 | corpus全绿，无mask/owner残留 |
| `python scripts/ci/evaluate_mixed_input_trace.py <trace>` | 低频host/实机trace是否违反B节合同 | 不代替玩家观察camera/cursor手感 | 0 neutral takeover、double writer、stuck held、stale apply、scope leak |
| `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` | 主目标、所有canonical host targets、DocGen、docs和legacy boundary统一门禁 | 不证明IDA/实机 | 所有step真实执行退出0；日志可见GameplayProjection build/run |
| `powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest` | Phase8、replay diff、Python、artifact、evidence、Graphify、diff总闸 | 不自动替代J节人工动作 | 退出0；manifest绑定测试commit/runtime/hash；evaluator PASS |
| `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout` | 代码图与最终主数据流同步 | 不替代编译/测试/IDA | 退出0；新producer/router/gate节点可查询 |
| `git diff --check` | patch基本格式无问题 | 不证明语义 | 退出0 |

### H.2 每个代码切片强制顺序

1. WP0 先完成一次性审计、冻结 `implementationBaseCommit`、核验 NativeButton 正文并补上 Phase8 canonical gate；该步骤不创建最终零差异检查。
2. WP0.5 先建立可独立编译的 batch transport/scaffold；WP1/WP2 不得调用尚未存在的 Hub API。
3. WP1 至 WP3 再依次填充 gamepad/KBM 事实语义和完整 causal boundary/recovery publication。
4. WP4启用next-Poll per-channel gate。
5. WP5只合入Poll receipt、prepared commit和current-cycle pure/shadow部分；I-P通过后才合入enable commit。
6. WP6实现Sprint逻辑与backend bridge；I-SPRINT决定是否出现额外guard commit。
7. WP7闭合recovery、稳定physical-code quarantine和optional reconcile。
8. WP8合入纯presentation、owner-timer pointer和cursor ack mailbox；I-CURSOR通过后才启用具体坐标side effect。
9. WP9先完成identity和Original gateway；I-0/I-1/I-2/I-MENU通过后才逐条件启用patch。
10. WP10最后更新evaluator、closeout CI、docs和Graphify，并以phase-aware closeout检查验证实现证据。
11. 任一I节gate未通过，相关shadow代码可留在开发分支，production capability仍标记NO-GO。

### H.3 自动化退出标准

必须同时满足：

- WP0、WP0.5 及 WP1 至 WP10 的红灯均曾稳定复现，并与预期红因一致。
- H.1所有适用命令退出0。
- Phase8日志明确显示`DualPadGameplayProjectionTests`已build和run。
- NativeButton target真实编译`SustainedContributorDecision`和backend seam。
- neutral HID在500/1000 Hz各owner rate下activity=0。
- unchanged non-neutral state在每个stable frame保留。
- current-cycle与next-Poll两个时间视图的writer count分别`<=1`。
- 每个 enabled current-cycle mutation 都能关联唯一、consume-once 的 Poll receipt；adapter failure 后 current-cycle-sensitive ledgers 与 previous state 一致。
- Sprint两序列仅一次aggregate press和一次final release。
- disconnect只换session并保留KBM；global reset换epoch并清virtual状态。
- causal latest 不越过累计 capture cutoff；空 capture 不回退 cutoff。overflow 不提交 ordered/semantic action，但保留 recovery-stamped 完整物理 analog/current-state 且不驱动 virtual output。
- prompt-only变化不改其它surface。
- synthetic helper 只有 provenance 已证明时才被排除；unproven 同 scancode event 不被吞并且 helper 冲突 route 保持关闭。
- unscoped/unknown engine query调用original。
- pointer candidate 即使没有后续 mouse event 也能在 owner deadline 一次性 promotion；cursor 无 verified consume-once ack 不 commit。
- evaluator能识别全部故意损坏fixture。
- legacy authority checker拒绝production新增旧singleton读取。
- Graphify、reviewed docs、generated docs、xmake source list与实现一致。

**编译、host tests和CI全绿只能证明模型和工程合同成立，不能描述为实机mixed-input已修复。**


---

## I. IDA / 调试器前置任务

### I.1 固定环境与记录格式

本节仅针对：

| 项目 | 固定值 |
|---|---|
| Runtime | Skyrim SE 1.5.97.0 |
| Image base | `0x140000000` |
| EXE SHA-256 | `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD` |
| Poll loop | `0x140C150B0` |
| gamepad Poll | `0x140C1AB40` |
| `XInputGetState` callsite | `0x140C1AB9D` |
| gamepad-enabled query | `0x140C15240` |
| shared 2D transform | `0x140705AE0` |
| menu SetPlatform | `0x140ECD970` |
| menu cursor update | `0x140ED2F90` |

动态样本统一写入`docs/research/skyrim_mixed_input_dynamic_evidence_zh.md`：

```text
binarySha256, dllCommit, dllSha256, pdbSha256,
sampleId, caseId, threadId, phaseOrdinal,
functionVa, returnAddress, callerFunction, callerDomain,
ownerTickToken, eventBatchToken, runtimeGeneration,
inputStateEpoch, gamepadSessionId, contextRevision, controlMapRevision,
pollReceiptSequence, pollReceiptThreadId, pollReceiptConsumeCount,
materializedPollPublicationGeneration, materializedPacketNumber,
currentCycleAuditFailure, preparedCommitDisposition, sensitiveLedgerHashBefore, sensitiveLedgerHashAfter,
inputDevice, inputSemantic, eventOrdinal, eventPayloadBefore, eventPayloadAfter,
originalEntryResult, originalDeviceVfuncResult, scopedResult,
lookOwner, moveOwner, combatOwner, digitalOwner,
menuOwner, cursorOwner, promptFamily,
registers, inputVector, outputVector, menuName,
result, notes
```

调试断点不得推进runtime。相关性字段来自最近一份immutable debug snapshot。

### I.2 Gate I-0：REL、vtable、original gateway与device availability身份

#### 静态阶段

1. 在真实1.5.97进程记录`REL::Relocation{67320}.address()`，要求与研究地址`0x140C15240`一致。
2. 导出入口前32 bytes、完整控制流、26个direct xrefs和original返回路径。
3. 记录`REL::Relocation{560029}.address()`的真实语义，dump handler vtable 0至9项。
4. 分别导出slot `0x7`和`0x8` target前64 bytes、签名、返回类型和副作用。
5. 在`0x140C15240`的`devices[kGamepad]->IsEnabled()`间接调用处记录runtime vtable pointer、slot offset和实际target。
6. 证明handler slot、delegate `BSIInputDevice` slot和`0x140C15240`之间的真实调用关系。不能从CommonLib逻辑slot直接推导handler index。

#### 动态阶段

断点：

- `0x140C150B0`
- `0x140C1AB40`
- `0x140C1AB9D`
- `0x140C15240`
- 静态阶段确认的device vfunc original target

A组：禁用当前entry behavior override和device vfunc replacement，仅保留virtual XInput producer。分别测试：

- controller connected neutral 30秒
- LS held 30秒
- RS held 30秒
- LT/RT held 30秒
- button press/held/release

B组：在A组基础上切换KBM最近活动、gamepad最近活动、gameplay、Inventory、Journal和remap。

记录：

```text
gamepad device pointer, handler/delegate pointer, vtable target,
connected, delegateReady, XInput return code, complete XInput state,
Poll count, original entry/vfunc result, current menu/remap,
compat patch group state
```

#### 唯一出口

- **结果A Native**：恢复完整original后，`0x140C1AB9D`仍按input cycle调用，non-neutral current-state持续消费。删除default device vfunc patch，availability policy=`Native`。
- **结果B ScopedConnectivity**：恢复original稳定阻断Poll/初始化，且可定位唯一封闭caller域。只允许在该域按`connected && delegateReady`覆盖。
- **失败**：REL/slot身份不唯一、只能靠global fixed true工作、caller域无法封闭或remap/transform/menu被连带改变。mixed-input保持NO-GO。

退出标准：REL到VA、入口bytes、handler vtable base、唯一slot index、original target、calling convention和availability结论全部唯一，并写入host identity fixture。

### I.3 Gate I-P：Poll、event materialization、InputFramePump与下游consumer顺序

该gate决定current-cycle adapter是否可启用。

#### 地址与断点

- `0x140C150B0`入口、四device Poll点、Gamepad Poll返回点
- `0x140C1AB9D`
- `UpstreamGamepadHook` receipt publish 与 mailbox consume 点
- event list构造完成点
- event-source dispatch entry/return
- `InputFramePump::ProcessEvent` entry、receipt consume、prepare、gate apply、audit commit、return
- 每个后续sink entry
- Look、Move、Combat、Jump、Sprint实际consumer
- `0x140888190`及其它已知二维event caller

#### 必须记录

```text
phase ordinal,
event head, each node address/type/device/id/userEvent/value/next,
XInput call sequence/thread/result, packet number and serialized Poll publication identity,
receipt publish/consume count and exact identity,
callback-local physical current/activation,
ownerTickToken, eventBatchToken, batch tail, assembled cutoff,
prepared current-cycle plan identity and consume-once token,
adapter audit/failure, sensitive ledgers before/after, commit disposition,
payload before/after,
first consumer and value actually read
```

event pointer只在当前callback记录，不缓存到callback外。

#### A/B动作

1. 上一cycle RS held，本cycle首次mouse delta。
2. 上一cycle LS held，本cycle首次mapped move press。
3. 上一cycle LT/RT held，本cycle首次physical attack。
4. virtual Jump与physical Jump同cycle。
5. G Sprint held后K加入；K Sprint held后G加入。
6. physical source release而gamepad current-state仍held。
7. global reset和gamepad disconnect分别发生在旧route event已materialize的cycle。
8. event位于list head、中间、尾部各20次。
9. 60 FPS和120 FPS各一轮。
10. 对每种 adapter failure 注入 provisional transient/Sprint/channel next state，确认 failure 后正式 state 不前进；另测无需 mutation 的 commit-safe 路径。
11. 人为制造 missing、duplicate、reused 与 cross-thread receipt，确认零修改且不以第二次 acquire 的最新帧补洞。

#### 唯一出口

- **结果A InputFramePump 可用**：InputFramePump 始终早于全部相关 consumer；callback 看到完整 list；consume-once receipt 与 XInput serialized packet 一一对应；`Prepare -> Apply -> Commit` 位于同一调用栈；adapter failure 不推进 sensitive ledger；目标 event 可 in-place neutralize 且不伪造 release、不破坏 queue ownership。启用 adapter。
- **结果B 更早共同点**：InputFramePump 过晚或接口不能安全处理，但存在 pure decision 已 prepare、consumer 尚未读取的更早共同 mutable point。将同一 callback-local plan/adapter 下移到该点，并保留 receipt 与 audit commit 合同；不得退回跨 callback publication。
- **结果C 不可闭合**：存在 pre-dispatch gameplay consumer、receipt 无法与该 input cycle 唯一配对，或无法同时满足“decision 已 prepare”和“任何 consumer 未读取”。对应 channel 保持 NO-GO，不用下一 Poll gate 冒充零泄漏。

停止标准：Look/Move/Combat/Transient/Sprint相关event在gameplay和至少10个代表context中各20次争夺均只有一个下游semantic writer；head/middle/tail均有已证明策略；未支持event type明确fail-closed。

### I.4 Gate I-1：分类`0x140C15240`全部26个direct xrefs

断点条件：`RIP==0x140C15240`且`[RSP]`属于导出的26个return-address集合。

每个caller记录：

- return address和caller function
- gameplay、camera、menu platform、menu transform、cursor、remap、poll/init、other分类
- query发生在owner publication之前还是之后
- 原始返回值
- 返回值是局部branch使用，还是写入跨caller全局cache
- 当前输入source/semantic、owner snapshot、availability decision、menu stack

A/B动作：

- 完全idle
- mouse look
- right-stick look
- mapped keyboard move
- left-stick move
- KBM combat
- LT/RT
- Sprint
- Inventory/Journal/Map/Favorites
- keyboard/gamepad menu navigation
- mouse/gamepad cursor
- remap

退出标准：

- 26个xref全部静态命名或标注。
- 可能影响gameplay、camera、poll/init的caller全部有动态样本。
- 每个caller被归为`AfterOwnerPublication`、`EventLocalSource`或`OriginalOnly`。
- 任何写入跨callerglobal cache的caller不得按同cycle不同source返回不同bool；它必须下移scope或保持Original。
- release-relevant unknown为0。

### I.5 Gate I-2：恢复`0x140705AE0` ABI、输入语义和分支效果

断点：

- `0x140705AE0`入口、内部`0x140C15240`调用、全部返回点
- 已知caller `0x140707110`、`0x140888190`、`0x1408E1560`、`0x1408E1620`
- 新发现的全部实到caller

记录：

```text
RCX/RDX/R8/R9, XMM0-XMM5, stack 0x80 bytes,
input vector address/value, output vector,
acceleration/timer state fields,
internal original query result,
caller domain, event source,
owner publication phase
```

A/B：对同一mouse delta、同一RS current-state、同一WASD/LS和menu cursor输入分别测试Native、debug force false、debug force true。每组30次，60/120 FPS各一轮。

唯一出口：

- **结果A NativePassThrough**：native chain下各类caller进入与实际source一致的数学且无异常手感。所有transform override保持禁用。
- **结果B ScopedOverride**：至少一个已分类caller稳定进入错误分支，强制正确source可修复，ABI/caller/caching完整闭合。优先上游source-specific scope；无更窄入口时才允许入口router。
- **失败**：vector身份不清、caller混杂、强制分支副作用、gateway不完整或signature不稳。保持Original和NO-GO。

不复制Bethesda数学，只调用原函数并改变局部query scope。

### I.6 Gate I-MENU：锁定`_root.SetPlatform`最小作用域

地址：

- `0x140ECD970`
- 函数内query callsite
- Scaleform `_root.SetPlatform` invoke点
- `SkyrimCompatibilitySurface::DoRefreshMenus`及owned callback/`RefreshPlatform`

实验A：不patch内部callsite，只在现有refresh调用外压入RAII `MenuSetPlatform` scope。

实验B：只有A留下可见错误初次平台时，调试构建临时覆盖内部query callsite。

动作：mouse/gamepad打开Inventory、Journal、Map、Favorites；菜单内KBM->GP->KBM导航；neutral HID持续；快速关闭/重开。

记录：menu/movie identity、presentation epoch、original query、scope result、SetPlatform参数、native与DualPad-induced调用次数。

唯一出口：

- **结果A**：外层scope使每owner/context epoch最多一次DualPad refresh，最终平台正确且无可见错误帧。不安装direct callsite patch。
- **结果B**：A稳定留下错误初始平台，内部单一callsite可签名、可回退且不影响transform/remap/poll。允许独立条件patch。
- **失败**：多个不可封闭query点、scope泄漏、refresh storm或必须扩大成global owner hook。menu mixed保持NO-GO。

### I.7 Gate I-CURSOR：恢复`0x140ED2F90`双方向坐标合同

断点：

- mouse分支入口
- `GetCursorPos`返回
- `GetWindowInfo`返回
- 坐标换算完成点
- gamepad分支读取内部position点
- movie notification点
- `MenuCursor.cursorPosX/Y` read/write watchpoint

记录：screen/client coords、window/client rect、DPI、window mode、safe-zone、GFx viewport、内部position、可见cursor、menu/movie identity、context/presentation epoch。

动作：Inventory、Magic、Map、Journal四类菜单；左上、上中、右上、中心、左下、下中、右下七点；fullscreen和borderless/windowed；两个handoff方向各20次。

每个方向独立裁决：

- **无需同步**：切owner后首帧误差不超过4 physical px且无拉扯，允许直接commit。
- **需要同步**：存在唯一可逆公式，write/read-back一致，UI task同步后误差达标，允许plan/ack commit。
- **无法证明**：保持旧cursor owner，该方向mixed cursor NO-GO。

stale menu/movie identity必须零写入。

### I.8 Gate I-SPRINT：真实held-state与release语义

通过CommonLib VTABLE定位：

- `SprintHandler::ProcessButton`
- `HeldStateHandler::UpdateHeldStateActive`
- `HeldStateHandler::SetHeldStateActive`

对`heldStateActive`和`triggerReleaseEvent`设置hardware watchpoint。

执行：

1. G -> K -> release G -> release K，20次。
2. K -> G -> release K -> release G，20次。
3. Mouse替代Keyboard，两组各20次。
4. disconnect发生在G|K阶段，20次。

记录：event source/id/phase、current contributor mask、virtual bridge、suppressed edge、字段写入者、实际player sprint状态。

唯一出口：

- **结果A 无guard**：bridge和edge suppression下，remaining mask非0时`heldStateActive`从不false，动画/速度不中断。不创建guard。
- **结果B 条件guard**：remaining mask非0时仍被原生release写false，且可定位单一release clear点。创建只针对Sprint、同代immutable snapshot的guard。
- **失败**：需要拦截其它held action、snapshot时序不稳或无法只定位Sprint。mixed Sprint NO-GO。

### I.9 Gate I-KBM：complete raw-state provider与synthetic provenance

以`0x140C150B0` device loop为锚点，在Keyboard/Mouse Poll返回后采样`devices[0]`和`devices[1]`。

目标：定位稳定只读的physical key/button current-state API，不写device memory。

测试：

- 至少四个ControlMap动态映射键和两个mouse buttons
- press/hold/release
- Alt-Tab/focus lost
- ControlMap remap
- DualPad synthetic helper injection
- 同scancode真实physical紧随synthetic

provider必须满足：

- held期间每owner tick为down。
- release后下一owner tick为up。
- remap不改变physical code读取。
- synthetic provenance 试验必须回答“是否存在 event-level origin 或 verified physical-only current-state 可区分性”。仅命中 scancode、phase、context/output generation 和短时间窗不算证明。
- focus lost返回`complete=false`，不能伪造all-up。

raw-state provider 独立出口：

- **结果A 启用reconcile**：complete、physical-only provenance稳定，允许产生`ReconciledRelease`。
- **结果B 禁用reconcile**：读口不稳定或provenance不完整。继续quarantine/fresh-press恢复，不阻断其它mixed能力。

synthetic provenance 独立出口：

- **结果 S-A 精确来源可证**：存在稳定 event-level injection receipt/origin，且紧随其后的真实同 scancode physical event 不被遮蔽。允许 exact suppression。
- **结果 S-B 仅 reserved control 可证**：无法给普通 event 标记来源，但可证明一组 helper controls 与 physical mapping 不冲突。只允许这些 reserved controls 使用 suppression。
- **结果 S-C 来源不可证**：普通 synthetic 与真实同 scancode event 在观察面不可区分。禁止基于 token/time-window 吞 event；关闭冲突 helper route，或把 event 按 physical 输入处理。该结果不阻断不依赖 helper 的 mixed gameplay，但对应 helper-backed capability 保持 `NO-GO`。

### I.10 Gate I-5：hook transaction、signature与partial rollback

为以下site记录前32 bytes、instruction boundary、relocation target和恢复字节：

- verified `REL::ID 67320` entry/original gateway
- I-0确认的device vfunc slot
- `0x140ED2F90`相关cursor入口或现有cursor bool surface
- 条件menu direct callsite
- 条件shared transform入口/callsite
- 条件Sprint guard clear point
- 条件current-cycle更早共同mutable point

安装前验证：runtime version、module hash或强signature、REL VA、目标bytes、vtable base、slot index和original target。

patch group拆分：

1. `VerifiedEngineQueryGateway`
2. `VerifiedDeviceAvailabilityScope`
3. `CurrentCycleEventGate`
4. `MenuRefreshScope`
5. `MenuPlatformDirectCallsite`
6. `CursorHandoff`
7. `Shared2DTransformScope`
8. `SprintHeldStateGuard`

必需identity/gateway失败时整个mixed compatibility group不提交，virtual gameplay fail-closed。可选group失败时回到Original/旧cursor并保持对应capability NO-GO。

退出标准：安装、卸载、重复初始化、故意破坏entry、交换0x7/0x8、破坏optional site和模拟partial failure均无残留。

### I.11 停止继续逆向的条件

满足下列条件后停止扩大逆向范围：

- I-0唯一闭合REL、vtable和availability结论。
- I-P得到A或B出口，或明确C并保持对应channel NO-GO。
- 26个query xrefs全部分类，release-relevant unknown为0。
- `0x140705AE0` ABI、实到caller和A/B结果闭合。
- menu得到外层scopeA或direct-callsiteB的唯一裁决。
- cursor两个方向分别得到无需同步、需同步或禁用的唯一裁决。
- Sprint得到无guard或条件guard的唯一裁决。
- KBM raw provider 得到启用/禁用 reconcile 的唯一裁决，synthetic provenance 独立得到 S-A/S-B/S-C 裁决。
- hook transaction所有启用site均有稳定signature和rollback。
- 60/120 FPS、fullscreen/windowed、load/reopen重复测试没有新caller或新ABI分支。

无需给与本任务无关的UI helper命名，也无需重写Bethesda响应曲线。


---

## J. 实机验收矩阵

### J.1 低频日志与evaluator合同

日志只在以下时机输出：owner/gate/contributor/reset/presentation/engine decision变化、hook install/uninstall、unknown caller、current-cycle failure、cursor ack、每10秒health摘要。不得每份HID report输出。

每条JSONL至少包含：

```text
schemaVersion, sessionId, caseId,
buildCommit, dllSha256, pdbSha256, skyrimRuntime, binarySha256,
ownerTickToken, eventBatchToken, runtimeGeneration, captureGeneration,
inputStateEpoch, gamepadSessionId, contextRevision, controlMapRevision,
orderedCutoffSeq, sourceBatchTailSeq, currentBatchAssembled,
padStateGeneration, padActivityGeneration, kbmFactsGeneration,
kbmBaseline, kbmSemanticHeldMask, kbmRawDownCount, kbmQuarantineCount,
meaningfulSourceSeq, meaningfulSourceKind, routedSourceDomain,
lookOwner, moveOwner, combatOwner, digitalOwner,
lookGate, moveGate, combatGate, digitalGate,
gamepadCandidateLook, gamepadCandidateMove, gamepadCandidateCombat,
mouseQuietUntilOwnerUs,
materializedPollPublicationGeneration, materializedPollOwnerTickToken,
materializedPollInputStateEpoch, materializedPollGamepadSessionId,
pollReceiptSequence, pollReceiptThreadId, pollReceiptConsumeCount, materializedPacketNumber,
currentCycleGateApplied, currentCycleGateFailure, preparedCommitDisposition,
sensitiveLedgerHashBefore, sensitiveLedgerHashAfter,
currentEventLookWriters, currentEventMoveWriters,
currentEventCombatWriters, currentEventTransientWriters,
nextPollLookWriters, nextPollMoveWriters,
nextPollCombatWriters, nextPollTransientWriters,
transientActionId, transientDecision, transientDedupKey,
sustainedActionId, sustainedSourceMask, virtualBridgeDesired,
virtualMaterialized, nativePressCount, nativeReleaseCount,
menuOwner, navigationOwner, menuEpoch, menuRefreshCount,
cursorRequestedOwner, cursorCommittedOwner, cursorPlanToken,
cursorAckResult, cursorSyncErrorPx,
promptFamily, promptRevision, promptReason,
deviceAvailabilityPolicy, deviceAvailabilityDomain, deviceAvailabilityResult,
engineQueryCallerRva, engineQueryDomain, engineOverrideEnabled,
engineOriginalResult, engineScopedResult,
resetScope, resetReasons, recoveryState,
hookGroupHealth, overflowCount
```

Evaluator硬规则：

1. neutral/unchanged HID不得推进`padActivityGeneration`或触发owner takeover。
2. 每个owner tick、每个时间视图、每个gameplay通道writer count必须`<=1`。
3. 同一`TransientDedupKey`最多apply一次press。
4. sustained mask非0时aggregate held必须true；joining press和non-final release不得制造额外native edge。
5. mask变0后最终release恰好一次，virtual held不得stuck。
6. `inputStateEpoch`变化后旧epochfacts/plan不得apply；`gamepadSessionId`变化后旧sessionfacts/plan不得apply。
7. disconnect不得清完整KBM held或physical Sprint contributor。
8. quarantine非空或raw complete=false时baseline不得Clean。
9. lost release只能由verified reconcile、对应release或fresh initial-press epoch恢复，不得出现timeout recovery reason。
10. prompt-only变化不得改变gameplay、engine、menu或cursor hashes。
11. unscoped/unknown/remap engine query必须Original。
12. cursor ownercommit前必须存在同token/identity/epoch的成功ack，或该方向已由I-CURSOR证明无需sync。
13. synthetic helper 只有在 provenance verdict 为 S-A/S-B 且 exact 条件成立时才不得进入 physical held/source activity/current-cycle 竞争；S-C 下真实同 scancode event 必须保留，冲突 helper route 必须关闭。
14. enabled current-cycle mutation 必须有唯一 Poll receipt；adapter audit failure 后 sensitive ledger hash 必须等于 previous，且 prepared token 不得再次 commit。
15. `orderedCutoffSeq` 单调不减；overflow 后 ordered/semantic action 不得出现，但 complete physical analog/current-state 可带 recovery flag 保留且不能驱动 virtual output。
16. session结束时owners/gates/contributors/pending pulse/cursor plan/ack mailbox全部回初值。

### J.2 最短smoke

| ID | 操作步骤 | 预期可观察结果 | evaluator重点字段 | PASS / FAIL与合同 |
|---|---|---|---|---|
| S1 idle controller + KBM | DualSense连接静置60秒；持续mouse look、mapped move和KBM combat | KBM连续可用；neutral手柄不抢owner/prompt/menu/cursor/engine；virtual Poll按I-0裁决工作 | pad state/activity、source seq、owners、availability | activity不增长、无takeover；Poll停止或fixed global override即fail，B.1/I-0 |
| S2 mouse look + gamepad move | 持续mouse环视，同时LS移动30秒 | camera只跟mouse；移动只跟LS；RS current-cycle和next-Poll均不写Look | Look/Move owners、current/next writer、RS/LS、engine look domain | Look=KBM、Move=GP；任一时间视图双writer即fail，B.3 |
| S3 gamepad look + keyboard move | 持续RS环视，同时交替mapped move keys30秒 | camera跟RS；Move只由keyboard；KBM Move不全局翻look transform | owners、gates、engine caller/domain | Look=GP、Move=KBM；engine不随Move抖动，B.3/C.2 |
| S4 同通道争夺 | mouse+RS、keyboard+LS、KBM combat+LT/RT，各30秒 | physical优先；停止physical后按candidate规则恢复；无双写 | candidate、quiet deadline、writer counts | 每组两时间视图writer<=1；恢复符合0/1 cycle合同，B.2/B.3 |
| S5 transient | physical Jump与gamepad Jump同cycle20次；Activate重复 | 每次只触发一次；pending virtual遇physical先cancel | dedup key、current-cycle disposition、dispatch count | 20次均single dispatch，B.3 transient |
| S6 Sprint双序列 | 执行G->K->松G->松K；K->G->松K->松G，各20次 | Sprint不中断、不重复press，最后一source release才停止 | mask、bridge、native edge count、handler probe | 每序列一次press一次release，mask非0无inactive，B.3 Sprint |
| S7 menu/platform/cursor | 用mouse和gamepad分别打开Inventory/Journal；切strong nav、pointer-only和cursor | platform/nav稳定；10px/120ms只切cursor/prompt；cursor无跳 | menu epoch/refresh、cursor plan/ack/error、prompt | 每epoch最多一次refresh；误差<=I-CURSOR阈值；无ack不commit，B.3 menu |
| S8 disconnect/reset | G Sprint+K Sprint同时held时拔手柄；重连；开关菜单 | gamepad state立即neutral；K Sprint连续；旧session不复活 | session、epoch、reset scope、mask、Poll | 断连只清GP；K final release一次；旧sessionapply=0，B.3 recovery |
| S9 regression | 平滑转动两摇杆；Journal L2/R2切页20次 | 摇杆无旧卡顿；Journal trigger修复不回归 | pad generation、Poll frame、action摘要 | 任一旧问题复发即fail |

Smoke全部通过后才能进入完整矩阵。任一失败先按合同编号定位，不通过重复跑获得偶然绿灯。

### J.3 完整矩阵

| ID | 操作步骤 | 预期可观察结果 | 必须记录 | PASS / FAIL定位 |
|---|---|---|---|---|
| F1 频率交错 | 500 Hz和1000 Hz各一轮；owner 30/60/120 Hz；每轮执行KBM press/release/mouse delta | report rate只推进current-state；owner/activity结果与rate无关 | report count、pad gen、activity gen、owner transitions | neutral activity>0或结果随rate变化，WP1失败 |
| F2 unchanged held | RS/LS分别0.60保持30秒，中间交替KBM其它通道 | held state不丢；无新activity；已有owner维持 | axis、pad gen、activity gen、owners | axis掉0、owner丢失或activity线性增长，WP1/WP4失败 |
| F3 Look quiet/candidate | mouse+RS，停mouse；测试199ms、201ms，candidate true/false | 199ms仍KBM；201ms按candidate+threshold规则恢复 | quietUntil、candidate、magnitude、reason | 与B.2不一致，WP4失败 |
| F4 Move candidate | LS先跨enter后被keyboard抢占；再松keyboard；另测未跨enter的LS0.16 | latched可按sustain同tick恢复；未latched0.16不enter | candidate、held mask、owner/gate | 错误reclaim或要求新activity，WP4失败 |
| F5 Combat candidate | trigger先跨enter后被KBM combat抢占；最后release | 整个Combat通道single writer，按candidate恢复 | trigger、physical mask、owner、writer | 双写或左右trigger一侧漏gate，WP4/WP5失败 |
| F6 transient adjacency | physical/gamepad同tick、相邻tick、physical release+GP press | 同key只一次；release不误当activation | seq、dedup、disposition | duplicate或合法GP press被吞，WP5失败 |
| F7 Sprint pressure | 两序列各50轮，hold 0.1至2秒随机；Keyboard/Mouse各一组 | 无gap、无duplicate、最终必release | mask、bridge、slot state、handler field | 任一中间clear/stuck/重复edge，WP6/I-SPRINT失败 |
| F8 synthetic provenance | helper injection后立即同scancode真实physical；分别运行S-A/S-B/S-C fixture | S-A仅exact receipt被排除；S-B仅reserved control；S-C不吞真实event并关闭冲突helper route | provenance verdict、injection receipt、raw/semantic/activity | 仅凭时间窗吞真实key，或S-C仍开启冲突route，WP2/I-KBM失败 |
| F9 context boundary | held Look/Move/Sprint依次进入Inventory、Journal、Dialogue、Console、loading | transition neutral，旧press不跨界；quarantine按证据rearm | epoch、baseline、quarantine、owners | old epoch apply或stuck，WP7失败 |
| F10 ControlMap reload | held Move/Sprint时改绑并重排binding index；保持旧key；release/fresh press新key | old semantic清；quarantine仍按稳定(device,idCode)指向旧key；new mapping与首批event同boundary生效 | map revision、fingerprint、binding gen、physical-code quarantine | index重用认错key、首event被旧revision拒绝、旧press重解释或永不rearm，WP2/WP3/WP7失败 |
| F11 lost release | test-only丢KBM release；provider启用/禁用各一轮 | provider通过时一次reconcile；否则quarantine等待release/fresh press | provider complete/provenance、reconcile seq、quarantine | timeout猜测或误清真实held，I-KBM/WP7失败 |
| F12 overflow/gap | test-only overflow和sequence gap，analog held并在overflow report改变轴值 | ordered/semantic不partial apply；old held不重附着；新完整物理axis以recovery latest保留；恢复前virtual fail-closed | cumulative cutoff/tail、epoch、physical latest、eligibility、dispatch | 脏action、old held复活、cutoff回退或analog冻结，WP0.5/WP3/WP7失败 |
| F13 prompt隔离 | mouse Look保持，周期按gamepad非Look；反向保持RS按keyboard非Move | glyph可变；gameplay/engine/menu/cursor不变 | prompt seq/reason、surface hashes | prompt反写其它surface，WP8失败 |
| F14 menu pointer | gamepad owner菜单内mouse 9px/119ms；10px在50ms完成后不再移动并等到120ms；candidate后插入更高seq gamepad strong；click/wheel | 前者不切；owner deadline后一次性只切cursor/prompt；较新strong使旧candidate失效；click/wheel切menu | pending seq、deadline、promotion count、owner/nav/cursor/prompt | 需要额外mouse event、重复promotion、旧timer反抢或pointer-only翻SetPlatform，WP8失败 |
| F15 cursor lifecycle | 四菜单、七位置、两window mode，两个方向各20次；排队后关/换菜单 | 证据批准方向误差达标；stale task零写；失败保持旧owner | identity、coords、ack、error px | stale写、无ack commit或误差超阈值，I-CURSOR/WP8失败 |
| F16 disconnect/reconnect | neutral、stick、trigger、G-only Sprint、G∪K Sprint状态各拔插20次 | 1 owner tick内GP neutral；K held保留；neutral reconnect不抢owner | session、epoch、scope、mask、Poll | disconnect误清KBM或old session pulse，WP7失败 |
| F17 engine caller | 按I节动作覆盖poll/init/gameplay/menu/cursor/remap；shadow与enable对比 | unscoped original；只批准domain覆盖；unknown/remap original | caller RVA、causality、original/scoped | scope泄漏、unknown override、global jitter，WP9/I-1/I-2失败 |
| F18 current-cycle positions/audit | Look/Move/Combat/Transient event分别处于head/middle/tail，并逐类注入adapter failure | success时target virtual输家安全中和；failure时physical/payload不变、sensitive ledger不前进、next Poll fail-closed | receipt、event ordinal、payload、audit、ledger hashes、consumer | unsafe node、pre-consumer读取或failure后state前进，I-P/WP5失败 |
| F19 clock-domain/receipt | receipt证明materialized frame41，InputFramePump前HID变neutral/换方向；另让最新publication变frame42；有/无physical竞争 | current-cycle始终绑定receipt frame41；无竞争Keep，有physical抑制；next-Poll用new state；不得re-acquire混用42 | receipt call/thread/identity、later HID gen、current/next decisions | later HID倒灌、latest frame冒充materialized或receipt复用，WP5失败 |
| F20 focus/lifecycle | Alt-Tab、读档、死亡/复活、快速旅行各10轮 | global reset后无stuck，首个新输入正常 | epoch、baseline、first post-reset edge | old epoch apply或无法rearm，WP7失败 |
| F21 RuntimeOwner migration | owner callback在两个OS thread间串行迁移；并发故意注入 | 串行rebind成功，并发writer fail-closed | thread id、ticket、generation、fault | 固定thread绑定或并发mutation，E.1失败 |
| F22 Favorites gate | 按基线支持方式打开/关闭Favorites并切换来源 | native gate语义逐字保持，无新SWF依赖 | existing route/health | 与基线不同，立即回退相关commit |

### J.4 Soak层

| ID | 时长/次数 | 操作 | 自动判定标准 |
|---|---:|---|---|
| K1 gameplay mixed soak | 30分钟 | 每2分钟轮换mouse+LS、RS+keyboard、KBM combat+GP其它通道、transient、Sprint两序列 | neutral takeover=0；两个时间视图double writer=0；duplicate transient=0；stuck sustained=0 |
| K2 idle/active soak | 2小时 | controller持续连接；前30分钟idle，随后每5分钟做一次KBM和GP操作 | idle段activity不随report增长；memory/queue/log稳定；KBM不被idle GP抢回 |
| K3 menu/cursor soak | 200次handoff | Inventory/Map/Journal/Container循环strong nav、pointer、open/close | 每epochrefresh<=1；stale write=0；批准方向cursor error不超阈值；无ack commit=0 |
| K4 reconnect soak | 50次 | 在neutral、stick、trigger、G-only Sprint、G∪K Sprint下断连重连 | 每次1 owner tick内GP neutral；old session apply=0；K contributor不中断 |
| K5 recovery host soak | 1000随机序列 | overflow、gap、reload、focus、synthetic、lost release、boundary交错 | 每序列最终clean；无partial apply；quarantine只由证据drain |
| K6 engine shadow soak | 30分钟 | 全部gameplay/menu动作，记录Original与shadow建议 | release-relevant unknown=0；scope leak=0；未enable caller返回Original |

### J.5 实机证据包

每个候选发布保留：

- `mixed_input_smoke_<session>.jsonl`和evaluator摘要。
- `mixed_input_full_<session>.jsonl`和矩阵结果。
- K1至K6 soak摘要。
- `skyrim_mixed_input_dynamic_evidence_zh.md`，绑定EXE/DLL/PDB hash。
- build commit、artifact manifest、config hash。
- hook identity/signature manifest。
- 失败trace、合同编号和对应feature flag状态。

证据包不得包含用户私有路径或高频原始输入流。

---

## K. 迁移、回退和发布门禁

### K.1 平滑迁移顺序

| 阶段 | production authority | shadow内容 | 进入下一阶段条件 |
|---|---|---|---|
| M-1 baseline | 当前代码 | audit snapshot、implementation base、file/content/Phase8 preflight | WP0绿灯；base已冻结且不存在最终零diff门禁 |
| M-0 ingress scaffold | 当前authority不变 | batch API、累计cutoff、optional slots | WP0.5 compile/atomicity fixtures全绿 |
| M0 activity shadow | 旧presentation evidence | new classifier meaningful/neutral差异 | neutral activity=0，真实activity无漏报 |
| M1 activity authority | new classifier + existing current-state | old evidence只记录 | WP1全部自动化和S1通过 |
| M2 KBM facts shadow | 六个KBM policy仍false | new complete facts vs legacy bounded compare | mapping/synthetic/quarantine一致，无generation撕裂 |
| M3 next-Poll channel authority | new per-channel gate | oldprojection只记录 | WP4和S2-S4通过 |
| M4 current-cycle shadow | 不改event | Poll receipt、prepared plan、adapter audit与rollback hash | I-P得到A或B出口，failure不推进ledger |
| M5 current-cycle enable | verified event type逐项启用 | downstream observer | 对应channel S/F通过；未验证type保持NO-GO |
| M6 sustained authority | new contributor + existingPollCommitSlot | oldsingle-emitter compare | WP6、I-SPRINT、S6/F7通过 |
| M7 scoped recovery | epoch/session/quarantine | old reset telemetry | WP7、S8、F9-F12/F16通过 |
| M8 atomic presentation | new routed activity/menu/cursor/prompt | oldcombined owner compare | WP8、I-CURSOR、S7/F13-F15通过 |
| M9 engine Original gateway | unscoped Original | caller shadow table | I-0/I-1 identity与causality闭合 |
| M10 scoped enable | menu/availability/transform逐条件caller | original A/B | I节对应唯一出口和F17/K6通过 |
| M11 legacy close-out | input_v2唯一authority | 无 | WP10、Phase8、RC、Graphify全绿 |

Shadow必须与authority消费同一`AssembledFactFrame`，不能另读global latest。allowlist外差异立即失败，不能靠扩大ignore名单掩盖。

### K.2 feature flags与回退边界

| 开关 | 控制范围 | 回退后安全状态 |
|---|---|---|
| `mixed_input_activity_classifier` | meaningful gamepad activity | 回旧evidence仅供诊断；mixed NO-GO；pad current-state不丢 |
| `mixed_input_kbm_facts` | KBM facts publication | 六字段回false；不恢复legacy live writer |
| `mixed_input_channel_arbitration` | next-Poll per-channel owner/gate | 旧projection；mixed capability关闭 |
| `current_cycle_event_gate` | 当前event输家中和 | shadow-only；对应channel NO-GO |
| `mixed_input_sustained_aggregate` | Sprint source OR与bridge | 先clear全部virtual held，再回gamepad-only；mixed Sprint关闭 |
| `mixed_input_scoped_recovery` | epoch/session/quarantine | 整组回退并要求重启session；不得部分混用 |
| `mixed_input_atomic_presentation` | routed activity/menu/cursor/prompt | cursor回原生；menu mixed NO-GO |
| `mixed_input_cursor_handoff` | 具体坐标side effect | 保持旧cursor owner |
| `engine_query_original_gateway` | unscoped Original和identity gate | 完整卸载compat behavior group |
| `menu_platform_scope` | menu RAII scope | menu回Original，menu mixed NO-GO |
| `device_availability_scope` | verified poll/init availability | 回Native，mixed NO-GO |
| `engine_transform_scope_<caller>` | 单caller transform override | 该caller回Original |
| `sprint_held_state_guard` | 条件Sprint release clear guard | 卸载guard，mixed Sprint NO-GO |
| `kbm_raw_state_reconcile` | provider-based release reconcile | 继续quarantine/fresh-press恢复 |
| `synthetic_helper_provenance` | 仅I-KBM S-A/S-B批准的helper event排除 | S-C时关闭冲突helper route或按physical处理，不使用时间窗吞event |

回退顺序：

1. 停止新的virtual press/pulse。
2. 发布一次neutral/clear计划，释放受影响virtual held。
3. 清current-cycle plan、one-shot token和pending cursor plan。
4. 卸载可选patch或关闭flag。
5. 恢复Original/旧安全路径。
6. 不在held状态中热切换两套authority。

### K.3 commit级回退

| slice | 独立回退内容 | 强制回退条件 |
|---|---|---|
| WP0.5 | ingress batch scaffold | API依赖未来WP才编译、cutoff回退、source list分叉、意外接管authority |
| WP1 | classifier和live HID activity接线 | neutral takeover、held state丢失、activity漏报 |
| WP2 | KBM adapter/producer | stable physical identity、synthetic provenance或boundary observation不可靠；core依赖RE/legacy |
| WP3 | causal tail/boundary/epoch/session | latest越累计cutoff、overflow制造action或冻结analog、disconnect误清KBM、首批new mapping event错配 |
| WP4 | per-channel next-Poll | cross-channel失效、candidate错误、quiet被HID续期 |
| WP5 shadow | receipt、prepared plan/audit | clock-domain、receipt pairing或prepared rollback模型自相矛盾 |
| WP5 enable | event adapter | physical被吞、pre-consumer、unsafe event shape、failure后ledger仍推进 |
| WP6 | sustained aggregate/bridge | gap、duplicate、stuck、影响其它actions |
| WP7 | scoped recovery | reset后不clean、quarantine永久、provider误清 |
| WP8 | atomic presentation/cursor | SetPlatform抖动、stale write、prompt反写 |
| WP9 A-C | identity/Original/shadow | REL/slot不稳、unscoped非Original、partial residue |
| WP9 D-G | 各条件patch | 对应I节证据失效、实机异常或scope leak |
| WP10 | evaluator/CI/docs | 漏跑canonical target、缺证据误报GO、日志爆量 |

### K.4 hardcoded false与idle HID具体迁移

1. `DualPadRuntime.cpp:394-401`不直接改成全局bool读取。只从同代stable frame计算：
   - baseline Clean、epoch/revision匹配时填真实facts；
   - unclean、marker缺失或skew时全部false并fail-closed。
2. `HidReader.cpp:90-94`不再为每report发布gamepad evidence：
   - 每report仍发布`LatestPadState`；
   - classifier仅在meaningful时发布activity；
   - connection只更新session/connectivity。
3. `SourceEvidenceCollector::RecordGamepadEvidence(true)`从live HID入口移除。兼容API不得决定gameplay、engine或prompt。
4. 初次启用KBM producer、reload、overflow、gap或focus lost时进入quarantine。semantic mask清零但physical evidence保留；不禁用Skyrim原生KBM。

### K.5 Favorites、SWF与范围保护

- Favorites native gate和默认值保持逐字节语义不变。
- `Interface/**`、SWF、glyph视觉资源、haptics、rumble和无关bindings不得出现在mixed-input commit。
- RC脚本加入allow-path检查，出现禁止路径即失败。
- F22和现有Favorites tests必须通过。

### K.6 mixed-input必须标记NO-GO的条件

任一成立即NO-GO：

1. 审计 snapshot、`implementationBaseCommit` 或受影响文件无法核验且未重做审计；或者 final close-out 错误要求相对旧 snapshot/base 零代码差异。
2. `REL::ID 67320`、`REL::ID 560029`、vtable slot 或 original target 身份不唯一。
3. neutral/unchanged HID 产生 takeover activity，或 HID draft 携带 context-dependent gameplay channel/action。
4. unchanged non-neutral current-state 在 stable frame 丢失。
5. current-cycle 或 next-Poll 任一通道 writer count 大于 1。
6. current-cycle mutation 没有唯一 consume-once Poll receipt，或用重新 acquire 的最新帧冒充 materialized identity。
7. later HID latest 倒灌改写已 materialize、无 physical 竞争的 event。
8. InputFramePump 之前已有 consumer 且找不到更早安全共同点。
9. adapter audit 失败后 transient、Sprint contributor、channel 或其它 current-cycle-sensitive ledger 仍推进，或 prepared token 被重复 commit。
10. transient 同 action 双触发或 cancel 顺序错误。
11. Sprint mask 非 0 出现 inactive、重复 press 或重复 final release。
12. gamepad disconnect 复用 global reset、误清 KBM owner/held，或 old session facts/plan 应用。
13. context/ControlMap 首批 batch 未与现有 `IngressBoundaryKey` 原子推进，空 capture 使 cutoff 回退，或 old epoch facts 重附着。
14. overflow 产生 ordered/semantic action、old held 复活，或丢失本可安全保留的完整物理 analog/current-state。
15. KBM quarantine 按 binding index 持久化，ControlMap 重排后认错物理键。
16. lost release 只能靠 timeout、sleep、长 lease 或禁用输入恢复。
17. synthetic provenance 未证明却仍按 token/time-window 吞 event，或 S-C 下冲突 helper route 仍启用。
18. prompt family 影响 gameplay、engine、menu 或 cursor。
19. connectivity、MoveOwner、CombatOwner 或 prompt 全局翻转未分类 engine caller。
20. unknown/remap/unscoped query 没有调用 original。
21. menu scope 泄漏、同 epoch 重复 refresh 或 direct callsite 无稳定 signature。
22. pointer candidate 必须依赖后续 mouse event 才 promotion、旧 timer 反抢更新 strong source、cursor 无 exact consume-once ack 却 commit、stale task 写入，或误差超过 I-CURSOR 阈值。
23. Sprint guard 影响其它 held action 或 stale snapshot 下吞 release。
24. Phase8 未真实 build/run GameplayProjection target。
25. NativeButton 测试文件存在，但 Sprint contributor/source-mask 行为覆盖缺失或被静默忽略。
26. WP0.5 batch API 到 WP1/WP2 才出现，导致工作包不能独立编译。
27. 任一 focused/canonical/RC/Graphify/diff gate 失败。
28. 只有 host 绿灯，没有 I/J 真实证据。
29. 摇杆卡顿或 Journal L2/R2 回归。
30. Favorites native gate、SWF、haptics、glyph 资源或无关 binding 被修改。

### K.7 解除发布门禁所需证据

只有以下证据全部存在且一致时解除：

- WP0、WP0.5 及 WP1 至 WP10 全部红灯记录、绿灯结果、相邻回归和可回退 commit。
- H.1全部适用命令通过。
- I-0、I-P、I-1、I-2、I-MENU、I-CURSOR、I-SPRINT、I-5 均有唯一出口记录；I-KBM 同时记录 raw-state reconcile A/B 与 synthetic provenance S-A/S-B/S-C 两个独立裁决。
- J.2全部smoke通过。
- J.3全部full matrix通过。
- J.4全部soak通过，evaluator violation=0。
- artifact manifest绑定被测试commit、Skyrim runtime、EXE/DLL/PDB hash和配置hash。
- release-relevant unknown engine caller=0。
- current-cycle enable event types与dynamic evidence表逐项一致。
- Favorites/SWF/haptics/glyph/unrelated binding diff=0。
- 没有open mixed-input release blocker。

---

## L. 自检

### L.1 需求覆盖检查

| 检查项 | 结果 | 落点 |
|---|---:|---|
| 九项当前代码事实逐项核实并给出repo路径/行号 | 通过 | A.2 |
| 两份方案差异完成综合而非直接接受任一方案 | 通过 | A.3、C.1 |
| mouse look + gamepad move有确定结果 | 通过 | B.3、J S2/F2 |
| gamepad look + keyboard move有确定结果 | 通过 | B.3、J S3 |
| Look/Move/Combat同通道单writer覆盖current-cycle与next-Poll | 通过 | B.1、D.6-D.8、WP4/WP5、J |
| transient digital双来源不重复 | 通过 | D.8、WP5、J S5/F6 |
| Sprint两种交接无中断 | 通过 | D.9、WP6、I-SPRINT、J S6/F7 |
| idle connected controller不抢KBM | 通过 | D.4、WP1、J S1/F1 |
| menu entry、menu切换、cursor handoff有单一确定合同 | 通过 | B.3、D.10、WP8、I-MENU/I-CURSOR |
| disconnect、overflow、context、reload、lost release有scope化恢复 | 通过 | D.12、WP7、J F9-F12/F16 |
| synthetic keyboard不误takeover | 通过 | D.2、WP2/WP7、J F8 |
| 四个指定候选和更优第五方案完成比较 | 通过 | C.1 |
| engine mode不是纯presentation字段 | 通过 | A.2、C.2、D.11、I节 |
| engine默认驱动者、hook需求和caller证据明确 | 通过 | C.2-C.3、I-0/I-1/I-2 |
| prompt与engine/gameplay/menu/cursor分离 | 通过 | B.1、D.3/D.10/D.11、WP8/WP9 |
| menu presentation与navigation共用owner，cursor独立 | 通过 | C.4、D.10 |
| priority、enter/sustain、200ms quiet、candidate reclaim明确 | 通过 | B.2、D.6 |
| current-state、connectivity、availability、activity分离 | 通过 | D.3-D.5、D.11 |
| causal generation/cutoff/epoch/session明确 | 通过 | D.1、D.5 |
| latest-wins、ordered、owner-only mutable明确 | 通过 | D.13 |
| writer/reader与数据流覆盖全部指定组件 | 通过 | E.1-E.2 |
| producer clock-domain误排序被禁止 | 通过 | E.3-E.4 |
| 创建、修改、不改、legacy降级路径完整 | 通过 | F.1-F.4 |
| 每个代码任务有红灯、命令、红因、最小实现、绿灯、回归、commit、rollback | 通过 | G WP0、WP0.5、WP1-WP10 |
| 指定canonical命令说明证明与不证明范围 | 通过 | H.1 |
| IDA任务包含地址、caller、断点、寄存器/参数、A/B、退出标准 | 通过 | I.1-I.11 |
| smoke、full、soak含步骤、观察、日志、pass/fail | 通过 | J.2-J.4 |
| 迁移、shadow、feature flag、commit回退、NO-GO/GO明确 | 通过 | K.1-K.7 |

### L.2 类型与职责一致性检查

全文统一使用以下核心类型：

```text
InputResetReason / InputResetScope / InputResetMarker / IngressBoundaryObservation
CausalLatestHeader
KbmPhysicalCode / KbmPhysicalCodeSet / KbmBindingSnapshot
KbmRawCurrentState / IKbmCurrentStateProvider / SyntheticProvenanceMode
KbmGameplayCurrentFacts / KbmPhysicalLedger / LatestKbmGameplayFacts
MeaningfulSourceActivity / RoutedSourceActivityFrame
GamepadConnectionFacts / LatestPadState / GamepadMeaningfulActivity
InputFactCoherenceKey
ChannelArbitrationState / ChannelArbitrationDecision
MaterializedPollIdentity / PollMaterializationReceipt
CurrentCycleGatePlan / CurrentCycleGateAudit / PreparedRuntimeInputCommit
TransientDedupKey / TransientActionGateDecision
SustainedContributorDecision
PublishedPresentationState / CursorHandoffPlan / CursorHandoffAck / CursorHandoffAckMailbox
GamepadDeviceAvailabilityDecision
EngineCallerRule / EngineModeDecisionSnapshot
PublishedRuntimeInputSnapshot
InputRecoveryRequest
```

一致性结论：

- `src/input_v2/`始终是唯一正式runtime mainline。
- `InputFramePump / RuntimeOwnerGuard`始终是唯一runtime mutation owner。
- `LatestPadState`仍是唯一gamepad current-state latest。
- `PollCommitSlot`仍是唯一mutable sustained materialization authority。
- current-cycle gate不成为第二套owner authority，只materializeowner tick已裁决plan。
- runtime综合快照不复制第二份prompt/menu/cursor authority。
- device availability、engine query、gameplay owner、menu owner、cursor和prompt没有合并成一个bool。
- current-cycle与next-Poll使用不同时间视图，later HID不会倒灌。
- quarantine是lost release保底，raw provider只是证据批准的优化。
- engine router默认Original，条件override逐caller启用。
- cursor坐标写入受direction-specific动态证据、identity和ack约束。

### L.3 禁止项复核

- 没有恢复`InputModalityTracker`、`GameplayOwnershipCoordinator`、legacy processor或singleton tracker authority。
- 没有建议延长lease、扩大queue、sleep、硬编码WASD/鼠标键或禁用一类输入。
- 没有固定Skyrim全局engine mode。
- 没有把`IsUsingGamepad/IsGamepadEnabled`描述成纯glyph字段。
- 没有用prompt family反向驱动gameplay或engine。
- 没有修改Favorites native gate、SWF、haptics、glyph视觉资源或无关binding。
- 没有把host tests、编译、CI或Graphify描述成实机通过。
- 没有在证据未闭合时预先启用current-cycle mutation、cursor坐标写入、raw reconcile、Sprint guard、availability scope、menu direct callsite或transform router。

### L.4 最终裁决复核

本综合版优于两份原计划各自单独实施的原因：

1. 以original-first治理Skyrim global query，避免caller-scoped方案在证据不足时过度侵入。
2. 采用hook发布的consume-once Poll receipt、callback-local事实和 `Prepare -> Apply -> Commit`，同时关闭“只修下一Poll”“later HID倒灌”和“adapter失败但ledger已前进”三类首周期漏洞。
3. 用causal tail、累计cutoff、现有boundary key、global epoch和pad session解决partial drain、首批new mapping错配、overflow action半提交和disconnect误清KBM；overflow仍保留recovery-stamped完整物理analog/current-state。
4. 保留200 ms既有Look语义和现有enter/sustain阈值，不把根因修复与手感重调混为一个commit。
5. 用candidate latch同时满足KBM release后的快速pad reclaim与确定enter门槛。
6. 用稳定 `(device,idCode)` quarantine 提供不依赖未证明 raw API 的安全恢复，避免 ControlMap 重排误认物理键，同时允许 verified provider 加速 reconcile。
7. 用virtual bridge优先解决Sprint无缝交接，把handler hook降为证据触发的最后手段。
8. 用context-neutral activity和runtime routing避免HID线程理解menu/gameplay。
9. 用 audit 后原子 runtime/presentation publication、owner-timer pointer 和 consume-once cursor ack mailbox 防止跨 surface 撕裂。
10. 把 Phase8 漏跑 GameplayProjection 和 NativeButton 缺少 Sprint 行为覆盖转为真实、phase-aware preflight/closeout，而不是把 target/file 名称当成覆盖证据。

**最终实施顺序固定为：事实语义 -> 因果publication -> next-Poll通道仲裁 -> current-cycle shadow与证据 -> transient/Sprint -> scope化恢复 -> presentation/cursor -> Original-first engine治理 -> live evidence与发布close-out。**

任何动态门禁失败，都回到Original或安全fail-closed路径，并保持对应mixed-input capability为`NO-GO`，不得用更大的全局布尔值把问题藏起来。
