#include "ProxyDirectInputDevice8A.h"

#include "ProxyConfig.h"
#include "ProxyLogger.h"

namespace dualpad::dinput8_proxy
{
    namespace
    {
        // Keyboard-native families have diverged at the process/feel layer:
        // - Jump: one-shot pulse with a minimum down window
        // - Activate: short pulse, but needs a slightly wider down window
        // - Sneak: toggle-like, only light release smoothing
        // - Sprint: held-like, needs a longer release delay to avoid collapsing
        //   back into a too-narrow pulse.
        constexpr std::uint8_t kJumpPulseReleaseExtraGetDeviceDataCalls = 1;
        constexpr std::uint8_t kActivateReleaseExtraGetDeviceDataCalls = 2;
        constexpr std::uint8_t kSneakReleaseExtraGetDeviceDataCalls = 1;
        constexpr std::uint8_t kSprintReleaseExtraGetDeviceDataCalls = 4;

        bool IsWrappedDeviceInterface(REFIID riid)
        {
            return ::IsEqualGUID(riid, IID_IUnknown) ||
                ::IsEqualGUID(riid, IID_IDirectInputDeviceA) ||
                ::IsEqualGUID(riid, IID_IDirectInputDevice2A) ||
                ::IsEqualGUID(riid, IID_IDirectInputDevice7A) ||
                ::IsEqualGUID(riid, IID_IDirectInputDevice8A);
        }

        std::uint8_t GetBridgeReleaseExtraGetDeviceDataCalls(const std::uint8_t scancode)
        {
            switch (scancode) {
            case DIK_E:
                return kActivateReleaseExtraGetDeviceDataCalls;
            case DIK_LCONTROL:
                return kSneakReleaseExtraGetDeviceDataCalls;
            case DIK_LMENU:
                return kSprintReleaseExtraGetDeviceDataCalls;
            case DIK_SPACE:
                return kJumpPulseReleaseExtraGetDeviceDataCalls;
            default:
                return 0;
            }
        }

    }

