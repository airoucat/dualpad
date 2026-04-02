# DualPad（codex/dynamic-glyph-widget @ 30b689e）架构审查报告

## Architecture assessment

**目标**：把“平台表现/菜单输入归属（UI owner）”与“gameplay 通道写入权限（gameplay owner）”这两类问题彻底分层，让 glyph/presentation 的刷新稳定、让 gameplay 的通道仲裁可验证，并把“丢包/合帧后的恢复”从止血逻辑提升成可推理的正式机制，同时避免继续在错误抽象（尤其是 `IsUsingGamepad()`）上加码。fileciteturn34file0L1-L1 fileciteturn35file0L1-L1

### 代码中实际形成的层次与数据流

从实现看，这个分支已经基本形成两条主链，并在关键点汇合：

- **UI / presentation 链**主要由 `InputModalityTracker` 接管：它通过 hook 影响引擎侧 `IsUsingGamepad`/`GamepadControlsCursor`/`BSPCGamepadDeviceHandler::IsEnabled`，并在菜单平台切换时主动调用 `menu->RefreshPlatform()` 及 `_root.DualPad_OnPresentationChanged` 回调来触发 SWF 刷新。fileciteturn15file0L1-L1 fileciteturn17file0L1-L1 fileciteturn38file0L1-L1  
- **gameplay 注入/物化链**以 snapshot 机制为核心：`PadEventSnapshotDispatcher -> PadEventSnapshotProcessor -> FrameActionPlan/NativeButtonCommitBackend/PollCommitCoordinator -> AuthoritativePollState`，其中 `PadEventSnapshotProcessor::FinishFramePlanning` 在发布 analog 前引入了 `GameplayOwnershipCoordinator` 对轴与数字动作做 gating。fileciteturn35file0L1-L1 fileciteturn20file0L1-L1 fileciteturn22file0L1-L1

这条分离方向整体是正确的：UI 层最终需要的是引擎认可的“单一平台结论”（presentation），而 gameplay 层需要的是“通道级写权限 + 数字动作执行合同”。文档里的判断与你当前的落点一致：UI 仲裁不应该直接扩展成 gameplay 全局 owner；gameplay owner 应落在 `PadEventSnapshotProcessor` 与 `AuthoritativePollState` 之间。fileciteturn34file0L1-L1 fileciteturn35file0L1-L1

### 针对审查问题的结论

**UI owner 与 gameplay owner 是否清晰分离？**  
“模块边界”已经分离（`InputModalityTracker` vs `GameplayOwnershipCoordinator`），但“运行时耦合”仍存在：  
- `InputModalityTracker::IsUsingGamepad()` 在 gameplay 场景会读取 `GameplayOwnershipCoordinator::GetPublishedLookOwner()` 并结合 “mouse look 活跃窗口”来决定是否返回 false，这意味着 gameplay 的 LookOwner 会反向影响引擎平台判断（从而影响 glyph/presentation）。fileciteturn17file0L1-L1 fileciteturn18file0L1-L1  
- `GameplayOwnershipCoordinator` 反过来依赖 `InputModalityTracker` 的“KBM 事实收集”（mouse look / keyboard move / combat / digital 等）来判定各通道 owner。fileciteturn18file0L1-L1 fileciteturn17file0L1-L1  
这属于“互相引用但职责不同”的状态：能跑，但长期会把两个层次绑死，难以独立演进。

**`InputModalityTracker` 是否承担过多职责？**  
是的，而且已经到“难以保持不变量”的程度。它同时承担：  
- 引擎 hook 安装与 patch（输入管理器、`IsUsingGamepad`、cursor、gamepad enable）。fileciteturn17file0L1-L1  
- 菜单 owner 状态机（Presentation/Navigation/Cursor/PointerIntent）与策略表（按 `InputContext` 的 policy kind + mouse move promotion）。fileciteturn15file0L1-L1 fileciteturn17file0L1-L1  
- gameplay 侧 KBM “事实采集”（move/combat/digital/sprint down mask、mouse look 计时窗、synthetic scancode 抑制）。fileciteturn17file0L1-L1  
- 菜单刷新与 SWF 回调触发（RefreshPlatform + `_root.DualPad_OnPresentationChanged`）。fileciteturn17file0L1-L1  
从架构上看，它已经不是“tracker”，更像“UI 仲裁器 + gameplay 事实探针 + 引擎适配层”的合集。

