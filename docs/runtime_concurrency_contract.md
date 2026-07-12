# Runtime 并发与发布合同

本文记录 `S-DP5-RC20-HOTFIX` 已落地的线程所有权、不可变输出和 pulse 推进合同。它是 reviewed narrative，不替代代码、测试或实机证据。

## 所有权矩阵

| Surface | Writer | Reader | 合同 |
| --- | --- | --- | --- |
| `LatestPadState` / `LatestSourceEvidence` | HID / source producer，经 `IngressHub` 互斥临界区发布 | runtime owner | latest-wins；一次 capture 只能看到完整 generation |
| `LatestKbmGameplayFacts` | Skyrim input callback 内的 KBM producer，经 Hub 原子 batch 发布 | runtime owner | binding snapshot、physical ledger、ordered facts 与 boundary key 同 callback 一致；adapter 失败不发布也不推进 ledger |
| `OrderedEdgeQueue` | ingress producer | runtime owner | 只保存 digital edge、boundary、reset/overflow 等有序事实；event hard cap |
| `ContextResolver` publication | runtime owner | presentation/prompt/debug consumer | mutex-protected by-value snapshot，不返回内部动态对象引用 |
| `UiMenuObserver` live capture | verified SKSE UI task | runtime owner | menu event 先发布 `Partial` transition facts；`RE::UI::menuStack` 只在 coalesced `AddUITask` 中读取，并以 event sequence 拒绝 stale capture |
| `DualPadRuntime` mutation | `InputFramePump` 的唯一 active owner ticket | debug/replay view | `RuntimeOwnerGuard` 拒绝重入、重复/倒退 token、并发 thread drift 和 stop 后重绑；允许已释放 ticket 之间的显式串行 handoff |
| pulse FSM / native commit | runtime owner | `PollOutputAdapter` | down/up 以 runtime generation 推进；Poll 调用次数不是时钟 |
| `PollOutputFrame` | runtime owner | 任意数量 Poll readers | `atomic<shared_ptr<const PollOutputFrame>>` 一次发布；reader 只 acquire/serialize |
| `PollMaterializationReceipt` | verified Poll serialize 完成点 | 同 callback runtime owner | 固定容量、按 callback thread exact consume-once；missing/ambiguous/reused/thread mismatch 全部 fail-closed，禁止 reacquire 最新 frame 代替 |
| current-cycle prepared commit | runtime owner | callback-local event adapter / backend | 顺序固定 `receipt -> Prepare -> Apply -> Commit`；失败重放 previous channel/transient/Sprint/backend snapshot |
| cursor handoff ack | verified UI task | runtime owner | token/context/presentation/menu identity/position sync exact match，且只能消费一次 |
| menu refresh request | runtime owner capture | verified SKSE UI task | request 绑定 target identity；UI task 只复验并触碰一个 allowlisted target |

## 唯一 runtime owner

首选 owner candidate 是 `BSInputDeviceManager` 的 `InputFramePump`。每个 tick 分配单调 frame token，并按固定顺序完成 context refresh、ingress capture、frame assembly、interaction、gameplay/presentation/prompt projection、pulse commit 和 output publication。

SKSE task fallback 必须经过同一个 `RuntimeOwnerGuard`。实机读档证明 `BSInputDeviceManager` event sink 可能在严格后续 token 上迁移 OS thread，因此逻辑 owner 由 RAII ticket 表达，不由进程全生命周期固定 thread ID 表达：

- 前一 ticket 已释放且 frame token 严格递增时，允许串行 handoff，更新 owner thread hash，并记录 `event=rebound` / handoff count；
- ticket 仍 active 时，异线程进入属于并发 writer，永久 degraded 并发布 neutral output；
- 重复、倒退 token 或 stop 后进入仍 fail-closed；
- 所有 runtime mutation 继续要求当前线程持有唯一 active ticket。

host tests 已证明 serialized handoff 与 concurrent drift 两条相反路径。build `1b3ca5a2bc7a` 已证明真实读档会发生 thread migration；build `71c57b30ae0a` 已证明 handoff 后 generation 可推进到 3000 且没有 `thread_drift`。同一日志的 1519 次 handoff 也证明 runtime owner 不能获得 UI authority，因此 owner tick 现在只消费 immutable menu snapshot，不读取 `RE::UI` 或执行 Scaleform attach。

