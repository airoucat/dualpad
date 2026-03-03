#pragma once
#include "haptics/HapticsTypes.h"
#include "haptics/MPSCQueue.h"
#include <atomic>
#include <cstddef>
#include <memory>

namespace dualpad::haptics
{
    // Submit hook 多线程生产 -> scorer 单线程消费
    class VoiceManager
    {
    public:
        static VoiceManager& GetSingleton();

        void Initialize();
        void Shutdown();

        bool PushAudioFeature(const AudioFeatureMsg& msg);
        std::size_t PopAudioFeatures(AudioFeatureMsg* out, std::size_t maxCount);

        std::size_t GetQueueSize() const;
        std::size_t GetQueueCapacity() const;

        struct Stats
        {
            std::uint64_t featuresPushed{ 0 };
            std::uint64_t featuresDropped{ 0 };
        };

        Stats GetStats() const;

    private:
        VoiceManager() = default;

        std::unique_ptr<MPSCQueue<AudioFeatureMsg>> _featureQueue;
        std::atomic<bool> _initialized{ false };
        std::atomic<std::size_t> _capacity{ 0 };

        std::atomic<std::uint64_t> _featuresPushed{ 0 };
        std::atomic<std::uint64_t> _featuresDropped{ 0 };
    };
}