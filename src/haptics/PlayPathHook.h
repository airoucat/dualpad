#pragma once

#include <atomic>
#include <cstdint>
#include <xaudio2.h>

namespace dualpad::haptics
{
    class PlayPathHook
    {
    public:
        struct Stats
        {
            #define X(name) std::uint64_t name{ 0 };
            #include "haptics/PlayPathCounterFields.def"
            #undef X
        };

        static PlayPathHook& GetSingleton();

        // Called from init-sound hook: best-effort form semantic hydration.
        void OnInitSoundObject(
            std::uintptr_t initSoundObject,
            std::uintptr_t voicePtr,
            std::uint64_t instanceId,
            std::uint64_t nowUs);

        // Called from submit hook: fallback hydration from XAUDIO2_BUFFER::pContext.
        void OnSubmitContext(
            IXAudio2SourceVoice* voice,
            const XAUDIO2_BUFFER* buffer,
            std::uint64_t nowUs);

        Stats GetStats() const;
        void ResetStats();

    private:
        PlayPathHook() = default;

        struct Counters
        {
            #define X(name) std::atomic<std::uint64_t> name{ 0 };
            #include "haptics/PlayPathCounterFields.def"
            #undef X
        };

        Counters _counters;
    };
}