**`GameplayOwnershipCoordinator` 是否是 gameplay ownership 单一真源？**  
对“每帧输出的通道仲裁结果（Look/Move/Combat/Digital owners + analog suppression）”而言，它基本是单一真源：`PadEventSnapshotProcessor::FinishFramePlanning` 里先算 analog，再交给 coordinator 应用 owner 后发布。fileciteturn22file0L1-L1 fileciteturn18file0L1-L1  
但它仍不是“架构意义上的单一真源”，因为：  
- 它的输入不是纯粹的 gameplay frame（还依赖 `InputModalityTracker` 的 KBM 活跃事实）。fileciteturn18file0L1-L1  
- 它在 `UpdateDigitalOwnership` 中直接触发 backend 侧的“强制取消 gate-aware transient actions”（通过 `NativeButtonCommitBackend::ForceCancelGateAwareGameplayTransientActions`），这把“owner 判定”与“下游执行生命周期”硬连在一起。fileciteturn18file0L1-L1 fileciteturn28file0L1-L1  

**`IsUsingGamepad()`、glyph、cursor、menu/platform refresh 是否过度纠缠？**  
当前仍然纠缠，而且主要纠缠点不是 `ScaleformGlyphBridge`，而是 `InputModalityTracker`：  
- 你用引擎 hook 的 `IsUsingGamepad()` 作为“统一 presentation 结论”，同时又在 gameplay 场景引入 LookOwner 影响，导致 gameplay 通道冲突可能表现为 UI/glyph 抖动（文档也明确提到这类风险）。fileciteturn17file0L1-L1 fileciteturn36file0L1-L1  
- `RefreshMenus()->RefreshPlatform()` 与 SWF 回调触发也是由 `InputModalityTracker` 在 presentation owner 变更时直接驱动。fileciteturn17file0L1-L1  
- `ScaleformGlyphBridge` 本身更像“查询式 token 服务”：SWF 调 `DualPad_GetActionGlyphToken`，桥接层按 binding manager 映射出 token；它并不直接读取 `IsUsingGamepad` 或 owner。fileciteturn30file0L1-L1  

**通道拆分（Look/Move/Combat/Digital）是否合理？**  
总体合理，且实现与文档规划一致：  
- Look：mouse look 活跃（短窗口）优先，否则右摇杆超过阈值提升到 gamepad。fileciteturn18file0L1-L1  
- Move：键盘移动事实优先，否则左摇杆超过阈值提升到 gamepad。fileciteturn18file0L1-L1  
- Combat：KBM combat 事实优先，否则 trigger 超阈值提升到 gamepad，并对触发器轴做 suppression。fileciteturn18file0L1-L1  
- Digital：并没有把所有 digital 一锅端，而是围绕“gate-aware + transient（Pulse/Toggle）”来做 family-level owner（并刻意把 Hold/Repeat 排除在 family suppression 外）。这点与后续要把 Sprint 做成单独模型的方向兼容。fileciteturn18file0L1-L1 fileciteturn28file0L1-L1  

**数字动作家族（Pulse/Toggle/Hold/Repeat）是否在下游真正区分？**  
是区分的，而且“合同（contract）→ digitalPolicy → commit mode/state machine”的链路比较完整：  
- `ActionLifecycleCoordinator` 按 `ActionOutputContract` 解析出 `NativeDigitalPolicyKind`，并为每个 planned action 补齐 gateAware、minDownMs、repeatDelay/interval 等参数。fileciteturn24file0L1-L1  
- `NativeButtonCommitBackend::TranslatePlannedActionToCommitRequest` 将 policy 映射到 `PollCommitMode` 与 request kind（Pulse/ToggleFire/HoldSet/Clear/RepeatSet/Clear）。fileciteturn28file0L1-L1  
- `PollCommitCoordinator` 用 slot 状态机分别处理 Pulse、Toggle、Hold、Repeat，并对 Pulse 有“同一 slot 的 pendingNextPulse 合并/丢弃计数”，对 Hold/Repeat 有 contributor/active emitter 逻辑。fileciteturn26file0L1-L1 fileciteturn27file0L1-L1  

