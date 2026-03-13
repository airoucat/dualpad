# Action-Contract-Output Runtime Notes

更新时间：2026-03-13

## Jump

- `Jump` 首按漏吃当前高置信度根因不是 routing，也不是 pulse 太短。
- 当前更像是引擎自己的 shared gameplay family gate 在拦截首个 gameplay pulse。
- 已确认 `sub_140706AF0` 会检查 shared controls state 里的 `Fader Menu(+0xC0)`。
- 现有日志与静态逆向共同说明：
  `Loading Menu` 关闭早于 `Fader Menu` clear。
- 因此上下文虽然已经恢复到 `Gameplay`，引擎内部仍可能处于 transition-not-ready。
- 这会导致第一发 gameplay pulse 被吞，第二发则在 `Fader Menu` 自然 clear 后通过。

## Menu Cancel

- `Menu.Cancel` 当前暴露出的“有时无效”问题，不应再被视为简单时序问题。
- 现实现把 `Menu.Cancel` 与 `Book.Close` 都压成固定 `Tab` 输出。
- 这说明 UI cancel 语义在当前设计里被压扁得过早。
- 更合理的方向是：
  `输入触发器 -> 上下文 -> 上下文化 UI action -> contract -> 具体输出键/后端`
- 不应继续沿着“同一个手柄键在所有菜单里都硬映射成同一个键盘键”这条路走。

## Implication

- 如果后续要对 `Jump` 做机制级修正，优先方向应是：
  对齐或修正引擎自己的 readiness / transition gate。
- 不应继续增加动作特定的脉冲宽度或 release 拖尾参数。

## 2026-03-13 Experimental Patch

- 当前实验补丁没有直接删除 `sub_140706AF0` 这道 gate。
- 当前做法是在 `0x140704DE0 -> call sub_140706AF0` 这个窄 callsite 上观察：
  是否出现“原本所有 gameplay 条件都满足，唯一阻塞项只剩 stale `Fader Menu`”。
- 这版已经从 `Jump` 单点观测提升为：
  只要当前 dispatch 候选是 gameplay action，且命中上述 stale-transition 条件，
  就允许该次 family gate 临时放行。
- 因此它的目标不再是“给 Jump 打补丁”，而是验证：
  gameplay 首按漏吃是否确实属于一类 shared stale transition gate 问题。

## 2026-03-13 Context/UI Sync Prototype

- 当前实验分支已经补了一版 `ContextManager readiness + backend deferred replay`。
- `ContextManager` 现在除 `current context` 外，还会维护：
  - `gameplayReady`
  - `uiReady`
  - `transitionActive`
  - `Fader/Loading/MainMenu/MessageBox` 活跃位
- keyboard-native backend 在 not-ready 时不再返回失败并掉回 compatibility；
  而是把 action 接住，挂起到 deferred 队列，等对应 domain ready 后再重放。
- 这版的目标是把：
  - 菜单关闭后过早送出的 gameplay 首按
  - 菜单打开但 UI 尚未稳定时过早响应的 UI 按键
  收敛到同一套“domain readiness”机制里。
