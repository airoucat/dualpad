# Skyrim 混合输入与全局手柄模式查询静态证据

本文记录 2026-07-11 对 Skyrim SE 1.5.97 混合输入路径的补充 IDA 调查。重点是回答两个不同问题：

1. Skyrim 底层是否会在同一输入周期轮询键盘、鼠标和手柄？
2. `BSInputDeviceManager::IsGamepadEnabled()` 一类全局查询是否只影响菜单图标和平台表现？

结论是：**多设备轮询在底层成立，但全局手柄模式查询不只是 presentation 开关。** 它还参与共享二维输入变换。因此，正式方案不能简单把 engine mode 固定为手柄，也不能让任意 gameplay 通道活动都无条件翻转这项全局状态。

## 二进制身份

| 项目 | 值 |
| --- | --- |
| Runtime | Skyrim SE 1.5.97.0 |
| IDA 输入 | unpacked `SkyrimSE.exe`，34,586,624 bytes |
| SHA-256 | `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD` |
| MD5 | `0879EE0900C4A8B53E3D7B1B5CE298C0` |
| Image base | `0x140000000` |

SHA-256 已在本轮从磁盘重新计算。Poll call-site 的完整身份、calling convention 和动态证据见 [skyrim_xinput_poll_callsite.md](skyrim_xinput_poll_callsite.md)。

## 地址摘要

| RVA / VA | 静态身份或行为 | 证据状态 |
| --- | --- | --- |
| `0xC150B0 / 0x140C150B0` | `BSInputDeviceManager::PollInputDevices`，遍历 4 个 device slots | 已证明 |
| `0xC15240 / 0x140C15240` | 查询 `devices[kGamepad]->IsEnabled()` | 已证明 |
| `0xC1AB40 / 0x140C1AB40` | `BSWin32GamepadDevice::Poll` | 已证明 |
| `0xC1AB9D / 0x140C1AB9D` | `XInputGetState` call-site | 已证明 |
| `0x705AE0 / 0x140705AE0` | 根据全局 gamepad-enabled 结果选择不同二维输入变换 | 已证明行为，函数正式名称未恢复 |
| `0xECD970 / 0x140ECD970` | 菜单 `_root.SetPlatform` 发布 | 已证明 |
| `0xED2F90 / 0x140ED2F90` | 鼠标位置与手柄光标积分两条路径 | 已证明 |

## 证据一：同一输入周期会轮询四个设备槽

`0x140C150B0` 的反编译结构为：

```text
devices = this + 0x60
repeat 4 times:
    if devices[i] != null:
        devices[i]->Poll()

process control/input state
dispatch input event source
```

CommonLib 1.5.97 的 `BSInputDeviceManager` 布局定义：

```cpp
BSIInputDevice* devices[4];
```

对应枚举顺序为：

1. `Keyboard`
2. `Mouse`
3. `Gamepad`
4. `FlatVirtualKeyboard`

相关头文件：

- `lib/commonlibsse-ng/include/RE/B/BSInputDeviceManager.h`
- `lib/commonlibsse-ng/include/RE/I/InputDevices.h`

因此，低层设备采集不是“先选定一个当前平台，再只轮询那个设备”。这支持不同设备在同一 gameplay 周期提供不同通道输入的静态可行性。

## 证据二：手柄路径消费完整 XInput current-state

`0x140C1AB40` 在调用 `XInputGetState` 前把 current state 保存为 previous state；成功返回后：

- 遍历 14 个 button 位；
- 归一化 `LT / RT`；
- 归一化左右 stick；
- 将 stick activity 提交给后续 producer。

核心调用为：

```text
XInputGetState(userIndex, this + 0x100)
```

这与 DualPad 当前 `immutable PollOutputFrame -> XInputStateBridge -> Skyrim Poll` 的 current-state 输出模型一致。

## 证据三：`0x140C15240` 是 gamepad device enabled 查询

反编译结构为：

```text
device = manager->devices[2]
return device != null && device->vtable[7](device)
```

`devices[2]` 对应 `kGamepad`。CommonLib 的 `BSIInputDevice` 第 7 个虚函数是：

```cpp
virtual bool IsEnabled() const = 0;  // vtable + 0x38
```

因此，可以把 `0x140C15240` 识别为 `BSInputDeviceManager::IsGamepadEnabled()` 或语义等价查询，而不只是一个未分类的 presentation helper。

IDA 找到该函数的 **26 个直接代码引用点**。其中包括：

- 菜单 `SetPlatform` 路径；
- LevelUp、MessageBox、SleepWait、Stats、Training、remap 等 UI 路径；
- `0x140705AE0` 的共享二维输入变换路径；
- 其它尚未完整命名的 gameplay/UI 调用者。

这直接否定了“该查询只影响菜单图标，可以任意固定”的过度简化假设。

## 证据四：全局手柄模式会改变二维输入变换

`0x140705AE0` 接收一个二维浮点向量。它首先查询 `0x140C15240`，随后进入两套明显不同的处理：

### Gamepad-enabled 分支

