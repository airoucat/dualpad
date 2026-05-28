# 状态整形 / 事件生成层审查记录（2026-04-09）

## 审查范围

本次只看输入链路中“状态整形 / 事件生成”这一层：

- `src/input/state/*`
- `src/input/mapping/PadEvent.h`
- `src/input/mapping/PadEventGenerator.*`
- `src/input/mapping/AxisEvaluator.*`
- `src/input/mapping/ComboEvaluator.*`
- `src/input/mapping/LayerEvaluator.*`
- `src/input/mapping/TapHoldEvaluator.*`
- `src/input/mapping/TouchpadMapper.*`

必要时也交叉核对了：

- `src/input/HidReader.cpp`
- `src/input/injection/SyntheticStateReducer.*`
- `src/input/injection/PadEventSnapshotProcessor.cpp`

## 前提假设

这次审查默认按当前仓库现状判断：

- 目标不是重做整条输入主链
- 重点是确认 `PadState -> PadEventBuffer -> SyntheticPadFrame` 这一段有没有会影响当前运行时的事件失真
- 优先关注：
  - 漏边 / 重边
  - 上下文切换时的状态污染
  - 触控板区域 / 手势识别错误
  - 已配置能力和 latent bug 的优先级差异

## 结论摘要

- 当前没有看到这一层会稳定打出 `P0 / P1` 的问题
- 这轮最值得先记住的是两个 `P2`：
  - evaluator 内部状态会跨 context 泄漏
  - touchpad mapper 过度依赖 `touch1`，并会复用 stale slot-1 坐标
- 另外有一个 `P3`：
  - combo latch 只有两颗键都松开才释放，不适合 modifier 风格的重复触发
- 当前配置下，最容易真实撞到的是 touchpad 相关问题
  - 因为 `BookMenu` 已经绑定了 `Gesture:TpSwipeLeft/Right`
  - `Combo:*` 当前没有大规模启用，所以 combo 问题更像下一阶段结构债

## 具体问题

### 1. Evaluator 状态会跨 context 泄漏

`PadEventGenerator` 内部持有：

- `_comboEvaluator`
- `_tapHoldEvaluator`
- `_touchpadMapper`

这些子状态只有在 `PadEventGenerator::Reset()` 时才会清掉。

但当前运行时里：

- `Generate(...)` 每帧都接受新的 `context`
- `HidReader` 只在设备重连时调用 `eventGenerator.Reset()`
- 普通的 gameplay/menu context 切换不会触发 reset

这意味着：

- 在旧 context 里开始的 hold
- 在旧 context 里开始的 combo pending press
- 在旧 context 里开始的 touch tracking / press tracking

都可能在新 context 里完成并结算。

受影响文件：

- `src/input/mapping/PadEventGenerator.cpp`
- `src/input/HidReader.cpp`

当前判断：

- 这是 `P2`
- 问题不在 parser
- 而在“有状态 evaluator 没有和 context 边界对齐”

### 2. Touchpad mapper 过度依赖 `touch1`，并会复用 stale slot-1 坐标

`TouchpadMapper` 目前的核心事实是：

- `ProcessTouch(...)` 只追踪 `state.touch1`
- `GeneratePressEvent(...)` 在当前 click 帧没有 active touch 时，会回退到 `_lastKnownTouch*`
- `_lastKnownTouch*` 也只来自 `touch1`

因此会出现两类风险：

1. 如果当前有效手指落在 `touch2`，press / slide 事件可能直接丢失
2. 如果点击发生时当前帧已无 active touch，就会拿上一次缓存的 slot-1 坐标做区域判定

这会导致：

- 区域 press 落错区
- slide 起终点坐标不可信
- 事件直接不触发

对当前仓库来说，这不是纯理论问题，因为：

- `BookMenu` 已经使用 `TpSwipeLeft / TpSwipeRight`
- 触控板这条线当前是正式支持面的一部分

受影响文件：

- `src/input/mapping/TouchpadMapper.cpp`

当前判断：

- 这是 `P2`
- 比 combo 问题更贴近当前实际配置
- 后续如果继续扩 touchpad 区域按压或 edge 模式，这个点会更早爆出来

### 3. Combo latch 只有两颗键都释放才会清掉

`ComboEvaluator::PurgeReleasedPairs(...)` 当前的释放条件是：

- `firstButton` 和 `secondButton` 都不在 `currentMask` 里

这意味着一个 pair 一旦成功触发 combo：

- 只要其中任意一颗还按着
- 这个 pair 就仍然保持 latched

结果是 modifier 风格的使用方式会很别扭：

- 按住 A
- 点一下 B 触发一次 combo
- 继续按住 A 再点 B

第二次通常不会再触发，因为 pair 还没有被 purge。

受影响文件：

- `src/input/mapping/ComboEvaluator.cpp`

当前判断：

- 这是 `P3`
- 原因不是它不对，而是当前配置里 `Combo:*` 还没有成为主流正式支持面
- 但如果后面真把背键 / 组合键作为正式输入表面，这个行为大概率要改

## 非阻塞但值得记住的观察

### 1. 轴状态本身没有看到高风险错误

这轮没有看到以下问题的硬证据：

- `AxisEvaluator` 和 `SyntheticStateReducer` 之间出现值错位
- `PadState` 轴值在 reduce 后被错误覆盖
- 轴事件直接导致 analog state 丢失

当前实现更像是：

- `AxisChange` 事件只承担“变化事件”角色
- 最终 analog 值仍以 `snapshot.state` 为准落到 `SyntheticPadFrame`

所以轴这块暂时不列为本轮重点问题。

### 2. 触控板第二触点已经被 parser 保留下来，但映射层基本没消费

当前 `touch2` 并不是 parser 没产出，而是：

- `PadState` 里有
- debugger 里会打印
- 但 mapping 侧几乎不用

这说明后续修 touchpad 时，重点不在协议层，而在 mapping 层的 contact 选择策略。

## 建议调整顺序

### 第一批：先修 context 边界

1. 给 `PadEventGenerator` 增加 context/epoch 边界感知
2. context 切换时 reset combo / tap-hold / touch tracking 状态
3. 避免旧 context 的按住、滑动、组合键在新 context 里结算

### 第二批：修 touchpad contact 选择和 stale fallback

1. 明确 press / slide 应该怎么在 `touch1` / `touch2` 间选 active contact
2. 取消或收紧“无 active touch 时回退到 last known slot-1”的策略
3. 至少保证区域判断不会默默使用过期坐标

### 第三批：再决定 combo 语义

1. 先明确项目想要的是“严格两键同时一次性 combo”
2. 还是“允许 modifier held + 第二键重复点按”
3. 再决定 latch purge 该按“任一键释放”还是“双方都释放”

## 当前建议口径

后续如果有人继续改这一层，建议默认接受这两个原则：

- `PadEventGenerator` 不是纯函数，它内部有跨帧状态
- 既然它有跨帧状态，就必须和 context 边界对齐，而不能只和设备重连对齐

以及：

- `touch2` 不是附带信息
- 对 DualSense 触控板来说，它是正式输入事实的一部分
- 如果 mapping 层继续只围着 `touch1` 写，后续任何 touchpad 强化都会反复踩同一类 bug