**provenance-aware resync / missing-press recovery 是否足够稳健？**  
整体设计方向正确：dispatcher 记录 `firstSequence`、`coalesced`、`crossContextMismatch`、`overflowed` 等退化信息；processor 在检测 seq gap 后 Resync，并在 Resync 后按“合同 + context 连续性 + preGapDown”决定 replay / recover owner / suppress。fileciteturn20file0L1-L1 fileciteturn22file0L1-L1  
但目前“退化判定触发点”还不够统一：只有发生 seq gap 才走 `RecoverMissingPressStateAfterResync`，否则会走 `SynthesizeMissingButtonEdges`；这意味着 **coalesce + crossContextMismatch 但没有 seq gap** 的场景，仍可能触发“缺失 press 合成”并导致跨上下文误重放（文档里也把它列为技术债方向）。fileciteturn22file0L1-L1 fileciteturn35file0L1-L1  

## Priority findings / risks

**优先级最高的风险**集中在“跨层真源不清 + 特例堆积 + 退化恢复触发条件不一致”三类。

### 风险一：`IsUsingGamepad()` 被当作跨层总线，反向把 gameplay owner 引入 UI / HUD 表现

`InputModalityTracker::IsUsingGamepad()` 在 gameplay 域会读取 `GameplayOwnershipCoordinator` 的 published look owner，并用 mouse look 活跃窗做直接分支。它的工程动机明确（避免 mouse look 时 UI/presentation 被切走），但代价是：**gameplay 通道仲裁开始影响引擎“平台表现”结论**。fileciteturn17file0L1-L1  
这是典型的“短期止血有效、长期会反复回到纠缠问题”的结构：你会不断在 `IsUsingGamepad()` 里加例外去修体验，而这条函数又是引擎内部广泛依赖的全局开关。fileciteturn36file0L1-L1

**判定**：这部分更适合作为“临时补丁/兼容层”而不是正式架构核心。

### 风险二：`InputModalityTracker` 既做 UI 仲裁又做 gameplay 事实采集，导致任何改动都可能产生双向回归

`InputModalityTracker` 目前同时维护 UI owner（presentation/navigation/cursor/pointer intent）以及 gameplay KBM 活跃事实（move/combat/digital/sprint/mouselook）。后者又直接喂给 `GameplayOwnershipCoordinator` 与 `NativeButtonCommitBackend` 的 sprint probe。fileciteturn15file0L1-L1 fileciteturn18file0L1-L1 fileciteturn28file0L1-L1  
这使得“UI 手感调整（比如 mouse move promotion）”潜在会改变 gameplay owner 结果；反过来“gameplay owner 调整”又会改变 `IsUsingGamepad` 输出，从而改变 UI。fileciteturn17file0L1-L1

**判定**：这是当前最主要的架构债源头之一。

### 风险三：DigitalOwner 目前是“部分家族”的 family-level 规则，且 owner 判定与下游执行生命周期存在环引用

目前 DigitalOwner 的设计是围绕 gate-aware transient（Pulse/Toggle）做 family-level suppression：  
- coordinator 通过 “KBM digital 活跃事实 / frame plan 中是否存在 meaningful gamepad transient digital / gameplay owner 回退”来发布 digital owner。fileciteturn18file0L1-L1  
- backend 依据 published digital owner 直接 suppress 某些 planned action（仅 Pulse/Toggle）。fileciteturn28file0L1-L1  
- digital owner 从 Gamepad 切到 KeyboardMouse 时，coordinator 直接触发 backend 的 force cancel（gate-aware transient slots）。fileciteturn18file0L1-L1  

这套机制短期上能有效避免“双触发”与“卡住的 transient token”，但它把 **“owner 决策”“下游状态机”“取消策略”**耦合成一个循环：后续你很难单独替换某一层而不引发行为变化。

**判定**：这可以接受为“阶段性正式设计的雏形”，但需要尽快把交互改成单向数据依赖（owner → gate/cancel plan），否则会继续膨胀成特例森林。

### 风险四：Sprint 的“单发射源/交接”方向已出现雏形，但关键桥接仍是 stub 或未闭环

`PollCommitCoordinator` 已对 `Sprint` 实现了“single emitter hold”的特例路径（active emitter、handoff gap 等），并在日志里专门探针。fileciteturn27file0L1-L1 fileciteturn28file0L1-L1  
但 `NativeButtonCommitBackend::SyncExternalHeldContributors` 目前硬编码 `kbmSprintHeld=false`，意味着“从原生键盘/鼠标来源把 held 意图同步进入 poll commit”的路径并未真正落地。fileciteturn28file0L1-L1  
与此同时，文档清晰指出 Sprint 的根因更可能来自“原生 keyboard sprint 绕过仲裁直接驱动游戏”，需要做 Sprint-only 的 native keyboard mediation（queue 过滤或可控重放）。fileciteturn37file0L1-L1

