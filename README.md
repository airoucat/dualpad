# dualpad

## Skyrim SE 1.5.97 DualSense input plugin

当前项目聚焦于 `Skyrim SE 1.5.97 + CommonLibSSE-NG` 的 DualSense 输入适配与注入重构。

当前主线原则：

- 稳定运行时仍以官方 `poll-xinput-call` 路线为主
- `keyboard-native` 作为数字动作的实验主线持续推进
- 旧的 consumer-side `ButtonEvent` 注入实验只保留为逆向与复盘材料，不再作为正向方案

## 文档入口

- [docs/native_button_experiment_postmortem.md](docs/native_button_experiment_postmortem.md)
  - 旧 native button 实验为什么不是最终方案
- [docs/backend_routing_decisions.md](docs/backend_routing_decisions.md)
  - 当前后端 ownership / routing 决策
- [docs/action_contract_output_experiment_zh.md](docs/action_contract_output_experiment_zh.md)
  - `输入触发器 -> 上下文 -> actionId -> contract -> 输出后端` 实验方案记录
- [docs/action_contract_output_runtime_notes_zh.md](docs/action_contract_output_runtime_notes_zh.md)
  - keyboard-native / action-contract-output 运行时结论与问题收束
- [docs/unified_action_lifecycle_model_zh.md](docs/unified_action_lifecycle_model_zh.md)
  - ButtonEvent / keyboard / mod 共用的统一动作生命周期模型
- [docs/final_native_state_backend_plan.md](docs/final_native_state_backend_plan.md)
  - 长期目标架构与迁移方向
- [src/ARCHITECTURE.md](src/ARCHITECTURE.md)
  - 当前输入架构总览

## 模拟键盘当前已完成工作

当前 `keyboard-native` 这条线已经不是单纯的“打一发键盘 pulse”，而是逐步收成一条独立的数字动作后端。已完成的工作包括：

- 建立了 `keyboard-native / scancode / dinput8 proxy` 主线，不再回到 `ButtonEvent` 注入路线
- 落地了 `actionId -> contract -> output backend` 这套实验骨架
- 在 backend policy 中给动作分配输出 contract，当前已覆盖 `Pulse / Hold / Toggle / Repeat`
- 引入 `DesiredKeyboardState`，区分 windowed pulse、transactional pulse、continuous hold 等交付方式
- 打通了 `TriggerAction / SubmitActionState` 路径，逐步替代旧的命令式 `Press / Release / Pulse` 思路
- 为 `Toggle / Repeat` 做了独立 contract 调度，不再简单借旧 pulse/held 行为
- 加入 deferred replay 机制，让 not-ready 时的 keyboard-native 动作先挂起，再在可交付窗口重放
- 加强了 UI canonical scancode 解析，避免菜单确认/取消直接落回 gameplay 键面
- 清理了大量旧实验残留，包括旧 `InputLoop` 探针链、旧 preprocess 内部实验块、旧 poll/external worker 历史残留
- 补了大量运行时诊断日志，用来观察 mapping、dispatch、family gate、handler、deferred/replay 等关键边界

当前更准确的状态是：

- `keyboard-native` 已经具备“动作规划 + 生命周期调度 + scancode 输出”的基本形状
- 但游戏侧 readiness / transition 语义仍未完全对齐，尤其是 `Loading/MainMenu` 退出后的 `Fader Menu` 尾巴

## TODO：游戏侧 transition/readiness 机制

这项工作先记为后续 TODO，当前不作为立即推进项。

待办内容：

- 继续逆向并验证游戏侧 `transition/readiness` 机制，重点是 `Fader Menu` / `FadeDone` / gameplay family gate 的真实同步边界
- 在证据足够后，再考虑做“窄范围的游戏侧修正”，而不是继续靠 backend 参数或动作特判兜底

当前已确认的问题：

- `Gameplay` 上下文恢复，不等于游戏真正 ready 接收 gameplay 输入
- `Loading/MainMenu` 关闭后，shared `Fader Menu` 可能仍持续一段时间
- 如果在这段窗口里提前交付 gameplay keyboard-native pulse，会出现首按无效或语义抢跑

当前暂缓原因：

- 直接动游戏侧 transition/readiness 机制风险较高
- 全局放松 gate 可能带来 UI 过渡期输入污染、菜单/加载期误触发、整合包兼容性问题
- 下一步如果推进，必须坚持“窄 callsite、窄条件、可回滚”的修法

一句话记录：

- 这个问题已经确认存在，也确认值得做
- 但它属于游戏侧机制修正题，不应在当前窗口里继续用粗放补丁推进
