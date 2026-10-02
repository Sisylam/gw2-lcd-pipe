# clean/ - from-scratch client DLL

`LgLcdApi.clean.dll` is a clean-room implementation of the Logitech LCD client
API that Guild Wars 2 imports. It **replaces `LgLcdApi.dll` and `LCore.exe`
entirely**: GW2 loads it (through the Logitech LCD CLSID), calls
`GetInterface(5)`, and receives a function table we own. No Logitech binary, no
named pipe, no server process.

## How it works

GW2 uses only five of the 29 interface slots (recovered empirically; see the
"Client API surface" section of `docs/PROTOCOL.md`):

| Slot | Function | What ours does |
|-----:|----------|----------------|
| 0 | `lgLcdInit` | creates the shim and control sections |
| 4 | `lgLcdConnectEx` | stores the `onNotify` callback, then delivers a `DEVICE_ARRIVAL` notification (`notifyParm1 = QVGA`) |
| 26 | `lgLcdOpen` / `lgLcdOpenByType` | stores the `onSoftbuttonsChanged` callback, returns device handle `101` |
| 13 | `lgLcdUpdateBitmap` | publishes the frame to `Local\GW2LCDShim` |
| 24 | `lgLcdSetAsLCDForegroundApp` | state only |

The other 24 slots return `ERROR_SUCCESS`.

Two details were needed to make GW2 happy:

1. **Device-arrival notification.** After `connect`, GW2 does not open the
   device until the client delivers `LGLCD_NOTIFICATION_DEVICE_ARRIVAL` with
   `notifyParm1 = LGLCD_DEVICE_QVGA` through the `onNotify` callback. The real
   client gets this from `LCore`; ours synthesises it.
2. **Press/release gap.** GW2's `onSoftbuttonsChanged` handler needs the press
   and the release separated in time. Firing them back-to-back makes GW2 miss
   the press, so the worker thread sleeps 60 ms between them.

A worker thread reads button requests from `Local\LGLCDCtl` (written by the
viewer) and invokes GW2's callback. The API shape follows the public SDK header
`lglcd.h`; the slot indices and the context/notification layouts were recovered
by observation, not copied from Logitech code.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File clean\build.ps1          # release
powershell -ExecutionPolicy Bypass -File clean\build.ps1 -Debug   # + %TEMP%\cleanapi.log
```

## Register

Point the Logitech LCD CLSID at the DLL (needs an elevated shell):

```
HKLM\SOFTWARE\Classes\CLSID\{FE750200-B72E-11d9-829B-0050DA1A72D3}\ServerBinary
    -> ...\clean\LgLcdApi.clean.dll
```

This is the same key `lcdproxy\registry.ps1` uses for the proxy; use one or the
other, not both.
