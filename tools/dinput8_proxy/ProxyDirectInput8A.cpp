#include "ProxyDirectInput8A.h"

#include "ProxyDirectInputDevice8A.h"
#include "ProxyLogger.h"

namespace dualpad::dinput8_proxy
{
    ProxyDirectInput8A::ProxyDirectInput8A(IDirectInput8A* realInput) :
        _realInput(realInput)
    {}

    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::QueryInterface(REFIID riid, LPVOID* object)
    {
        if (object == nullptr) {
            return E_POINTER;
        }

        if (::IsEqualGUID(riid, IID_IUnknown) || ::IsEqualGUID(riid, IID_IDirectInput8A)) {
            *object = static_cast<IDirectInput8A*>(this);
            AddRef();
            return S_OK;
        }

        return _realInput->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE ProxyDirectInput8A::AddRef()
    {
        _realInput->AddRef();
        return static_cast<ULONG>(++_refs);
    }

    ULONG STDMETHODCALLTYPE ProxyDirectInput8A::Release()
    {
        const auto wrapperRefs = static_cast<ULONG>(--_refs);
        const auto realRefs = _realInput->Release();
        if (wrapperRefs == 0) {
            delete this;
        }
        return realRefs;
    }

    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::CreateDevice(
        REFGUID guid,
        LPDIRECTINPUTDEVICE8A* outDevice,
        LPUNKNOWN outer)
    {
        const auto result = _realInput->CreateDevice(guid, outDevice, outer);
        Logf(
            "DirectInput8::CreateDevice guid={} result={} device=0x{:X}",
            GuidToString(guid),
            HResultToString(result),
            outDevice && *outDevice ? reinterpret_cast<std::uintptr_t>(*outDevice) : 0);

        if (FAILED(result) || outDevice == nullptr || *outDevice == nullptr) {
            return result;
        }

        if (!IsKeyboardGuid(guid)) {
            return result;
        }

        auto* wrapped = new (std::nothrow) ProxyDirectInputDevice8A(*outDevice, guid);
        if (wrapped == nullptr) {
            LogLine("CreateDevice keyboard wrap allocation failed; returning real device");
            return result;
        }

        auto* realDevice = *outDevice;
        *outDevice = wrapped;
        Logf(
            "Wrapped keyboard device guid={} real=0x{:X} wrapper=0x{:X}",
            GuidToString(guid),
            reinterpret_cast<std::uintptr_t>(realDevice),
            reinterpret_cast<std::uintptr_t>(*outDevice));
        return result;
    }

    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::EnumDevices(DWORD devType, LPDIENUMDEVICESCALLBACKA callback, LPVOID ref, DWORD flags) { return _realInput->EnumDevices(devType, callback, ref, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::GetDeviceStatus(REFGUID guidInstance) { return _realInput->GetDeviceStatus(guidInstance); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::RunControlPanel(HWND owner, DWORD flags) { return _realInput->RunControlPanel(owner, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::Initialize(HINSTANCE instance, DWORD version) { return _realInput->Initialize(instance, version); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::FindDevice(REFGUID guidClass, LPCSTR name, LPGUID guidInstance) { return _realInput->FindDevice(guidClass, name, guidInstance); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::EnumDevicesBySemantics(LPCSTR userName, LPDIACTIONFORMATA actionFormat, LPDIENUMDEVICESBYSEMANTICSCBA callback, LPVOID ref, DWORD flags) { return _realInput->EnumDevicesBySemantics(userName, actionFormat, callback, ref, flags); }
    HRESULT STDMETHODCALLTYPE ProxyDirectInput8A::ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK callback, LPDICONFIGUREDEVICESPARAMSA params, DWORD flags, LPVOID ref) { return _realInput->ConfigureDevices(callback, params, flags, ref); }
}
