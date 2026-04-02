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
9. [dynamic_glyph_svg_system_plan_zh.md](dynamic_glyph_svg_system_plan_zh.md)
10. [ui_input_ownership_arbitration_plan_zh.md](ui_input_ownership_arbitration_plan_zh.md)
11. [gameplay_input_ownership_investigation_and_plan_zh.md](gameplay_input_ownership_investigation_and_plan_zh.md)
12. [gameplay_sustained_digital_and_cursor_handoff_plan_zh.md](gameplay_sustained_digital_and_cursor_handoff_plan_zh.md)
13. [sprint_native_source_mediation_plan_zh.md](sprint_native_source_mediation_plan_zh.md)
14. [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
15. [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
16. [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)
17. [agents5_review_reconciliation_refactor_plan_zh.md](agents5_review_reconciliation_refactor_plan_zh.md)
18. [agents5_9403e73_customized_refactor_plan_zh.md](agents5_9403e73_customized_refactor_plan_zh.md)
19. [gameplay_ui_owner_code_ida_refactor_plan_zh.md](gameplay_ui_owner_code_ida_refactor_plan_zh.md)
20. [phase2_gameplay_presentation_owner_minimal_plan_zh.md](phase2_gameplay_presentation_owner_minimal_plan_zh.md)
21. [phase1_phase4_code_review_findings_zh.md](phase1_phase4_code_review_findings_zh.md)
22. [phase1_phase4_elegance_followups_zh.md](phase1_phase4_elegance_followups_zh.md)

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

### 动态图标

- [dynamic_glyph_svg_system_plan_zh.md](dynamic_glyph_svg_system_plan_zh.md)
  - 新的动态图标总方案：SVG 做真源，映射层做语义真相源，按 Widget / HTML `<img>` / ButtonArt 兼容层三段落地。
- [ui_input_ownership_arbitration_plan_zh.md](ui_input_ownership_arbitration_plan_zh.md)
  - 结合项目现状与 IDA 反编译结果，对“键鼠/手柄抢输入”做统一的 UI 输入所有权仲裁方案。
- [gameplay_input_ownership_investigation_and_plan_zh.md](gameplay_input_ownership_investigation_and_plan_zh.md)
  - 结合当前注入链和 IDA 里 `BSWin32GamepadDevice::Poll / _root.SetPlatform` 的实际路径，说明为什么 UI owner 不能直接管 gameplay，并给出 gameplay owner 的落点与实施顺序。
- [gameplay_sustained_digital_and_cursor_handoff_plan_zh.md](gameplay_sustained_digital_and_cursor_handoff_plan_zh.md)
  - 单独收 `Sprint` 持续态数字动作 handoff 与 gameplay 光标/平台表现交接问题，明确它们为什么不该继续用 `DigitalOwner` 或阈值补丁硬修。
- [sprint_native_source_mediation_plan_zh.md](sprint_native_source_mediation_plan_zh.md)
  - 基于最新 `SprintProbe` 日志、当前 poll/backend 实现和游戏侧 `SprintHandler` 语义，说明为什么 `Sprint` 需要升级成 `SingleEmitterHold + native keyboard mediation`，而不是继续依赖 held OR 或 coarse owner。

### Mod 与清理

- [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
  - `ModEvent1-24` 的固定槽位 ABI、键池以及 `KeyboardHelperBackend` 口径。
- [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)
  - 当前代码仍值得继续观察或精简的点。
- [agents5_review_reconciliation_refactor_plan_zh.md](agents5_review_reconciliation_refactor_plan_zh.md)
  - 最新一轮 `agents5.md` 深度研究建议与当前主线的对齐分析，以及下一轮重构计划。
- [agents5_9403e73_customized_refactor_plan_zh.md](agents5_9403e73_customized_refactor_plan_zh.md)
  - 基于提交 `9403e73` 和新版 `agents5.md` 的定制化重构方案，按“立即做 / 先验证 / 暂不做”重新分层。
- [gameplay_ui_owner_code_ida_refactor_plan_zh.md](gameplay_ui_owner_code_ida_refactor_plan_zh.md)
  - 结合 `agents.md` 观点、当前代码与 IDA 关键路径，对 UI owner / gameplay owner / provenance-aware recovery 的下一阶段开发计划做收口。
- [phase2_gameplay_presentation_owner_minimal_plan_zh.md](phase2_gameplay_presentation_owner_minimal_plan_zh.md)
  - 针对 Phase 2 单独拆出的最小实施方案，重点收 `gameplay presentation` 真相源、menu-entry seed/latch、回滚点与测试矩阵。
- [phase1_phase4_code_review_findings_zh.md](phase1_phase4_code_review_findings_zh.md)
  - 对照 `gameplay_ui_owner_code_ida_refactor_plan_zh.md` 回看当前 Phase 1-4 实现后的代码审阅结论，重点记录 Phase 2/4 的结构偏差与后续处理顺序。
- [phase1_phase4_elegance_followups_zh.md](phase1_phase4_elegance_followups_zh.md)
  - 对当前已修正的 Phase 1-4 再做一轮“优雅性/可维护性”补充审阅，记录剩余结构债与推荐清理顺序。

## 维护规则

- 当前文档应优先描述“正在运行的正式主线”，不要继续把旧实验路线和现行方案混写。
- 新的阶段性研究如果只是临时草案，优先放到单独临时文件或外部记录，不要直接混入这份索引。
- 如果以后再次产生大量历史文档，建议单独建 `archive/`，避免根目录再堆积一批失效说明。
