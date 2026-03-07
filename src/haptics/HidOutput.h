#pragma once

#include "haptics/HapticsTypes.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

struct hid_device_;
using hid_device = struct hid_device_;

namespace dualpad::haptics
{
    // Sends rumble frames to the currently connected controller.
    class HidOutput
    {
    public:
        static HidOutput& GetSingleton();

        // Called by the reader thread when the HID device appears or disappears.
        void SetDevice(hid_device* device);

        // Writes one haptics frame to the controller.
        bool SendFrame(const HidFrame& frame);

        // Forces both motors to stop immediately.
        void StopVibration();

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

        // Public send path that acquires the device mutex.
        bool SendVibrateCommand(std::uint8_t leftMotor, std::uint8_t rightMotor);

        // Internal send path used when the caller already owns the device mutex.
        bool SendVibrateCommandUnlocked(std::uint8_t leftMotor, std::uint8_t rightMotor);

        static std::string WideToUtf8(const wchar_t* wstr);

    private:
        // HidReader owns the raw HID handle lifetime.
        hid_device* _device{ nullptr };

        mutable std::mutex _mutex;

        std::atomic<std::uint32_t> _totalFramesSent{ 0 };
        std::atomic<std::uint32_t> _totalBytesSent{ 0 };
        std::atomic<std::uint32_t> _sendFailures{ 0 };
    };
}
