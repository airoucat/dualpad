# Layer / Combo 设计方案

更新日期：2026-03-25

本文基于：

- [agents5.md](../agents5.md) 中关于怪物猎人系列双键输入、输入缓冲与优先级机制的研究结论
- 当前正式输入主线 [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
- 当前 cleanup / 风险复盘 [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)

目标不是照搬怪猎的动作语义，而是借鉴其“双键判定手感”来改造 DualPad 的映射层。

---

## 一句话结论

当前建议正式把“双键输入”拆成两种完全不同的语义：

- `Layer:*`
  - 严格先后按
  - 类似 Fn / 修饰键 / 切层
  - 保留当前语义，不做根本修改
- `Combo:*`
  - 近同时双键
  - 用于双键快捷命令
  - 新增独立状态机，不再复用旧 `Button:A+B`

并且：

- 不再使用 `Button:A+B`
- `Combo:*` 不走 subset fallback
- `Combo:*` 默认 exact match only
- `Combo:*` 继续遵守现有铁律：**任意 Fn 键不得与任意面键形成 Layer/Combo**

---

## 设计依据

### 1. 怪猎给出的真正启发

根据 `agents5.md` 的研究结论，怪猎更像：

- 单键先正常进入自己的动作分支
- 双键在短窗口内发生重叠时，抢占到第三动作
- 更像“重叠升级”，不像“明显延迟单键再等待组合”

对 DualPad 来说，应该借的是：

- 近同时双键判定可以不要求完美同帧
- 组合成立更依赖“短窗口 + 重叠”
- 不应默认给所有单键引入明显额外延迟

不该照搬的是：

- 怪猎具体的攻击家族 / motion graph / 武器专属语义

Skyrim 里的双键更像“人为定义的快捷命令”，不是“轻攻击 + 重攻击 = 第三攻击”。

### 2. 当前项目约束

当前正式主线里：

- 映射层负责 `PadState -> PadEventSnapshot`
- `BindingResolver` 负责 `Trigger -> actionId`
- 注入层不应该承担模糊的“组合键推断”

因此 Layer / Combo 的主战场应放在**映射层 / trigger 解析层**，而不是放到后面的 backend 或注入层。

---

## 正式语义

## `Layer:*`

### 定义

`Layer:A+B=Action`

表示：

- 必须先按住 `A`
- 再按 `B`
- 命中后触发 `Action`

### 特点

- 有顺序
- 保留当前 momentary modifier / 严格切层语义
- 允许 subset fallback 继续存在于当前旧链路中，但后续只对 `Layer` 观察，不扩展到 `Combo`

### 适用场景

- Fn / 切层
- 背键 + 某键
- 修饰键 + 主键

---

## `Combo:*`

### 定义

`Combo:A+B=Action`

表示：

- `A` 与 `B` 是**无序**的一对数字键
- 只要两者在一个短窗口内按下，并且发生实际重叠，就命中

`Combo:A+B` 与 `Combo:B+A` 视为同一条配置。

### 特点

- 不要求完美同帧
- 不要求严格先后顺序
- 必须发生按键重叠
- 只支持两键
- exact match only
- 不走 subset fallback

### 适用场景

- 双键快捷命令
- 日常扩展输入
- 不适合 Fn 层修饰键模型

---

## 当前推荐实现方向

## 方案选择

当前建议优先落地 **低延迟重叠型 Combo**，而不是“缓冲单键等待组合”的高容错型 Combo。

原因：

- 更符合怪猎研究里体现的“重叠升级”方向
- 不会给单键普遍引入明显延迟
- 更适合 Skyrim 这种本来没有动作升级状态机的项目

---

## `Combo:*` 的判定规则

### 1. 基础规则

对一条 `Combo:A+B=Action`：

- 只要 `A`、`B` 都是数字键
- 且第二个键的按下时间与第一个键的按下时间差在 `combo_window_ms` 内
- 且当前时刻两者都处于 `down`
- 即命中该 combo

建议默认：

- `combo_window_ms = 22`

后续可允许 `18-30ms` 可配。

### 2. 同帧按下

若 `A` 与 `B` 在同一 `PadEventSnapshot` 中都出现 `ButtonPress`：

- 直接视为 combo 命中
- 不要求顺序

### 3. 跨帧近同时

若：

- 第 1 帧按下 `A`
- 第 2 帧按下 `B`
- 时间差仍在 `combo_window_ms` 内
- 并且 `A` 仍然保持 `down`

则也视为 combo 命中。

### 4. 只触发一次

combo 命中后：

- 当前按住周期内只触发一次
- 直到 `A` 和 `B` 都松开，才重新武装

---

## 单键延迟策略

### 当前推荐

**默认只对参与 `Combo:*` 的单键做局部缓冲等待。**

也就是：

- 只有在 INI 里实际参与了 `Combo:*` 配置的键，才进入 `22ms` 的 combo 判定窗口
- 不参与任何 `Combo:*` 的键，保持按下即执行
- 把额外延迟限制在最小范围内

### 这带来的后果

如果某个 `Combo:A+B` 的组成键本身也都绑定了各自的单键 action，那么：

- 在窗口内，组成键对应的单键 action 会先进入短暂待定状态
- 如果另一键在 `22ms` 内补上并形成重叠，则只触发 combo
- 如果窗口到期仍未形成 combo，则补发原本的单键 action

这是一种**有意接受的取舍**：

- 优点：只对实际参与 `Combo:*` 的键引入极小延迟，其它键不受影响
- 缺点：参与 combo 的单键会有一段很短的待定窗口

### 为什么先接受这个取舍

因为 Skyrim 的双键更像快捷命令，而不是怪猎那种“同家族动作升级”。  
在没有游戏内动作状态机兜底的前提下，如果对所有单键一刀切地做“缓冲单键等待 combo”，很容易把大量基础操作都拖慢。

因此当前建议：

- Phase 1 先做 **仅限 Combo 参与键的局部缓冲版 Combo**
- 如果后续实测表明“组合过于容易漏判”或“子键误触发不可接受”，再考虑引入**可选的 exclusive/buffered Combo 模式**

---

## 冲突与限制

### 1. 铁律继续保留

以下组合继续禁止：

- 任意 `Fn` 键 + 任意面键

适用范围：

- `Layer:*`
- `Combo:*`

### 2. `Combo:*` 只接受数字键

当前不建议让下面这些直接参与 `Combo`：

- 摇杆轴
- 扳机模拟量
- 手势
- 触摸板滑动

原因：

- 它们更适合单独的 gesture / hold / analog 语义
- 混进 `Combo` 会让实现和调试复杂度明显上升

### 3. `Combo:*` 不参与 subset fallback

原因：

- 组合输入本来就比单键更难预测
- 再叠 subset fallback 会非常容易出现歧义

---

## 实现分期

## Phase 1：语义拆分与解析层改造

目标：

- 正式把 `Layer:*` 和 `Combo:*` 分成两条语义

工作：

1. `BindingConfig`
   - `Layer:*` 继续解析为严格顺序触发器
   - `Combo:*` 解析为新的无序双键触发器
2. 旧 `Combo:*`
   - 直接作为新的无序双键语义生效，不再复用 `Layer:*`
   - 文档与模板不再推荐继续写旧用法
3. `Button:A+B`
   - 保持移除，不再恢复

## Phase 2：ComboEvaluator

新增独立 evaluator，职责：

- 维护 combo 参与键的按下时间
- 识别同帧 / 跨帧近同时命中
- 检查重叠条件
- 维护“命中后仅触发一次”的 latch 状态

建议数据结构：

- 固定大小数组或 bitset
- 每个数字键记录：
  - `isDown`
  - `lastPressedAtUs`
  - `comboConsumedEpoch`

## Phase 3：与 BindingResolver 集成

规则：

- exact single
- exact layer
- exact combo

优先级建议：

1. exact combo
2. exact layer
3. exact single
4. subset fallback（仅 Layer / 单键旧链路）

说明：

- `Combo` 由于 exact only，不进入 fallback
- 若同一 snapshot 内 single 与 combo 同时可命中，优先 combo

## Phase 4：调试与验证

新增日志：

- combo pending
- combo exact hit
- combo rearm
- single suppressed by same-snapshot combo

专项验证：

1. 同帧双键
2. 跨帧近同时双键
3. 超窗口失败回退
4. 子键本身已有单键 action 的场景
5. Fn + 面键非法组合保护

## Phase 5：视结果决定是否做 buffered/exclusive Combo

仅在满足以下任一条件时再推进：

- 现有低延迟方案 combo 成功率明显不够
- 用户强烈要求“只要 combo 成立就绝不触发子键”

再考虑：

- 只对参与 combo 的键做极短候选缓冲
- 作为可选模式，不作为默认行为

---

## 当前正式建议

当前建议立即推进的是：

1. 把 `Layer:*` 与 `Combo:*` 完整拆语义
2. 新增无序、短窗口、必须重叠的 `ComboEvaluator`
3. 先做仅限 Combo 参与键的局部缓冲版
4. `Combo` exact only
5. 继续保留 Fn + 面键禁配

暂不建议默认推进的是：

1. 全局单键缓冲延迟
2. buffered/exclusive Combo 默认开启
3. 让 `Combo` 继续复用旧 `Layer` 语义
4. 恢复 `Button:A+B`