**判定**：现在继续在 Sprint 上调 owner/lease/阈值，会明显进入“越修越像”的陷阱；应转向补齐 mediation 的闭环。

### 风险五：退化投递（overflow/coalesce/cross-context）下的恢复触发条件仍不统一

dispatcher 具备 coalesce、跨上下文 mismatch 标记、overflow 语义，并在 bounded drain 后强制合帧保留最新 snapshot。fileciteturn20file0L1-L1  
processor 对 seq gap 的 Resync 与 “missing press recovery”实现很细致（按合同与 preGapDown 决策）。fileciteturn22file0L1-L1  
但“是否进入 recovery 模式”目前高度依赖 seq gap；对于没有 gap 的 coalesce/crossContextMismatch，仍可能走普通的 missing edge 合成路径，存在跨上下文误重放的结构性风险。fileciteturn22file0L1-L1

**判定**：这是“正确方向已具备，但差临门一脚”的债；适合尽快转成正式规则，而不是继续靠场景开关止血。

## Recommended staged refactor plan

下面的计划目标是：**最小风险地把“耦合点”从代码路径中剥离出来**，让后续任何新增需求都必须通过更清晰的接口落地，而不是往 `IsUsingGamepad()`/tracker/owner/coordinator 里继续塞条件。

### 阶段一：冻结接口并建立可验证的不变量

**实现路径**：  
- 明确把“UI presentation 结论”定义为 `PresentationOwner` 的单一输出（你当前已经这么做），并规定：**gameplay owner 不得直接参与 UI owner 决策**；保留现有的 `IsUsingGamepad()` gameplay 分支作为临时兼容，但把它标记为 compatibility shim（后续可删）。fileciteturn15file0L1-L1 fileciteturn17file0L1-L1  
- 明确把 `GameplayOwnershipCoordinator` 的输出定义为：`OwnershipDecision{ ownedAnalog + per-channel owner + suppression }`，并规定：**coordinator 不直接调用 backend 的“执行动作”（例如 force cancel）**，只输出“建议的 cancel/gate 计划”。fileciteturn16file0L1-L1 fileciteturn18file0L1-L1  

**所需工具**：SKSE 日志分级开关（RuntimeConfig）、现有 probe 日志（SprintProbe/SneakProbe/GameplayOwner/Snapshot）。fileciteturn32file0L1-L1 fileciteturn28file0L1-L1

**验收标准**：  
- gameplay 场景下，禁用/启用 gameplay ownership（`enable_gameplay_ownership`）不会改变“菜单平台切换”的基本行为（只影响 gameplay 注入）。fileciteturn33file0L1-L1  
- `GameplayOwnershipCoordinator` 的日志能独立解释 analog suppression，而无需查看 `IsUsingGamepad()` 的分支日志。fileciteturn18file0L1-L1

### 阶段二：把 `InputModalityTracker` 拆成“UI 仲裁器”与“gameplay KBM facts”两个明确部件

**实现路径**：  
- 将 gameplay KBM facts（move/combat/digital/sprint masks、mouse look active window、synthetic scancode suppression）从 `InputModalityTracker` 的核心职责中抽出来，形成一个轻量的 `GameplayKbmFactTracker`（或同等概念），其输入仅来自 gameplay 域输入事件，其输出以结构体按帧读取。当前这些事实在 `InputModalityTracker` 内部已经是独立字段与方法集合，拆分成本低。fileciteturn17file0L1-L1  
- `GameplayOwnershipCoordinator` 不再直接调用 `InputModalityTracker`，而改为由 `PadEventSnapshotProcessor` 将 facts 作为参数传入（单向依赖：processor → (facts + frame) → coordinator）。fileciteturn22file0L1-L1 fileciteturn18file0L1-L1  

**风险/兼容性**：这一步主要风险是“事实采集与现有行为偏差”，但可通过对比日志（旧 tracker vs 新 fact tracker）与 A/B config 开关控制回退。fileciteturn32file0L1-L1

