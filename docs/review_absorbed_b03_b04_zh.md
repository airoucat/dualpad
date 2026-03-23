# B03/B04 已吸收审核项清单

这份清单用于记录第三方 Claude 审核后，当前仓库已经吸收、已记录待后续处理、或明确暂不修改的 `B03 / B04` 项目。

状态约定：

- `已吸收`：已经落实到代码/配置/注释
- `已记录待后续`：当前只记录风险或后续处理点，未改主线行为
- `已确认暂不修改`：当前判断为低风险、需等后续批次，或不值得在本轮改

---

## B03 映射层原子事件生成

### F1 `PadEvent.code` 在 AxisChange 与 Button 事件之间值域重叠

- 状态：`已吸收`
- 处理方式：
  - 在 [PadEvent.h](/c:/Users/xuany/Documents/dualPad/src/input/mapping/PadEvent.h) 中把 `PadAxisId` 的底层值移出了 synthetic 按钮位域
  - 目的是避免 `IsSyntheticPadBitCode()`、processor、reducer、dispatcher 这些跨层 helper 把 axis event 误判成按钮位

### F2 同帧 `ButtonPress + Combo` 双发

- 状态：`已记录待后续`
- 当前判断：
  - 这是一个真实的跨层契约风险，不是纯理论问题
  - 但它是否会在运行时真正造成错误，取决于 `B06/B07` 中：
    - `ActionBackendPolicy::Decide(...).ownsLifecycle`
    - `PadEventSnapshotProcessor::CollectPlannedActions()` 的 lifecycle / planner / source-blocking 路径
- 当前不在 `B03/B04` 直接修的原因：
  - 粗暴在 generator 层抑制 `ButtonPress` 会改掉“先发 raw edge，再发 interpreted combo”的现有契约
  - 更合适的修法属于 `B06/B07` 主流程边界

### F3 同帧 `ButtonRelease + Tap` 双发

- 状态：`已确认暂不修改`
- 原因：
  - 当前 binding 系统不对 `ButtonRelease` 建立解析路径
  - 风险主要是概念层双发，不是当前实际运行 bug

### F4 `PadEventBuffer` overflow 时存在不可恢复的丢边沿风险

- 状态：`已确认暂不修改`
- 原因：
  - 风险存在，但需要 `B05/B07` 结合 overflow 恢复路径一起判断
  - 当前 `PadEventSnapshot` 已同时传递完整 `PadState` 与 `overflowed` 标志

### F5 同帧两键同时按下不产生 Combo

- 状态：`已确认暂不修改`
- 原因：
  - 当前审核已将其定性为合理约定
  - 真正支持“同帧双键同按 combo”会引入 modifier / main key 歧义

---

## B04 Touchpad / Trigger Mapping / Binding Resolve

### F1 同帧 `ButtonPress + Combo` 双触发，对非 lifecycle 动作无保护

- 状态：`已记录待后续`
- 当前判断：
  - 这是 `B03 F2` 在 `B04/B06/B07` 层的具体化
  - 现阶段先保留为后续批次要解决的主线契约问题
- 计划归属：
  - 在 `B06/B07` 审核和修正时统一处理

### F2 `LeftRightBoundary=0.50` 使 `LeftCenterRight` 模式中区宽度退化为零

- 状态：`已吸收`
- 处理方式：
  - 将两份绑定文件中的默认值从 `0.50` 调整为 `0.33`
  - 同时补了注释，明确：
    - 允许范围是 `[0.0, 0.5]`
    - `0.50` 会让中区实际退化为零宽
- 文件：
  - [DualPadBindings.ini](/c:/Users/xuany/Documents/dualPad/config/DualPadBindings.ini)
  - [DualPadBindings.ini](/G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadBindings.ini)

### F3 `ClampHalf` 的范围限制未在 ini 中说明

- 状态：`已吸收`
- 处理方式：
  - 在两份绑定文件的 `LeftRightBoundary` 注释中补充范围说明 `[0.0, 0.5]`

---

## 当前建议

- 可以继续推进 `B05`
- `B03/B04` 当前最重要的未解决点已经收敛成一个明确主题：
  - `ButtonPress + Combo` 在 `B06/B07` 中的跨事件保护与去重契约

