#pragma once

#include <atomic>
#include <cstdint>

namespace dualpad::haptics
{
    class HapticsSystem
    {
    public:
        struct Stats
        {
            std::uint64_t nativeRequests{ 0 };
            std::uint64_t nativeFramesSent{ 0 };
            std::uint64_t nativeFramesDropped{ 0 };
            std::uint16_t lastLeftMotor{ 0 };
            std::uint16_t lastRightMotor{ 0 };
        };

        static HapticsSystem& GetSingleton();

        bool Initialize();
        bool Start();
        void Stop();
        void Shutdown();
        bool SubmitNativeVibration(std::uint16_t leftMotor, std::uint16_t rightMotor);

        bool IsRunning() const { return _running.load(std::memory_order_acquire); }
        bool IsInitialized() const { return _initialized.load(std::memory_order_acquire); }

        Stats GetStats() const;
        void PrintStats();

    private:
        HapticsSystem() = default;

        static std::uint8_t ConvertMotorSpeed(
            std::uint16_t rawSpeed,
            float scale,
            float deadzone,
            float maxIntensity);

        std::atomic<bool> _initialized{ false };
        std::atomic<bool> _running{ false };
        std::atomic<std::uint64_t> _nativeRequests{ 0 };
        std::atomic<std::uint64_t> _nativeFramesSent{ 0 };
        std::atomic<std::uint64_t> _nativeFramesDropped{ 0 };
        std::atomic<std::uint16_t> _lastLeftMotor{ 0 };
        std::atomic<std::uint16_t> _lastRightMotor{ 0 };
    };
}
