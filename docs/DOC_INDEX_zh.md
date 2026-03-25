# 文档索引

这份索引只保留当前仍作为主线参考的文档。已经完成的阶段计划、旧实验复盘、被当前主线吸收的 review 汇总都已移除。

## 推荐阅读顺序

1. [../README.md](../README.md)
2. [../src/ARCHITECTURE.md](../src/ARCHITECTURE.md)
3. [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
4. [mapping_snapshot_atomicity_audit_and_injection_contract_zh.md](mapping_snapshot_atomicity_audit_and_injection_contract_zh.md)
5. [backend_routing_decisions.md](backend_routing_decisions.md)
6. [native_pc_event_semantics_zh.md](native_pc_event_semantics_zh.md)
7. [controlmap_gamepad_event_inventory_zh.md](controlmap_gamepad_event_inventory_zh.md)
8. [controlmap_combo_profile_zh.md](controlmap_combo_profile_zh.md)
9. [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
10. [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
11. [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)
12. [agents5_review_reconciliation_refactor_plan_zh.md](agents5_review_reconciliation_refactor_plan_zh.md)
13. [agents5_9403e73_customized_refactor_plan_zh.md](agents5_9403e73_customized_refactor_plan_zh.md)

## 当前主线文档

### 架构与运行时

- [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
  - 当前输入主链路，从 `HidReader` 到 `Skyrim Poll` 的正式运行时流程。
- [mapping_snapshot_atomicity_audit_and_injection_contract_zh.md](mapping_snapshot_atomicity_audit_and_injection_contract_zh.md)
  - 对照 AGENTS 旧目标核对映射层 producer-side 原子快照、主线程交付边界和当前注入层契约。
- [backend_routing_decisions.md](backend_routing_decisions.md)
  - 当前 backend ownership、routing 以及哪些动作不在正式支持面内。
- [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
  - 当前统一动作生命周期合同，以及 planner / backend 的职责边界。

### 原生语义与 controlmap

- [native_pc_event_semantics_zh.md](native_pc_event_semantics_zh.md)
  - 目前已确认的原生 producer / handler 家族与设计结论。
- [controlmap_gamepad_event_inventory_zh.md](controlmap_gamepad_event_inventory_zh.md)
  - 按 vanilla `controlmap` 上下文展开的 gamepad 原生事件清单。
- [controlmap_combo_profile_zh.md](controlmap_combo_profile_zh.md)
  - DualPad 运行时覆盖到 `ControlMap` 的 combo-native profile。

### Mod 与清理

- [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
  - `ModEvent1-24` 的固定槽位 ABI、键池以及 `KeyboardHelperBackend` 口径。
- [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)
  - 当前代码仍值得继续观察或精简的点。
- [agents5_review_reconciliation_refactor_plan_zh.md](agents5_review_reconciliation_refactor_plan_zh.md)
  - 最新一轮 `agents5.md` 深度研究建议与当前主线的对齐分析，以及下一轮重构计划。
- [agents5_9403e73_customized_refactor_plan_zh.md](agents5_9403e73_customized_refactor_plan_zh.md)
  - 基于提交 `9403e73` 和新版 `agents5.md` 的定制化重构方案，按“立即做 / 先验证 / 暂不做”重新分层。

## 维护规则

- 当前文档应优先描述“正在运行的正式主线”，不要继续把旧实验路线和现行方案混写。
- 新的阶段性研究如果只是临时草案，优先放到单独临时文件或外部记录，不要直接混入这份索引。
- 如果以后再次产生大量历史文档，建议单独建 `archive/`，避免根目录再堆积一批失效说明。
