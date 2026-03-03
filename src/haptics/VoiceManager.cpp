#include "pch.h"
#include "haptics/VoiceManager.h"
#include "haptics/HapticsConfig.h"
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    VoiceManager& VoiceManager::GetSingleton()
    {
        static VoiceManager instance;
        return instance;
    }

    void VoiceManager::Initialize()
    {
        if (_initialized.exchange(true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][VoiceManager] already initialized");
            return;
        }

        const auto& cfg = HapticsConfig::GetSingleton();
        _featureQueue = std::make_unique<MPSCQueue<AudioFeatureMsg>>(cfg.queueCapacity);
        _capacity.store(_featureQueue->Capacity(), std::memory_order_release);

        logger::info("[Haptics][VoiceManager] initialized capacity={}", _featureQueue->Capacity());
    }

    void VoiceManager::Shutdown()
    {
        if (!_initialized.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        _featureQueue.reset();
        logger::info("[Haptics][VoiceManager] shutdown");
    }

    bool VoiceManager::PushAudioFeature(const AudioFeatureMsg& msg)
    {
        if (!_initialized.load(std::memory_order_acquire) || !_featureQueue) {
            return false;
        }

        if (_featureQueue->TryPush(msg)) {
            _featuresPushed.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        _featuresDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::size_t VoiceManager::PopAudioFeatures(AudioFeatureMsg* out, std::size_t maxCount)
    {
        if (!_initialized.load(std::memory_order_acquire) || !_featureQueue || !out || maxCount == 0) {
            return 0;
        }

        return _featureQueue->PopBatch(out, maxCount);
    }

    std::size_t VoiceManager::GetQueueSize() const
    {
        if (!_initialized.load(std::memory_order_acquire) || !_featureQueue) {
            return 0;
        }
        return _featureQueue->SizeApprox();
    }

    std::size_t VoiceManager::GetQueueCapacity() const
    {
        return _capacity.load(std::memory_order_acquire);
    }

    VoiceManager::Stats VoiceManager::GetStats() const
    {
        Stats s{};
        s.featuresPushed = _featuresPushed.load(std::memory_order_relaxed);
        s.featuresDropped = _featuresDropped.load(std::memory_order_relaxed);
        return s;
    }
}