**验收标准**：  
- 在不改 owner 规则的前提下，拆分前后 `GameplayOwnershipCoordinator` 的 owner 切换日志序列一致（允许时间戳/噪声不同，但 owner 变更事件一致）。fileciteturn18file0L1-L1  
- UI 菜单 owner（Presentation/Navigation/Cursor/PointerIntent）代码路径不再包含任何 gameplay 事实字段。fileciteturn15file0L1-L1

### 阶段三：把 DigitalOwner 的“抑制/取消”从环引用改成单向“gate plan”

**实现路径**：  
- 将 “DigitalOwner 从 Gamepad → KeyboardMouse 时需要 cancel gate-aware transient slots” 从 `GameplayOwnershipCoordinator` 中移出：coordinator 输出一个 `DigitalGatePlan`（例如：`suppressNewTransient=true/false`、`cancelExistingTransient=true/false`），由 `PadEventSnapshotProcessor::FinishFramePlanning` 在同一处统一应用到 backend。fileciteturn18file0L1-L1 fileciteturn22file0L1-L1  
- `NativeButtonCommitBackend` 可以继续执行 suppression，但读取的输入应从 “coordinator published owner”转为“processor 当帧下发的 gate plan”（避免 backend 依赖 gameplay owner 的全局状态）。fileciteturn28file0L1-L1  

**验收标准**：  
- DigitalOwner 变化时的 cancel 触发点在日志中只出现一次（由 processor 输出），不会同时在 coordinator/backend 两侧各出现一套“我认为该 cancel”的路径。fileciteturn18file0L1-L1 fileciteturn28file0L1-L1

### 阶段四：把“退化投递恢复”升级为正式状态机，而不是靠是否 seq gap 判定

**实现路径**：  
- 在 `PadEventSnapshotProcessor::Process` 中，将进入 recovery 的条件从 `didResync` 扩展为：`didResync || syntheticFrame.overflowed || syntheticFrame.coalesced || snapshot.crossContextMismatch`（具体取舍可按你现有字段），并统一走一条“provenance-aware 的恢复函数”。fileciteturn22file0L1-L1  
- 把当前 “普通帧 missing edge 合成（SynthesizeMissingButtonEdges）” 限定为 **同上下文、未退化** 的情况；跨上下文 mismatch 时一律禁止 replay Press，改为 suppress/block，避免菜单开关键被重放。fileciteturn20file0L1-L1 fileciteturn22file0L1-L1  

**验收标准**：  
- bounded drain coalesce 日志出现时，不再出现“跨上下文重放导致菜单闪退/立刻关闭”的复现（以你现有 warn 日志与实际现象为准）。fileciteturn20file0L1-L1 fileciteturn35file0L1-L1  
- 退化情况下（overflow/coalesce），仍能保持“不会触发重复 Pulse/Toggle”的不变量（通过 PollCommit 的 dropped/coalesced 计数与 action plan 日志验证）。fileciteturn27file0L1-L1 fileciteturn28file0L1-L1

### 阶段五：把 Sprint 从 owner 体系中完全移出，完成 mediation 闭环

**实现路径**：  
- 明确 Sprint 走 “single-emitter hold + native keyboard mediation”路线：`PollCommitCoordinator` 里的 single emitter 特例可以保留并正式化，但要补齐“键盘来源进入仲裁系统”的路径（替换 `kbmSprintHeld=false` stub）。fileciteturn27file0L1-L1 fileciteturn28file0L1-L1  
- 优先按文档建议做 Sprint-only queue mediation（只拦 Sprint，不扩大战线），把“原生 keyboard sprint 绕过仲裁”这个根因关掉，再评估是否需要 re-edge 语义。fileciteturn37file0L1-L1  

**验收标准**：  
- 三类场景稳定：仅 KBM sprint、先手柄后键盘、先键盘后手柄再松键盘；且 active emitter 与 contributor 的日志能解释结果。fileciteturn37file0L1-L1 fileciteturn28file0L1-L1

**下一步待办（最小集合）**：  
- 先做阶段二（facts 拆分）+ 阶段四（退化恢复统一触发），因为它们能同时降低耦合与减少“神秘复现”。fileciteturn22file0L1-L1  
- 随后做阶段三（digital gate plan 单向化），再开始 Sprint mediation。fileciteturn18file0L1-L1 fileciteturn37file0L1-L1  

## Areas that should not receive more investment right now

