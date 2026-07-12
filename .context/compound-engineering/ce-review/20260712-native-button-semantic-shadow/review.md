# Native ButtonEvent 语义 shadow 审查

## 结论

无阻塞或可操作缺陷。改动只读取首个已观测 KBM event ordinal 对应的 native `ButtonEvent`，并复用现有低噪声日志；不修改 event list、engine query、current-cycle ledger 或 capability gate。

## 覆盖

- Correctness：ordinal 与 `SkyrimKbmInputAdapter` 的 1-based 遍历一致；指针仅在 callback 生命周期内用于同步日志。
- Performance：只在存在 observed KBM event 时扫描到首个目标；使用 `const char*` 避免每事件字符串分配。
- Testing：wiring contract 完成 RED/GREEN；focused、相邻输入/投影 target 与 canonical Phase 8 均通过。
- Governance：IDA 静态证据将 `0x140C15240` 与 `0x140C15280` 分为独立 target，16 个 native gameplay callers 继续 classification pending，production patch 保持关闭。

## 剩余风险

只有 matching Skyrim 1.5.97 实机样本能判定 native `userEvent/value/heldDuration` 是否有效；本切片不宣称 gameplay KBM 已修复。
