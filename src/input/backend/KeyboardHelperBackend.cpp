#include "pch.h"
#include "input/backend/KeyboardHelperBackend.h"

#include "input/InputModalityTracker.h"
#include "input/RuntimeConfig.h"
#include "input/backend/KeyboardNativeBridge.h"

#include <cctype>
#include <filesystem>
#include <limits>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    namespace
    {
        using namespace std::literals;

        constexpr bool UsesContinuousState(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Hold;
        }

        constexpr bool UsesDebouncedPulse(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Pulse;
        }

        constexpr bool UsesScheduledPulseContract(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Toggle ||
                contract == ActionOutputContract::Repeat;
        }

        constexpr float kRepeatInitialDelaySeconds = 0.35f;
        constexpr float kRepeatIntervalSeconds = 0.08f;
        constexpr float kRepeatScheduleEpsilon = 0.0001f;
        constexpr std::uint64_t kSyntheticHelperSuppressionWindowMs = 250;
        constexpr std::uint8_t kSyntheticPressPendingEvents = 1;
        constexpr std::uint8_t kSyntheticReleasePendingEvents = 1;
        constexpr std::uint8_t kSyntheticPulsePendingEvents = 2;

        struct HelperKeyEntry
        {
            std::string_view token;
            std::uint8_t scancode;
        };

        constexpr HelperKeyEntry kFunctionKeyPoolEntries[] = {
            { "F13"sv, 0x64 },
            { "F14"sv, 0x65 },
            { "F15"sv, 0x66 }
        };

        constexpr HelperKeyEntry kVirtualKeyPoolEntries[] = {
            { "DIK_F13"sv, 0x64 },
            { "DIK_F14"sv, 0x65 },
            { "DIK_F15"sv, 0x66 },
            { "DIK_KANA"sv, 0x70 },
            { "KANA"sv, 0x70 },
            { "DIK_ABNT_C1"sv, 0x73 },
            { "ABNT_C1"sv, 0x73 },
            { "DIK_CONVERT"sv, 0x79 },
            { "CONVERT"sv, 0x79 },
            { "DIK_NOCONVERT"sv, 0x7B },
            { "NO_CONVERT"sv, 0x7B },
            { "NOCONVERT"sv, 0x7B },
            { "DIK_ABNT_C2"sv, 0x7E },
            { "ABNT_C2"sv, 0x7E },
            { "NUMPADEQUAL"sv, 0x8D },
            { "NUMPAD_EQUAL"sv, 0x8D },
            { "PRINTSRC"sv, 0xB7 },
            { "PRINT_SRC"sv, 0xB7 },
            { "L_WINDOWS"sv, 0xDB },
            { "LWINDOWS"sv, 0xDB },
            { "R_WINDOWS"sv, 0xDC },
            { "RWINDOWS"sv, 0xDC },
            { "APPS"sv, 0xDD },
            { "POWER"sv, 0xDE },
            { "SLEEP"sv, 0xDF },
            { "WAKE"sv, 0xE3 },
            { "WEBSEARCH"sv, 0xE5 },
            { "WEB_SEARCH"sv, 0xE5 },
            { "WEBFAVORITES"sv, 0xE6 },
            { "WEB_FAVORITES"sv, 0xE6 },
            { "WEBREFRESH"sv, 0xE7 },
            { "WEB_REFRESH"sv, 0xE7 },
            { "WEBSTOP"sv, 0xE8 },
            { "WEB_STOP"sv, 0xE8 },
            { "WEBFORWARD"sv, 0xE9 },
            { "WEB_FORWARD"sv, 0xE9 },
            { "WEBBACK"sv, 0xEA },
            { "WEB_BACK"sv, 0xEA },
            { "MY_COMPUTER"sv, 0xEB },
            { "MYCOMPUTER"sv, 0xEB },
            { "MAIL"sv, 0xEC },
            { "MEDIASELECT"sv, 0xED },
            { "MEDIA_SELECT"sv, 0xED }
        };

        void MarkSyntheticKeyboardCommand(std::uint8_t scancode, std::uint8_t pendingEvents)
        {
            input::InputModalityTracker::GetSingleton().MarkSyntheticKeyboardScancode(
                scancode,
                pendingEvents,
                kSyntheticHelperSuppressionWindowMs);
        }

        bool EnqueueBridgePress(std::uint8_t scancode)
        {
            if (!KeyboardNativeBridge::GetSingleton().EnqueuePress(scancode)) {
                return false;
            }

            MarkSyntheticKeyboardCommand(scancode, kSyntheticPressPendingEvents);
            return true;
        }

        bool EnqueueBridgeRelease(std::uint8_t scancode)
        {
            if (!KeyboardNativeBridge::GetSingleton().EnqueueRelease(scancode)) {
                return false;
            }

            MarkSyntheticKeyboardCommand(scancode, kSyntheticReleasePendingEvents);
            return true;
        }

        bool EnqueueBridgePulse(std::uint8_t scancode)
        {
            if (!KeyboardNativeBridge::GetSingleton().EnqueuePulse(scancode)) {
                return false;
            }

            MarkSyntheticKeyboardCommand(scancode, kSyntheticPulsePendingEvents);
            return true;
        }

        std::string NormalizeHelperKeyToken(std::string_view token)
        {
            std::string normalized;
            normalized.reserve(token.size());
            for (const auto ch : token) {
                if (std::isalnum(static_cast<unsigned char>(ch))) {
                    normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                    continue;
                }

                if (ch == '-' || ch == '_' || ch == ' ') {
                    normalized.push_back('_');
                }
            }

            return normalized;
        }

        template <std::size_t N>
        std::optional<std::uint8_t> FindHelperKeyScancode(
            std::string_view normalized,
            const HelperKeyEntry (&entries)[N])
        {
            for (const auto& entry : entries) {
                if (entry.token == normalized) {
                    return entry.scancode;
                }
            }

            return std::nullopt;
        }

        std::optional<std::uint8_t> ResolveFunctionKeyPoolScancode(std::string_view token)
        {
            const auto normalized = NormalizeHelperKeyToken(token);
            return FindHelperKeyScancode(normalized, kFunctionKeyPoolEntries);
        }

        std::optional<std::uint8_t> ResolveVirtualKeyPoolScancode(std::string_view token)
        {
            const auto normalized = NormalizeHelperKeyToken(token);
            return FindHelperKeyScancode(normalized, kVirtualKeyPoolEntries);
        }

        std::optional<std::uint8_t> ResolveHelperKeyPoolScancode(std::string_view actionId)
        {
            constexpr auto kVirtualKeyPrefix = "VirtualKey."sv;
            constexpr auto kFKeyPrefix = "FKey."sv;

            if (actionId.starts_with(kFKeyPrefix)) {
                return ResolveFunctionKeyPoolScancode(actionId.substr(kFKeyPrefix.size()));
            }
            if (actionId.starts_with(kVirtualKeyPrefix)) {
                return ResolveVirtualKeyPoolScancode(actionId.substr(kVirtualKeyPrefix.size()));
            }

            return std::nullopt;
        }

        std::filesystem::path ResolveProcessDirectory()
        {
            std::array<wchar_t, MAX_PATH> modulePath{};
            const auto length = ::GetModuleFileNameW(
                nullptr,
                modulePath.data(),
                static_cast<DWORD>(modulePath.size()));
            if (length == 0 || length >= modulePath.size()) {
                return {};
            }

            return std::filesystem::path(modulePath.data()).parent_path();
        }

        std::filesystem::path ResolveGameRootProxyDllPath()
        {
            const auto processDirectory = ResolveProcessDirectory();
            return processDirectory.empty() ? std::filesystem::path{} : processDirectory / L"dinput8.dll";
        }

    }

    KeyboardHelperBackend& KeyboardHelperBackend::GetSingleton()
    {
        static KeyboardHelperBackend instance;
        return instance;
    }

    void KeyboardHelperBackend::Install()
    {
        if (_installed || _attemptedInstall) {
            return;
        }
        _attemptedInstall = true;

        const auto proxyDllPath = ResolveGameRootProxyDllPath();
        if (HasProxyDllInGameRoot()) {
            logger::info(
                "[DualPad][KeyboardHelper] Detected game-root proxy dll at {}; KeyboardHelperBackend will route helper output through the simulated keyboard bridge",
                proxyDllPath.string());
        } else {
            logger::warn(
                "[DualPad][KeyboardHelper] Game-root proxy dll not detected at {}; keyboard helper output is disabled until the optional dinput8 bridge is installed",
                proxyDllPath.empty() ? "<unknown>" : proxyDllPath.string());
        }
        _installed = true;
    }

    bool KeyboardHelperBackend::IsInstalled() const
    {
        return _installed;
    }

    bool KeyboardHelperBackend::HasProxyDllInGameRoot() const
    {
        const auto proxyDllPath = ResolveGameRootProxyDllPath();
        if (proxyDllPath.empty()) {
            return false;
        }

        std::error_code error;
        return std::filesystem::is_regular_file(proxyDllPath, error);
    }

    bool KeyboardHelperBackend::HasActiveBridgeConsumer() const
    {
        return KeyboardNativeBridge::GetSingleton().HasConsumerHeartbeat();
    }

    bool KeyboardHelperBackend::ShouldExposeModEventConfiguration() const
    {
        // TODO: Future MCM should use this gate to hide ModEvent-related UI when
        // the optional game-root dinput8 proxy is not installed.
        return HasProxyDllInGameRoot();
    }

    bool KeyboardHelperBackend::IsModEventTransportReady() const
    {
        return ShouldExposeModEventConfiguration() &&
            HasActiveBridgeConsumer();
    }

    bool KeyboardHelperBackend::IsRouteActive() const
    {
        return _installed && HasProxyDllInGameRoot();
    }

    void KeyboardHelperBackend::Reset()
    {
        {
            std::scoped_lock lock(_mutex);
            _bridgeDesiredRefCounts.fill(0);
            _activeActions.clear();
        }

        if (IsRouteActive()) {
            (void)KeyboardNativeBridge::GetSingleton().EnqueueReset();
        }
    }

    bool KeyboardHelperBackend::CanHandleAction(std::string_view actionId) const
    {
        return ResolveHelperKeyPoolScancode(actionId).has_value();
    }

    bool KeyboardHelperBackend::TriggerAction(
        std::string_view actionId,
        ActionOutputContract contract,
        InputContext context)
    {
        if (!IsRouteActive()) {
            return false;
        }

        const auto scancode = ResolveScancode(actionId, context);
        if (!scancode) {
            return false;
        }

        if (!EnqueueBridgePulse(*scancode)) {
            if (IsDebugLoggingEnabled()) {
                logger::warn(
                    "[DualPad][KeyboardHelper] failed pulse action={} contract={} scancode=0x{:02X} context={}",
                    actionId,
                    ToString(contract),
                    *scancode,
                    ToString(context));
            }
            return false;
        }

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardHelper] pulse action={} contract={} scancode=0x{:02X} context={}",
                actionId,
                ToString(contract),
                *scancode,
                ToString(context));
        }
        return true;
    }

    bool KeyboardHelperBackend::SubmitActionState(
        std::string_view actionId,
        ActionOutputContract contract,
        bool pressed,
        float heldSeconds,
        InputContext context)
    {
        if (!IsRouteActive()) {
            return false;
        }

        if (UsesScheduledPulseContract(contract)) {
            return SubmitScheduledPulseActionState(actionId, contract, pressed, heldSeconds, context);
        }

        if (UsesDebouncedPulse(contract)) {
            if (!pressed) {
                std::scoped_lock lock(_mutex);
                const auto it = _activeActions.find(actionId);
                if (it == _activeActions.end()) {
                    return false;
                }

                _activeActions.erase(it);
                return true;
            }

            {
                std::scoped_lock lock(_mutex);
                if (_activeActions.contains(actionId)) {
                    return true;
                }
            }

            const auto scancode = ResolveScancode(actionId, context);
            if (!scancode) {
                return false;
            }

            if (!EnqueueBridgePulse(*scancode)) {
                if (IsDebugLoggingEnabled()) {
                    logger::warn(
                        "[DualPad][KeyboardHelper] failed source pulse action={} contract={} scancode=0x{:02X} context={}",
                        actionId,
                        ToString(contract),
                        *scancode,
                        ToString(context));
                }
                return false;
            }

            std::scoped_lock lock(_mutex);
            _activeActions[std::string(actionId)] = ActiveKeyboardAction{ *scancode, contract };
            return true;
        }

        if (!UsesContinuousState(contract)) {
            return pressed ? TriggerAction(actionId, contract, context) : true;
        }

        const auto scancode = ResolveScancode(actionId, context);
        if (!scancode) {
            return false;
        }

        std::scoped_lock lock(_mutex);
        if (pressed) {
            if (_activeActions.contains(actionId)) {
                return true;
            }

            const auto previousRefCount = _bridgeDesiredRefCounts[*scancode];
            if (_bridgeDesiredRefCounts[*scancode] != std::numeric_limits<std::uint8_t>::max()) {
                ++_bridgeDesiredRefCounts[*scancode];
            }

            const auto needsPress = previousRefCount == 0;
            if (needsPress && !EnqueueBridgePress(*scancode)) {
                if (_bridgeDesiredRefCounts[*scancode] > 0) {
                    --_bridgeDesiredRefCounts[*scancode];
                }
                return false;
            }

            _activeActions[std::string(actionId)] = ActiveKeyboardAction{ *scancode, contract, true };
            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardHelper] hold down action={} scancode=0x{:02X} refCount={} context={}",
                    actionId,
                    *scancode,
                    _bridgeDesiredRefCounts[*scancode],
                    ToString(context));
            }
            return true;
        }

        const auto it = _activeActions.find(actionId);
        if (it == _activeActions.end()) {
            return false;
        }

        bool released = true;
        if (_bridgeDesiredRefCounts[it->second.scancode] > 0) {
            --_bridgeDesiredRefCounts[it->second.scancode];
            if (_bridgeDesiredRefCounts[it->second.scancode] == 0) {
                released = EnqueueBridgeRelease(it->second.scancode);
                if (!released) {
                    ++_bridgeDesiredRefCounts[it->second.scancode];
                }
            }
        }

        if (!released) {
            return false;
        }

        const auto releasedScancode = it->second.scancode;
        const auto remainingRefCount = _bridgeDesiredRefCounts[releasedScancode];
        _activeActions.erase(it);
        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardHelper] hold up action={} scancode=0x{:02X} refCount={} context={}",
                actionId,
                releasedScancode,
                remainingRefCount,
                ToString(context));
        }
        return true;
    }

    std::optional<std::uint8_t> KeyboardHelperBackend::ResolveScancode(
        std::string_view actionId,
        InputContext context) const
    {
        (void)context;
        return ResolveHelperKeyPoolScancode(actionId);
    }

    bool KeyboardHelperBackend::SubmitScheduledPulseActionState(
        std::string_view actionId,
        ActionOutputContract contract,
        bool pressed,
        float heldSeconds,
        InputContext context)
    {
        const auto scancode = ResolveScancode(actionId, context);
        if (!scancode) {
            return false;
        }

        std::scoped_lock lock(_mutex);
        if (!pressed) {
            const auto it = _activeActions.find(actionId);
            if (it == _activeActions.end()) {
                return false;
            }

            _activeActions.erase(it);
            return true;
        }

        auto [it, inserted] = _activeActions.try_emplace(std::string(actionId));
        auto& action = it->second;
        if (inserted) {
            action.scancode = *scancode;
            action.contract = contract;
        } else {
            action.scancode = *scancode;
            action.contract = contract;
        }

        const bool pressEdge = !action.sourceDown;
        action.sourceDown = true;

        bool queuedPulse = false;
        if (contract == ActionOutputContract::Toggle) {
            if (pressEdge) {
                queuedPulse = EnqueueBridgePulse(*scancode);
                if (!queuedPulse) {
                    return false;
                }
            }
        } else if (contract == ActionOutputContract::Repeat) {
            if (pressEdge) {
                queuedPulse = EnqueueBridgePulse(*scancode);
                if (!queuedPulse) {
                    return false;
                }
                action.nextRepeatAtHeldSeconds = kRepeatInitialDelaySeconds;
            } else {
                while ((heldSeconds + kRepeatScheduleEpsilon) >= action.nextRepeatAtHeldSeconds) {
                    if (!EnqueueBridgePulse(*scancode)) {
                        return false;
                    }
                    queuedPulse = true;
                    action.nextRepeatAtHeldSeconds += kRepeatIntervalSeconds;
                }
            }
        }

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardHelper] scheduled action={} contract={} scancode=0x{:02X} pressEdge={} queued={} held={:.3f} nextRepeatAt={:.3f} context={}",
                actionId,
                ToString(contract),
                action.scancode,
                pressEdge,
                queuedPulse,
                heldSeconds,
                action.nextRepeatAtHeldSeconds,
                ToString(context));
        }
        return true;
    }

    bool KeyboardHelperBackend::IsDebugLoggingEnabled() const
    {
        return RuntimeConfig::GetSingleton().LogKeyboardInjection();
    }
}
