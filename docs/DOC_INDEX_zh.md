# 文档索引

这份索引用来区分：

- 当前主线文档
- 审核 / review 文档
- 历史与 future 参考

## 一、先看这些

如果要快速理解当前项目，建议按这个顺序读：

1. [../README.md](../README.md)
2. [../src/ARCHITECTURE.md](../src/ARCHITECTURE.md)
3. [final_native_state_backend_plan.md](final_native_state_backend_plan.md)
4. [backend_routing_decisions.md](backend_routing_decisions.md)
5. [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
6. [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
7. [native_pc_event_semantics_zh.md](native_pc_event_semantics_zh.md)
8. [controlmap_gamepad_event_inventory_zh.md](controlmap_gamepad_event_inventory_zh.md)
9. [authoritative_poll_state_refactor_plan_zh.md](authoritative_poll_state_refactor_plan_zh.md)
10. [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
11. [controlmap_combo_profile_zh.md](controlmap_combo_profile_zh.md)
12. [plan_a_long_term_edge_lifecycle_zh.md](plan_a_long_term_edge_lifecycle_zh.md)
13. [poll_commit_coordinator_stage3_zh.md](poll_commit_coordinator_stage3_zh.md)

## 二、当前主线文档

### 架构与路线

- [final_native_state_backend_plan.md](final_native_state_backend_plan.md)
  - 当前主线、剩余阶段、长期目标
- [backend_routing_decisions.md](backend_routing_decisions.md)
  - backend ownership 与 routing 规则
- [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
  - `ModEvent` 固定槽位、虚拟键池 ABI 与 `KeyboardHelperBackend` 开发约定
- [authoritative_poll_state_refactor_plan_zh.md](authoritative_poll_state_refactor_plan_zh.md)
  - 统一最终输出状态的新方案重构计划
- [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
  - backend-neutral 生命周期模型
- [plan_a_long_term_edge_lifecycle_zh.md](plan_a_long_term_edge_lifecycle_zh.md)
  - 在 `agents2/agents3` 约束下的长期 edge 方案
- [poll_commit_coordinator_stage3_zh.md](poll_commit_coordinator_stage3_zh.md)
  - `LifecycleTransaction -> PollCommitCoordinator` 第三阶段说明

### 原生语义与逆向

- [native_pc_event_semantics_zh.md](native_pc_event_semantics_zh.md)
  - vanilla PC / gamepad user event 语义追踪
- [controlmap_gamepad_event_inventory_zh.md](controlmap_gamepad_event_inventory_zh.md)
  - 按 controlmap 上下文展开的 gamepad 事件母表
- [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
  - 从 HID 到游戏消费的当前运行时主链路
- [controlmap_combo_profile_zh.md](controlmap_combo_profile_zh.md)
  - DualPad 维护的 keyboard-exclusive native combo profile
- [upstream_native_state_reverse_targets.md](upstream_native_state_reverse_targets.md)
  - 未来 upstream native-state 逆向目标

## 三、审核与已吸收项

### 已吸收结果

- [review_absorbed_b01_b02_zh.md](review_absorbed_b01_b02_zh.md)
- [review_absorbed_b03_b04_zh.md](review_absorbed_b03_b04_zh.md)
- [review_absorbed_b05_zh.md](review_absorbed_b05_zh.md)
- [review_absorbed_b06_zh.md](review_absorbed_b06_zh.md)
- [review_absorbed_b07_zh.md](review_absorbed_b07_zh.md)
- [review_absorbed_b08_zh.md](review_absorbed_b08_zh.md)
- [review_absorbed_b09_zh.md](review_absorbed_b09_zh.md)

### 第三方审核投喂包

- [../CLAUDE_GAMEPAD_INPUT_REVIEW.md](../CLAUDE_GAMEPAD_INPUT_REVIEW.md)
- [../CLAUDE_REVIEW_B02_HID_PROTOCOL.md](../CLAUDE_REVIEW_B02_HID_PROTOCOL.md)
- [../CLAUDE_REVIEW_B03_EVENT_GENERATION.md](../CLAUDE_REVIEW_B03_EVENT_GENERATION.md)
- [../CLAUDE_REVIEW_B04_TRIGGER_BINDING_TOUCHPAD.md](../CLAUDE_REVIEW_B04_TRIGGER_BINDING_TOUCHPAD.md)
- [../CLAUDE_REVIEW_B05_SYNTHETIC_STATE_REDUCTION.md](../CLAUDE_REVIEW_B05_SYNTHETIC_STATE_REDUCTION.md)
- [../CLAUDE_REVIEW_B06_ROUTING_PLAN_CONTRACT.md](../CLAUDE_REVIEW_B06_ROUTING_PLAN_CONTRACT.md)
- [../CLAUDE_REVIEW_B07_PROCESSOR_ORCHESTRATION.md](../CLAUDE_REVIEW_B07_PROCESSOR_ORCHESTRATION.md)
- [../CLAUDE_REVIEW_B08_POLL_COMMIT_COORDINATOR.md](../CLAUDE_REVIEW_B08_POLL_COMMIT_COORDINATOR.md)
- [../CLAUDE_REVIEW_B09_UPSTREAM_STATE_BRIDGE.md](../CLAUDE_REVIEW_B09_UPSTREAM_STATE_BRIDGE.md)

## 四、历史 / 仅参考

- [native_button_experiment_postmortem.md](native_button_experiment_postmortem.md)
  - 旧 native button 实验的 postmortem，只作历史与 crash/reverse 参考

## 五、这轮已移除的旧实验稿

下列文档已经删除，因为它们的内容要么被当前主线吸收，要么继续保留只会误导维护方向：

- 旧 `action-contract-output` 过渡实验
- 旧 `keyboard-native` 主线实验族文档
- 旧 `native input reverse targets`
- 旧 `upstream XInput experiment`
- 过早的 feasibility/refactor 草案
- 与当前主线无直接关系的外部比较笔记
