#include "pch.h"
#include "haptics/SoundFormScanner.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    SoundFormScanner& SoundFormScanner::GetSingleton()
    {
        static SoundFormScanner s;
        return s;
    }

    std::string SoundFormScanner::GetEditorIdSafe(const RE::TESForm* form)
    {
        if (!form) {
            return {};
        }
        const char* eid = form->GetFormEditorID();
        return eid ? std::string(eid) : std::string{};
    }

    template <class TForm>
    void SoundFormScanner::ScanType(RE::TESDataHandler* dh, std::vector<ScannedSoundForm>& out)
    {
        auto& arr = dh->GetFormArray<TForm>();
        for (auto* f : arr) {
            if (!f) {
                continue;
            }

            ScannedSoundForm s{};
            s.formId = f->GetFormID();
            s.formType = f->GetFormType();
            s.editorId = GetEditorIdSafe(f);

            if (s.formId != 0) {
                out.push_back(std::move(s));
            }
        }
    }

    std::vector<ScannedSoundForm> SoundFormScanner::ScanAllSoundForms()
    {
        std::vector<ScannedSoundForm> out;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::warn("[Haptics][SoundFormScanner] TESDataHandler is null");
            return out;
        }

        out.reserve(8192);

        // 本阶段：声音相关主类型
        ScanType<RE::TESSound>(dh, out);
        ScanType<RE::BGSSoundDescriptorForm>(dh, out);
        ScanType<RE::BGSFootstepSet>(dh, out);
        ScanType<RE::BGSImpactDataSet>(dh, out);
        ScanType<RE::BGSMusicType>(dh, out);
        ScanType<RE::BGSAcousticSpace>(dh, out);

        logger::info("[Haptics][SoundFormScanner] scanned records={}", out.size());
        return out;
    }
}