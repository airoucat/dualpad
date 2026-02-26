#pragma once

namespace dualpad::input
{
    // 在你认定的主线程调用一次（建议 InitActionRuntime 里）
    void BindGameOutputThread();

    // Runtime 侧：只做“收集/排队”，不直接碰 RE::UI / PlayerControls
    void CollectGameOutputCommands();

    // 只在绑定线程每帧调用：真正执行 Game.* action + 写 axis 到游戏
    void FlushGameOutputOnBoundThread();
}