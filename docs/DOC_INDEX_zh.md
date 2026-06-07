# 文档索引

这份索引只负责路由与说明。可枚举事实由 `DualPadDocGen` 生成到 `docs/generated/`，reviewed docs 不再复制 generated facts。

## 当前活跃 Sprint

- 无
- `PH8b` 已完成；`PH0` - `PH8b` closeout 已收口。
- `DP5-RC20` 已完成 U0 contract preflight；后续 U1-U5 只作为 post-closeout hardening / RC readiness 推进。
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
- [backend_routing_decisions.md](backend_routing_decisions.md)
- [unified_action_lifecycle_model_zh.md](unified_action_lifecycle_model_zh.md)
- [main_menu_glyph_current_status_zh.md](main_menu_glyph_current_status_zh.md)
- [mod_event_keyboard_helper_backend_zh.md](mod_event_keyboard_helper_backend_zh.md)
- [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)
- [reviews/README_zh.md](reviews/README_zh.md)

历史资料保留给考古、复盘或重新提炼结论时使用，不作为默认首读主线。
