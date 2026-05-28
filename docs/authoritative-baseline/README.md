# DualPad Authoritative Baseline 入口

从 `2026-04-18` 起，`docs/authoritative-baseline/` 是 `DualPad` 的 workflow current truth 入口。

这里负责收口：

- 默认阅读顺序
- 当前工作包入口
- builder memory 与 sprint 路由
- graphify close-out 规则

它不替代 `README.md`、`src/ARCHITECTURE.md`、`docs/DOC_INDEX_zh.md` 或 generated facts。

## 当前工作路（2026-05-28）

- 当前活跃 Sprint：无
- 当前直接焦点：`PH8b Phase 8B governance closeout` 已完成核对
- 已完成阶段：`PH0` - `PH8b`
- 当前边界：
  - `PH8a` 已完成 runtime closeout；`PH8b` 不负责 runtime 主线裁决。
  - generated facts 只能放在 `docs/generated/`。
  - reviewed docs 只保留 narrative。
  - 默认 CI 必须直接引用同名 canonical targets。
  - replay root 固定为 `tests/replay/golden/`。
  - 不新增后续 runtime phase。

## 默认阅读顺序

1. `docs/authoritative-baseline/2026-04-18-dualpad-authoritative-baseline.md`
2. `docs/authoritative-baseline/work-packages/README.md`
3. `docs/harness/dualpad-builder.md`
4. `.dualpad-builder/spec.md`
5. `.dualpad-builder/feature_list.json`
6. `.dualpad-builder/sprint_plan.json`
7. `.dualpad-builder/progress.md`
8. `README.md`
9. `src/ARCHITECTURE.md`
10. `docs/DOC_INDEX_zh.md`
11. `docs/generated/*.md`
12. 再按任务类型跳到对应当前事实文档

## Generated Facts

- `docs/generated/context_catalog_zh.md`
- `docs/generated/action_sets_zh.md`
- `docs/generated/prompt_matrix_zh.md`
- `docs/generated/policies_zh.md`

上述文件由 `xmake run DualPadDocGen` 生成。reviewed docs 不复制这些表格。

## Builder Memory

repo 内默认记忆层固定为：

- `.dualpad-builder/spec.md`
- `.dualpad-builder/feature_list.json`
- `.dualpad-builder/sprint_plan.json`
- `.dualpad-builder/progress.md`

Sprint、工作包、验证状态变化必须同步写回这些文件。

## Graphify

- 初始化：`python3 scripts/dev/setup_graphify_local.py`
- 重建：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- 查询：`python3 -m graphify query "show the main flow"`

Graphify 只负责上下文加速，不替代本目录、`README.md`、`src/ARCHITECTURE.md`、`.dualpad-builder/` 或 `docs/generated/`。
