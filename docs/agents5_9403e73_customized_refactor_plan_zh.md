# 基于提交 9403e73 的定制化后续方案

更新日期：2026-03-24

这份文档用于把新版 [agents5.md](/C:/Users/xuany/Documents/dualPad/agents5.md) 的审查意见，按当前项目实际重新裁剪成可执行方案。

适用前提：

- 当前主线见 [current_input_pipeline_zh.md](/C:/Users/xuany/Documents/dualPad/docs/current_input_pipeline_zh.md)
- 当前 backend ownership 见 [backend_routing_decisions.md](/C:/Users/xuany/Documents/dualPad/docs/backend_routing_decisions.md)
- 当前清理与风险复查见 [current_cleanup_risk_review_zh.md](/C:/Users/xuany/Documents/dualPad/docs/current_cleanup_risk_review_zh.md)

## 当前结论

当前正式主线已经完成这些关键收口：

1. `AuthoritativePollState` 明确为“虚拟 XInput 手柄硬件状态”。
2. `XInputStateBridge` 已回到纯序列化角色。
3. native 元数据已统一到 `NativeActionDescriptor` 主表。
4. `PadEventSnapshotProcessor` 已拆出 `AxisProjection` 与 `UnmanagedDigitalPublisher`。
5. snapshot 主线程消费已改为 bounded drain，并支持 latest-state coalescing。
6. sequence gap 已改为 native-domain resync。
7. `ActionLifecycleBackend` 已移除。
8. `PollCommitCoordinator` 的无行为意义残留字段已清理。

因此，后续重点不再是“重做主线”，而是：

- 低风险护栏
- 组合键/配置层口径收口
- 补验证，再决定是否继续收紧行为

## 已落实项

### 1. `ControlMapOverlay` 护栏

已完成：

- 仅在 SSE 1.5.97 上启用 runtime overlay
- overlay 文件缺失时明确跳过
- 主入口在 overlay 未生效时给出总览 warning

### 2. `Hotkey3-8` runtime gate

已完成：

- 新增 `enable_combo_native_hotkeys3_to_8`
- 默认关闭
- gate 关闭时同时禁用 route 与 runtime `ControlMap` overlay 映射

### 3. subset fallback 诊断

已完成：

- `BindingManager::FindBestBindingForTriggerSubset()` 会统计同等级候选
- `BindingResolver` 在开启 `log_mapping_events` 时会记录：
  - exact resolve
  - subset fallback resolve
  - 歧义候选摘要

### 4. 配置层共用解析辅助

已完成：

- `BindingConfig` 与 `RuntimeConfig` 共享 `IniParseHelpers`

### 5. `Button:A+B` 语法移除

已完成：

- `Button:*` 现在只接受纯单键
- `Button:mods+key` 不再属于正式配置语法
- 当前严格先后按语义改为推荐使用 `Layer:*`
- `Combo:*` 暂时保留为 `Layer:*` 的兼容别名，不再推荐继续使用

## 当前剩余项

### 1. 映射层 `Layer:*` 专项验证

当前仍缺：

- `Layer:*` exact 命中专项回归
- `Layer:*` subset fallback 专项回归
- 多 modifier 同时按住时的命中优先级验证

这是验证缺口，不是当前已知 correctness 故障。

### 2. `Hotkey3-8` 补测

当前状态：

- route 与 overlay ABI 已接好
- 默认 gate 关闭

后续应在补测完成后，再决定是否默认开启。

### 3. 近同时双键语义尚未正式定义

当前状态：

- `Layer:*` 已明确代表“先按住 modifier，再按主键”的严格顺序语义
- `Button:A+B` 已移除
- `Combo:*` 暂时只是 `Layer:*` 的兼容别名

后续如果要正式支持“近同时双键触发第三动作”，建议：

1. 单独引入新的 chord/combo 评估器
2. 给组合参与键一个很短的时间窗
3. 对基础单键做短暂缓冲，避免组合和单键互相抢触发
4. exact match only，不走 subset fallback

## 明确不建议推进的方向

1. 不重新开启 `keyboard-native` 主线
2. 不重新引回旧 native button splice / queue surgery
3. 不重新扩张已撤出的 `OpenInventory / OpenMagic / OpenMap / OpenSkills`
4. 不把 `ControlMapOverlay` 从“原生 parser / rebuild”改回手工数组重建
5. 不在插件侧重写 Skyrim 原生 handler 家族语义

## 后续推荐顺序

1. 先补一轮映射层 `Layer:*` 专项验证
2. 再补 `Hotkey3-8`
3. 最后再根据验证结果，决定是否引入正式的“近同时双键”语义

理由：

- 当前低风险代码性收口已经完成
- 剩下主要是验证闭环，而不是继续堆代码
- 只有验证证明现状存在真实缺口时，才值得进入下一轮行为调整
