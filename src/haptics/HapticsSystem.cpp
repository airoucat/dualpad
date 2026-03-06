#include "pch.h"
#include "haptics/HapticsSystem.h"
#include "haptics/HapticsConfig.h"
#include "haptics/HidOutput.h"
#include "haptics/HapticsTypes.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cmath>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        inline float Clamp01(float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }
    }

    HapticsSystem& HapticsSystem::GetSingleton()
    {
        static HapticsSystem instance;
        return instance;
    }

    bool HapticsSystem::Initialize()
    {
        if (_initialized.exchange(true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][System] Already initialized");
            return true;
        }

        auto& config = HapticsConfig::GetSingleton();
        config.Load();

        if (!config.enabled) {
            logger::warn("[Haptics][System] Native vibration bridge disabled in config");
            _initialized.store(false, std::memory_order_release);
            return false;
        }

        _nativeRequests.store(0, std::memory_order_relaxed);
        _nativeFramesSent.store(0, std::memory_order_relaxed);
        _nativeFramesDropped.store(0, std::memory_order_relaxed);
        _lastLeftMotor.store(0, std::memory_order_relaxed);
        _lastRightMotor.store(0, std::memory_order_relaxed);

        logger::info(
            "[Haptics][System] Native vibration bridge initialized leftScale={:.2f} rightScale={:.2f} max={:.2f} deadzone={:.2f}",
            config.leftMotorScale,
            config.rightMotorScale,
            config.maxIntensity,
            config.deadzone);
        return true;
    }

    bool HapticsSystem::Start()
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            logger::error("[Haptics][System] Cannot start: not initialized");
            return false;
        }

        if (_running.exchange(true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][System] Already running");
            return true;
        }

        logger::info("[Haptics][System] Native vibration bridge started");
        return true;
    }

    void HapticsSystem::Stop()
    {
        if (!_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        HidOutput::GetSingleton().StopVibration();
        logger::info("[Haptics][System] Native vibration bridge stopped");
    }

    void HapticsSystem::Shutdown()
    {
        if (!_initialized.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        Stop();
        logger::info("[Haptics][System] Native vibration bridge shutdown complete");
    }

    bool HapticsSystem::SubmitNativeVibration(std::uint16_t leftMotor, std::uint16_t rightMotor)
    {
        _nativeRequests.fetch_add(1, std::memory_order_relaxed);
        _lastLeftMotor.store(leftMotor, std::memory_order_relaxed);
        _lastRightMotor.store(rightMotor, std::memory_order_relaxed);

        if (!_initialized.load(std::memory_order_acquire) || !_running.load(std::memory_order_acquire)) {
            _nativeFramesDropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const auto& config = HapticsConfig::GetSingleton();

        HidFrame frame{};
        frame.qpc = ToQPC(Now());
        frame.leftMotor = ConvertMotorSpeed(leftMotor, config.leftMotorScale, config.deadzone, config.maxIntensity);
        frame.rightMotor = ConvertMotorSpeed(rightMotor, config.rightMotorScale, config.deadzone, config.maxIntensity);

        const bool sent = HidOutput::GetSingleton().SendFrame(frame);
        if (sent) {
            _nativeFramesSent.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            _nativeFramesDropped.fetch_add(1, std::memory_order_relaxed);
        }

        if (config.logNativeVibration) {
            logger::trace(
                "[Haptics][System] Native vibration rawL={} rawR={} hidL={} hidR={} sent={}",
                leftMotor,
                rightMotor,
                frame.leftMotor,
                frame.rightMotor,
                sent);
        }

        return sent;
    }

    HapticsSystem::Stats HapticsSystem::GetStats() const
    {
        Stats stats{};
        stats.nativeRequests = _nativeRequests.load(std::memory_order_relaxed);
        stats.nativeFramesSent = _nativeFramesSent.load(std::memory_order_relaxed);
        stats.nativeFramesDropped = _nativeFramesDropped.load(std::memory_order_relaxed);
        stats.lastLeftMotor = _lastLeftMotor.load(std::memory_order_relaxed);
        stats.lastRightMotor = _lastRightMotor.load(std::memory_order_relaxed);
        return stats;
    }

    void HapticsSystem::PrintStats()
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            logger::info("[Haptics][System] Not initialized");
            return;
        }

        const auto stats = GetStats();
        const auto hidStats = HidOutput::GetSingleton().GetStats();

        logger::info(
            "[Haptics][System] Native requests={} sent={} dropped={} lastRawL={} lastRawR={}",
            stats.nativeRequests,
            stats.nativeFramesSent,
            stats.nativeFramesDropped,
            stats.lastLeftMotor,
            stats.lastRightMotor);
        logger::info(
            "[Haptics][HidOutput] Frames={} Bytes={} Failures={}",
            hidStats.totalFramesSent,
            hidStats.totalBytesSent,
            hidStats.sendFailures);
    }

    std::uint8_t HapticsSystem::ConvertMotorSpeed(
        std::uint16_t rawSpeed,
        float scale,
        float deadzone,
        float maxIntensity)
    {
        float normalized = static_cast<float>(rawSpeed) / 65535.0f;
        normalized = Clamp01(normalized * std::max(scale, 0.0f));
        normalized = std::min(normalized, Clamp01(maxIntensity));
        if (normalized <= Clamp01(deadzone)) {
            return 0;
        }

        return static_cast<std::uint8_t>(std::lround(normalized * 255.0f));
    }
}
