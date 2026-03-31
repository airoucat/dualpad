# DualPad

Skyrim SE 1.5.97 / CommonLibSSE-NG 的 DualSense 输入重构项目。

当前项目已经从早期的 `keyboard-native` 与旧 `native button splice` 实验，收口到一条更明确的默认主线：

- 输入采集：`HidReader -> PadState`
- 映射快照：`PadEventGenerator -> PadEventSnapshot`
- 主线程规划：`SyntheticStateReducer -> FrameActionPlan -> ActionLifecycleCoordinator`
- 状态提交：`NativeButtonCommitBackend + AxisProjection + UnmanagedDigitalPublisher -> AuthoritativePollState`
- 最终桥接：`UpstreamGamepadHook -> XInputStateBridge -> XInputButtonSerialization`

当前默认运行时不再依赖：

- `XInputGetState` IAT 输入 fallback
- `keyboard-native` 作为 Skyrim PC 原生事件主线
- consumer-side `ButtonEvent` 队列拼接作为默认实现

当前仍保留的内部补写步骤：

- `PadEventSnapshotProcessor` 内部的 unmanaged raw digital 发布步骤

## 当前主线原则

- `FrameActionPlan` 是运行时合同，不是影子日志。
- 数字主线采用 `Poll-owned current-state ownership`。
- `NativeButtonCommitBackend` 只做 `PlannedAction -> PollCommitRequest` 翻译。
- `PollCommitCoordinator` 只负责 Poll 可见性 materialization。
- `PadEventSnapshotProcessor` 直接把规划后的模拟量与 unmanaged raw digital 事实写入 `AuthoritativePollState`。
- native routing / axis projection / button materialization 当前由统一 `NativeActionDescriptor` 主表驱动。
- `AuthoritativePollState` 的正式口径是“虚拟 XInput 手柄硬件状态”，不是插件侧游戏动作状态表。
- `XInputStateBridge` 只负责把统一状态序列化成 `wButtons / bLeftTrigger / bRightTrigger / thumbsticks`，不在 bridge 层补做 gameplay 语义；XInput 按钮字由 `XInputButtonSerialization` 在 transport 侧按需计算。
- `KeyboardHelperBackend` 是正式 helper backend 名称。
- `KeyboardHelperBackend` 统一负责 `ModEvent / VirtualKey / FKey / 虚拟键池` 的模拟键盘输出。
- `KeyboardHelperBackend` 当前只走 `dinput8` 代理桥接的 simulated keyboard path，不再走本地原生 keyboard hook。
- `InputModalityTracker` 参考 AutoInputSwitch 的模式层思路，只负责“键盘/鼠标与手柄可并存”以及 UI 平台切换，不接管动作 routing。
- `KeyboardHelperBackend` 自发的 simulated keyboard 事件会被 `InputModalityTracker` 抑制，不会把平台误切到 KBM。
- 插件动作通过 `ActionDispatcher -> ActionExecutor` 执行，不再保留独立的 `PluginActionBackend` 壳层。
- `Wait / Journal` 已回归手柄原生 current-state 路由，分别走 `Create / Options`。
- 通用 `Menu.PageUp / Menu.PageDown` 不再作为默认 native 菜单绑定；当前默认按 `controlmap` 分拆到具体上下文：
  - `BookMenu` 走 `Book.PreviousPage / Book.NextPage -> D-pad Left / Right`
  - `JournalMenu` 走 `LT / RT` trigger current-state 翻标签
  - `MapMenu` 走独立的 `Map.Click / Map.OpenJournal / Map.PlayerPosition / Map.LocalMap` 与原生摇杆/扳机硬件位
