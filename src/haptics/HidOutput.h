#pragma once

#include "haptics/HapticsTypes.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

// 前向声明（避免在头文件引入 hidapi）
struct hid_device_;
using hid_device = struct hid_device_;

namespace dualpad::haptics
{
    class HidOutput
    {
    public:
        static HidOutput& GetSingleton();

        // 由 HidReader 在设备连接/断开时调用
        void SetDevice(hid_device* device);

        // 发送震动帧
        bool SendFrame(const HidFrame& frame);

        // 停止震动
        void StopVibration();

        // 查询状态
        bool IsConnected() const;

        struct Stats
        {
            std::uint32_t totalFramesSent{ 0 };
            std::uint32_t totalBytesSent{ 0 };
            std::uint32_t sendFailures{ 0 };
        };

        Stats GetStats() const;

    private:
        HidOutput() = default;
        ~HidOutput() = default;
        HidOutput(const HidOutput&) = delete;
        HidOutput& operator=(const HidOutput&) = delete;

        // 对外调用（加锁）
        bool SendVibrateCommand(std::uint8_t leftMotor, std::uint8_t rightMotor);

        // 内部：调用方已持锁（不加锁，避免死锁）
        bool SendVibrateCommandUnlocked(std::uint8_t leftMotor, std::uint8_t rightMotor);

        static std::string WideToUtf8(const wchar_t* wstr);

    private:
        // 由 HidReader 管理生命周期，这里只持有裸指针引用
        hid_device* _device{ nullptr };

        mutable std::mutex _mutex;

        // 统计
        std::atomic<std::uint32_t> _totalFramesSent{ 0 };
        std::atomic<std::uint32_t> _totalBytesSent{ 0 };
        std::atomic<std::uint32_t> _sendFailures{ 0 };
    };
}
