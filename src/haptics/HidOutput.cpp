#include "pch.h"
#include "haptics/HidOutput.h"

#include <SKSE/SKSE.h>
#include <Windows.h>
#include <hidapi/hidapi.h>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    HidOutput& HidOutput::GetSingleton()
    {
        static HidOutput instance;
        return instance;
    }

    std::string HidOutput::WideToUtf8(const wchar_t* wstr)
    {
        if (!wstr) {
            return "unknown";
        }

        const int needed = ::WideCharToMultiByte(
            CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);

        if (needed <= 1) {
            return "unknown";
        }

        std::string out(static_cast<std::size_t>(needed - 1), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, wstr, -1, out.data(), needed, nullptr, nullptr);

        return out;
    }

    void HidOutput::SetDevice(hid_device* device)
    {
        std::scoped_lock lock(_mutex);

        if (device && !_device) {
            logger::info("[Haptics][HidOutput] Device connected");
        }
        else if (!device && _device) {
            logger::info("[Haptics][HidOutput] Device disconnected");

            (void)SendVibrateCommandUnlocked(0, 0);
        }

        _device = device;
    }

    bool HidOutput::IsConnected() const
    {
        std::scoped_lock lock(_mutex);
        return _device != nullptr;
    }

    bool HidOutput::SendFrame(const HidFrame& frame)
    {
        const bool success = SendVibrateCommand(frame.leftMotor, frame.rightMotor);

        if (success) {
            _totalFramesSent.fetch_add(1, std::memory_order_relaxed);
            _totalBytesSent.fetch_add(48, std::memory_order_relaxed);
        }
        else {
            _sendFailures.fetch_add(1, std::memory_order_relaxed);
        }

        return success;
    }

    void HidOutput::StopVibration()
    {
        logger::info("[Haptics][HidOutput] Stopping vibration...");
        (void)SendVibrateCommand(0, 0);
    }

    HidOutput::Stats HidOutput::GetStats() const
    {
        Stats s;
        s.totalFramesSent = _totalFramesSent.load(std::memory_order_relaxed);
        s.totalBytesSent = _totalBytesSent.load(std::memory_order_relaxed);
        s.sendFailures = _sendFailures.load(std::memory_order_relaxed);
        return s;
    }

    bool HidOutput::SendVibrateCommand(std::uint8_t leftMotor, std::uint8_t rightMotor)
    {
        std::scoped_lock lock(_mutex);
        return SendVibrateCommandUnlocked(leftMotor, rightMotor);
    }

    bool HidOutput::SendVibrateCommandUnlocked(std::uint8_t leftMotor, std::uint8_t rightMotor)
    {
        if (!_device) {
            return false;
        }

        // This is the 48-byte simplified DualSense output report used by the current plugin.
        unsigned char buf[48]{};
        buf[0] = 0x02;
        buf[1] = 0xFF;
        buf[2] = 0xF7;
        buf[3] = rightMotor;
        buf[4] = leftMotor;

        const int written = hid_write(_device, buf, static_cast<size_t>(sizeof(buf)));
        if (written < 0) {
            const wchar_t* err = hid_error(_device);
            logger::warn("[Haptics][HidOutput] hid_write failed: {}", WideToUtf8(err));
            return false;
        }

        return true;
    }
}