    ProxyDirectInputDevice8A::ProxyDirectInputDevice8A(IDirectInputDevice8A* realDevice, REFGUID deviceGuid) :
        _realDevice(realDevice),
        _deviceGuid(deviceGuid)
    {}

    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::QueryInterface(REFIID riid, LPVOID* object)
    {
        if (object == nullptr) {
            return E_POINTER;
        }

        if (IsWrappedDeviceInterface(riid)) {
            *object = static_cast<IDirectInputDevice8A*>(this);
            AddRef();
            return S_OK;
        }

        return _realDevice->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE ProxyDirectInputDevice8A::AddRef()
    {
        _realDevice->AddRef();
        return static_cast<ULONG>(++_refs);
    }

    ULONG STDMETHODCALLTYPE ProxyDirectInputDevice8A::Release()
    {
        const auto wrapperRefs = static_cast<ULONG>(--_refs);
        const auto realRefs = _realDevice->Release();
        if (wrapperRefs == 0) {
            delete this;
        }
        return realRefs;
    }

    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetCapabilities(LPDIDEVCAPS caps) { return _realDevice->GetCapabilities(caps); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA callback, LPVOID ref, DWORD flags) { return _realDevice->EnumObjects(callback, ref, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetProperty(REFGUID property, LPDIPROPHEADER header) { return _realDevice->GetProperty(property, header); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::SetProperty(REFGUID property, LPCDIPROPHEADER header) { return _realDevice->SetProperty(property, header); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::Unacquire() { return _realDevice->Unacquire(); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::SetDataFormat(LPCDIDATAFORMAT dataFormat) { return _realDevice->SetDataFormat(dataFormat); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::SetEventNotification(HANDLE eventHandle) { return _realDevice->SetEventNotification(eventHandle); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::SetCooperativeLevel(HWND window, DWORD flags) { return _realDevice->SetCooperativeLevel(window, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA objectInfo, DWORD object, DWORD how) { return _realDevice->GetObjectInfo(objectInfo, object, how); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetDeviceInfo(LPDIDEVICEINSTANCEA deviceInfo) { return _realDevice->GetDeviceInfo(deviceInfo); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::RunControlPanel(HWND owner, DWORD flags) { return _realDevice->RunControlPanel(owner, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::Initialize(HINSTANCE instance, DWORD version, REFGUID guid) { return _realDevice->Initialize(instance, version, guid); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::CreateEffect(REFGUID effectGuid, LPCDIEFFECT effect, LPDIRECTINPUTEFFECT* outEffect, LPUNKNOWN outer) { return _realDevice->CreateEffect(effectGuid, effect, outEffect, outer); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::EnumEffects(LPDIENUMEFFECTSCALLBACKA callback, LPVOID ref, DWORD effectType) { return _realDevice->EnumEffects(callback, ref, effectType); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetEffectInfo(LPDIEFFECTINFOA info, REFGUID effectGuid) { return _realDevice->GetEffectInfo(info, effectGuid); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetForceFeedbackState(LPDWORD outState) { return _realDevice->GetForceFeedbackState(outState); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::SendForceFeedbackCommand(DWORD flags) { return _realDevice->SendForceFeedbackCommand(flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback, LPVOID ref, DWORD flags) { return _realDevice->EnumCreatedEffectObjects(callback, ref, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::Escape(LPDIEFFESCAPE escape) { return _realDevice->Escape(escape); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::SendDeviceData(DWORD objectDataSize, LPCDIDEVICEOBJECTDATA objectData, LPDWORD inOut, DWORD flags) { return _realDevice->SendDeviceData(objectDataSize, objectData, inOut, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::EnumEffectsInFile(LPCSTR fileName, LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID ref, DWORD flags) { return _realDevice->EnumEffectsInFile(fileName, callback, ref, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::WriteEffectToFile(LPCSTR fileName, DWORD entries, LPDIFILEEFFECT fileEffect, DWORD flags) { return _realDevice->WriteEffectToFile(fileName, entries, fileEffect, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::BuildActionMap(LPDIACTIONFORMATA actionFormat, LPCSTR userName, DWORD flags) { return _realDevice->BuildActionMap(actionFormat, userName, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::SetActionMap(LPDIACTIONFORMATA actionFormat, LPCSTR userName, DWORD flags) { return _realDevice->SetActionMap(actionFormat, userName, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA imageInfo) { return _realDevice->GetImageInfo(imageInfo); }

    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::Acquire()
    {
        const auto result = _realDevice->Acquire();
        if (ShouldLog(_acquireLogCount, 8)) {
            Logf(
                "Device::Acquire guid={} device=0x{:X} result={}",
                GuidToString(_deviceGuid),
                reinterpret_cast<std::uintptr_t>(_realDevice),
                HResultToString(result));
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetDeviceState(DWORD dataSize, LPVOID data)
    {
        input::backend::KeyboardNativeBridge::GetSingleton().TouchConsumerHeartbeat();
        const auto bridgeConsumed = ConsumeBridgeCommands();
        const auto result = _realDevice->GetDeviceState(dataSize, data);
        if (result == DI_OK && data != nullptr && dataSize != 0) {
            ApplyBridgeStateToDeviceState(dataSize, data);
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        const auto focus = (result == DI_OK && bytes != nullptr && dataSize > kFocusScancode) ?
            bytes[kFocusScancode] :
            0;
        if (bridgeConsumed != 0 || focus != 0 || ShouldLog(_getDeviceStateLogCount, 16)) {
            Logf(
                "Device::GetDeviceState guid={} device=0x{:X} result={} dataSize={} focus[0x39]=0x{:02X} bridgeConsumed={}",
                GuidToString(_deviceGuid),
                reinterpret_cast<std::uintptr_t>(_realDevice),
                HResultToString(result),
                dataSize,
                focus,
                bridgeConsumed);
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::GetDeviceData(
        DWORD objectDataSize,
        LPDIDEVICEOBJECTDATA objectData,
        LPDWORD inOut,
        DWORD flags)
    {
        const DWORD requested = inOut ? *inOut : 0;
        input::backend::KeyboardNativeBridge::GetSingleton().TouchConsumerHeartbeat();
        const auto result = _realDevice->GetDeviceData(objectDataSize, objectData, inOut, flags);
        DWORD returned = inOut ? *inOut : 0;

        const auto bridgePendingBefore = GetBridgePendingCount();
        std::size_t bridgeConsumed = 0;
        std::size_t bridgeAppended = 0;
        if (result == DI_OK &&
            objectData != nullptr &&
            objectDataSize == sizeof(DIDEVICEOBJECTDATA) &&
            inOut != nullptr) {
            bridgeConsumed = ConsumeBridgeCommands();
            bridgeAppended = AppendBridgePendingEvents(objectData, requested, returned);
            PromoteBridgeDeferredEvents();
            *inOut = returned;
        }
        const auto bridgePendingAfter = GetBridgePendingCount();

        const bool shouldLog =
            returned != 0 ||
            bridgeConsumed != 0 ||
            bridgeAppended != 0 ||
            ShouldLog(_getDeviceDataLogCount, 24);
        if (shouldLog) {
            Logf(
                "Device::GetDeviceData guid={} device=0x{:X} result={} cb={} requested={} returned={} flags=0x{:X} bridgeConsumed={} bridgeAppended={} bridgePendingBefore={} bridgePendingAfter={}",
                GuidToString(_deviceGuid),
                reinterpret_cast<std::uintptr_t>(_realDevice),
                HResultToString(result),
                objectDataSize,
                requested,
                returned,
                flags,
                bridgeConsumed,
                bridgeAppended,
                bridgePendingBefore,
                bridgePendingAfter);
            if (result == DI_OK && objectData != nullptr && returned != 0) {
                LogGetDeviceDataResult(objectDataSize, objectData, returned, flags);
            }
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE ProxyDirectInputDevice8A::Poll()
    {
        const auto result = _realDevice->Poll();
        if (ShouldLog(_pollLogCount, 8)) {
            Logf(
                "Device::Poll guid={} device=0x{:X} result={}",
                GuidToString(_deviceGuid),
                reinterpret_cast<std::uintptr_t>(_realDevice),
                HResultToString(result));
        }
        return result;
    }

    bool ProxyDirectInputDevice8A::ShouldLog(std::atomic_uint32_t& counter, std::uint32_t budget) const
    {
        const auto previous = counter.fetch_add(1);
        return previous < budget;
    }

    DIDEVICEOBJECTDATA ProxyDirectInputDevice8A::BuildBridgeEvent(std::uint8_t scancode, bool pressed) const
    {
        DIDEVICEOBJECTDATA event{};
        event.dwOfs = scancode;
        event.dwData = pressed ? 0x80u : 0x00u;
        event.dwTimeStamp = 0;
        event.dwSequence = 0;
        event.uAppData = 0;
        return event;
    }

    void ProxyDirectInputDevice8A::QueueBridgePendingEvent(const DIDEVICEOBJECTDATA& event)
    {
        std::scoped_lock lock(_bridgePendingMutex);
        if (_bridgePendingCount >= _bridgePendingEvents.size()) {
            for (std::uint32_t i = 1; i < _bridgePendingCount; ++i) {
                _bridgePendingEvents[i - 1] = _bridgePendingEvents[i];
            }
            _bridgePendingCount = static_cast<std::uint32_t>(_bridgePendingEvents.size() - 1);
        }

        _bridgePendingEvents[_bridgePendingCount] = event;
        ++_bridgePendingCount;
    }

    void ProxyDirectInputDevice8A::QueueBridgeDeferredEvent(
        const DIDEVICEOBJECTDATA& event,
        const std::uint8_t extraGetDeviceDataCalls)
    {
        std::scoped_lock lock(_bridgePendingMutex);
        if (_bridgeDeferredCount >= _bridgeDeferredEvents.size()) {
            for (std::uint32_t i = 1; i < _bridgeDeferredCount; ++i) {
                _bridgeDeferredEvents[i - 1] = _bridgeDeferredEvents[i];
            }
            _bridgeDeferredCount = static_cast<std::uint32_t>(_bridgeDeferredEvents.size() - 1);
        }

        _bridgeDeferredEvents[_bridgeDeferredCount] = DeferredBridgeEvent{
            event,
            extraGetDeviceDataCalls
        };
        ++_bridgeDeferredCount;
    }

    void ProxyDirectInputDevice8A::ClearBridgePendingEvents()
    {
        std::scoped_lock lock(_bridgePendingMutex);
        _bridgePendingCount = 0;
        _bridgeDeferredCount = 0;
    }

    void ProxyDirectInputDevice8A::PromoteBridgeDeferredEvents()
    {
        std::scoped_lock lock(_bridgePendingMutex);
        std::uint32_t writeIndex = 0;
        for (std::uint32_t readIndex = 0; readIndex < _bridgeDeferredCount; ++readIndex) {
            auto& deferred = _bridgeDeferredEvents[readIndex];
            if (deferred.remainingGetDeviceDataCalls != 0) {
                --deferred.remainingGetDeviceDataCalls;
                _bridgeDeferredEvents[writeIndex++] = deferred;
                continue;
            }

            if (_bridgePendingCount < _bridgePendingEvents.size()) {
                _bridgePendingEvents[_bridgePendingCount++] = deferred.event;
                continue;
            }

            _bridgeDeferredEvents[writeIndex++] = deferred;
        }
        _bridgeDeferredCount = writeIndex;
    }

    std::uint32_t ProxyDirectInputDevice8A::GetBridgePendingCount() const
    {
        std::scoped_lock lock(_bridgePendingMutex);
        return _bridgePendingCount;
    }

    std::size_t ProxyDirectInputDevice8A::ConsumeBridgeCommands()
    {
        std::array<input::backend::KeyboardBridgeCommand, 16> commands{};
        const auto commandCount = input::backend::KeyboardNativeBridge::GetSingleton().ConsumeCommands(
            commands.data(),
            commands.size());
        if (commandCount == 0) {
            return 0;
        }

        const auto queueBridgeRecord = [this](DWORD ofs, bool pressed) {
            DIDEVICEOBJECTDATA event{};
            event.dwOfs = ofs;
            event.dwData = pressed ? 0x80u : 0x00u;
            event.dwTimeStamp = 0;
            event.dwSequence = 0;
            event.uAppData = 0;
            QueueBridgePendingEvent(event);
        };

        for (std::size_t i = 0; i < commandCount; ++i) {
            const auto& command = commands[i];
            const auto scancode = static_cast<std::size_t>(command.scancode);
            switch (command.type) {
            case input::backend::KeyboardBridgeCommandType::Press:
                if (scancode < _bridgeLatchedDown.size() && !_bridgeLatchedDown[scancode]) {
                    queueBridgeRecord(command.scancode, true);
                    _bridgeLatchedDown[scancode] = true;
                }
                break;
            case input::backend::KeyboardBridgeCommandType::Release:
                if (scancode < _bridgeLatchedDown.size() && _bridgeLatchedDown[scancode]) {
                    const auto extraGetDeviceDataCalls =
                        GetBridgeReleaseExtraGetDeviceDataCalls(command.scancode);
                    if (extraGetDeviceDataCalls != 0) {
                        QueueBridgeDeferredEvent(
                            BuildBridgeEvent(command.scancode, false),
                            extraGetDeviceDataCalls);
                    }
                    else {
                        queueBridgeRecord(command.scancode, false);
                        _bridgeLatchedDown[scancode] = false;
                    }
                }
                break;
            case input::backend::KeyboardBridgeCommandType::Pulse:
                if (scancode < _bridgeLatchedDown.size()) {
                    QueueBridgePendingEvent(BuildBridgeEvent(command.scancode, true));
                    const auto extraGetDeviceDataCalls =
                        GetBridgeReleaseExtraGetDeviceDataCalls(command.scancode);
                    QueueBridgeDeferredEvent(
                        BuildBridgeEvent(command.scancode, false),
                        extraGetDeviceDataCalls);
                    _bridgeLatchedDown[scancode] = true;
                }
                break;
            case input::backend::KeyboardBridgeCommandType::Reset:
                ClearBridgePendingEvents();
                for (std::size_t latchedIndex = 0; latchedIndex < _bridgeLatchedDown.size(); ++latchedIndex) {
                    if (!_bridgeLatchedDown[latchedIndex]) {
                        continue;
                    }

                    queueBridgeRecord(static_cast<DWORD>(latchedIndex), false);
                    _bridgeLatchedDown[latchedIndex] = false;
                }
                break;
            case input::backend::KeyboardBridgeCommandType::None:
            default:
                break;
            }
        }

        return commandCount;
    }

    void ProxyDirectInputDevice8A::ApplyBridgeStateToDeviceState(DWORD dataSize, void* data) const
    {
        if (data == nullptr || dataSize == 0) {
            return;
        }

        auto* bytes = static_cast<std::uint8_t*>(data);
        std::scoped_lock lock(_bridgePendingMutex);
        const auto limit = (std::min)(static_cast<std::size_t>(dataSize), _bridgeLatchedDown.size());
        for (std::size_t i = 0; i < limit; ++i) {
            if (_bridgeLatchedDown[i]) {
                bytes[i] = 0x80;
            }
        }
    }

    std::size_t ProxyDirectInputDevice8A::AppendBridgePendingEvents(
        DIDEVICEOBJECTDATA* objectData,
        DWORD requested,
        DWORD& returned)
    {
        if (objectData == nullptr || requested == 0 || returned >= requested) {
            return 0;
        }

        std::scoped_lock lock(_bridgePendingMutex);
        std::size_t appended = 0;
        while (_bridgePendingCount != 0 && returned < requested) {
            objectData[returned] = _bridgePendingEvents[0];
            if (objectData[returned].dwOfs < _bridgeLatchedDown.size() &&
                objectData[returned].dwData == 0x00u) {
                _bridgeLatchedDown[objectData[returned].dwOfs] = false;
            }
            for (std::uint32_t i = 1; i < _bridgePendingCount; ++i) {
                _bridgePendingEvents[i - 1] = _bridgePendingEvents[i];
            }
            --_bridgePendingCount;
            ++returned;
            ++appended;
        }

        return appended;
    }

    void ProxyDirectInputDevice8A::LogGetDeviceDataResult(
        DWORD objectDataSize,
        const DIDEVICEOBJECTDATA* objectData,
        DWORD count,
        DWORD flags) const
    {
        const auto& config = GetProxyConfig();
        const auto limit = (std::min)(count, static_cast<DWORD>(6));
        for (DWORD i = 0; i < limit; ++i) {
            const auto& item = objectData[i];
            if (config.logOnlyInteresting && !IsInterestingKeyboardData(item)) {
                continue;
            }
            Logf(
                "  data[{}] ofs=0x{:X} data=0x{:X} ts={} seq={} app=0x{:X} interesting={}",
                i,
                item.dwOfs,
                item.dwData,
                item.dwTimeStamp,
                item.dwSequence,
                static_cast<std::uintptr_t>(item.uAppData),
                IsInterestingKeyboardData(item));
        }

        if (count > limit) {
            Logf(
                "  data truncated count={} shown={} cb={} flags=0x{:X}",
                count,
                limit,
                objectDataSize,
                flags);
        }
    }
}