## 不可变 Poll 输出

`PollOutputFrame` 同时携带：

- publication/runtime generation；
- manifest/context/presentation/action epoch；
- menu stack revision；
- 已序列化 buttons、axes、triggers、packet；
- route health；
- pulse token 与 down/up generation。

Poll hook 每次只 acquire 一份 frame 并序列化，不执行 ingress drain、runtime mutation、pulse tick 或 reader-side packet 更新。旧对象由 reader 的 `shared_ptr` 生命周期持有，不会被 writer 原地复用。

serialize 成功后可额外发布一次 immutable `PollMaterializationReceipt`，它只证明“哪一份 frame 已在本 cycle materialize”。owner callback 不能重新 `AcquireForPoll()` 推测本轮 identity；receipt 与当前 epoch/session/context/cutoff 不一致时，current-cycle mutation 必须保持关闭，next-Poll 仍按新事实独立计算。

engine query scope 使用 thread-local 固定容量栈，只允许 verified caller/domain 在栈顶读取同一 owner-token/epoch/context/runtime-generation snapshot。无 scope、Unknown、Remap、stale 或栈溢出都 exact 调用 original；当前 production 没有启用任何 caller scope。

## generation-based pulse

- down 在 owner generation N 的 commit acknowledgement 成功后生效。
- up 必须位于严格后续 owner generation，并继续满足 `minDownMs` 下界。
- 同 generation 重复 begin/tick/flush 不推进 pulse。
- context/epoch、overflow/recovery、disconnect 与 route unavailable 会以显式 boundary generation 取消 managed state。
- `Game.Favorites` native route 在真实游戏、matching dump/IDA 和循环门禁完成前保持默认关闭。

## Hook 与 UI task

Hook 安装使用 exact preflight、compare-write、replacement 复验和 expected-current rollback。安全回滚与 unsafe residue 分别记录为 `RolledBack` 和 `UnsafePartial + FailClosed`。compat entry gateway 保存完整指令边界，避免把普通 prologue 误当作原始 branch target。

menu event 到达后，`UiMenuObserver` 立即发布保留上一稳定节点的 `Partial` snapshot，使 owner 在 UI capture 尚未完成时保持 menu-side fail-closed。live `RE::UI::menuStack` capture 只经 coalesced `AddUITask` 执行；capture 的 event sequence 落后时不能覆盖新 pending event，也不能清除 dirty。`ScaleformPromptAdapter::AttachToMenu()` 与 `ActionExecutor` 的 HUD UI mutation 同样只从 UI task 调用。

菜单刷新不再遍历 `RE::UI::menuStack`。request 捕获 menu name、instance ID、menu/movie pointer、stack/context revision、presentation epoch 和 dirty flags；UI task 在调用前重新验证同一 target、movie、`_root` 与 callback readiness。

## 证据边界

已证明：host 状态机、并发 publication、2/4/8 readers、stale target、UI capture event-sequence rejection、owner/UI 静态隔离、transaction rollback 和 Windows build。

待证明：Skyrim 1.5.97 新 build 的 owner/Poll/UI ordering、菜单后按键恢复、真实 pulse 可见性、长时间 soak 与 Favorites crash dump 闭环。在这些证据完成前，发布状态不得为 `GO`。

mixed-input 另有 I-P、I-CURSOR、I-SPRINT、I-KBM 与 I-0/I-1/I-2/I-MENU/I-5 动态门禁；静态/host 并发测试只能证明 fail-closed 事务，不能批准对应 production capability。

I-P shadow evidence 复用 callback-local receipt 与 `Prepare -> Apply -> Commit` 结果，不重新 Acquire Poll frame。记录器默认关闭；开启后只在 decision 变化、时钟回退或每 10 秒健康采样时追加 JSONL，目录不可写时不抛异常并保留后续重试资格。该路径是同步 debug I/O，只允许短时证据采集，日常运行必须关闭。

presentation shadow evidence 在同一 owner tick 中先复制上一份 atomic presentation 与 pending cursor plan，再消费 exact ack、投影并提交新 snapshot，最后记录 plan/ack/owner/menu identity 与 Original-only engine snapshot。I-CURSOR 两个方向为 `NO-GO` 时均使用 `MappingUnverified`，没有 exact success ack 或方向级动态 `NotRequired` 裁决就不得推进 committed cursor owner。
