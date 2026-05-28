# 设备采集 / 协议解析层审查记录（2026-04-09）

## 审查范围

本次只看输入链路最前面的“设备采集 / 协议解析”层：

- `src/input/hid/HidTransport.*`
- `src/input/hid/DualSenseDevice.*`
- `src/input/HidReader.cpp`
- `src/input/protocol/DualSenseProtocol.*`
- `src/input/protocol/DualSenseUsbInputParser.cpp`
- `src/input/protocol/DualSenseBtInputParser.cpp`
- `src/input/protocol/DualSenseCommonFields.*`

## 前提假设

这次审查按当前产品边界判断，不把“没支持其它手柄”视为问题：

- mod 只适配 `DualSense`
- mod 只适配 `DualSense Edge`
- 不要求对其它 Sony / 非 Sony 手柄做兼容兜底

换句话说，这层代码应该更像“严格识别并正确处理 DS / Edge”，而不是“尽量泛化兼容各种手柄”。

## 结论摘要

- 当前没有看到会在 USB 主路径上立刻炸掉的 `P0 / P1`
- 当前最主要的风险集中在：
  - 蓝牙 transport 判定过于依赖启发式
  - 蓝牙 `0x01` partial parser 仍被当正式输入继续上送
  - HID reader 初始化失败后会进入不可恢复的假启动状态
- 对只支持 `DualSense / DualSense Edge` 的项目来说，“识别更严格、未知更早失败”比“模糊猜测后继续跑”更合适

## 具体问题

### 1. 蓝牙 transport 判定过于脆弱

当前 `DualSenseDevice::MaybeVerifyTransport(...)` 的确认逻辑主要依赖：

- 设备 path 里是否包含 `bth` / `bluetooth` / `usb`
- `reportId == 0x31`
- `reportId == 0x01` 时按包长猜 `USB` 或 `Bluetooth`

这有两个问题：

1. 真实运行日志已经出现过 `Transport hint unavailable from device path`
2. 一旦蓝牙设备走的是 `0x01`，但包长又不满足当前启发式，`transport` 就可能长期停留在 `Unknown`

而 `ParseDualSenseInputPacket(...)` 遇到 `Unknown` transport 会直接丢包。

受影响文件：

- `src/input/hid/DualSenseDevice.cpp`
- `src/input/protocol/DualSenseProtocol.cpp`

当前判断：

- 这是 `P2`
- 不是因为要兼容别的手柄
- 而是因为 DS / Edge 自己的蓝牙路径在当前启发式下仍可能被误判

### 2. 蓝牙 `0x01` parser 明知只支持 partial，仍继续进入正式事件链

`DualSenseBtInputParser.cpp` 里已经明确把 `BT 0x01` 标成：

- `ParseCompleteness::Partial`
- “当前只是 gameplay-subset parser”

但这份“不完整性”目前只体现在日志里，没有形成运行时保护：

- `HidReader` 在 parse 成功后直接继续 `NormalizePadState`
- 继续进入 `PadEventGenerator`
- 继续提交 `PadEventSnapshot`

这对 `DualSense Edge` 特别敏感，因为这条路径里：

- `buttons3` 被固定传成 `0`
- 所以 `BackLeft / BackRight / FnLeft / FnRight` 在该路径上天然读不到

而当前绑定文件已经把 `BackLeft / BackRight` 视为正式可绑定键位。

受影响文件：

- `src/input/protocol/DualSenseBtInputParser.cpp`
- `src/input/HidReader.cpp`
- `config/DualPadBindings.ini`

当前判断：

- 这是 `P2`
- 本质不是“蓝牙没做完”这么简单
- 而是“半验证路径已经在冒充正式路径”，并且会直接吞掉 Edge 扩展键能力

### 3. `hid_init()` 失败后，reader 会卡在“逻辑已启动，线程已退出”的坏状态

`StartHidReader()` 会先把 `g_running` 设成 `true`，然后拉起线程。

但在线程函数 `ReaderLoop()` 里，如果：

- `HidTransport::InitializeApi()` 失败

那么函数会直接 `return`，不会把 `g_running` 清回去。

结果就是：

- 对外看起来 reader 已经在运行
- 实际线程已经退出
- 后续也无法再次正常 `StartHidReader()`

这类问题不一定高频，但一旦踩到，恢复成本高，只能靠重新加载插件或重启游戏。

受影响文件：

- `src/input/HidReader.cpp`

当前判断：

- 这是 `P2`
- 不是协议格式问题
- 是设备采集层启动恢复契约没有闭合

## 非阻塞但值得一起收口的点

### 1. 设备打开逻辑还不够“只认对的 collection”

`HidTransport::OpenFirstDualSense()` 现在只按：

- Sony VID
- `DualSense` PID
- `DualSense Edge` PID

来找第一个可打开 path。

对当前项目边界来说，这个方向是对的，但还不够严格。后续最好继续补：

- HID collection / interface 过滤
- 更稳定的 transport 元数据来源

目标不是支持更多设备，而是更稳定地区分“这是不是我们真正要读的那个 DS / Edge 输入端口”。

### 2. 仍缺少硬件 packet sequence 观测

当前 `DualSenseDevice` 只维护软件自增 sequence。

这不会立刻造成行为错误，但会让后续排查这些问题时更吃力：

- 丢包
- reconnect 后的帧跳变
- USB / Bluetooth 报文连续性验证

这个点更适合作为第二阶段诊断增强，不必抢在前面三项之前做。

## 建议调整顺序

### 第一批：先把明显的运行时坏状态堵住

1. 修 `hid_init()` 失败后的假启动状态
2. 保证 reader 启动失败后可以明确重试，而不是卡死在 `g_running = true`

### 第二批：把 partial 蓝牙路径从正式主线里隔离出来

1. 在 `BT 0x01` 未完全验证前，不要把它当完整输入路径继续上送
2. 至少要显式标记降级，或直接禁用它承载 Edge 扩展键语义
3. 如果未来确认 `BT 0x01` 是正式支持面，再补齐字段和验证

### 第三批：收紧 DS / Edge 专用识别策略

1. 降低对 path 字符串和包长启发式的依赖
2. 增加 HID collection / interface 级过滤
3. 让 `Unknown` 更早暴露为“识别失败”，而不是继续靠猜

## 当前建议口径

后续如果有人继续改这一层，建议默认带着这个判断：

- 这不是“泛型 gamepad 输入层”
- 这是“只服务 DualSense / DualSense Edge 的专用输入入口”
- 因此可以接受更严格的识别和更保守的失败策略

对当前项目来说，宁可：

- 早失败
- 明确记录不支持的报文形态

也不要：

- 用启发式把未知报文伪装成已验证输入
- 让 partial 路径静默混进正式运行时链路
