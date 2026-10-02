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
- **Clean-room client DLL works** (`clean/`): GW2 loads our `LgLcdApi.clean.dll`,
  gets live frames, and all six buttons work - with no Logitech DLL and no
  server process. This removes the dependency on Logitech software entirely.
- Next: uninstall LGS and enable/test HVCI.

See `docs/PROTOCOL.md` for the full protocol notes, history and open items.

## Layout

| Path | Role |
|------|------|
| `lcdproxy/` | The replacement named-pipe server (`lgpipe_server.c`) and the passive proxy DLL (`lcdproxy.c`) that taps GW2's frames. |
| `clean/` | **Clean-room client DLL** (`LgLcdApi.clean.dll`): replaces `LgLcdApi.dll` **and** `LCore.exe`. No Logitech binary, no server. See `clean/README.md`. |
| `viewer/` | **Native Rust viewer** (`gw2lcd-viewer.exe`): shows `Local\GW2LCDShim` and provides the six soft buttons. Replaces the Python viewer. |
| `shim/` | Alternative approach: a shim for the legacy `LogiLcd*` API. |
| `lcdshim.py` | Python shared-memory reader, used only by `shim/test_shim.py`. |
| `docs/PROTOCOL.md` | Full protocol notes, reverse-engineering history and open items. |
| `docs/INTEROP.md` | How the protocol was derived (black-box) and the project's legal posture. |
| `docs/TESTING.md` | What the automated tests cover. |
| `scripts/` | Test runner (`test.ps1`). |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | MIT license and third-party attributions. |
| `third_party/logitech/` | Genuine Logitech DLLs the project depends on. **Not committed** — see its README. |
| `reference/` | GW2 LCD screen renders used during analysis - **unverified provenance**, see `reference/README.md`. |
| `cache/`, `.env` | Local API cache and secrets. **Not committed.** |

## Required Logitech files

The project ships **no** Logitech code. The proxy/server path (Option B below)
needs two genuine Logitech DLLs copied into `third_party/logitech/`
(gitignored); the **clean-room client DLL (`clean/`) needs none**:

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

Keep them in `third_party/logitech/` even after uninstalling Logitech Gaming
Software, so the stock client/server can be restored if ever needed.

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

## Testing

```powershell
powershell -ExecutionPolicy Bypass -File scripts\test.ps1
```

Runs the client-DLL integration test, the viewer unit tests, and the server mock
test. See `docs/TESTING.md` for what is and is not covered.

## Running

There are two ways to serve GW2's LCD. Both publish to `Local\GW2LCDShim`, so
the same viewer works with either.

**Option A - clean-room client DLL (no Logitech software).**

1. Register `clean\LgLcdApi.clean.dll` for the Logitech LCD CLSID (elevated;
   same key as `lcdproxy\registry.ps1`, pointed at the clean DLL). See
   `clean/README.md`.
2. Start GW2.
3. Run `viewer\target\release\gw2lcd-viewer.exe`.

**Option B - proxy + replacement server.**

1. Register `lcdproxy\LgLcdApiProxy.dll` for the CLSID with
   `lcdproxy\registry.ps1` (backup/restore supported).
2. Start `lgpipe_server4.exe`.
3. Start GW2. It connects to the server.
4. Run the viewer.

Option B still needs the genuine Logitech DLLs in `third_party/logitech/`;
Option A needs no Logitech software at all.

## Related projects and sources

- [`sliekens/gw2sdk`](https://github.com/sliekens/gw2sdk) — Guild Wars 2 SDK.
  Its [`Logitech-LCD` wiki page](https://github.com/sliekens/gw2sdk/wiki/Logitech-LCD)
  is the source of the `reference/` screenshots.
- [`endlessmind/Logitech-G19-display-reverse-engineering`](https://github.com/endlessmind/Logitech-G19-display-reverse-engineering)
  — reverse engineering of the G19's USB display protocol.
- [`g19daemon`](https://github.com/mortendynamite/g19daemon) and
  [`G19LCD`](https://github.com/endlessmind/G19LCD) drive the G19 over USB
  directly; neither implements the `LGLCDPIPE` protocol.
- [`henninglive/logitech-lcd`](https://github.com/henninglive/logitech-lcd)
  wraps the Logitech SDK rather than replacing its server.
- Discord discussion (GW2 / Logitech LCD):
  <https://discord.com/channels/384735285197537290/384735523521953792/1114274342734417942>
- The Logitech LCD SDK and `LCore.exe` from Logitech Gaming Software define the
  behaviour this project reproduces byte for byte.

## Acknowledgements

None of this stands alone. Sincere thanks to the people below — no code of
theirs is included, but their reverse engineering, documentation and reference
material are what let this project get there:

- **Logitech** — the LCD SDK documents the client API, and `LCore.exe` defines
  the pipe behaviour emulated here.
- **[sliekens](https://github.com/sliekens)** (`gw2sdk`) — Guild Wars 2 SDK; the
  `reference/` screenshots come from his
  [Logitech-LCD wiki page](https://github.com/sliekens/gw2sdk/wiki/Logitech-LCD).
- **[endlessmind](https://github.com/endlessmind)** — G19 USB display reverse
  engineering (`Logitech-G19-display-reverse-engineering`, `G19LCD`).
- **`g19daemon`**, **`henninglive/logitech-lcd`**, **`zzattack/Logitech-LCD`**,
  **`theldoria/lg-lcd`**, **`sidewinder94/Logitech-LCD`**, **`linkdata/LCDHost`**
  — protocol and API references.
- **`mpc-hc`**, **`MPC-BE`**, **`mumble`** — ship the public `lglcd.h` SDK header.

Thank you all. See `THIRD_PARTY_NOTICES.md` for the full list.

## License

[MIT](LICENSE). The Logitech DLLs under `third_party/logitech/` are proprietary,
are **not** covered by that license, and are excluded from the repository.

## Method and legal

The protocol was derived by **black-box observation** of the public
`LGLCDPIPE` named-pipe boundary — no decompilation, and no Logitech code or
symbols were copied. The genuine Logitech DLLs are **not** distributed; you
supply them from your own Logitech Gaming Software install. This is a hobby /
personal-use project, not for commercial use. See `docs/INTEROP.md` for details.

## Disclaimer

Not affiliated with, endorsed by, or supported by Logitech or ArenaNet. G19,
Logitech, Guild Wars 2 and their logos are trademarks of their respective
owners. This is an interoperability project; it contains no Logitech code and
redistributes none.
