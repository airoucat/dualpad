#include "pch.h"
#include "haptics/VoiceFormatRegistry.h"

namespace dualpad::haptics
{
    VoiceFormatRegistry& VoiceFormatRegistry::GetSingleton()
    {
        static VoiceFormatRegistry s;
        return s;
    }

    void VoiceFormatRegistry::RegisterVoice(IXAudio2SourceVoice* voice, const WAVEFORMATEX* fmt)
    {
        if (!voice || !fmt) return;

        VoiceMeta m{};
        m.fmt = *fmt;
        m.freqRatio = 1.0f;

        if (fmt->cbSize > 0) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(fmt) + sizeof(WAVEFORMATEX);
            m.ext.assign(p, p + fmt->cbSize);
        }

        std::unique_lock lk(_mtx);
        _map[reinterpret_cast<std::uintptr_t>(voice)] = std::move(m);
    }

    void VoiceFormatRegistry::UnregisterVoice(IXAudio2SourceVoice* voice)
    {
        if (!voice) return;
        std::unique_lock lk(_mtx);
        _map.erase(reinterpret_cast<std::uintptr_t>(voice));
    }

    bool VoiceFormatRegistry::GetVoiceFormat(IXAudio2SourceVoice* voice, WAVEFORMATEX& outFmt, std::vector<std::uint8_t>& outExt, float& outFreqRatio) const
    {
        if (!voice) return false;
        std::shared_lock lk(_mtx);
        auto it = _map.find(reinterpret_cast<std::uintptr_t>(voice));
        if (it == _map.end()) return false;

        outFmt = it->second.fmt;
        outExt = it->second.ext;
        outFreqRatio = it->second.freqRatio;
        return true;
    }

    void VoiceFormatRegistry::SetFrequencyRatio(IXAudio2SourceVoice* voice, float ratio)
    {
        if (!voice) return;
        std::unique_lock lk(_mtx);
        auto it = _map.find(reinterpret_cast<std::uintptr_t>(voice));
        if (it != _map.end()) {
            it->second.freqRatio = (ratio > 0.01f) ? ratio : 1.0f;
        }
    }
}