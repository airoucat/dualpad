# 文档索引

这份索引把文档拆成三类：

- **当前事实文档**：直接描述仓库里已经存在并正在运行的代码
- **进行中设计文档**：仍有价值，但属于方案、调查或后续计划
- **历史资料**：保留给考古、复盘和验证，不再当成首读主线

## 推荐首读

1. [../README.md](../README.md)
2. [../src/ARCHITECTURE.md](../src/ARCHITECTURE.md)
3. [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
4. [menu_context_policy_current_status_zh.md](menu_context_policy_current_status_zh.md)
5. [backend_routing_decisions.md](backend_routing_decisions.md)
6. [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
7. [main_menu_glyph_current_status_zh.md](main_menu_glyph_current_status_zh.md)
8. [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
9. [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)

## 当前事实文档

### 运行时主链

- [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
  - 当前输入主链，以及菜单上下文 / 表现层侧支放在哪。
- [mapping_snapshot_atomicity_audit_and_injection_contract_zh.md](mapping_snapshot_atomicity_audit_and_injection_contract_zh.md)
  - 映射层快照、主线程交付边界和注入层当前契约。
- [backend_routing_decisions.md](backend_routing_decisions.md)
  - 当前 backend ownership 与正式支持面。
- [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
  - planner、lifecycle 和 backend 的职责边界。

### 菜单上下文与表现层

- [menu_context_policy_current_status_zh.md](menu_context_policy_current_status_zh.md)
  - `MenuContextPolicy + InputContextNames + DualPadMenuPolicy.ini` 的当前实现状态。
- [main_menu_glyph_current_status_zh.md](main_menu_glyph_current_status_zh.md)
  - 当前主菜单动态图标的真实落地状态和仓库边界。

### 原生语义与 controlmap

- [native_pc_event_semantics_zh.md](native_pc_event_semantics_zh.md)
  - 当前仍依赖的原生 producer / handler 语义。
- [controlmap_gamepad_event_inventory_zh.md](controlmap_gamepad_event_inventory_zh.md)
  - 按 `controlmap` 上下文整理的 gamepad 原生事件母表。
- [controlmap_combo_profile_zh.md](controlmap_combo_profile_zh.md)
  - DualPad 当前使用的 combo-native overlay profile。

### Mod 与维护

- [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
  - `ModEvent1-24` 的 ABI、虚拟键池和 helper backend 约定。
- [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)
  - 当前主线的剩余风险和后续观察点。

## 进行中设计与专项方案

- [dynamic_glyph_svg_system_plan_zh.md](dynamic_glyph_svg_system_plan_zh.md)
  - 动态图标长期统一方案；不是当前仓库已经落地的实现。
- [ui_input_ownership_arbitration_plan_zh.md](ui_input_ownership_arbitration_plan_zh.md)
  - UI 输入所有权仲裁方案。
- [gameplay_input_ownership_investigation_and_plan_zh.md](gameplay_input_ownership_investigation_and_plan_zh.md)
  - gameplay 输入所有权的分层调查与实施顺序。
- [gameplay_sustained_digital_and_cursor_handoff_plan_zh.md](gameplay_sustained_digital_and_cursor_handoff_plan_zh.md)
  - `Sprint` 和 gameplay cursor/platform handoff 的专项方案。
- [sprint_native_source_mediation_plan_zh.md](sprint_native_source_mediation_plan_zh.md)
  - `Sprint` 的 `SingleEmitterHold + native keyboard mediation` 方案。

## 验证、计划与专项审查

- [plans/menu_context_runtime_policy_plan_zh.md](plans/menu_context_runtime_policy_plan_zh.md)
  - 菜单策略功能的实施计划记录；当前实现请优先看 `menu_context_policy_current_status_zh.md`。
- [verification/menu_context_runtime_policy_matrix_zh.md](verification/menu_context_runtime_policy_matrix_zh.md)
  - 菜单策略的验证矩阵。
- [reviews/README_zh.md](reviews/README_zh.md)
  - 审查资料目录入口。
- [reviews/2026-04-09-device-capture-protocol-review_zh.md](reviews/2026-04-09-device-capture-protocol-review_zh.md)
- [reviews/2026-04-09-state-shaping-event-generation-review_zh.md](reviews/2026-04-09-state-shaping-event-generation-review_zh.md)
- [reviews/2026-04-10-binding-action-semantics-review_zh.md](reviews/2026-04-10-binding-action-semantics-review_zh.md)
- [reviews/2026-04-10-owner-arbitration-gameplay-injection-review_zh.md](reviews/2026-04-10-owner-arbitration-gameplay-injection-review_zh.md)

## 历史资料

这些文档保留给复盘、考古或重新提炼结论时使用，默认不作为当前主线说明：

- `agents5_*`
- `gameplay_ui_owner_code_ida_refactor_plan_zh.md`
- `phase*`
- `brainstorms/`
- `ideation/`
- `research/`
- 其它未进入上面两类的临时笔记或阶段计划

## 维护规则

- 当前事实文档必须优先描述“仓库里现在真实存在什么”，不要默认继承旧工作区或上一轮 handoff。
- 设计文档要明确写清“已实现”与“计划中”的边界，避免把方案文档写成现状说明。
- 历史资料可以保留，但不应继续混入推荐首读顺序。
