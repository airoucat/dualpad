#pragma once

#include "Common.h"
#include "input/backend/KeyboardNativeBridge.h"

namespace dualpad::dinput8_proxy
{
    class ProxyDirectInputDevice8A final : public IDirectInputDevice8A
    {
    public:
        ProxyDirectInputDevice8A(IDirectInputDevice8A* realDevice, REFGUID deviceGuid);

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override;
        HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA callback, LPVOID ref, DWORD flags) override;
        HRESULT STDMETHODCALLTYPE GetProperty(REFGUID property, LPDIPROPHEADER header) override;
        HRESULT STDMETHODCALLTYPE SetProperty(REFGUID property, LPCDIPROPHEADER header) override;
        HRESULT STDMETHODCALLTYPE Acquire() override;
        HRESULT STDMETHODCALLTYPE Unacquire() override;
        HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD dataSize, LPVOID data) override;
        HRESULT STDMETHODCALLTYPE GetDeviceData(
            DWORD objectDataSize,
            LPDIDEVICEOBJECTDATA objectData,
            LPDWORD inOut,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT dataFormat) override;
        HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE eventHandle) override;
        HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD flags) override;
        HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA objectInfo, DWORD object, DWORD how) override;
        HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA deviceInfo) override;
        HRESULT STDMETHODCALLTYPE RunControlPanel(HWND owner, DWORD flags) override;
        HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version, REFGUID guid) override;
        HRESULT STDMETHODCALLTYPE CreateEffect(
            REFGUID effectGuid,
            LPCDIEFFECT effect,
            LPDIRECTINPUTEFFECT* outEffect,
            LPUNKNOWN outer) override;
        HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA callback, LPVOID ref, DWORD effectType) override;
        HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA info, REFGUID effectGuid) override;
        HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD outState) override;
        HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD flags) override;
        HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(
            LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback,
            LPVOID ref,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE escape) override;
        HRESULT STDMETHODCALLTYPE Poll() override;
        HRESULT STDMETHODCALLTYPE SendDeviceData(
            DWORD objectDataSize,
            LPCDIDEVICEOBJECTDATA objectData,
            LPDWORD inOut,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE EnumEffectsInFile(
            LPCSTR fileName,
            LPDIENUMEFFECTSINFILECALLBACK callback,
            LPVOID ref,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE WriteEffectToFile(
            LPCSTR fileName,
            DWORD entries,
            LPDIFILEEFFECT fileEffect,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE BuildActionMap(
            LPDIACTIONFORMATA actionFormat,
            LPCSTR userName,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE SetActionMap(
            LPDIACTIONFORMATA actionFormat,
            LPCSTR userName,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA imageInfo) override;

    private:
        struct DeferredBridgeEvent
        {
            DIDEVICEOBJECTDATA event{};
            std::uint8_t remainingGetDeviceDataCalls{ 0 };
        };

        bool ShouldLog(std::atomic_uint32_t& counter, std::uint32_t budget) const;
        void LogGetDeviceDataResult(
            DWORD objectDataSize,
            const DIDEVICEOBJECTDATA* objectData,
            DWORD count,
            DWORD flags) const;
        DIDEVICEOBJECTDATA BuildBridgeEvent(std::uint8_t scancode, bool pressed) const;
        void ApplyBridgeStateToDeviceState(DWORD dataSize, void* data) const;
        void QueueBridgePendingEvent(const DIDEVICEOBJECTDATA& event);
        void QueueBridgeDeferredEvent(const DIDEVICEOBJECTDATA& event, std::uint8_t extraGetDeviceDataCalls);
        void ClearBridgePendingEvents();
        void PromoteBridgeDeferredEvents();
        std::uint32_t GetBridgePendingCount() const;
        std::size_t ConsumeBridgeCommands();
        std::size_t AppendBridgePendingEvents(
            DIDEVICEOBJECTDATA* objectData,
            DWORD requested,
            DWORD& returned);

        std::atomic_ulong _refs{ 1 };
        IDirectInputDevice8A* _realDevice{ nullptr };
        GUID _deviceGuid{};
        mutable std::atomic_uint32_t _acquireLogCount{ 0 };
        mutable std::atomic_uint32_t _pollLogCount{ 0 };
        mutable std::atomic_uint32_t _getDeviceDataLogCount{ 0 };
        mutable std::atomic_uint32_t _getDeviceStateLogCount{ 0 };
        mutable std::atomic_uint32_t _bridgeSequence{ 1 };
        mutable std::mutex _bridgePendingMutex;
        std::array<DIDEVICEOBJECTDATA, 32> _bridgePendingEvents{};
        std::uint32_t _bridgePendingCount{ 0 };
        std::array<DeferredBridgeEvent, 32> _bridgeDeferredEvents{};
        std::uint32_t _bridgeDeferredCount{ 0 };
        std::array<bool, 256> _bridgeLatchedDown{};
    };
}
