# Runtime 并发与发布合同

本文记录 `S-DP5-RC20-HOTFIX` 已落地的线程所有权、不可变输出和 pulse 推进合同。它是 reviewed narrative，不替代代码、测试或实机证据。

## 所有权矩阵

| Surface | Writer | Reader | 合同 |
| --- | --- | --- | --- |
| `LatestPadState` / `LatestSourceEvidence` | HID / source producer，经 `IngressHub` 互斥临界区发布 | runtime owner | latest-wins；一次 capture 只能看到完整 generation |
| `OrderedEdgeQueue` | ingress producer | runtime owner | 只保存 digital edge、boundary、reset/overflow 等有序事实；event hard cap |
| `ContextResolver` publication | runtime owner | presentation/prompt/debug consumer | mutex-protected by-value snapshot，不返回内部动态对象引用 |
| `DualPadRuntime` mutation | `InputFramePump` 的唯一 active owner ticket | debug/replay view | `RuntimeOwnerGuard` 拒绝重入、重复/倒退 token、并发 thread drift 和 stop 后重绑；允许已释放 ticket 之间的显式串行 handoff |
| pulse FSM / native commit | runtime owner | `PollOutputAdapter` | down/up 以 runtime generation 推进；Poll 调用次数不是时钟 |
| `PollOutputFrame` | runtime owner | 任意数量 Poll readers | `atomic<shared_ptr<const PollOutputFrame>>` 一次发布；reader 只 acquire/serialize |
| menu refresh request | runtime owner capture | verified SKSE UI task | request 绑定 target identity；UI task 只复验并触碰一个 allowlisted target |

## 唯一 runtime owner

首选 owner candidate 是 `BSInputDeviceManager` 的 `InputFramePump`。每个 tick 分配单调 frame token，并按固定顺序完成 context refresh、ingress capture、frame assembly、interaction、gameplay/presentation/prompt projection、pulse commit 和 output publication。

SKSE task fallback 必须经过同一个 `RuntimeOwnerGuard`。实机读档证明 `BSInputDeviceManager` event sink 可能在严格后续 token 上迁移 OS thread，因此逻辑 owner 由 RAII ticket 表达，不由进程全生命周期固定 thread ID 表达：

- 前一 ticket 已释放且 frame token 严格递增时，允许串行 handoff，更新 owner thread hash，并记录 `event=rebound` / handoff count；
- ticket 仍 active 时，异线程进入属于并发 writer，永久 degraded 并发布 neutral output；
- 重复、倒退 token 或 stop 后进入仍 fail-closed；
- 所有 runtime mutation 继续要求当前线程持有唯一 active ticket。

host tests 已证明 serialized handoff 与 concurrent drift 两条相反路径。build `1b3ca5a2bc7a` 已证明真实读档会发生 thread migration；修复 build `1edb1949ecc4` 的 handoff 后 generation 连续性仍属于动态复测门。

## 不可变 Poll 输出

`PollOutputFrame` 同时携带：

- publication/runtime generation；
- manifest/context/presentation/action epoch；
- menu stack revision；
- 已序列化 buttons、axes、triggers、packet；
- route health；
- pulse token 与 down/up generation。

Poll hook 每次只 acquire 一份 frame 并序列化，不执行 ingress drain、runtime mutation、pulse tick 或 reader-side packet 更新。旧对象由 reader 的 `shared_ptr` 生命周期持有，不会被 writer 原地复用。

## generation-based pulse

- down 在 owner generation N 的 commit acknowledgement 成功后生效。
- up 必须位于严格后续 owner generation，并继续满足 `minDownMs` 下界。
- 同 generation 重复 begin/tick/flush 不推进 pulse。
- context/epoch、overflow/recovery、disconnect 与 route unavailable 会以显式 boundary generation 取消 managed state。
- `Game.Favorites` native route 在真实游戏、matching dump/IDA 和循环门禁完成前保持默认关闭。

## Hook 与 UI task

Hook 安装使用 exact preflight、compare-write、replacement 复验和 expected-current rollback。安全回滚与 unsafe residue 分别记录为 `RolledBack` 和 `UnsafePartial + FailClosed`。compat entry gateway 保存完整指令边界，避免把普通 prologue 误当作原始 branch target。

菜单刷新不再遍历 `RE::UI::menuStack`。request 捕获 menu name、instance ID、menu/movie pointer、stack/context revision、presentation epoch 和 dirty flags；UI task 在调用前重新验证同一 target、movie、`_root` 与 callback readiness。

## 证据边界

已证明：host 状态机、并发 publication、2/4/8 readers、stale target、transaction rollback 和 Windows build。

待证明：Skyrim 1.5.97 修复后 owner handoff 的 generation 连续性、完整 owner/Poll/UI ordering、真实 pulse 可见性、长时间 soak 与 Favorites crash dump 闭环。在这些证据完成前，发布状态不得为 `GO`。
