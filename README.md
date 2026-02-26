# dualpad
HidReader.cpp/h：HID线程读手柄，解析按钮/触摸/摇杆，摇杆写到 AnalogState，按钮发到 ActionRouter
DualSenseEdgeMapping.h：DualSense报文定义、按键位、触摸解析
StickFilter.cpp/h：摇杆死区/平滑/曲线处理
AnalogState.cpp/h：线程安全的“最新模拟量快照”
InputActions.h：Trigger/Axis 枚举与字符串映射
ActionRouter.cpp/h：事件路由中心（输入事件→ActionId，支持绑定表、队列、Drain）
ActionConfig.cpp/h：读 DualPadActions.ini，热重载绑定
ActionRuntime.cpp/h：主线程每帧总调度（poll配置、发axis事件、pump输出）
GameActionOutput.cpp/h：把 Game.* action 真正执行到游戏（开菜单、写 move/look）