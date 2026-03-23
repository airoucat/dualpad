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

        std::optional<std::uint8_t> ResolveFunctionKeyPoolScancode(std::string_view token)
        {
            const auto normalized = NormalizeHelperKeyToken(token);
            if (normalized == "F13"sv) {
                return static_cast<std::uint8_t>(0x64);
            }
            if (normalized == "F14"sv) {
                return static_cast<std::uint8_t>(0x65);
            }
            if (normalized == "F15"sv) {
                return static_cast<std::uint8_t>(0x66);
            }

            return std::nullopt;
        }

        std::optional<std::uint8_t> ResolveVirtualKeyPoolScancode(std::string_view token)
        {
            const auto normalized = NormalizeHelperKeyToken(token);

            if (normalized == "DIK_F13"sv) {
                return static_cast<std::uint8_t>(0x64);
            }
            if (normalized == "DIK_F14"sv) {
                return static_cast<std::uint8_t>(0x65);
            }
            if (normalized == "DIK_F15"sv) {
                return static_cast<std::uint8_t>(0x66);
            }
            if (normalized == "DIK_KANA"sv || normalized == "KANA"sv) {
                return static_cast<std::uint8_t>(0x70);
            }
            if (normalized == "DIK_ABNT_C1"sv || normalized == "ABNT_C1"sv) {
                return static_cast<std::uint8_t>(0x73);
            }
            if (normalized == "DIK_CONVERT"sv || normalized == "CONVERT"sv) {
                return static_cast<std::uint8_t>(0x79);
            }
            if (normalized == "DIK_NOCONVERT"sv || normalized == "NO_CONVERT"sv || normalized == "NOCONVERT"sv) {
                return static_cast<std::uint8_t>(0x7B);
            }
            if (normalized == "DIK_ABNT_C2"sv || normalized == "ABNT_C2"sv) {
                return static_cast<std::uint8_t>(0x7E);
            }
            if (normalized == "NUMPADEQUAL"sv || normalized == "NUMPAD_EQUAL"sv) {
                return static_cast<std::uint8_t>(0x8D);
            }
            if (normalized == "PRINTSRC"sv || normalized == "PRINT_SRC"sv) {
                return static_cast<std::uint8_t>(0xB7);
            }
            if (normalized == "L_WINDOWS"sv || normalized == "LWINDOWS"sv) {
                return static_cast<std::uint8_t>(0xDB);
            }
            if (normalized == "R_WINDOWS"sv || normalized == "RWINDOWS"sv) {
                return static_cast<std::uint8_t>(0xDC);
            }
            if (normalized == "APPS"sv) {
                return static_cast<std::uint8_t>(0xDD);
            }
            if (normalized == "POWER"sv) {
                return static_cast<std::uint8_t>(0xDE);
            }
            if (normalized == "SLEEP"sv) {
                return static_cast<std::uint8_t>(0xDF);
            }
            if (normalized == "WAKE"sv) {
                return static_cast<std::uint8_t>(0xE3);
            }
            if (normalized == "WEBSEARCH"sv || normalized == "WEB_SEARCH"sv) {
                return static_cast<std::uint8_t>(0xE5);
            }
            if (normalized == "WEBFAVORITES"sv || normalized == "WEB_FAVORITES"sv) {
                return static_cast<std::uint8_t>(0xE6);
            }
            if (normalized == "WEBREFRESH"sv || normalized == "WEB_REFRESH"sv) {
                return static_cast<std::uint8_t>(0xE7);
            }
            if (normalized == "WEBSTOP"sv || normalized == "WEB_STOP"sv) {
                return static_cast<std::uint8_t>(0xE8);
            }
            if (normalized == "WEBFORWARD"sv || normalized == "WEB_FORWARD"sv) {
                return static_cast<std::uint8_t>(0xE9);
            }
            if (normalized == "WEBBACK"sv || normalized == "WEB_BACK"sv) {
                return static_cast<std::uint8_t>(0xEA);
            }
            if (normalized == "MY_COMPUTER"sv || normalized == "MYCOMPUTER"sv) {
                return static_cast<std::uint8_t>(0xEB);
            }
            if (normalized == "MAIL"sv) {
                return static_cast<std::uint8_t>(0xEC);
            }
            if (normalized == "MEDIASELECT"sv || normalized == "MEDIA_SELECT"sv) {
                return static_cast<std::uint8_t>(0xED);
            }

            return std::nullopt;
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
