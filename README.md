# GW2 LCD out of LCore

Replace the obsolete `LCore.exe` (Logitech Gaming Software) as the server for
Guild Wars 2's 320x240 LCD stream, so Microsoft Defender's memory-integrity
(HVCI) can be enabled. GW2 still renders the LCD; this project speaks the
`\\.\pipe\LGLCDPIPE-00000001` protocol on the other end.

The native stream is published to the shared memory section `Local\GW2LCDShim`
(320x240, 24bpp RGB) for viewing or casting to other displays (ESP32, phone,
monitor, ...).

## Status

- Replacement server negotiates with GW2, receives its 307224-byte frames, and
  answers the pipe exactly as LCore did.
- The six G19S soft buttons work: `[<]` / `[>]` cycle GW2's LCD views, driven
  from the viewer's on-screen buttons.
- Next: uninstall LGS and enable/test HVCI.

See `docs/PROTOCOL.md` for the full protocol notes, history and open items.

## Layout

| Path | Role |
|------|------|
| `lcdproxy/` | The replacement named-pipe server (`lgpipe_server.c`) and the passive proxy DLL (`lcdproxy.c`) that taps GW2's frames. |
| `viewer/` | **Native Rust viewer** (`gw2lcd-viewer.exe`): shows `Local\GW2LCDShim` and provides the six soft buttons. Replaces the Python viewer. |
| `shim/` | Alternative approach: a shim for the legacy `LogiLcd*` API. |
| `lcdshim.py` | Python shared-memory reader, used only by `shim/test_shim.py`. |
| `docs/PROTOCOL.md` | Full protocol notes, reverse-engineering history and open items. |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | MIT license and third-party attributions. |
| `third_party/logitech/` | Genuine Logitech DLLs the project depends on. **Not committed** — see its README. |
| `reference/` | Screenshots of GW2's LCD screens used during analysis. |
| `cache/`, `.env` | Local API cache and secrets. **Not committed.** |

## Required Logitech files

The project ships **no** Logitech code. Two genuine Logitech DLLs must be
copied into `third_party/logitech/` (gitignored) before running:

| File (x64) | Why it is needed |
|------------|------------------|
| `LgLcdApi.dll` | The real client library. GW2 imports its `GetInterface` table, so `LgLcdApiProxy.dll` must load this DLL and hand the real table back unchanged, hooking only the pipe I/O. This is what lets the proxy tap GW2's frames and inject the soft-button `0x0702` messages. Without it the proxy has nothing to forward to. |
| `LogitechLcd.dll` | The stock server binary for the legacy `LogiLcd*` API. Used by the alternative `shim/` approach and kept so the registry can be restored. |

Both come from a Logitech Gaming Software install:

```
C:\Program Files\Logitech Gaming Software\SDK\LCD\x64\
```

They are © Logitech, are **not** covered by this project's license, and must not
be committed or redistributed. See `third_party/logitech/README.md` for the copy
commands and provenance.

## Building

Build scripts need MinGW (`C:\w64\bin`) on `PATH`; the viewer needs a Rust
toolchain (`rustup`, any host).

```powershell
# proxy DLL (lcdproxy\LgLcdApiProxy.dll)
powershell -ExecutionPolicy Bypass -File lcdproxy\build.ps1

# replacement server (lcdproxy\lgpipe_server4.exe)
powershell -ExecutionPolicy Bypass -File lcdproxy\build-server.ps1 -Out lgpipe_server4.exe

# native viewer (viewer\target\release\gw2lcd-viewer.exe)
powershell -ExecutionPolicy Bypass -File viewer\build.ps1
```

Before running the proxy, populate `third_party/logitech/` (see its README).

## Running

1. Register `lcdproxy\LgLcdApiProxy.dll` for the Logitech LCD CLSID with
   `lcdproxy\registry.ps1` (backup/restore supported).
2. Start `lgpipe_server4.exe`.
3. Start GW2. It connects to the server.
4. Run `viewer\target\release\gw2lcd-viewer.exe` to see the live screen and
   drive the six soft buttons (`<`, `>`, `ok`, `^`, `v`, `menu`).

The genuine Logitech software is still required for the stock DLLs and for
`LCore.exe` until it is removed.

## Related projects

- [`g19daemon`](https://github.com/mortendynamite/g19daemon) and
  [`G19LCD`](https://github.com/endlessmind/G19LCD) drive the G19 over USB
  directly; neither implements the `LGLCDPIPE` protocol.
- [`henninglive/logitech-lcd`](https://github.com/henninglive/logitech-lcd)
  wraps the Logitech SDK rather than replacing its server.
- The Logitech LCD SDK and `LCore.exe` from Logitech Gaming Software define the
  behaviour this project reproduces byte for byte.

## License

[MIT](LICENSE). The Logitech DLLs under `third_party/logitech/` are proprietary,
are **not** covered by that license, and are excluded from the repository.

## Disclaimer

Not affiliated with, endorsed by, or supported by Logitech or ArenaNet. G19,
Logitech, Guild Wars 2 and their logos are trademarks of their respective
owners. This is an interoperability project; it contains no Logitech code and
redistributes none.
