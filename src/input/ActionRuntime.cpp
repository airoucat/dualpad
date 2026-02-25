#include "pch.h"
#include "input/ActionRuntime.h"
#include "input/ActionRouter.h"
#include "input/ActionConfig.h"
#include "input/GameActionOutput.h"

namespace dualpad::input
{
    void InitActionRuntime()
    {
        // 先默认绑定，后用 ini 覆盖（如果文件存在且解析成功）
        ActionRouter::GetSingleton().InitDefaultBindings();
        InitActionConfigHotReload();
    }

    void TickActionRuntimeMainThread()
    {
        // 低频检查文件变化（内部节流）
        PollActionConfigHotReload();

        // 主线程消费 Action -> UI/Game
        PumpActionEvents();
    }
}