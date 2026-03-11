# Keyboard Native 共存修复总结

本文只记录已经验证成立的结论，不再回顾已经证伪的旧猜测。

## 目标

在 Skyrim SE 1.5.97 中实现下面这组共存能力：

- 手柄模拟量继续走正式可用的 `Route B`
- 键盘型数字动作走真正的 PC keyboard 链
- 两者同时存在
- 不再依赖全局平台切换或 consumer-side `InputEventQueue` 拼接

## 最终结论

最终有效路径分成两段：

1. `DualPad.dll` 通过共享桥把 keyboard-native 命令发给 `dinput8.dll` proxy
2. `dinput8.dll` proxy 在 keyboard `GetDeviceData` 返回边界追加 synthetic `DIDEVICEOBJECTDATA`

仅做到这一步还不够。

当游戏设置里“启用手柄”打开时，游戏内还有一道更早的 keyboard/gamepad 共存 gate，会让 `raw Space(0x39)` 在 `sub_140C11600` 中无法升级成 `Jump`。

最终有效修复是：

- 保留 `dinput8` 边界 keyboard-native emit
- 在 `sub_140C11600` 调用期间，对 gate object 的 `+0x121` 做 scoped override：
  - 进入前：`1 -> 0`
  - 返回后：恢复原值

这样可以恢复：

- 真空格在“启用手柄”时的 `Space -> Jump`
- bridge 注入的 keyboard-native `Jump`
- 同时不影响手柄模拟量

## 已验证链路

### 1. `dinput8` 边界足够早

`skse64_1_5_97.dll` 内部 worker 不是最终注入边界。  
进一步前移到 `dinput8` keyboard `GetDeviceData` 返回边界后，游戏已经能把注入的 `0x39` 当成真实 keyboard 路径处理。

这一步证明：

- 问题不在 `DIDEVICEOBJECTDATA` 是否像 native
- 问题在更后面的游戏内 keyboard/gamepad 共存条件

### 2. `GetDeviceState` 同步不是根因

后续实验补齐了：

- `GetDeviceData` 中的 `0x39 down/up`
- `GetDeviceState[0x39] = 0x80`

但在“启用手柄”时仍然失败。  
所以问题不在 proxy 是否同时补了事件和状态。

### 3. 真空格成功/失败的第一处分叉在 `sub_140C11600`

对照样本：

- 游戏设置手柄关闭 + 真空格成功
- 游戏设置手柄开启 + 真空格失败

可见：

- `AFTER InputLoopProcess[0]` 前后都还是 `raw-space + empty`
- `BEFORE sub_140C11600` 仍然一样
- 成功样本在 `AFTER sub_140C11600` 变成 `Jump`
- 失败样本在 `AFTER sub_140C11600` 仍然是 `empty`

所以真正分叉不在 `150B0`，而在 `11600` 内部。

### 4. 不是“没查到 Jump”，而是最后写回选错了源对象

失败样本中已经能看到：

- `LookupCopy` 已经查到 `Jump`
- 但 `FinalField18Write SELECT` 选成了 `Skip`
- 然后 `LateField18Write` 又回退到 `empty`

成功样本则是：

- `FinalField18Write SELECT` 直接选了 lookup temp `Jump`
- 没有 late 回退

因此问题不是 lookup，而是最后写回前的 gate。

### 5. 真 gate 是 `gateObj + 0x121`

修正 Hex-Rays 偏移理解之后，真正相关字段是：

- `gateObj + 0x118`
- `gateObj + 0x120`
- `gateObj + 0x121`
- `gateObj + 0x122`

其中真正产生分叉的是：

- 游戏设置手柄关闭：`gateObj + 0x121 = 0`
- 游戏设置手柄开启：`gateObj + 0x121 = 1`

并且 live experiment 已直接验证：

- 仅在 `sub_140C11600` 调用期间把 `+0x121` 临时压成 `0`
- 真空格和 bridge `Jump` 都能恢复成功

这说明它不是相关性，而是因果点。

## 为什么不用全局平台切换

之前尝试过：

- 强制改 `IsUsingGamepad`
- 强制改 `GamepadControlsCursor`
- 改 `BSPCGamepadDeviceHandler` enable / vfunc

这些方法的问题是：

- 容易让 UI 平台和真实输入链分裂
- 直接导致手柄按键或摇杆失效
- 有的版本还会闪退

结论：

- 这不是正确修复层
- 正确修复应当只改 keyboard upgrade 的最小 gate
- 不应该全局重写“当前平台是键鼠还是手柄”

## 当前正式方案

### 保留

- `Route B`：手柄模拟量正式路径
- `KeyboardNativeBridge`：`DualPad.dll -> dinput8.dll` 共享桥
- `dinput8.dll` proxy：keyboard `GetDeviceData / GetDeviceState` 边界 emit
- `sub_140C11600` scoped `+0x121` coexistence patch

### 清理/放弃

- `MixedInputPlatformHook`
- 全局平台仲裁实验
- `dinput8` proxy 的 `inject_test` / `F10` / `pure_synthetic_mode`
- 一批外部 worker / DirectInput object / preprocess helper 的历史 probe

## 剩余事项

当前主问题已经从“定位 gate”转成“正式化和回归验证”。

建议下一步只做产品化验证：

- 真空格
- bridge `Jump`
- `Activate`
- `Sneak`
- `Sprint`

验证目标：

- 手柄模拟量保持正常
- keyboard-native digital 事件在“启用手柄”时也能正常升级
- 不依赖全局平台切换

## 一句话总结

最终不是“把游戏平台改成键鼠”，也不是“继续伪造更像真的 keyboard 数据”。  
真正有效的做法是：

- 在 `dinput8` 边界发 keyboard-native 事件
- 在 `sub_140C11600` 内只干掉 `gateObj + 0x121` 这道最小共存 gate

这样才能同时保住手柄模拟量和 PC 键盘型数字动作。
