# DualPad dinput8 Proxy

This folder contains a separate research target for a `dinput8.dll` proxy.

Current scope:

- export `DirectInput8Create`
- load the real system `dinput8.dll`
- wrap `IDirectInput8A`
- wrap only keyboard `IDirectInputDevice8A`
- log `Acquire`, `Poll`, `GetDeviceState`, and `GetDeviceData`
- consume the formal shared keyboard bridge from `DualPad.dll`
- append bridge-owned `DIDEVICEOBJECTDATA` at the keyboard `GetDeviceData`
  return boundary
- mirror bridge-owned pressed state into `GetDeviceState`

Build:

```powershell
xmake build DualPadDInput8Proxy
```

Output:

- `G:/g/SkyrimSE/dinput8.dll`
- log file next to the proxy DLL: `G:/g/SkyrimSE/DualPadDInput8.log`
- config next to the proxy DLL: `G:/g/SkyrimSE/DualPadDInput8.ini`

This target is intentionally separate from the main SKSE plugin so the proxy
code stays easy to review and can be published as the formal keyboard-native
bridge layer.
