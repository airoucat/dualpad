#pragma once
#include <filesystem>

namespace dualpad::input
{
    // 初始化（设置路径并尝试首次加载）
    void InitActionConfigHotReload(const std::filesystem::path& path = {});

    // 主线程每帧调用（内部会节流，默认 ~1s 检查一次）
    void PollActionConfigHotReload();
}