- 对二维向量求长度并归一化；
- 应用多项式响应曲线；
- 应用 deadzone、灵敏度和非线性指数；
- 对 X/Y 分量应用独立缩放；
- 存在持续输入时间与 acceleration 相关状态。

### Keyboard/mouse 分支

- 使用另一组基于时间步和灵敏度的线性缩放；
- 不走同一套 stick deadzone/response curve；
- 在特定 camera/state 条件下应用额外倍率。

已确认的调用者包括：

- `0x140707110`：处理对象内 `a1 + 44` 的二维输入状态，然后继续 camera/angle 相关限制；
- `0x140888190`：从事件的 `a2 + 40 / a2 + 44` 读取二维整数输入，调用该变换后继续处理；
- `0x1408E1560`、`0x1408E1620`：菜单或 movie 相关输入路径。

函数正式 C++ 名称和所有 caller 的业务身份尚未恢复，因此这里不把它直接命名成某个具体 PlayerControls 方法。但其行为已经足以证明：**engine gamepad-enabled 状态会改变实际输入数值处理，不只是 glyph/presentation。**

## 证据五：菜单平台仍要求单一结论

`0x140ECD970` 会：

1. 查询 gamepad-enabled 状态；
2. 检查 movie 是否支持 `_root.SetPlatform`；
3. 调用一次 `_root.SetPlatform`；
4. 更新菜单内部平台状态位。

因此，菜单不能直接消费 `LookOwner / MoveOwner / CombatOwner / DigitalOwner` 四个 gameplay owner。它仍需要一份单一、稳定的 presentation 结论。

## 证据六：光标切换需要位置交接

`0x140ED2F90` 有两条位置来源：

- mouse 路径：`GetCursorPos -> GetForegroundWindow -> GetWindowInfo`；
- gamepad 路径：从内部 cursor position 按方向和 gamepad cursor speed 积分。

如果只翻转 cursor owner 而不同步内部坐标，容易出现真实鼠标位置与旧 gamepad cursor 缓存互相拉扯。

## 对正式方案的约束

### 已经可以采用的结论

- gameplay 允许按通道仲裁；底层多设备 Poll 不构成结构性阻碍。
- `current-state` 与 `meaningful activity` 必须分离；空闲 HID report 不能刷新 activity。
- menu presentation 必须折叠为单一 owner。
- prompt/glyph family 可以独立于 gameplay channel owner 建模。

### 不能直接采用的结论

- 不能把 Skyrim engine gamepad-enabled 状态当成纯 UI 字段。
- 不能在没有 caller 分类和实机 A/B 的情况下永久固定为 `true` 或 `false`。
- 不能让任意 KBM gameplay 通道活动都立刻翻转该全局状态；例如只按键盘移动时，是否应改变 look transform 仍需论证。
- 不能仅用“最近输入设备”替代 per-channel gate；同一通道双写仍需独立处理。

## 需要 GPT 在方案中比较的候选方向

下面是待评估方向，不是预先裁定的答案：

1. **Gameplay engine mode 跟随 `LookOwner`**
   - 理由：已证明的全局查询会改变二维 look/pointer 变换。
   - 风险：其它 25 个 caller 可能需要不同语义。
2. **Gameplay engine mode 跟随 last meaningful primary source**
   - 理由：与 Skyrim 原生单一 mode 更接近。
   - 风险：不同通道同时操作时可能抖动，并重新污染 per-channel ownership。
3. **对关键输入变换做 source-aware 分流，不再依赖全局 mode**
   - 理由：语义最精确。
   - 风险：hook 面、版本耦合、calling convention 和维护成本显著上升。
4. **保留 Skyrim 原生 engine mode，只让 DualPad 负责 virtual XInput gate 与 prompt family**
   - 理由：侵入较小。
   - 风险：需要证明原生 mode 在鼠标/手柄混用时不会错误选择变换曲线。

正式计划必须比较这些方向，给出选择依据、证据缺口和回退边界，不能只写“把 engine presentation 与 glyph 分离”。

## 仍需追加的 IDA / 动态证据

1. 恢复 `0x140705AE0` 的正式类/方法身份和输入语义。
2. 将 `0x140C15240` 的 26 个 direct caller 分类为：gameplay、camera、menu、cursor、remap、其它。
3. 在 mouse-look、right-stick look、WASD、menu cursor 四个场景对 `0x140C15240` 和 `0x140705AE0` 下断点，记录 caller、输入事件 device、返回值和变换前后向量。
4. A/B 比较 engine mode 为 `true / false` 时，同一 mouse delta 和同一 right-stick state 的实际 camera 输出。
5. 确认是否存在比全局 `IsGamepadEnabled()` 更接近“当前事件来源”的游戏内部字段或 handler-local branch。
6. 检查 `GamepadControlsCursor`、remap enable 和 menu `SetPlatform` 是否可以继续共享一份状态，还是必须拆为不同 compatibility surface。

在这些证据闭合前，可以制定分阶段实现计划，但不能把 engine mode 的最终策略写成已证明事实。
