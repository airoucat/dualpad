#pragma once
#include <string>

namespace dualpad::utils
{
    // 使用 DirectX 11 从渲染管线截取当前帧并保存为 PNG
    std::string TakeScreenshot();
}