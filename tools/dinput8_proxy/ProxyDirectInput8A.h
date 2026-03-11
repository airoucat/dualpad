#pragma once

#include "Common.h"

namespace dualpad::dinput8_proxy
{
    class ProxyDirectInput8A final : public IDirectInput8A
    {
    public:
        explicit ProxyDirectInput8A(IDirectInput8A* realInput);

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, LPDIRECTINPUTDEVICE8A* outDevice, LPUNKNOWN outer) override;
        HRESULT STDMETHODCALLTYPE EnumDevices(DWORD devType, LPDIENUMDEVICESCALLBACKA callback, LPVOID ref, DWORD flags) override;
        HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID guidInstance) override;
        HRESULT STDMETHODCALLTYPE RunControlPanel(HWND owner, DWORD flags) override;
        HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version) override;
        HRESULT STDMETHODCALLTYPE FindDevice(REFGUID guidClass, LPCSTR name, LPGUID guidInstance) override;
        HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(
            LPCSTR userName,
            LPDIACTIONFORMATA actionFormat,
            LPDIENUMDEVICESBYSEMANTICSCBA callback,
            LPVOID ref,
            DWORD flags) override;
        HRESULT STDMETHODCALLTYPE ConfigureDevices(
            LPDICONFIGUREDEVICESCALLBACK callback,
            LPDICONFIGUREDEVICESPARAMSA params,
            DWORD flags,
            LPVOID ref) override;

    private:
        std::atomic_ulong _refs{ 1 };
        IDirectInput8A* _realInput{ nullptr };
    };
}
