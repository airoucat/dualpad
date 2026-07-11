# Learnings

Corrections, insights, and knowledge gaps captured during development.

**Categories**: correction | insight | knowledge_gap | best_practice
**Areas**: frontend | backend | infra | tests | docs | config
**Statuses**: pending | in_progress | resolved | wont_fix | promoted | promoted_to_skill

## Status Definitions

| Status | Meaning |
|--------|---------|
| `pending` | Not yet addressed |
| `in_progress` | Actively being worked on |
| `resolved` | Issue fixed or knowledge integrated |
| `wont_fix` | Decided not to address (reason in Resolution) |
| `promoted` | Elevated to CLAUDE.md, AGENTS.md, or copilot-instructions.md |
| `promoted_to_skill` | Extracted as a reusable skill |

## Skill Extraction Fields

When a learning is promoted to a skill, add these fields:

```markdown
**Status**: promoted_to_skill
**Skill-Path**: skills/skill-name
```

Example:
```markdown
## [LRN-20250115-001] best_practice

**Logged**: 2025-01-15T10:00:00Z
**Priority**: high
**Status**: promoted_to_skill
**Skill-Path**: skills/docker-m1-fixes
**Area**: infra

### Summary
Docker build fails on Apple Silicon due to platform mismatch
...
```

---

## [LRN-20260523-001] best_practice

**Logged**: 2026-05-23T21:15:00+08:00
**Priority**: high
**Status**: promoted
**Area**: infra

### Summary
本仓库变更完成并验证后，默认提交并推送到当前跟踪远端分支。

### Detail
用户明确要求在本仓库记住该工作流规则：完成变更后默认推送远端。已提升到 `AGENTS.md` 的工作规则；除非用户明确要求只保留本地改动、不提交或不推送，否则后续收尾应包含 commit + push。

## [LRN-20260613-001] best_practice

**Logged**: 2026-06-13T00:08:24+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 实机链路中，底层 HID 有输入但 authoritative poll 仍为零时，不得用菜单 gate 或输出层条件补丁止血，必须逐边界追踪 ingress、runtime projection 与 native commit 的合同。

### Detail
本轮用户明确指出上一版修法像临时止血。实际根因链路也验证了这个风险：source evidence marker 与 source snapshot 分开入队会被其他设备 evidence 交错，导致 `FrameAssembler` 看到较旧 revision 后 hard reset；同时 native commit 若使用 input_v2 `contextRevision` 作为 `contextEpoch`，会与 `ContextResolver` 的 `legacyContextEpoch` 不一致，菜单态下 slot 可能立刻被判 stale。后续处理类似 “有 processed HID 输入但 poll/commit 为零” 的问题时，应先要求或打开 `RuntimePlan`、`NativeButtonCommit`、`runtime_debug_snapshot.csv` 等边界证据，再改边界合同，而不是在最终输出处加特殊 case。

## [LRN-20260613-002] insight

**Logged**: 2026-06-13T00:23:19+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 的 `gateAware` 只应表达 gameplay ownership gate，不应让 Menu / Favorites / Console 等 context-local native output 在 poll commit 层等待 gate。

### Detail
本轮继续审计发现，`Menu.ScrollDown` 等 repeat native actions 的 descriptor 是 `gateAware=true`，但 `NativeButtonCommitBackend` 旧的 `IsGameplayGateOpen()` 对 `InputContext::Menu` 返回 false。结果是 projection 生成 menu repeat command 后，`PollCommitCoordinator` 仍会停在 waiting gate，XInput poll 读到的 digital mask 可能继续为 0。后续改 native routing 时要把“gameplay keyboard/mouse ownership suppression”和“context-local native output 是否允许提交”分开；菜单态本地 native output 不应被 gameplay gate 拦截。

## [LRN-20260614-001] insight

**Logged**: 2026-06-14T20:58:00+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 启动/首次进入 Menu 时，当前帧的 source evidence 必须优先于默认或历史 `PublishedGameplayPresentation.menuEntryOwner`。

