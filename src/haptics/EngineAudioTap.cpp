#include "pch.h"
#include "haptics/EngineAudioTap.h"
#include "haptics/VoiceManager.h"
#include "haptics/HapticsTypes.h"

#include <SKSE/SKSE.h>
#include <Windows.h>
#include <xaudio2.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        using InitSound_t = std::int64_t(__fastcall*)(std::uintptr_t a1);
        inline InitSound_t g_origInitSound{ nullptr };

        // SkyrimSE 1.5.97
        constexpr std::uintptr_t kRva_Sub_140BFD780 = 0x00BFD780;

        // 已验证：slot21 是 SubmitSourceBuffer 路径
        constexpr std::size_t kSubmitSlot = 21;
        constexpr std::uint32_t kMinVtableHitsBeforeHook = 1;

        using ProbeSubmit_t = HRESULT(STDMETHODCALLTYPE*)(
            IXAudio2SourceVoice*,
            const XAUDIO2_BUFFER*,
            const XAUDIO2_BUFFER_WMA*);

        std::atomic<bool> g_installed{ false };

        std::atomic<std::uint64_t> g_hookHits{ 0 };
        std::atomic<std::uint64_t> g_submitCalls{ 0 };
        std::atomic<std::uint64_t> g_submitFeaturesPushed{ 0 };
        std::atomic<std::uint64_t> g_submitCompressedSkipped{ 0 };
        std::atomic<std::uint64_t> g_probeHits{ 0 };

        std::mutex g_probeMx;
        std::unordered_set<std::uintptr_t> g_hookedVtables;
        std::unordered_map<void*, ProbeSubmit_t> g_origSubmitByTarget;
        std::vector<void*> g_probeTargets;

        std::mutex g_vtblMx;
        std::unordered_map<std::uintptr_t, std::uint32_t> g_vtblSeenCount;

        static bool IsReadablePtr(const void* p, size_t bytes = sizeof(void*))
        {
            if (!p) {
                return false;
            }

            MEMORY_BASIC_INFORMATION mbi{};
            if (::VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) {
                return false;
            }
            if (mbi.State != MEM_COMMIT) {
                return false;
            }
            if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) {
                return false;
            }

            const auto s = reinterpret_cast<std::uintptr_t>(p);
            const auto e = s + bytes;
            const auto r = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            return e <= r;
        }

        static bool SafeReadPtr(const void* addr, void*& out)
        {
            out = nullptr;
            if (!IsReadablePtr(addr, sizeof(void*))) {
                return false;
            }
            out = *reinterpret_cast<void* const*>(addr);
            return true;
        }

        static std::string ModuleNameFromAddr(const void* p)
        {
            if (!p) {
                return "null";
            }

            HMODULE mod = nullptr;
            if (!::GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(p), &mod)) {
                return "unknown";
            }

            char path[MAX_PATH]{};
            ::GetModuleFileNameA(mod, path, MAX_PATH);
            return path;
        }

        static std::uint32_t EstimateDurationUsFromBytesPCM16(
            std::uint32_t audioBytes,
            std::uint32_t sampleRate,
            std::uint32_t channels,
            const XAUDIO2_BUFFER* pBuffer)
        {
            if (sampleRate == 0) sampleRate = 48000;
            if (channels == 0) channels = 1;

            const std::uint32_t bytesPerFrame = channels * 2; // PCM16
            if (bytesPerFrame == 0) return 40'000;

            const std::uint64_t framesByBytes = audioBytes / bytesPerFrame;
            std::uint64_t frames = framesByBytes;

            if (pBuffer && pBuffer->PlayLength > 0 && framesByBytes > 0) {
                const std::uint64_t pl = pBuffer->PlayLength;
                // 只有与 bytes 推导接近时才信任 PlayLength
                if (pl >= framesByBytes / 2 && pl <= framesByBytes * 2) {
                    frames = pl;
                }
            }

            if (frames == 0) return 40'000;

            std::uint64_t us = (frames * 1'000'000ull) / sampleRate;

            if (pBuffer) {
                if (pBuffer->LoopCount == XAUDIO2_LOOP_INFINITE) {
                    us = std::min<std::uint64_t>(us, 180'000ull);
                }
                else if (pBuffer->LoopCount > 0) {
                    us = std::min<std::uint64_t>(us * (static_cast<std::uint64_t>(pBuffer->LoopCount) + 1ull), 180'000ull);
                }
            }

            us = std::clamp<std::uint64_t>(us, 8'000ull, 180'000ull);
            return static_cast<std::uint32_t>(us);
        }

        static bool LooksLikeXAudioObject(void* obj)
        {
            if (!obj) {
                return false;
            }

            void* vtbl = nullptr;
            if (!SafeReadPtr(obj, vtbl) || !vtbl) {
                return false;
            }

            auto mod = ModuleNameFromAddr(vtbl);
            std::transform(mod.begin(), mod.end(), mod.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            return mod.find("xaudio2_7") != std::string::npos ||
                mod.find("xaudio2_8") != std::string::npos ||
                mod.find("xaudio2_9") != std::string::npos;
        }

        static IXAudio2SourceVoice* ResolveVoicePtr(void* raw)
        {
            if (!raw) {
                return nullptr;
            }
            return LooksLikeXAudioObject(raw)
                ? reinterpret_cast<IXAudio2SourceVoice*>(raw)
                : nullptr;
        }

        static HRESULT STDMETHODCALLTYPE Hook_SubmitSlot_AsSubmit(
            IXAudio2SourceVoice* self,
            const XAUDIO2_BUFFER* pBuffer,
            const XAUDIO2_BUFFER_WMA* pBufferWMA)
        {
            g_submitCalls.fetch_add(1, std::memory_order_relaxed);
            const auto hit = g_probeHits.fetch_add(1, std::memory_order_relaxed) + 1;

            ProbeSubmit_t orig = nullptr;
            if (self && IsReadablePtr(self, sizeof(void*))) {
                auto vt = *reinterpret_cast<void***>(self);
                void* target = vt ? vt[kSubmitSlot] : nullptr;

                std::scoped_lock lk(g_probeMx);
                auto it = g_origSubmitByTarget.find(target);
                if (it != g_origSubmitByTarget.end()) {
                    orig = it->second;
                }
            }

            if (!pBuffer || !IsReadablePtr(pBuffer, sizeof(XAUDIO2_BUFFER)) ||
                !pBuffer->pAudioData || pBuffer->AudioBytes < 64) {
                return orig ? orig(self, pBuffer, pBufferWMA) : E_FAIL;
            }

            if (pBufferWMA != nullptr) {
                g_submitCompressedSkipped.fetch_add(1, std::memory_order_relaxed);
                if (hit <= 40 || (hit % 200) == 0) {
                    logger::info("[Haptics][SubmitTap] compressed skip self=0x{:X} bytes={} pWma=0x{:X}",
                        reinterpret_cast<std::uintptr_t>(self),
                        pBuffer->AudioBytes,
                        reinterpret_cast<std::uintptr_t>(pBufferWMA));
                }
                return orig ? orig(self, pBuffer, pBufferWMA) : E_FAIL;
            }

            const auto* data = pBuffer->pAudioData;
            const auto bytes = pBuffer->AudioBytes;

            if (!IsReadablePtr(data, std::min<size_t>(bytes, 256))) {
                return orig ? orig(self, pBuffer, pBufferWMA) : E_FAIL;
            }

            XAUDIO2_VOICE_DETAILS vd{};
            self->GetVoiceDetails(&vd);

            const std::uint32_t channels = std::clamp<std::uint32_t>(vd.InputChannels, 1u, 8u);
            const std::uint32_t sampleRate = (vd.InputSampleRate > 0) ? vd.InputSampleRate : 48000u;
            const std::uint32_t durUs = EstimateDurationUsFromBytesPCM16(
                static_cast<std::uint32_t>(bytes),
                sampleRate,
                channels,
                pBuffer);
            // 仍按 int16 PCM 路径抽特征（与你现有实现保持一致）
            const auto totalSamples = bytes / sizeof(std::int16_t);
            if (totalSamples >= 32) {
                const auto* s16 = reinterpret_cast<const std::int16_t*>(data);
                const std::size_t frameCount = totalSamples / channels;
                if (frameCount > 0) {
                    double sumL = 0.0, sumR = 0.0, sumM = 0.0;
                    float peak = 0.0f;

                    const std::size_t step = (frameCount > 4096) ? 4 : 1;
                    std::size_t used = 0;

                    for (std::size_t f = 0; f < frameCount; f += step) {
                        const std::size_t base = f * channels;

                        const float l = static_cast<float>(s16[base + 0]) / 32768.0f;
                        const float r = (channels >= 2)
                            ? static_cast<float>(s16[base + 1]) / 32768.0f
                            : l;

                        const float m = 0.5f * (l + r);

                        sumL += static_cast<double>(l) * static_cast<double>(l);
                        sumR += static_cast<double>(r) * static_cast<double>(r);
                        sumM += static_cast<double>(m) * static_cast<double>(m);

                        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
                        ++used;
                    }

                    if (used > 0) {
                        const float rmsL = static_cast<float>(std::sqrt(sumL / static_cast<double>(used)));
                        const float rmsR = static_cast<float>(std::sqrt(sumR / static_cast<double>(used)));
                        const float rms = static_cast<float>(std::sqrt(sumM / static_cast<double>(used)));

                        if (rms > 0.0005f || peak > 0.0015f) {
                            AudioFeatureMsg msg{};
                            msg.qpcStart = ToQPC(Now());
                            msg.qpcEnd = msg.qpcStart + durUs;  // 关键：真实时长
                            msg.voiceId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(self));
                            msg.sampleRate = sampleRate;
                            msg.channels = channels;

                            msg.rms = rms;
                            msg.peak = peak;
                            msg.attack = peak;
                            msg.energyL = rmsL;
                            msg.energyR = rmsR;
                            msg.bandLow = rms;
                            msg.bandMid = rms;
                            msg.bandHigh = rms;

                            if (VoiceManager::GetSingleton().PushAudioFeature(msg)) {
                                g_submitFeaturesPushed.fetch_add(1, std::memory_order_relaxed);
                            }

                            if (hit <= 40 || (hit % 200) == 0) {
                                logger::info("[Haptics][SubmitTap] feature pushed rms={:.4f} peak={:.4f} ch={} sr={} dur={}us bytes={}",
                                    rms, peak, channels, sampleRate, durUs, bytes);
                            }
                        }
                    }
                }
            }

            return orig ? orig(self, pBuffer, pBufferWMA) : E_FAIL;
        }

        static bool HookSubmitSlotFromVoice(IXAudio2SourceVoice* voice)
        {
            if (!voice) {
                return false;
            }

            void* vtbl = nullptr;
            if (!SafeReadPtr(voice, vtbl) || !vtbl) {
                return false;
            }

            const auto vtblAddr = reinterpret_cast<std::uintptr_t>(vtbl);
            auto vt = reinterpret_cast<void**>(vtbl);
            void* target = vt[kSubmitSlot];
            if (!target) {
                return false;
            }

            std::scoped_lock lk(g_probeMx);

            if (g_hookedVtables.contains(vtblAddr)) {
                return true;
            }

            logger::info("[Haptics][SafeProbe] install submit hook: vtbl=0x{:X} slot{}=0x{:X} mod={}",
                vtblAddr, kSubmitSlot, reinterpret_cast<std::uintptr_t>(target), ModuleNameFromAddr(target));

            ProbeSubmit_t orig = nullptr;
            auto ch = MH_CreateHook(target, &Hook_SubmitSlot_AsSubmit, reinterpret_cast<void**>(&orig));
            if (ch != MH_OK && ch != MH_ERROR_ALREADY_CREATED) {
                logger::warn("[Haptics][SafeProbe] MH_CreateHook failed: {}", static_cast<int>(ch));
                return false;
            }

            auto eh = MH_EnableHook(target);
            if (eh != MH_OK && eh != MH_ERROR_ENABLED) {
                logger::warn("[Haptics][SafeProbe] MH_EnableHook failed: {}", static_cast<int>(eh));
                return false;
            }

            g_origSubmitByTarget[target] = orig;
            g_probeTargets.push_back(target);
            g_hookedVtables.insert(vtblAddr);

            logger::info("[Haptics][SafeProbe] submit hook installed OK slot={} vtbl=0x{:X}",
                kSubmitSlot, vtblAddr);
            return true;
        }

        std::int64_t __fastcall Hook_Sub_140BFD780(std::uintptr_t a1)
        {
            const auto ret = g_origInitSound(a1);
            const auto n = g_hookHits.fetch_add(1, std::memory_order_relaxed) + 1;

            void* raw = nullptr;
            const bool okRaw = SafeReadPtr(reinterpret_cast<void*>(a1 + 0x128), raw);

            IXAudio2SourceVoice* voice = nullptr;
            std::uintptr_t rawVtAddr = 0;
            std::uint32_t vtblHits = 0;

            if (okRaw && raw) {
                voice = ResolveVoicePtr(raw);

                void* rawVt = nullptr;
                SafeReadPtr(raw, rawVt);
                rawVtAddr = reinterpret_cast<std::uintptr_t>(rawVt);

                std::scoped_lock lk(g_vtblMx);
                vtblHits = ++g_vtblSeenCount[rawVtAddr];
            }

            if (n <= 40 || (n % 500) == 0) {
                logger::info(
                    "[Haptics][EngineAudioTap] hit={} a1=0x{:X} raw_ok={} raw=0x{:X} rawVt=0x{:X} rawMod={} resolved=0x{:X} vtblHits={}",
                    n, a1, okRaw ? 1 : 0, reinterpret_cast<std::uintptr_t>(raw), rawVtAddr,
                    ModuleNameFromAddr(reinterpret_cast<void*>(rawVtAddr)),
                    reinterpret_cast<std::uintptr_t>(voice), vtblHits);
            }

            if (voice && vtblHits >= kMinVtableHitsBeforeHook) {
                (void)HookSubmitSlotFromVoice(voice);
            }

            return ret;
        }
    }

    bool EngineAudioTap::Install()
    {
        if (g_installed.load(std::memory_order_acquire)) {
            return true;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(L"SkyrimSE.exe"));
        if (!base) {
            logger::error("[Haptics][EngineAudioTap] SkyrimSE.exe base not found");
            return false;
        }

        const auto target = reinterpret_cast<void*>(base + kRva_Sub_140BFD780);

        const auto initHr = MH_Initialize();
        if (initHr != MH_OK && initHr != MH_ERROR_ALREADY_INITIALIZED) {
            logger::error("[Haptics][EngineAudioTap] MH_Initialize failed: {}", static_cast<int>(initHr));
            return false;
        }

        const auto ch = MH_CreateHook(target, &Hook_Sub_140BFD780, reinterpret_cast<void**>(&g_origInitSound));
        if (ch != MH_OK && ch != MH_ERROR_ALREADY_CREATED) {
            logger::error("[Haptics][EngineAudioTap] MH_CreateHook failed: {}", static_cast<int>(ch));
            return false;
        }

        const auto eh = MH_EnableHook(target);
        if (eh != MH_OK && eh != MH_ERROR_ENABLED) {
            logger::error("[Haptics][EngineAudioTap] MH_EnableHook failed: {}", static_cast<int>(eh));
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        logger::info("[Haptics][EngineAudioTap] installed at 0x{:X}", reinterpret_cast<std::uintptr_t>(target));
        return true;
    }

    void EngineAudioTap::Uninstall()
    {
        if (!g_installed.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        logger::info("[Haptics][EngineAudioTap] Uninstall begin");

        // 保持“快速退出”策略，避免停服阶段卡死；
        // 进程结束时 OS 回收 hook 资源。
        {
            std::scoped_lock lk(g_probeMx);
            g_probeTargets.clear();
            g_hookedVtables.clear();
            g_origSubmitByTarget.clear();
        }

        {
            std::scoped_lock lk(g_vtblMx);
            g_vtblSeenCount.clear();
        }

        g_probeHits.store(0, std::memory_order_relaxed);

        logger::info("[Haptics][EngineAudioTap] Uninstall complete (fast-exit mode)");
    }

    EngineAudioTap::Stats EngineAudioTap::GetStats()
    {
        Stats s{};
        s.hookHits = g_hookHits.load(std::memory_order_relaxed);
        s.submitCalls = g_submitCalls.load(std::memory_order_relaxed);
        s.submitFeaturesPushed = g_submitFeaturesPushed.load(std::memory_order_relaxed);
        s.submitCompressedSkipped = g_submitCompressedSkipped.load(std::memory_order_relaxed);

        // 兼容旧字段
        s.attachAttempts = 0;
        s.attachSuccess = 0;
        s.attachFailed = 0;
        return s;
    }
}