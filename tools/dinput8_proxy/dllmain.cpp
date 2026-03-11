#include "ProxyDirectInput8A.h"
#include "ProxyLogger.h"
#include "SystemDInput8Loader.h"

HRESULT WINAPI DirectInput8Create(
    HINSTANCE instance,
    DWORD version,
    REFIID riid,
    LPVOID* output,
    LPUNKNOWN outer)
{
    using namespace dualpad::dinput8_proxy;

    const auto realOutput = output && *output ? reinterpret_cast<std::uintptr_t>(*output) : 0;
    const auto result = CallRealDirectInput8Create(instance, version, riid, output, outer);
    Logf(
        "DirectInput8Create version=0x{:X} riid={} result={} output=0x{:X}",
        version,
        GuidToString(riid),
        HResultToString(result),
        output && *output ? reinterpret_cast<std::uintptr_t>(*output) : realOutput);

    if (FAILED(result) || output == nullptr || *output == nullptr) {
        return result;
    }

    if (!::IsEqualGUID(riid, IID_IDirectInput8A)) {
        return result;
    }

    auto* realInput = static_cast<IDirectInput8A*>(*output);
    auto* wrapped = new (std::nothrow) ProxyDirectInput8A(realInput);
    if (wrapped == nullptr) {
        LogLine("DirectInput8Create wrap allocation failed; returning real interface");
        return result;
    }

    *output = wrapped;
    Logf(
        "Wrapped IDirectInput8A real=0x{:X} wrapper=0x{:X}",
        reinterpret_cast<std::uintptr_t>(realInput),
        reinterpret_cast<std::uintptr_t>(wrapped));
    return result;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        ::DisableThreadLibraryCalls(module);
        dualpad::dinput8_proxy::InitializeProxyLogger(module);
    }
    return TRUE;
}
