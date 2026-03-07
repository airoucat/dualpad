# 给 Codex 的提示词

你正在重构一个 **Windows / C++ / SKSE 插件**项目中的 DualSense 输入子系统。
仓库是：`airoucat/dualpad` 的 `main` 分支。
当前输入代码主要在 `src/input` 目录下。

## 总目标

只重构以下两层：

1. **底层 I/O 层**
2. **DualSense 协议层**

**不要重构映射层、绑定层、游戏动作层、UI 层。**
这些部分后续再改，现在要保证调试边界清晰。

本次工作的核心目标：

* 更高性能
* 更高稳定度
* 更清晰的分层
* 更容易调试 USB / Bluetooth 差异
* 更容易为后续重构映射层做准备
* 保持现有功能尽量不回退

---

## 设计原则

### 必须遵守

* 保留 `hidapi` 作为唯一底层 HID 依赖
* 不要直接把外部 DualSense 项目作为核心运行时依赖
* 可以参考以下项目的架构和协议思路，但要自己实现：

  * `nondebug/dualsense`
  * `Ohjurot/DualSense-Windows`
  * `pydualsense`
  * `dualsensectl`
* 不要引入 Python、C#、额外大型第三方运行时
* 不要先动 mapping / binding / synthetic gamepad 注入层
* 尽量减少公开接口破坏，必要时通过适配层兼容旧调用
* 所有改动要以“便于调试”为优先，保留详细日志点，但默认低开销

### 性能和稳定性要求

* Reader loop 不要做复杂业务逻辑
* Reader loop 中避免堆分配
* 尽量减少锁竞争，优先使用栈对象、固定缓冲区、无锁或低锁状态交换
* 报文读取、协议解析、状态归一化分离
* 明确区分 USB 和 Bluetooth 输入报告解析
* 对异常报文、短报文、未知 report id、设备断开要有健壮处理
* 连接断开和重连路径必须可恢复
* 代码必须适合后续加入 gyro、accel、touch2、adaptive trigger output

---

## 当前代码存在的问题（重构时必须修复）

请先阅读并理解当前 `src/input` 代码，再按下面问题进行重构：

1. 当前解析逻辑本质上是 **USB 固定布局解析器**，没有正确抽象 USB / Bluetooth 输入报告差异。
2. HID reader、协议解析、状态更新、触发器分发耦合过重。
3. `DualSenseProtocol` 不是完整协议层，只是一个偏移读取工具。
4. 当前实现对 touchpad click、mute 等按钮语义不完整或命名不准确。
5. 模拟量输入（sticks / triggers）还没有形成独立、稳定的状态模型。
6. 后续映射层如果要支持 axis / combo / hold / gyro，会受当前底层设计限制。
7. 调试 USB / BT 报文时缺少清晰边界和统一日志入口。
8. 当前结构不利于未来加入输出报告（rumble / lightbar / adaptive trigger）。

---

## 本次重构的明确范围

### 允许修改

* `src/input` 下的底层读取、协议解析、状态表示相关文件
* 新增 `hid/`、`protocol/`、`state/` 子目录
* 新增适配层，兼容旧接口
* 重命名明显错误的协议字段命名
* 新增测试辅助代码、调试日志、报文 dump 工具
* 新增面向未来输出层的占位接口，但**本次不要实现 haptics / trigger output 业务**

### 不允许修改

* mapping / binding / gesture 业务语义
* Skyrim 注入层的外部行为
* 游戏动作触发逻辑
* 配置系统
* UI / MCM / Papyrus 逻辑
* 不要把整个项目改成另一套架构
* 不要顺手修 unrelated issue

---

## 目标分层结构

请把 `src/input` 重构为接近下面的结构：

```text
src/input/
  hid/
    HidTransport.h
    HidTransport.cpp
    DualSenseDevice.h
    DualSenseDevice.cpp

  protocol/
    DualSenseProtocol.h
    DualSenseProtocolTypes.h
    DualSenseUsbInputParser.cpp
    DualSenseBtInputParser.cpp
    DualSenseReportIds.h
    DualSenseButtons.h

  state/
    PadState.h
    PadState.cpp
    PadSnapshot.h
    PadStateNormalizer.cpp
    PadStateDebugger.cpp

  legacy/
    InputCompatBridge.cpp
```

如果目录名略有调整可以接受，但必须体现以下边界：

