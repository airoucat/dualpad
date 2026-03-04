#pragma once

#include <RE/Skyrim.h>
#include <cstdint>
#include <string>
#include <vector>

namespace dualpad::haptics
{
    struct ScannedSoundForm
    {
        std::uint32_t formId{ 0 };
        RE::FormType formType{ RE::FormType::None };
        std::string editorId;
    };

    class SoundFormScanner
    {
    public:
        static SoundFormScanner& GetSingleton();

        std::vector<ScannedSoundForm> ScanAllSoundForms();

    private:
        SoundFormScanner() = default;

        template <class TForm>
        static void ScanType(
            RE::TESDataHandler* dh,
            std::vector<ScannedSoundForm>& out);

        static std::string GetEditorIdSafe(const RE::TESForm* form);
    };
}