这里的原则是：**凡是会进一步强化错误抽象或强化跨层环引用的改动，一律暂停**，直到上面的阶段性分层完成。

- **暂停在 `InputModalityTracker::IsUsingGamepad()` gameplay 分支继续加特判**（比如再引入 MoveOwner/CombatOwner 的参与、再加更多 UI/presentation 例外）。这会把 gameplay owner 再次拉回 UI 总线。fileciteturn17file0L1-L1  
- **暂停继续在 `InputModalityTracker` 内部追加新的 gameplay facts/owner 逻辑**（尤其是更多 digital family、更多“某键不允许翻转 owner”的散点规则）。应先完成 facts 拆分，让 tracker 回到 UI 仲裁职责。fileciteturn15file0L1-L1  
- **暂停对 Sprint 的 owner/lease/阈值做“手感调参式”修复**。现状更像缺 mediation 闭环；继续调参只会增加不可解释状态。fileciteturn37file0L1-L1 fileciteturn28file0L1-L1  
- **暂停在 resync/合帧问题上做“场景开关式”止血扩散**（例如仅对某些菜单禁用合成、仅对某些键特殊处理），应把“退化触发恢复”的条件统一后，再决定细化策略。fileciteturn20file0L1-L1 fileciteturn22file0L1-L1  
- **暂停扩大 SWF 页面的 ad-hoc 刷新改动范围**。当前 C++ 已尝试调用 `_root.DualPad_OnPresentationChanged()`，SWF 是否实现回调属于逐页补齐工作，按文档建议延后到 ownership 主线稳定后进行。fileciteturn17file0L1-L1 fileciteturn38file0L1-L1  

## Specific implementation notes tied to the branch code

以下笔记以“这段代码应当被视为：临时止血 / 值得正式化 / 值得回滚或替换”来标注。

### `src/input/InputModalityTracker.*`

- **值得正式化**：按 `InputContext` 选择 policy kind（StrictGamepadSticky / Neutral / PointerFirst）+ mouse move promotion target，这套策略表与文档一致，并且通过 “cursor-only / presentation+cursor”把 pointer 噪声与平台切换拆开，是 UI 手感稳定的关键骨架。fileciteturn17file0L1-L1 fileciteturn34file0L1-L1  
- **可接受的临时止血**：`RefreshMenus()` 通过 UI task 批量调用 `RefreshPlatform()` 并尝试触发 `_root.DualPad_OnPresentationChanged()`，这是在 SWF 未全面实现刷新入口之前的合理折中。fileciteturn17file0L1-L1 fileciteturn38file0L1-L1  
- **应逐步替换**：`IsUsingGamepad()` gameplay 域对 LookOwner 的读取（以及 mouse look 活跃窗的分支）属于“把 gameplay 通道问题借道 UI 总线解决”。它可以暂时保留以避免体验回归，但应被替换为“engine gameplay presentation 稳定策略 vs DualPad glyph presentation 独立策略”的分层。fileciteturn17file0L1-L1 fileciteturn36file0L1-L1  
- **拆分建议（低风险）**：`_gameplayKeyboard*DownMask`、`_lastGameplayMouseLookAtMs`、`MarkSyntheticKeyboardScancode/ConsumeSyntheticKeyboardEvent` 这些字段与 UI presentation 并无强绑定，天然适合作为“gameplay facts 采集器”的独立组件。fileciteturn17file0L1-L1  

### `src/input/injection/GameplayOwnershipCoordinator.*`

- **总体评价**：per-channel ownership 的落点正确；对 analog suppression 的实现也很直观：当某通道 owner=KBM 时，把对应合成轴归零并打 suppression 标记。fileciteturn18file0L1-L1  
- **需要立刻降耦合**：`UpdateDigitalOwnership` 内部在 owner 从 Gamepad → KBM 时直接调用 backend 的 force cancel，这是把“判定”与“执行”绑死的关键点之一，应尽快改成 coordinator 输出 gate plan，由 processor 统一应用。fileciteturn18file0L1-L1  
- **DigitalOwner 的边界目前是清晰且合理的**：`HasMeaningfulGamepadDigitalAction` 只把 Pulse/Toggle 纳入“meaningful”判定，回避 Hold/Repeat，与你希望 Sprint 单独治理的方向一致；backend 的 suppression candidate 同样只覆盖 Pulse/Toggle。fileciteturn18file0L1-L1 fileciteturn28file0L1-L1  