* **hid/** 只负责设备枚举、打开、关闭、读原始字节、连接状态
* **protocol/** 只负责把原始 HID 报文解析成结构化输入状态
* **state/** 只负责统一状态对象、归一化、快照、调试输出
* **legacy/compat** 只负责向现有上层暴露兼容接口

---

## 需要实现的核心抽象

### 1. Raw packet 抽象

新增统一原始输入包结构：

```cpp
enum class TransportType {
    USB,
    Bluetooth,
    Unknown
};

struct RawInputPacket {
    TransportType transport;
    uint8_t reportId;
    const uint8_t* data;
    size_t size;
};
```

要求：

* reader loop 从 HID 读到数据后，先构造成 `RawInputPacket`
* 不要在 reader loop 里直接做按钮业务逻辑
* packet 可以附带时间戳或 sequence 编号，便于调试

---

### 2. 统一 PadState 抽象

新增一个明确、完整、与上层映射解耦的状态结构：

```cpp
struct TouchPointState {
    bool active;
    uint16_t x;
    uint16_t y;
    uint8_t id;
};

struct StickState {
    float x;
    float y;
};

struct TriggerState {
    uint8_t raw;
    float normalized;
};

struct ImuState {
    int16_t gyroX;
    int16_t gyroY;
    int16_t gyroZ;
    int16_t accelX;
    int16_t accelY;
    int16_t accelZ;
};

struct PadButtons {
    uint32_t digitalMask;
    bool touchpadClick;
    bool mute;
    bool ps;
};

struct PadState {
    bool connected;
    TransportType transport;
    uint8_t reportId;
    uint64_t timestampUs;

    PadButtons buttons;
    StickState leftStick;
    StickState rightStick;
    TriggerState leftTrigger;
    TriggerState rightTrigger;

    TouchPointState touch1;
    TouchPointState touch2;

    ImuState imu;

    uint8_t battery;
    bool batteryValid;
};
```

要求：

* 即使当前上层暂时不用 gyro / accel / touch2，也先纳入状态
* USB / BT 都要解析到同一个 `PadState`
* 状态字段命名要准确，避免 `Mic` 这种歧义，应使用 `Mute`

---

### 3. 协议解析接口

实现类似这样的接口：

```cpp
class IDualSenseInputParser {
public:
    virtual ~IDualSenseInputParser() = default;
    virtual bool Parse(const RawInputPacket& packet, PadState& outState) = 0;
};

bool ParseDualSenseInputPacket(const RawInputPacket& packet, PadState& outState);
```

具体要求：

* 按 transport 区分 USB / Bluetooth 解析
* 明确支持至少：

  * USB input report `0x01`
  * Bluetooth input report `0x01`
* 对未知 report id：

  * 返回 false
  * 写调试日志
  * 不要污染已有状态
* 对短报文和 malformed packet 要健壮处理

---

### 4. USB / BT 解析器分离

实现两个解析器：

* `DualSenseUsbInputParser`
* `DualSenseBtInputParser`

要求：

* 不要在同一个函数里用大量 if/else 混合解析
* 每个解析器内部用清晰的字节偏移常量
* 偏移常量集中定义，不要魔法数字散落在代码里
* 解析函数中尽量不做额外业务转换，只做协议到状态的转换

请参考社区资料对齐以下信息：

* 左右摇杆
* L2 / R2 模拟量
* face buttons / shoulder buttons / stick buttons / dpad
* PS 按钮
* mute 按钮
* touchpad click
* 触摸点 1 / 触摸点 2
* gyro / accel
* battery（如果报文中可得）

---

### 5. State normalizer

协议解析完成后，增加一个轻量状态归一化步骤：

```cpp
void NormalizePadState(PadState& state);
```

要求：

* 摇杆 raw `0..255` -> `[-1.0, 1.0]`
* 扳机 raw `0..255` -> `[0.0, 1.0]`
* Y 轴方向保持与当前项目兼容
* 暂时不要加复杂 deadzone 算法
* 但要为以后 deadzone / response curve 留出扩展点

要求实现为独立模块，而不是塞进 parser 里。

---

### 6. Device / transport 层

实现一个设备对象：

```cpp
class DualSenseDevice {
public:
    bool Open();
    void Close();
    bool IsOpen() const;

    bool ReadPacket(RawInputPacket& outPacket);
    TransportType GetTransportType() const;
};
```

要求：

* 封装 hidapi 设备打开/关闭/读取
* 尝试识别 transport 是 USB 还是 Bluetooth
* reader thread 只依赖 `DualSenseDevice`
* 后续如果要加 output report，也从这个对象扩展
* 设备断开后应可安全关闭并重试重连
* 对错误码有统一日志

注意：

* 这里优先做**单设备稳定读取**
* 不要在本次引入复杂多设备管理器，除非当前代码已经依赖它

---

### 7. 调试能力

请加入专门的调试支持，便于后续人工验证：

1. **报文摘要日志**

   * report id
   * transport
   * size
   * 关键字段摘要

2. **可选的十六进制 dump**

   * 仅在 debug 宏或配置开启时输出

3. **状态摘要日志**

   * buttons
   * sticks
   * triggers
   * touch1/touch2
   * gyro/accel 是否更新

4. **解析失败日志**

   * 不支持的 report id
   * packet size 太短
   * transport 不匹配
   * device disconnected

要求：

* 默认低开销
* 不要让日志污染主 reader 性能
* 最好用编译期开关或轻量运行时开关

---

## 对现有代码的兼容要求

为了避免一次性破坏上层逻辑，本次请保留一个兼容桥接层，让现有上层仍可拿到当前需要的最小输入信息。

也就是说：

* 新协议层输出 `PadState`
* 然后通过一个 `InputCompatBridge` 转成旧上层当前需要的按钮/轴数据
* 上层暂时不感知底层已经重构

如果发现现有接口明显不合理，可以加 `TODO` 注释说明后续映射层重构时再彻底替换。

---

## 实现风格要求

* 使用现代 C++，但不要过度模板化
* 优先可读性和调试性
* 减少全局状态
* 明确 ownership
* 尽量让 parser 成为纯函数式或接近纯函数式模块
* 使用 `constexpr` 定义 report offset / button bits
* 所有新增类型和函数都加简洁注释
* 对“为什么这样设计”写清楚，不只写“做了什么”

---

## 交付形式

请按以下顺序工作：

1. 先分析当前 `src/input` 现状
2. 列出要改动的文件和新增文件
3. 分步实施重构
4. 每一步说明原因
5. 尽量小步提交，避免一次性巨大 diff
6. 重构完成后，给出：

   * 新架构说明
   * USB / BT 解析流程说明
   * 与旧接口兼容方式说明
   * 后续如何接入映射层重构的建议

---

## 非目标

本次不要实现以下内容：

* 自适应扳机输出
* 震动 / lightbar 输出
* gyro aiming
* 新的手势系统
* combo / axis binding
* mapping 层语义升级
* 配置 UI
* 多控制器热插拔复杂管理

这些都留到下一阶段。

---

## 重构成功标准

当你完成后，应满足这些标准：

1. Reader loop 更薄，只做读包和分发
2. USB / Bluetooth DualSense 报告解析已分离
3. `PadState` 成为统一状态模型
4. 上层现有逻辑基本还能跑
5. 代码结构明显更清晰
6. 未来加 gyro / touch2 / output report 不需要再推翻底层
7. 调试时能快速判断是 HID 问题、协议问题还是上层映射问题

---

## 额外要求：性能与稳定性优化点

在实现过程中，请主动关注并优化以下方面：

* 避免 reader thread 中的动态内存分配
* 避免重复解析同一字段
* 用固定大小缓冲区接收 HID 数据
* 避免锁包围整个读循环
* 对设备断开采用明确状态机而不是异常式散落处理
* 尽量让报文解析和状态归一化无副作用
* 为未来双缓冲或原子快照交换预留空间

---

## 请开始执行

先不要直接大改。
第一步先输出：

1. 对当前 `src/input` 的结构分析
2. 一份重构计划
3. 一份准备新增/修改文件列表
4. 哪些现有函数会被替换或保留兼容

然后再开始逐步改代码。

---

# 我给你的附加建议

你把上面这段给 Codex 后，再补一句约束，会更稳：

> 如果你不确定某个 DualSense 报文字段偏移，请优先保守实现，并在代码中写明 `TODO(protocol verification)`，不要凭空猜测；同时保持 USB 与 Bluetooth 解析路径完全分离。

再补一句会更适合你当前节奏：

> 本次目标是“把底层打稳”，不是“把所有高级功能一次做完”。优先让调试边界清晰、diff 可控、现有功能不倒退。

如果你愿意，我还能继续帮你写第二份更具体的版本：**“面向 Codex 的分阶段执行提示词”**，把任务拆成第 1 轮、第 2 轮、第 3 轮，避免它一次改太多。
