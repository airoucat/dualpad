#pragma once

namespace dualpad::input
{
    // 启动时调用一次（设置默认绑定 + 启动热重载）
    void InitActionRuntime();

    // 主线程每帧调用（热重载轮询 + 消费Action队列）
    void TickActionRuntimeMainThread();
}