### Detail
实机调试中旧日志已经证明 HID/parser 可以读到有效手柄状态，但菜单兼容面和 glyph owner 仍可能保持 KeyboardMouse。`PresentationProjection` 原逻辑在 `_published.uiContextId == None && hostMode == Menu` 时先继承 `menuEntryOwner`，即使同一帧已有 `gamepadEvidence/gamepadLease`。启动主菜单没有 gameplay owner 历史时，默认 `menuEntryOwner=KeyboardMouse` 会压过当前手柄证据。修复时应把非 gameplay 的当前 source evidence 放在 menu-entry inheritance 之前；只有没有新证据时才继承 gameplay menu entry owner。同时 live trace 的 `expected_presentation_surface.csv` 要写实际 committed compatibility surface，不能继续用硬编码 KeyboardMouse 当现场证据。

## [LRN-20260615-001] insight

**Logged**: 2026-06-15T00:13:00+08:00
**Priority**: medium
**Status**: pending
**Area**: build

### Summary
DualPad 本地验证不要假设 CMake 在 PATH；优先按仓库 CI 使用 `xmake build -y <target>` / `xmake run -y <target>`。

### Detail
本轮红灯验证时先尝试 `cmake --build ...`，本机 PowerShell PATH 没有 `cmake`。随后又把 `xmake -y build <target>` 写成了错误顺序，xmake 将 target 解析为 invalid argument。仓库事实入口是 `scripts/ci/run_phase8_ci.ps1`，其中 target 构建/运行格式固定为 `xmake build -y DualPadIngressTests` 与 `xmake run -y DualPadIngressTests`。后续 focused 验证应先看 CI 脚本里的实际命令。

## [LRN-20260615-002] insight

**Logged**: 2026-06-15T22:13:47+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 菜单确认不能把 dinput8 `DIK_E` 键盘桥当作 gamepad `Accept` 的等价替代入口。

### Detail
本轮实机 A/B 中，`Menu.Confirm` 已正常生成 RuntimePlanAction，`KeyboardHelperBackend` 也成功向 dinput8 发送 12 次 `DIK_E(0x12)` press/release，`G:/g/SkyrimSE/DualPadDInput8.log` 确认 `Device::GetDeviceData` 消费了 bridge event，但游戏菜单仍无确认反应。因此在菜单 native output 断链里，KeyboardHelper / DirectInput 键盘桥只能作为诊断证伪工具，不能作为正式 `Menu.Confirm` 修复路径。随后 direct `BSInputEventQueue` Accept probe 在实机闪退，且 `docs/backend_routing_decisions.md` / `docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md` 明确禁止把 direct `ButtonEvent/InputEventQueue` 拼接恢复为主线；后续应回到旧基线的 poll-owned virtual XInput current-state 语义，比较当前与 `0d2c93a` 的 poll 时序、context/controlmap overlay 和菜单模式消费者差异。

## [LRN-20260615-003] insight

**Logged**: 2026-06-15T22:58:00+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 菜单“Triangle 确认导致下移”不能再默认归因于输出 D-pad Down 或未 deadzone 的左摇杆轴量。

### Detail
用户复测 deadzone 版后仍下移，最新 `DualPad.log` 的首个 Triangle 窗口已经证明：raw `mask=0x00000008` 只生成 `RuntimePlanAction action=Menu.Confirm phase=Press`；`AuthoritativePoll` 输出 `xinputButtons=0x1000`，`move=(0.000,0.000)`；`UpstreamGamepad` 输出 `buttons=0x1000 lx=0 ly=0 rx=0 ry=0`；没有 `Menu.ScrollDown`，也没有 `xinputButtons=0x0002`。因此后续排查应转向 Skyrim/Scaleform 对 `Accept` 的菜单消费链、ControlMap context/linked mapping、pulse 时序或菜单平台状态，而不是继续在 resolver、D-pad 或 axis deadzone 上加补丁。

## [LRN-20260616-001] insight

**Logged**: 2026-06-16T23:56:09+08:00
**Priority**: high
**Status**: pending
**Area**: runtime / menu presentation

### Summary
DualPad 回看旧基线时必须确认保留下来的 helper 是否仍有调用点，尤其是菜单 platform refresh 这类跨旧 runtime 和 `input_v2` 的兼容语义。

### Detail
主菜单 Triangle 首次下移问题中，`SkyrimCompatibilitySurface::ShouldRefreshMenus()` 在 `input_v2` 仍存在，容易误以为 PH8 迁移保留了旧刷新语义；但历史 grep 证明 `261869b` 删除 `InputModalityTracker` 时也删除了唯一调用链：`ShouldRefreshMenus() -> RefreshMenus() -> menu->RefreshPlatform() -> _root.DualPad_OnPresentationChanged()`。后续遇到“旧基线可用、当前不可用”的菜单兼容问题时，不能只比较数据结构和 helper 实现，必须用 `git grep <old> <symbol>` 与 `git grep HEAD <symbol>` 验证调用链是否仍闭合。

