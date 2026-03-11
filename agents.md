你正在重构一个 Windows / C++ / SKSE 插件的 DualSense 注入层，游戏版本是se 1597版本，使用commonlibsse-ng。

项目状态：

- 底层 I/O + 协议层已完成，提供 PadState，包括：
  - 按钮状态、摇杆、扳机、touch1/touch2、touchpadClick、IMU、battery、transport
- 映射层已重构，输出事件类型：
  - ButtonPress / ButtonRelease
  - AxisChange
  - Combo / Hold / Tap
  - Gesture
  - Touchpad 按下 / 滑动
- 映射层声称输出了 **帧级原子事件快照**（atomic per-frame event list），需要检查一下

目标：

- DualSense 输入通过映射层生成事件，注入层根据事件类型执行对应功能
- 控制事件包括：
  - 原生 Skyrim controlmap 事件
  - 自定义事件（插件功能）
  - Mod 事件（预留接口）
- 每帧事件快照可能变化，注入层必须处理所有类型事件

任务：

1. **检查映射层是否已输出帧级原子事件快照**
   - 验证 PadEventGenerator / TriggerMapper / TouchpadMapper 是否在一帧内生成完整事件列表
   - 事件顺序应与 PadState snapshot 对齐
   - 确保事件列表一次性传递给注入层，不分批或异步

2. **设计注入层重构方案**
   - 消费映射层事件快照（PadEvent列表）
   - 更新 SyntheticPadState 管理按键状态：
     - Pressed / Released / Held
     - 长按（hold）和短按（tap）
     - Combo / 多键组合
     - 摇杆 / 触控板轴事件
   - 保证：
     - 不丢帧、不吞键
     - 长按连续正确
     - 组合键顺序正确
   - 高性能：
     - 栈对象或固定缓冲区
     - 低延迟
     - 多线程安全
   - 可调试：
     - 每帧输出事件快照和 SyntheticPadState 状态
     - 可选 debug 宏或运行时开关

3. **接口设计**
   - 注入层接口独立于映射层实现
   - 支持接收一帧完整事件快照并一次性更新虚拟按键
   - 支持将 SyntheticPadState 输出到游戏动作
   - 原生 controlmap 事件通过 **直接操作游戏内 ControlMap 内存或函数** 注入：
     - ButtonEvent / ThumbstickEvent
     - InputEventQueue / BSInputDeviceManager
   - 自定义事件调用插件内部接口（如截图事件）
   - Mod事件接口预留，不做实现

4. **游戏上下文与允许事件**
   - **Gameplay**: Jump, Forward, Back, Strafe Left, Strafe Right, Move, Look, Left Attack/Block, Right Attack/Block, Activate, Ready Weapon, Tween Menu, Toggle POV, Zoom Out, Zoom In, Sprint, Shout, Sneak, Run, Toggle Always Run, Auto-Move, Favorites, Hotkey1–Hotkey8, Quicksave, Quickload, Wait, Journal, Pause, Screenshot, Multi-Screenshot, Console, CameraPath
   - **Menu**: Accept, Cancel, Up, Down, Left, Right, Left Stick
   - **Map**: MapLookMode, LocalMap, LocalMapMoveMode, PlayerPosition
   - **Inventory**: Inventory Navigation, Toggle POV, Hotkey1–Hotkey8
   - **Lockpicking**: RotatePick, RotateLock, Cancel
   - **Book**: PrevPage, NextPage, TabSwitch
   - **Stats**: Camera, Rotate, Page Navigation
   - **Creation / TFC / Debug**: CameraZUp, CameraZDown, WorldZUp, WorldZDown, Console commands
   - **Mod虚拟按键**: 虚拟按键池所列出的, 可作为 ModEvent1–ModEvent8

5. **映射层 → 注入层规则**
   - DualSense 按键/组合 → controlmap / 自定义 / Mod 事件
   - DualSense 摇杆 → Move / Look 轴事件
   - 触控板 → Mod / 自定义事件
   - 支持每帧事件更新，保证原子性
   - 支持 INI 自定义键位和自由组合键
   - 严格禁止两个 FN 键与面键组合

6. **虚拟按键池（Mod事件专用）**
   - 必须在 keyboard_english.txt 出现，且未在 controlmap.txt 使用
   - 主推荐：F13, F14, F15
   - 可选备用：DIK_KANA, DIK_ABNT_C1, DIK_CONVERT, DIK_NOCONVERT, DIK_ABNT_C2, NumPadEqual, PrintSrc, L-Windows, R-Windows, Apps, Power, Sleep, Wake, WebSearch, WebFavorites, WebRefresh, WebStop, WebForward, WebBack, MyComputer, Mail, MediaSelect

7. **帧处理规则**
   - 每帧：
     1. 读取 PadEvent 快照
     2. 更新 SyntheticPadState
     3. 检测状态变化（Pressed / Held / Released）
     4. 上下文验证
     5. 注入事件或调用插件功能
   - 保证：
     - 顺序确定性
     - 不丢帧、不吞键
     - 长按和组合键正确处理

8. **Codex 任务**
   - 检查映射层是否输出帧级原子事件快照
   - 生成注入层骨架：
     - SyntheticPadState 更新
     - 按键生命周期管理
     - 组合键 / 长按 / Tap 处理
     - 摇杆 / 触控板事件注入
     - Mod 虚拟按键注入（F13–F20）
     - 自定义事件调用接口
     - Debug 输出
   - 支持未来扩展：
     - 新触控板模式
     - 新组合键
     - 新 Mod 事件