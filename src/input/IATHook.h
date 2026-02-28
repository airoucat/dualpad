#pragma once

namespace dualpad::input
{
    // 安装 XInput IAT hook
    // 返回 true 表示找到并 hook 了 XInput 函数
    // 返回 false 表示 Skyrim 不使用 XInput
    bool InstallXInputIATHook();
}