- `Pause / NativeScreenshot / Hotkey3-8` 这批 PC 键盘独占原生事件，当前已作为独立 action surface 接入，并由 DualPad 在运行时覆盖 gamepad `ControlMap` 到固定 combo ABI；它们仍不默认占用现有手柄键位。
- 当前这批 combo-native 事件按“第一拍直接 materialize 完整组合”运行；`Pause / NativeScreenshot` 已实机验证可用。
- `Hotkey3-8` 代码与 overlay ABI 已接入，但当前默认受 runtime gate 保护，需在 `Data/SKSE/Plugins/DualPadDebug.ini` 里显式开启 `enable_combo_native_hotkeys3_to_8=true` 后再做补测。
- `OpenInventory / OpenMagic / OpenMap / OpenSkills` 当前已撤出正式支持面；这四个动作所属的 `MenuOpenHandler` 家族在当前 mod 栈下不稳定，不再继续作为 combo-native 默认能力推进。
- `OpenFavorites` 仍不在这批 `controlmap combo` 正式支持面里。
- DualPad 当前会在 `kDataLoaded` 后读取 `Data/SKSE/Plugins/DualPadControlMap.txt`，直接调用游戏自己的 `ControlMap` parser/rebuild 链重载这份配置，并故意跳过 `ControlMap_Custom.txt`；玩家或其它 mod 的 remap 不再属于兼容目标。
- `ControlMapOverlay` 当前已补运行时版本与 overlay 文件存在性护栏；非 SSE 1.5.97 或 overlay 文件缺失时，会明确跳过并记录原因，而不是静默进入不确定状态。
- 配套 overlay 源文件当前仓库路径是 `config/controlmap_profiles/DualPadNativeCombo/Interface/Controls/PC/controlmap.txt`。
- `Game.Attack / Game.Block` 这类旧 action-routed compatibility 路径也已退出正式支持面；原生战斗输入应通过虚拟 `LT / RT` current-state 交给 Skyrim 自己的 `AttackBlockHandler` 推导。
- `ModEvent` 当前不再规划独立 transport backend，而是作为逻辑槽位，经 `KeyboardHelperBackend` materialize 为固定虚拟键。
- 正式输出口径现在只保留两条：手柄原生 `current-state` 与 mod 键盘事件。
- 原生线后续的统一方向是：只 materialize 标准手柄硬件位/轴，尽量不在插件侧重做 Skyrim 原生 user event 语义。
- 对已经确认存在独立原生身份的 UI 动作，当前策略是“保持上下文专属 action，再 materialize 对应硬件位”，而不是继续压扁成通用 `Menu.*` 事件。
- 对键盘独占但仍属于 Skyrim 原生 user event 的少量高价值动作，当前策略是“保持独立 action 身份，再 materialize 固定 combo ABI”；物理按键选择仍交给映射层，`actionId -> combo` 则由 DualPad 自维护的 gamepad `ControlMap` overlay 固定。

## 文档入口

- [docs/DOC_INDEX_zh.md](docs/DOC_INDEX_zh.md)
  - 当前文档总索引与推荐阅读顺序
- [docs/current_input_pipeline_zh.md](docs/current_input_pipeline_zh.md)
  - 从 `HID -> PadState -> PadEventSnapshot -> AuthoritativePollState -> XInput/Mod` 的当前运行时主链路
- [docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md](docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md)
  - 对照 AGENTS 旧目标，核对映射层原子快照、主线程交付边界，以及注入层当前正式契约
- [docs/backend_routing_decisions.md](docs/backend_routing_decisions.md)
  - 当前 backend ownership 与 routing 规则
- [docs/mod_event_keyboard_helper_backend_zh.md](docs/mod_event_keyboard_helper_backend_zh.md)
  - `ModEvent` 固定槽位、虚拟键池 ABI、`KeyboardHelperBackend` 开发约定
- [docs/unified_action_lifecycle_model_zh.md](docs/unified_action_lifecycle_model_zh.md)
  - 统一动作生命周期模型
- [docs/native_pc_event_semantics_zh.md](docs/native_pc_event_semantics_zh.md)
  - 已追到的 Skyrim 原生 PC / gamepad user event 语义
- [docs/controlmap_gamepad_event_inventory_zh.md](docs/controlmap_gamepad_event_inventory_zh.md)
  - 按 controlmap 上下文整理的 gamepad 原生事件母表
- [docs/controlmap_combo_profile_zh.md](docs/controlmap_combo_profile_zh.md)
  - DualPad 自维护的 keyboard-exclusive native combo profile
- [docs/dynamic_glyph_svg_system_plan_zh.md](docs/dynamic_glyph_svg_system_plan_zh.md)
  - 重新收口的动态图标方案：以 SVG 为真源，兼容异形键、不同尺寸和组合键，长期以 Widget/sprite 为主、HTML `<img>` 为 fallback
- [docs/ui_input_ownership_arbitration_plan_zh.md](docs/ui_input_ownership_arbitration_plan_zh.md)
  - 结合当前 `InputModalityTracker` 与 IDA 反编译结果整理的 UI 输入所有权仲裁方案，用来解决键鼠/手柄抢平台与抢图标问题
- [docs/gameplay_input_ownership_investigation_and_plan_zh.md](docs/gameplay_input_ownership_investigation_and_plan_zh.md)
  - 基于当前注入链和 IDA 里的 `BSWin32GamepadDevice::Poll / _root.SetPlatform` 真实路径，整理 gameplay 输入所有权应该放在哪一层、为什么不能直接复用 UI owner 状态机
- [docs/gameplay_sustained_digital_and_cursor_handoff_plan_zh.md](docs/gameplay_sustained_digital_and_cursor_handoff_plan_zh.md)
  - 基于当前代码与 IDA 反编译结果，单独整理 `Sprint` 持续态数字动作为何不该继续走 `DigitalOwner`，以及 gameplay 光标/平台表现交接为何需要独立 handoff 方案
