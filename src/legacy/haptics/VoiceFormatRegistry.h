#pragma once
#include <xaudio2.h>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    class VoiceFormatRegistry
    {
    public:
        static VoiceFormatRegistry& GetSingleton();

        void RegisterVoice(IXAudio2SourceVoice* voice, const WAVEFORMATEX* fmt);
        void UnregisterVoice(IXAudio2SourceVoice* voice);

        // 取格式 + 频率比（先默认1.0）
        bool GetVoiceFormat(IXAudio2SourceVoice* voice, WAVEFORMATEX& outFmt, std::vector<std::uint8_t>& outExt, float& outFreqRatio) const;

        void SetFrequencyRatio(IXAudio2SourceVoice* voice, float ratio);

    private:
        struct VoiceMeta
        {
            WAVEFORMATEX fmt{};
            std::vector<std::uint8_t> ext; // cbSize附加
            float freqRatio{ 1.0f };
        };

        mutable std::shared_mutex _mtx;
        std::unordered_map<std::uintptr_t, VoiceMeta> _map;
    };
}