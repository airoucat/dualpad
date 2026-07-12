#include "pch.h"

#include "input/injection/GameplayLookTransformHook.h"

#include "input/injection/GameplayLookTransformPolicy.h"
#include "input/injection/HookPatchTransaction.h"
#include "input_v2/presentation/SkyrimEngineModeRouter.h"

#include <RE/Skyrim.h>
#include <SKSE/Version.h>

#include <array>
#include <cstring>
#include <vector>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::uintptr_t kLookHandlerThumbstickRva = 0x7091F0;
        constexpr std::uintptr_t kLookHandlerMouseMoveRva = 0x7091C0;
        constexpr std::uintptr_t kScopedTransformCallsiteRva = 0x70711D;
        constexpr std::uintptr_t kTransformQueryCallsiteRva = 0x705B12;
        constexpr std::uintptr_t kSharedTransformRva = 0x705AE0;
        constexpr std::uintptr_t kIsUsingGamepadRva = 0xC15240;
        constexpr std::size_t kThumbstickVfuncSlot = 2;
        constexpr std::size_t kMouseMoveVfuncSlot = 3;

        const patching::Bytes kExpectedScopedTransformCall{
            0xE8, 0xBE, 0xE9, 0xFF, 0xFF
        };
        const patching::Bytes kExpectedTransformQueryCall{
            0xE8, 0x29, 0xF7, 0x50, 0x00
        };

        thread_local GameplayLookSourceLatch g_sourceLatch{};

        patching::Bytes ReadPatchBytes(std::uintptr_t address, std::size_t size)
        {
            patching::Bytes bytes(size);
            std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), size);
            return bytes;
        }

        patching::Bytes PointerBytes(std::uintptr_t value)
        {
            patching::Bytes bytes(sizeof(value));
            std::memcpy(bytes.data(), &value, sizeof(value));
            return bytes;
        }

        bool CompareWritePatchBytes(
            std::uintptr_t address,
            const patching::Bytes& expected,
            const patching::Bytes& desired)
        {
            return !expected.empty() && expected.size() == desired.size() &&
                REL::safe_write(
                    address,
                    desired.data(),
                    desired.size(),
                    expected.data(),
                    expected.size());
        }

        std::uintptr_t AllocateAbsoluteJumpStub(std::uintptr_t destination)
        {
            const auto bytes = patching::MakeAbsoluteJump(destination);
            if (bytes.empty()) {
                return 0;
            }
            auto* memory = SKSE::GetTrampoline().allocate(bytes.size());
            if (!memory) {
                return 0;
            }
            const auto address = reinterpret_cast<std::uintptr_t>(memory);
            REL::safe_write(address, bytes.data(), bytes.size());
            return ReadPatchBytes(address, bytes.size()) == bytes ? address : 0;
        }
    }

    GameplayLookTransformHook& GameplayLookTransformHook::GetSingleton()
    {
        static GameplayLookTransformHook hook;
        return hook;
    }

    bool GameplayLookTransformHook::InstallI2DiagnosticCandidate()
    {
        bool expected = false;
        if (!_attempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return _installed.load(std::memory_order_acquire);
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            logger::error(
                "[DualPad][GameplayLookTransform][I2Candidate] unsupported runtime {}; hook disabled",
                REL::Module::get().version().string());
            return false;
        }

        const auto moduleBase = REL::Module::get().base();
        REL::Relocation<std::uintptr_t> lookHandlerVtable{ RE::VTABLE_LookHandler[0] };
        const auto vtableAddress = lookHandlerVtable.address();
        const auto thumbstickSlot =
            vtableAddress + kThumbstickVfuncSlot * sizeof(std::uintptr_t);
        const auto mouseMoveSlot =
            vtableAddress + kMouseMoveVfuncSlot * sizeof(std::uintptr_t);
        const auto scopedTransformCallsite = moduleBase + kScopedTransformCallsiteRva;
        const auto transformQueryCallsite = moduleBase + kTransformQueryCallsiteRva;
        const auto originalThumbstick = moduleBase + kLookHandlerThumbstickRva;
        const auto originalMouseMove = moduleBase + kLookHandlerMouseMoveRva;
        const auto originalTransform = moduleBase + kSharedTransformRva;
        const auto originalQuery = moduleBase + kIsUsingGamepadRva;

        const auto expectedThumbstick = PointerBytes(originalThumbstick);
        const auto expectedMouseMove = PointerBytes(originalMouseMove);
        if (ReadPatchBytes(thumbstickSlot, sizeof(std::uintptr_t)) != expectedThumbstick ||
            ReadPatchBytes(mouseMoveSlot, sizeof(std::uintptr_t)) != expectedMouseMove ||
            ReadPatchBytes(scopedTransformCallsite, kExpectedScopedTransformCall.size()) !=
                kExpectedScopedTransformCall ||
            ReadPatchBytes(transformQueryCallsite, kExpectedTransformQueryCall.size()) !=
                kExpectedTransformQueryCall) {
            logger::error(
                "[DualPad][GameplayLookTransform][I2Candidate] identity mismatch; all four source-scope sites remain disabled");
            return false;
        }

        const auto scopedTransformStub = AllocateAbsoluteJumpStub(
            reinterpret_cast<std::uintptr_t>(&ScopedGameplayLookTransformHook));
        const auto transformQueryStub = AllocateAbsoluteJumpStub(
            reinterpret_cast<std::uintptr_t>(&TransformIsUsingGamepadHook));
        const auto scopedTransformReplacement = patching::MakeRelativePatch(
            scopedTransformCallsite,
            scopedTransformStub,
            patching::RelativePatchOpcode::Call,
            kExpectedScopedTransformCall.size());
        const auto transformQueryReplacement = patching::MakeRelativePatch(
            transformQueryCallsite,
            transformQueryStub,
            patching::RelativePatchOpcode::Call,
            kExpectedTransformQueryCall.size());
        if (scopedTransformStub == 0 || transformQueryStub == 0 ||
            scopedTransformReplacement.empty() || transformQueryReplacement.empty()) {
            logger::error(
                "[DualPad][GameplayLookTransform][I2Candidate] trampoline preparation failed; hook disabled");
            return false;
        }

        _originalThumbstick.store(originalThumbstick, std::memory_order_release);
        _originalMouseMove.store(originalMouseMove, std::memory_order_release);
        _originalTransform.store(originalTransform, std::memory_order_release);
        _originalQuery.store(originalQuery, std::memory_order_release);

        std::vector<patching::PatchSite> sites;
        const auto addSite = [&sites](
                                 std::string name,
                                 std::uintptr_t address,
                                 patching::Bytes original,
                                 patching::Bytes replacement) {
            const auto size = original.size();
            sites.push_back(patching::PatchSite{
                .name = std::move(name),
                .original = std::move(original),
                .replacement = std::move(replacement),
                .read = [address, size]() { return ReadPatchBytes(address, size); },
                .compareWrite = [address](const auto& from, const auto& to) {
                    return CompareWritePatchBytes(address, from, to);
                }
            });
        };
        addSite(
            "look_handler_thumbstick_vfunc",
            thumbstickSlot,
            expectedThumbstick,
            PointerBytes(reinterpret_cast<std::uintptr_t>(&ProcessThumbstickHook)));
        addSite(
            "look_handler_mouse_move_vfunc",
            mouseMoveSlot,
            expectedMouseMove,
            PointerBytes(reinterpret_cast<std::uintptr_t>(&ProcessMouseMoveHook)));
        addSite(
            "player_look_shared_transform_call",
            scopedTransformCallsite,
            kExpectedScopedTransformCall,
            scopedTransformReplacement);
        addSite(
            "shared_transform_gamepad_query_call",
            transformQueryCallsite,
            kExpectedTransformQueryCall,
            transformQueryReplacement);

        const auto transaction = patching::ExecutePatchTransaction(sites);
        if (transaction.outcome != patching::PatchTransactionOutcome::Installed) {
            logger::error(
                "[DualPad][GameplayLookTransform][I2Candidate] install failed outcome={} site={} applied={} rolledBack={}; route remains passthrough",
                patching::ToString(transaction.outcome),
                transaction.failedSite,
                transaction.appliedSites,
                transaction.rolledBackSites);
            return false;
        }

        _routeEnabled.store(true, std::memory_order_release);
        _installed.store(true, std::memory_order_release);
        logger::info(
            "[DualPad][GameplayLookTransform][I2Candidate] installed exact source-scoped original-math route vtable=0x{:X} transformCall=0x{:X} queryCall=0x{:X}",
            vtableAddress,
            scopedTransformCallsite,
            transformQueryCallsite);
        return true;
    }

    bool GameplayLookTransformHook::IsInstalled() const noexcept
    {
        return _installed.load(std::memory_order_acquire);
    }

    void GameplayLookTransformHook::ProcessThumbstickHook(
        RE::LookHandler* self,
        RE::ThumbstickEvent* event,
        RE::PlayerControlsData* data)
    {
        auto& hook = GetSingleton();
        using Original = void (*)(RE::LookHandler*, RE::ThumbstickEvent*, RE::PlayerControlsData*);
        if (const auto target = hook._originalThumbstick.load(std::memory_order_acquire);
            target != 0) {
            reinterpret_cast<Original>(target)(self, event, data);
        }
        if (hook._routeEnabled.load(std::memory_order_acquire) && data) {
            g_sourceLatch.Note(
                reinterpret_cast<std::uintptr_t>(data),
                GameplayLookInputSource::Gamepad);
        }
    }

    void GameplayLookTransformHook::ProcessMouseMoveHook(
        RE::LookHandler* self,
        RE::MouseMoveEvent* event,
        RE::PlayerControlsData* data)
    {
        auto& hook = GetSingleton();
        using Original = void (*)(RE::LookHandler*, RE::MouseMoveEvent*, RE::PlayerControlsData*);
        if (const auto target = hook._originalMouseMove.load(std::memory_order_acquire);
            target != 0) {
            reinterpret_cast<Original>(target)(self, event, data);
        }
        if (hook._routeEnabled.load(std::memory_order_acquire) && data) {
            g_sourceLatch.Note(
                reinterpret_cast<std::uintptr_t>(data),
                GameplayLookInputSource::Mouse);
        }
    }

    void GameplayLookTransformHook::ScopedGameplayLookTransformHook(
        void* controls,
        float* lookVector)
    {
        auto& hook = GetSingleton();
        using Original = void (*)(void*, float*);
        const auto target = hook._originalTransform.load(std::memory_order_acquire);
        if (target == 0) {
            return;
        }
        const auto callOriginal = [&]() {
            reinterpret_cast<Original>(target)(controls, lookVector);
        };
        if (!hook._routeEnabled.load(std::memory_order_acquire) || !controls || !lookVector) {
            g_sourceLatch.Reset();
            callOriginal();
            return;
        }

        auto* playerControls = static_cast<RE::PlayerControls*>(controls);
        auto* expectedLookVector = reinterpret_cast<float*>(&playerControls->data.lookInputVec);
        if (lookVector != expectedLookVector) {
            g_sourceLatch.Reset();
            callOriginal();
            return;
        }

        const auto source = g_sourceLatch.ConsumeFor(
            reinterpret_cast<std::uintptr_t>(&playerControls->data));
        if (source == GameplayLookInputSource::None) {
            callOriginal();
            return;
        }

        const auto mode = ResolveGameplayLookTransformGamepadMode(false, source) ?
            input_v2::gameplay::EngineInputMode::Gamepad :
            input_v2::gameplay::EngineInputMode::KeyboardMouse;
        auto scope = input_v2::presentation::SkyrimEngineModeRouter{}
            .EnterScopedOverride(
                input_v2::gameplay::EngineQueryDomain::GameplayLookTransform,
                mode,
                0,
                0);
        callOriginal();
    }

    bool GameplayLookTransformHook::TransformIsUsingGamepadHook(
        void* inputDeviceManager)
    {
        auto& hook = GetSingleton();
        using Original = bool (*)(void*);
        return input_v2::presentation::SkyrimEngineModeRouter{}.DecideWithOriginal(
            [&]() {
                if (const auto target = hook._originalQuery.load(std::memory_order_acquire);
                    target != 0) {
                    return reinterpret_cast<Original>(target)(inputDeviceManager);
                }
                return false;
            });
    }
}