### `src/input/injection/PadEventSnapshot*` 与恢复路径

- **dispatcher 的设计是正确方向**：ring buffer + bounded drain + coalesce（非 AxisChange 优先）能在主线程压力下保留“最新状态 + 关键边沿”，并显式标记跨上下文 mismatch。fileciteturn20file0L1-L1  
- **processor 的 Resync/missing-press recovery 质量很高**：按 contract 区分 replay/owner-only/suppress 的决策，尤其对 Hold/Repeat 用 `RegisterRecoveredOwningAction` 来避免重复 press，是将“恢复”从纯事件补丁提升到“生命周期修复”的正确做法。fileciteturn22file0L1-L1 fileciteturn24file0L1-L1  
- **最需要补齐的缺口**：恢复逻辑的触发条件目前过度依赖 seq gap；建议把 `coalesced/overflowed/crossContextMismatch` 也纳入“进入恢复模式”的统一入口，避免跨上下文 coalesce 时普通缺失 press 合成造成误重放。fileciteturn22file0L1-L1 fileciteturn35file0L1-L1  

### `src/input/backend/ActionLifecycleCoordinator.*`、`NativeButtonCommitBackend.*`、`PollCommitCoordinator.*`

- **值得正式化**：`ActionOutputContract -> NativeDigitalPolicyKind -> PollCommitMode` 链条已经把 Pulse/Toggle/Hold/Repeat 的合同区别落实到了执行层，并且把 gateAware 的概念收敛到少量动作（Jump/Activate/Sprint）。这是“数字动作家族区分”能够长期维护的基础。fileciteturn24file0L1-L1 fileciteturn28file0L1-L1  
- **处于“可接受但需尽快清理”的状态**：`NativeButtonCommitBackend` 直接读取 `GameplayOwnershipCoordinator` 的 published DigitalOwner 来做 suppression，会导致 backend 对 owner 的全局依赖。短期 OK，但应改为 processor 下发 gate plan（单向依赖）。fileciteturn28file0L1-L1  
- **Sprint 的现状**：`PollCommitCoordinator` 里已出现 single-emitter hold 的特例（包括 handoff gap），这是对文档方向的实现雏形；但 `SyncExternalHeldContributors` 仍是 stub，说明“KBM held 意图进入 poll commit”的闭环尚未完成。fileciteturn27file0L1-L1 fileciteturn28file0L1-L1  
- **应该替换/补齐的点**：Sprint 的根因若在“原生 keyboard sprint 绕过仲裁”，那么仅在 poll commit 内做 contributor OR/handoff 仍不足，需要 Sprint-only 的 native keyboard mediation（按文档优先方案）。这部分不宜继续靠 owner 阈值补丁推进。fileciteturn37file0L1-L1  

### `src/input/glyph/ScaleformGlyphBridge.*`

- **当前实现更像稳定的基础设施**：它提供 SWF 查询 action→glyph token 的能力，按 binding manager 的 trigger 映射到 token；并通过 fxDelegate 绑定到菜单。它本身不应被塞进 owner 逻辑。fileciteturn29file0L1-L1 fileciteturn30file0L1-L1  
- **与动态 presentation 的连接方式目前是正确的**：由 `InputModalityTracker` 在平台变更时触发 SWF 回调，而不是让 glyph bridge 自己“猜模态”。缺的主要是 SWF 页面覆盖面与刷新入口一致性，文档也明确这应延后。fileciteturn17file0L1-L1 fileciteturn38file0L1-L1  

### 运行时配置与调试

- `enable_gameplay_ownership` 作为 A/B 开关很关键：能让你在 UI owner 稳定的同时隔离 gameplay ownership 的回归面，建议继续保留并在阶段性重构中作为回退闸门。fileciteturn33file0L1-L1 fileciteturn31file0L1-L1  
- 当前 `DualPadDebug.ini` 里存在一些键值（例如 fail-closed source isolation、treat coalesced snapshot as overflow、tutorial debug hotkey）不在 `RuntimeConfig` 解析列表中；这本身不破坏运行（未知键会被忽略），但容易让调试与预期不一致。建议明确哪些键由哪个 config 负责，避免“以为开了但没生效”的时间浪费。fileciteturn33file0L1-L1 fileciteturn32file0L1-L1



