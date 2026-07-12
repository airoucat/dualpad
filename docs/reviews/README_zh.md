# 审查记录目录

这个目录专门收：

- 定向 code review 记录
- 某一层级的风险审查
- 方便后续集中整改的专项问题清单

这里的文档默认不是“当前正式主线说明书”，而是：

- 帮后续窗口快速理解某一块已经看过什么
- 把分散在聊天记录里的审查结论沉淀下来
- 为后续集中修整提供稳定入口

## 建议写法

每份审查文档尽量包含：

1. 审查范围
2. 前提假设
3. 结论摘要
4. 具体问题
5. 建议调整顺序

## 命名约定

建议继续使用当前仓库已有风格：

- 日期前缀
- ASCII slug
- 中文正文
- 需要时带 `_zh`

示例：

- `2026-04-09-device-capture-protocol-review_zh.md`

## 近期审查材料

- [2026-07-11-mixed-input-solution-plan-request_zh.md](2026-07-11-mixed-input-solution-plan-request_zh.md)：要求 GPT 基于源码和 Skyrim 证据输出精确到类型、线程、文件、TDD、IDA 门禁和实机验收的正式方案。
- [2026-07-11-mixed-input-feasibility-gpt-review-brief_zh.md](2026-07-11-mixed-input-feasibility-gpt-review-brief_zh.md)：键鼠与手柄混合操作的现有设计、当前实现断点、IDA 证据和外部 GPT 审查提示词。

外部方案经过当前 checkout 与 IDA 事实复核后的仓库修订版，位于 [../plans/2026-07-12-dualpad-mixed-input-formal-implementation-plan_zh.md](../plans/2026-07-12-dualpad-mixed-input-formal-implementation-plan_zh.md)。该文档是待执行计划，不代表已有活跃 Sprint 或 mixed-input 已解除 `NO-GO`。