- [docs/sprint_native_source_mediation_plan_zh.md](docs/sprint_native_source_mediation_plan_zh.md)
  - 基于最新 `SprintProbe` 日志与当前 poll/backend 实现，单独整理为什么 `Sprint` 还需要 `SingleEmitterHold + native keyboard mediation`，并给出后续实施顺序
- [docs/current_cleanup_risk_review_zh.md](docs/current_cleanup_risk_review_zh.md)
  - 当前主线代码的冗余点、风险复查点，以及外部 GPT 深度研究提示词
- [docs/agents5_9403e73_customized_refactor_plan_zh.md](docs/agents5_9403e73_customized_refactor_plan_zh.md)
  - 针对提交 `9403e73` 的定制化后续方案，以及本轮已落实项与剩余观察项
- [docs/agents5_review_reconciliation_refactor_plan_zh.md](docs/agents5_review_reconciliation_refactor_plan_zh.md)
  - 最新一轮 `agents5.md` 深度研究建议与当前主线的对齐分析，以及下一轮重构计划
- [src/ARCHITECTURE.md](src/ARCHITECTURE.md)
  - 当前代码模块与主链路总览

## 已知后续点

- `Menu.Scroll* -> Menu.Confirm` 这类跨按钮微小时序，当前仍是 future work。
- 少数顺序敏感 UI 场景，未来可能需要 `direct native event`，但当前不作为主线。
- 游戏侧 `transition/readiness` 机制仍只记录为后续逆向点，不在当前窗口继续硬推。
- 基于 MCP/IDA 重新核对后，`BSWin32KeyboardDevice::Poll` 本地原生 hook 对少量离散 PC action 仍有后续逆向价值，但当前没有闭环，不纳入默认运行时。
- 基于 MCP/IDA 重新核对后，扳机原生链路确认是“`LT/RT byte -> 阈值归一化 -> trigger producer -> AttackBlockHandler`”；因此当前主线继续按“虚拟手柄硬件状态优先”推进，而不是在插件里重写战斗 handler 语义。
- HID 硬件序列号 gap 检测已记 TODO，等待后续协议/状态层升级时再接入。
- 调度层未来可考虑做 `snapshot coalescing` 优化：
  - 目标应是“一个 Poll 尽量反映 latest state，同时保留中间的瞬时边沿事实”。
  - 不建议走“每个 Poll 只处理一个旧 snapshot”的方向，以免放大 backlog 与输入延迟。
- `ModEvent` 目前建议走虚拟键池：
  - 当前公开槽位已扩充为 `ModEvent1-24`
  - 主推荐 `1-8` 是 `DIK_F13 / DIK_F14 / DIK_F15 / DIK_KANA / DIK_ABNT_C1 / DIK_CONVERT / DIK_NOCONVERT / DIK_ABNT_C2`
  - 扩展 `9-24` 是 `NumPadEqual / L-Windows / R-Windows / Apps / Power / Sleep / Wake / WebSearch / WebFavorites / WebRefresh / WebStop / WebForward / WebBack / MyComputer / Mail / MediaSelect`
  - 当前 simulated keyboard bridge 实际发出的 raw DirectInput/DIK 十进制键码依次是 `100 / 101 / 102 / 112 / 115 / 121 / 123 / 126 / 141 / 219 / 220 / 221 / 222 / 223 / 227 / 229 / 230 / 231 / 232 / 233 / 234 / 235 / 236 / 237`
  - 当前对外只认 raw DIK / DirectInput 键码，不再维护额外兼容码口径
  - 范围按 PC `keyboard_english.txt` 与 `controlmap.txt` 收敛；`PrintSrc` 因为已被 `Screenshot` 占用，不纳入 mod 虚拟键池
  - 当前合同只做 `Pulse`
  - `Hold / Toggle / Repeat / 更多 mod 键位类型` 记为未来扩展项，待后续版本再设计
  - 已补 `dinput8.dll` 代理检测接口：
    - `HasProxyDllInGameRoot()`
    - `HasActiveBridgeConsumer()`
    - `ShouldExposeModEventConfiguration()`
    - `IsModEventTransportReady()`
  - TODO: 未来 MCM 应基于 `ShouldExposeModEventConfiguration()` 决定是否显示 `ModEvent` 相关内容

## 历史路线现状

- 旧 `keyboard-native` 主线实验：已归档，不再作为默认设计。
- 旧 consumer-side native-button experiment：代码入口已移除，仅保留历史复盘文档。
- 旧 `action-contract-output` 过渡实验：已被 `FrameActionPlan + PollCommitCoordinator` 替代。
- 旧 `native input reverse targets` / `upstream XInput experiment` 文档：已被当前架构文档与 review 吸收。

