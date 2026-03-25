# 当前代码清理与风险复查

这份文档只记录当前主线里仍值得继续观察、精简或谨慎维护的点，不再重复已经完成的重构阶段。

适用前提：

- 当前正式架构见 [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
- 当前 backend ownership 见 [backend_routing_decisions.md](backend_routing_decisions.md)

## 已完成的大项

下面这些已经不再属于“待清理主问题”：

1. `NativeButtonCommitBackend` 的旧 lifecycle bridge 已移除。
2. `ActionDispatcher` 的 keyboard helper 路径已压平。
3. native 元数据已集中到 `NativeActionDescriptor` 主表。
4. `PadEventSnapshotProcessor` 已拆出：
   - `AxisProjection`
   - `UnmanagedDigitalPublisher`
5. `AuthoritativePollState` 已移除 XInput transport 派生字段。
6. `KeyboardHelperBackend` 的 helper-key 解析已改成表驱动。
7. `ActionLifecycleBackend` 薄接口已移除。
8. Poll 主线程 drain 已改为 bounded drain，并在 backlog 场景做 latest-state coalescing。
9. sequence gap 已改为 native-domain resync，不再默认连坐 keyboard helper 域。
10. `XInputStateBridge` 已回到纯序列化角色，commit 时机已上提到 upstream poll hook。
11. `PollCommitCoordinator` 已移除 `ToggleFlip / toggledOn / suppressedPulseCount` 这类无行为意义的残留字段。

## 当前仍值得观察的点

### 1. `BindingResolver` 的 subset fallback 仍需继续观察

相关文件：

- `src/input/mapping/BindingResolver.cpp`

现状：

- 当前仍保留 `kAllowFallbackWithoutModifiers = true`
- 这有利于兼容一部分自由组合绑定，但组合键面继续扩大后，仍可能带来“过宽回退命中”风险
- 本轮已经补上 subset fallback 诊断：
  - `BindingManager::FindBestBindingForTriggerSubset()` 会统计同等级候选数
  - `BindingResolver` 在开启 `log_mapping_events` 时会记录精确命中 / subset fallback 命中，以及歧义候选摘要

判断：

- 当前没有直接 correctness 故障证据，不建议为了收紧而贸然改行为。
- 这仍是后续最值得继续跟踪的输入匹配点。

### 2. `ControlMapOverlay` 属于高影响模块

相关文件：

- `src/input/ControlMapOverlay.cpp`

现状：

- 当前已改成走游戏原生 parser / rebuild。
- 功能上已经稳定。
- 本轮已补运行时护栏：
  - 仅在 SSE 1.5.97 上启用
  - overlay 文件缺失时明确跳过
  - 主入口会在 overlay 未生效时打印总览 warning

判断：

- 当前不建议再主动重构。
- 但应继续保留清晰的阶段日志与失败保护，因为这层一旦出问题，影响的是整个输入装载期。

### 3. combo-native 仍只验证到部分动作

相关文档：

- [controlmap_combo_profile_zh.md](controlmap_combo_profile_zh.md)

现状：

- `Pause / NativeScreenshot` 已实机验证。
- `Hotkey3-8` 仍属于已接入、待补测状态。
- 本轮已加 runtime gate：
  - `enable_combo_native_hotkeys3_to_8=false` 时，route 与 `ControlMapOverlay` 会一起禁用 `Hotkey3-8`
  - 这样即使 overlay 已加载，未补测阶段也不会意外进入正式运行面

判断：

- 这是验证缺口，不是当前架构缺口。

### 4. 映射层严格先后按键仍缺少单独实机验证

相关文件：

- `src/input/mapping/BindingResolver.cpp`
- `src/input/BindingManager.cpp`
- `src/input/BindingConfig.cpp`

现状：

- 当前 `BindingResolver` 仍保留 subset fallback。
- 但到目前为止，项目主要验证的是：
  - 单键映射
  - native current-state 输出
  - controlmap combo-native 输出
- 映射层自己的“严格先后按 modifier 再按主键”这条语义，还没有单独做过成体系实测。
- 当前用户侧推荐写法已切到 `Layer:*`。
- `Combo:*` 已经重新定义成“近同时双键”，并且只对实际参与 `Combo:*` 的键引入局部 `22ms` 等待窗口。
- `Button:mods+key` 已从配置层移除，不再作为组合键语法。

判断：

- 这属于验证缺口，不是当前已知 correctness 故障。
- 在后续继续扩张自由组合绑定前，建议先分别补一轮 `Layer:*` 与 `Combo:*` 的专项回归。

## 当前不建议重新投入的旧方向

下面这些不再建议继续作为活跃主线或主文档方向：

- 旧 `keyboard-native` 主线
- 旧 native button splice / queue surgery
- compatibility fallback 作为平行数字主线
- 已完成的阶段计划文档
- 已被当前主线吸收的 review 批次汇总

## 如果还要继续做小规模清理

建议顺序：

1. 继续跟踪 `BindingResolver` 的 subset fallback，等组合键面再扩张时决定是否收紧。
2. 先补一轮映射层 `Layer:*` 绑定专项验证，再决定是否调整 subset fallback。
3. 继续观察 `ControlMapOverlay`，但短期内不再主动重构其核心实现。
4. 等 `Hotkey3-8` 补测完成后，再决定是否进一步扩张 combo-native 正式支持面。
