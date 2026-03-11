# Keyboard-Native Process/手感分族总结

更新时间：2026-03-11

## 背景

在当前 keyboard-native 路线里，结构性问题已经基本分层解决：

- `Jump` 的主问题是 `sub_140C11600` 里的翻译链 gate。
- `Activate / Sprint / Sneak` 的主问题先是 gameplay validate miss，随后已经通过 gameplay-root validate override 打通。

因此，当前剩余主问题已经从“路由/准入”转移到了“process / 时序 / 手感语义”。

## 当前按手感/处理策略的动作分族

### 1. Jump 族

动作：

- `Game.Jump`

当前判断：

- 已经能稳定进入正确的 gameplay 翻译链。
- 剩余问题更像是 `down/up` 窗口过窄导致的偶发首按失效。

策略：

- 采用 **一次性 pulse**
- 但必须保留一个最小按下窗口
- 不要在同一轮过快释放

### 2. Sneak 族

动作：

- `Game.Sneak`

当前判断：

- validate 已通
- gameplay process 已开始真实消费
- 更接近可用状态

策略：

- 采用 **toggle / press-consume**
- 只做轻量防抖和轻微 release 平滑
- 不需要像 Sprint 那样的长 held

### 3. Activate / Sprint 族

动作：

- `Game.Activate`
- `Game.Sprint`

共同点：

- validate 已经能命中 gameplay root
- 但 process 还没有像 Sneak 那样稳定消费

当前拆分策略：

#### Activate

- 更像 **短按动作**
- 但比 `Jump` 需要更稳的最小按下窗口
- 允许短 pulse，但不能太窄

当前策略：

- release 额外延后 `2` 次 `GetDeviceData`

#### Sprint

- 更像 **held 动作**
- 不适合再用接近 `Jump` 的短 pulse 模型
- 需要更明确的按住期

当前策略：

- release 额外延后 `4` 次 `GetDeviceData`
- 方向上按 held 语义调优，而不是继续缩短 pulse

## 当前结论

现阶段不应再把这些动作都当成同一类 keyboard-native 事件处理：

- `Jump`：翻译链已通，后续主要是 pulse 宽度优化
- `Sneak`：validate/process 已基本通，后续主要是轻量手感优化
- `Activate / Sprint`：validate 已通，但 process 仍需继续按各自语义调优

这意味着后续优化顺序应为：

1. 先稳住 `Sprint` 的 held 语义
2. 再稳住 `Activate` 的最小按下窗口
3. 最后轻量微调 `Sneak`
