# 文档索引

这份索引只负责路由与说明。可枚举事实由 `DualPadDocGen` 生成到 `docs/generated/`，reviewed docs 不再复制 generated facts。

## 当前工作状态

- 当前无活跃 Sprint；最近完成的 `S-DP5-RC20-HOTFIX` 是 post-closeout field-readiness hardening，当前发布状态为 `GO WITH NATIVE FAVORITES DISABLED`。
- `PH8b` 已完成；`PH0` - `PH8b` closeout 已收口。
- `DP5-RC20` U0-U5 已完成；后续为 RC evidence / field-readiness fixes 与实机 QA，不是新 runtime phase。
- `PR-A`、`PR-B1`、`PR-B2`、`PR-B3` 已合入；`PR-C` 只修正文档合同卫生、死链和状态滞后。
- 最终 `main` head 同时通过远端 `phase8` 与 `rc-readiness` 后，才可称为 `RC QA baseline`。
- runtime closeout：`PH8a` 已完成，`PH8b` 不重开 runtime 主线归属。
- 不新增后续 runtime phase。

## 工作流入口

1. [authoritative-baseline/README.md](authoritative-baseline/README.md)
2. [authoritative-baseline/work-packages/README.md](authoritative-baseline/work-packages/README.md)
3. [harness/dualpad-builder.md](harness/dualpad-builder.md)
4. [../.dualpad-builder/spec.md](../.dualpad-builder/spec.md)
5. [../.dualpad-builder/feature_list.json](../.dualpad-builder/feature_list.json)
6. [../.dualpad-builder/sprint_plan.json](../.dualpad-builder/sprint_plan.json)
7. [../.dualpad-builder/progress.md](../.dualpad-builder/progress.md)
8. [../AGENTS.md](../AGENTS.md)

## Generated Facts

- [generated/context_catalog_zh.md](generated/context_catalog_zh.md)
- [generated/action_sets_zh.md](generated/action_sets_zh.md)
- [generated/prompt_matrix_zh.md](generated/prompt_matrix_zh.md)
- [generated/policies_zh.md](generated/policies_zh.md)

这些文件由 `xmake run DualPadDocGen` 生成。文件头必须包含 source config root、manifest hash、trace schema version、generator version / command。

## Reviewed Narrative

- [../README.md](../README.md)
- [../src/ARCHITECTURE.md](../src/ARCHITECTURE.md)
- [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
- [runtime_concurrency_contract.md](runtime_concurrency_contract.md)
- [runtime_backpressure_contract.md](runtime_backpressure_contract.md)
- [testing/rc20_runtime_validation.md](testing/rc20_runtime_validation.md)
- [research/skyrim_xinput_poll_callsite.md](research/skyrim_xinput_poll_callsite.md)
- [research/skyrim_mixed_input_mode_queries_zh.md](research/skyrim_mixed_input_mode_queries_zh.md)（多设备 Poll、engine gamepad-enabled 查询和二维输入变换的 IDA 证据）
- [reviews/2026-07-11-mixed-input-solution-plan-request_zh.md](reviews/2026-07-11-mixed-input-solution-plan-request_zh.md)（要求外部 GPT 制定可执行正式方案的主提示词）
- [reviews/2026-07-11-mixed-input-feasibility-gpt-review-brief_zh.md](reviews/2026-07-11-mixed-input-feasibility-gpt-review-brief_zh.md)（专项可行性与外部审查材料，不替代 current truth）
- [menu_context_policy_current_status_zh.md](menu_context_policy_current_status_zh.md)
- [authoritative-baseline/README.md](authoritative-baseline/README.md)
- [harness/dualpad-builder.md](harness/dualpad-builder.md)

这些文档只解释“为什么、怎么读、当前推进到哪”，不再维护 context/action/prompt/policy 表格副本。

## Phase 8 Close-Out

- [plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md](plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md)
- [plans/dualpad_rearchitecture/09a_slice_phase8_runtime_closeout_zh.md](plans/dualpad_rearchitecture/09a_slice_phase8_runtime_closeout_zh.md)
- [plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md](plans/dualpad_rearchitecture/09b_slice_phase8_governance_closeout_zh.md)

## 当前事实与方案文档

- [authoritative-baseline/dp5_rc20_contract_zh.md](authoritative-baseline/dp5_rc20_contract_zh.md)
- [releases/dp5_rc20_u3_release_notes_zh.md](releases/dp5_rc20_u3_release_notes_zh.md)
- [releases/dp5_rc20_u4_config_prompt_menu_glyph_contract_zh.md](releases/dp5_rc20_u4_config_prompt_menu_glyph_contract_zh.md)
- [releases/dp5_rc20_u5_rc_readiness_closeout_zh.md](releases/dp5_rc20_u5_rc_readiness_closeout_zh.md)
- [backend_routing_decisions.md](backend_routing_decisions.md)
- [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
- [main_menu_glyph_current_status_zh.md](main_menu_glyph_current_status_zh.md)
- [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
- [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)
- [reviews/README_zh.md](reviews/README_zh.md)

历史资料保留给考古、复盘或重新提炼结论时使用，不作为默认首读主线。