## [LRN-20260627-001] insight

**Logged**: 2026-06-27T22:09:46+08:00
**Priority**: high
**Status**: pending
**Area**: runtime / menu presentation

### Summary
DualPad 异步 menu refresh key 只能包含会产生 refresh-relevant dirty 的字段，否则会制造假 superseded 并漏掉冷启动首次 `RefreshPlatform()`。

### Detail
冷启动 Main Menu 首次 Triangle/Y 仍下移的现场日志显示：首个 `[MenuRefreshTrace] event=request` 捕获 `epoch=1 dirty=0x3F gameplayPresentationRevision=1`，UI task 执行时 committed state 已变成同一 presentation epoch/context 但 `gameplayPresentationRevision=2 dirty=0x00`，旧 key 因包含 gameplay revision 被判 `Superseded`，导致第一次 `Menu.Confirm` 的 `xinputButtons=0x1000` 在任何 `result=Completed` 前发出。后续设计异步 request key 时，key 字段必须和 dirty/eligibility 语义一致；若字段变化不会触发 dirty，就不应让它使 in-flight request stale。对于 `DeferredNotReady` held intent，也不能在相同 key 的每个 stable tick 上重排，否则会形成 refresh storm。

## [LRN-20260711-001] workflow

**Logged**: 2026-07-11T00:53:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tooling / PowerShell / ripgrep

### Summary
Windows PowerShell 不会替 `rg` 展开 `path/*.cpp` 形式的文件参数；应搜索目录并用 `-g` 传 glob。

### Detail
在 PowerShell 中多次使用 `rg pattern src/input/Foo.*` 时，未展开的 `*` 被传给 Windows 文件 API 并报“文件名、目录名或卷标语法不正确”。稳定写法是 `rg pattern src/input -g 'Foo.*'`，或明确列出具体文件。该规则适用于本仓库所有 `rg` 复查命令。

## [LRN-20260711-002] correction

**Logged**: 2026-07-11T08:05:00+08:00
**Priority**: high
**Status**: resolved
**Area**: runtime / single-writer ownership

### Summary
Skyrim 的 `BSInputDeviceManager` input event sink 在读档生命周期中可能串行迁移到另一条 OS 线程；逻辑单 writer 不能等同于进程全生命周期固定 thread ID。

### Detail
匹配 build `1b3ca5a2bc7a` 的实机日志证明：owner 在线程 `29128` 完成 frame token 1-359 后，Loading/Fader 建立期间由线程 `22420` 进入 token 360。旧 guard 因固定 thread ID 假设永久 `thread_drift`。正确合同由 RAII owner ticket、`tickActive` 和严格单调 frame token 共同建立：前一 ticket 已释放时允许显式、可观察的串行 thread handoff；ticket 活跃期间的异线程进入仍必须永久 fail-closed。不要仅凭函数名或单次主菜单采样声称 Skyrim 输入入口固定线程。

## [LRN-20260711-003] correction

**Logged**: 2026-07-11T09:30:00+08:00
**Priority**: high
**Status**: resolved
**Area**: ingress / ordering / latest publication

### Summary
`IngressSource` 是粗粒度诊断标签，不是 producer identity 或 clock-domain identity；DualPad ordered ingress 只能以 hub 分配的 `IngressEvent.seq` 作为排序权威。

### Detail
build `32617f1ed2fa` 的实机日志出现 429 次 `transition=sequence_gap`，但没有设备 sequence-gap marker 或 queue overflow。`DeviceFamilyPublisher` 标签同时承载 HID 与键鼠线程在不同采集时钟下生成的 marker，时间戳倒退并不表示数据丢失。另一个独立问题是 latest-wins `LatestSourceEvidence` 可以描述仍在本轮 capture cutoff 后方的 marker；这种 revision 超前必须延迟，不能当作 pairing corruption，且配对完成前 latest/ordered pad facts 不能发布新 boundary 的 stable frame。后续修改 ingress 时，应以 seq 检测真实顺序损坏，以时间戳全局 max 推进 evaluation time，并显式区分“latest 领先 cutoff”和“同一 ordered pair 不匹配”。
