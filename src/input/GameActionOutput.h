#pragma once

namespace dualpad::input
{
    // 消费 ActionRouter 队列并执行 Game.* action
    // MVP: 可从任意线程调用（先跑通）
    // 后续建议改为主线程每帧调用
    void PumpActionEvents();
}