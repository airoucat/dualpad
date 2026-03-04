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
            // 注意：这里已持锁，必须调用 Unlocked 版本，避免自死锁
            // (void)SendVibrateCommandUnlocked(0, 0);
        }
        if (device != _device) {
            logger::info("[Haptics][HidOutput] SetDevice ptr=0x{:X}", reinterpret_cast<std::uintptr_t>(device));
        }
        _device = device;
    }

    bool HidOutput::IsConnected() const
    {
        std::scoped_lock lock(_mutex);
        return _device != nullptr;
    }

    bool HidOutput::SubmitFrameNonBlocking(const HidFrame& frame)
    {
        _pendingLeft.store(frame.leftMotor, std::memory_order_relaxed);
        _pendingRight.store(frame.rightMotor, std::memory_order_relaxed);
        _pendingDirty.store(true, std::memory_order_release);
        return true;
    }

    bool HidOutput::SendFrame(const HidFrame& frame)
    {
        return SubmitFrameNonBlocking(frame);
    }

    void HidOutput::StopVibration()
    {
        logger::info("[Haptics][HidOutput] Stopping vibration...");
        HidFrame f{};
        f.leftMotor = 0;
        f.rightMotor = 0;
        (void)SubmitFrameNonBlocking(f);
    }

    void HidOutput::FlushPendingOnReaderThread(hid_device* device)
    {
        if (!device) {
            return;
        }
        if (!_pendingDirty.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        const auto l = _pendingLeft.load(std::memory_order_relaxed);
        const auto r = _pendingRight.load(std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        _device = device; // 同步当前句柄

        const bool ok = SendVibrateCommandUnlocked(l, r);
        if (ok) {
            _totalFramesSent.fetch_add(1, std::memory_order_relaxed);
            _totalBytesSent.fetch_add(48, std::memory_order_relaxed);
            _sendWriteOk.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            _sendFailures.fetch_add(1, std::memory_order_relaxed);
            // 失败分型仍在 SendVibrateCommandUnlocked 里维护
        }
    }

    HidOutput::Stats HidOutput::GetStats() const
    {
        Stats s;
        s.totalFramesSent = _totalFramesSent.load(std::memory_order_relaxed);
        s.totalBytesSent = _totalBytesSent.load(std::memory_order_relaxed);
        s.sendFailures = _sendFailures.load(std::memory_order_relaxed);
        s.sendNoDevice = _sendNoDevice.load(std::memory_order_relaxed);
        s.sendWriteFail = _sendWriteFail.load(std::memory_order_relaxed);
        s.sendWriteOk = _sendWriteOk.load(std::memory_order_relaxed);
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
            static std::atomic_uint32_t s_noDevLog{ 0 };
            auto n = s_noDevLog.fetch_add(1);
            if (n < 5 || (n % 500) == 0) {
                logger::warn("[Haptics][HidOutput] send skipped: no device");
            }
            _sendNoDevice.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // DualSense 输出报文（你当前工程使用的简化格式）
        unsigned char buf[48]{};
        buf[0] = 0x02;        // Report ID
        buf[1] = 0xFF;        // flags
        buf[2] = 0xF7;        // flags2
        buf[3] = rightMotor;  // right (small)
        buf[4] = leftMotor;   // left (large)

        const int written = hid_write(_device, buf, static_cast<size_t>(sizeof(buf)));
        if (written < 0) {
            const wchar_t* err = hid_error(_device);
            _sendWriteFail.fetch_add(1, std::memory_order_relaxed);
            logger::warn("[Haptics][HidOutput] hid_write failed: {}", WideToUtf8(err));
            return false;
        }

        // _sendWriteOk.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
}