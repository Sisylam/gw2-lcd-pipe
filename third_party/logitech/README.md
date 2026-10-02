# third_party/logitech

This folder holds the **genuine Logitech binaries** that the project depends on
at runtime. They are **not committed to the repository** (see `.gitignore`)
because they are proprietary, copyrighted software:

> `LgLcdApi.dll` — © 2009-2010 Logitech. All rights reserved.
> `LogitechLcd.dll` — © 2004-2022 Logitech. All rights reserved.

They are redistributed with Logitech Gaming Software / the Logitech LCD SDK,
not by this project.

## Files

| File | Role |
|------|------|
| `LgLcdApi.dll` (x64) | The real client DLL. `lcdproxy\LgLcdApiProxy.dll` loads this and returns its `GetInterface` table unchanged, hooking only the pipe I/O. |
| `LogitechLcd.dll` (x64) | The stock server binary for the legacy `LogiLcd*` API; kept so the `shim\` variant can be restored. |

## How to populate this folder

Copy the two x64 DLLs from an installed Logitech Gaming Software:

```
C:\Program Files\Logitech Gaming Software\SDK\LCD\x64\LgLcdApi.dll
C:\Program Files\Logitech Gaming Software\SDK\LCD\x64\LogitechLcd.dll
```

Or run, from the repository root:

```powershell
$sdk = 'C:\Program Files\Logitech Gaming Software\SDK\LCD\x64'
Copy-Item "$sdk\LgLcdApi.dll"      third_party\logitech\ -Force
Copy-Item "$sdk\LogitechLcd.dll"   third_party\logitech\ -Force
```

## How it is referenced

`lcdproxy\lcdproxy.c` resolves `LgLcdApi.dll` **relative to the proxy DLL**:

```
lcdproxy\LgLcdApiProxy.dll  ->  ..\third_party\logitech\LgLcdApi.dll
```

If that local copy is missing it falls back to the stock Program Files path, so
the proxy still works on a machine where the SDK is installed normally.
