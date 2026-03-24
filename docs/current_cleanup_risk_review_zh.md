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

## 当前仍值得观察的点

### 1. `ActionLifecycleBackend` 只剩 keyboard helper 在用

相关文件：

- `src/input/backend/ActionLifecycleBackend.h`
- `src/input/backend/KeyboardHelperBackend.h`

现状：

- `NativeButtonCommitBackend` 已不再实现这层接口。
- 当前只剩 `KeyboardHelperBackend` 还继承 `IActionLifecycleBackend`。

判断：

- 这不是 bug。
- 但它已经更像只为 helper 线路保留的一层薄接口，后续可以继续评估是否内联收口。

### 2. `NativeControlCode::ToString()` 仍有少量元数据重复

相关文件：

- `src/input/backend/NativeControlCode.h`
- `src/input/backend/NativeActionDescriptor.*`

现状：

- native action 主表已经统一。
- 但字符串化仍在 `NativeControlCode` 侧单独维护。

判断：

- 当前没有行为风险。
- 这仍是下一轮最适合继续去重的地方之一。

### 3. `ControlMapOverlay` 属于高影响模块

相关文件：

- `src/input/ControlMapOverlay.cpp`

现状：

- 当前已改成走游戏原生 parser / rebuild。
- 功能上已经稳定。

判断：

- 当前不建议再主动重构。
- 但应继续保留清晰的阶段日志与失败保护，因为这层一旦出问题，影响的是整个输入装载期。

### 4. combo-native 仍只验证到部分动作

相关文档：

- [controlmap_combo_profile_zh.md](controlmap_combo_profile_zh.md)

现状：

- `Pause / NativeScreenshot` 已实机验证。
- `Hotkey3-8` 仍属于已接入、待补测状态。

判断：

- 这是验证缺口，不是当前架构缺口。

## 当前不建议重新投入的旧方向

下面这些不再建议继续作为活跃主线或主文档方向：

- 旧 `keyboard-native` 主线
- 旧 native button splice / queue surgery
- compatibility fallback 作为平行数字主线
- 已完成的阶段计划文档
- 已被当前主线吸收的 review 批次汇总

## 如果还要继续做小规模清理

建议顺序：

1. 评估 `ActionLifecycleBackend` 是否继续保留为独立接口。
2. 继续减少 `NativeControlCode` 与 descriptor 主表之间的重复元数据。
3. 在不动行为的前提下，仅继续加强 `ControlMapOverlay` 的日志与诊断。
4. 等 `Hotkey3-8` 补测完成后，再决定是否进一步扩张 combo-native 正式支持面。