# Third-party notices

This project contains no third-party source code. It depends at runtime on
proprietary Logitech binaries that the user must supply; those are not part of
this repository and are not covered by its license.

## Logitech (not distributed)

| File | Copyright | Source |
|------|-----------|--------|
| `LgLcdApi.dll` | © 2009-2010 Logitech. All rights reserved. | Logitech Gaming Software / LCD SDK |
| `LogitechLcd.dll` | © 2004-2022 Logitech. All rights reserved. | Logitech Gaming Software / LCD SDK |

The x64 builds are expected in `third_party/logitech/`, which is gitignored.
See `third_party/logitech/README.md` for provenance and copy commands.

## Rust crates (viewer)

The native viewer depends on the following crates, each under its own
permissive license (MIT and/or Apache-2.0). Verify with `cargo license` or the
crate manifests:

- `minifb`
- `font8x8`
- `raw-window-handle`
- `winapi`

## Prior work and knowledge

No source code from these projects is included. They informed the
interoperability work - the documented API surface, protocol knowledge, and the
reference screenshots - and are credited here.

| Project | Contributed |
|---------|-------------|
| Logitech LCD SDK / `LCore.exe` | The documented `lgLcd*` / `LogiLcd*` client API and the `LGLCDPIPE` behaviour this project emulates. |
| [`sliekens/gw2sdk`](https://github.com/sliekens/gw2sdk) ([Logitech-LCD wiki](https://github.com/sliekens/gw2sdk/wiki/Logitech-LCD)) | Guild Wars 2 SDK; the `reference/` screenshots come from its wiki page. |
| [`endlessmind/Logitech-G19-display-reverse-engineering`](https://github.com/endlessmind/Logitech-G19-display-reverse-engineering) | Reverse engineering of the G19's USB display protocol. |
| [`endlessmind/G19LCD`](https://github.com/endlessmind/G19LCD) | .NET G19 USB display library. |
| [`mortendynamite/g19daemon`](https://github.com/mortendynamite/g19daemon) | Linux G19 USB daemon. |
| [`henninglive/logitech-lcd`](https://github.com/henninglive/logitech-lcd) | Rust SDK bindings; independent confirmation of the soft-button bit values. |
| [`zzattack/Logitech-LCD`](https://github.com/zzattack/Logitech-LCD) | C# wrapper; the flat `lgLcd*` export list. |
| [`theldoria/lg-lcd`](https://github.com/theldoria/lg-lcd) | Ruby binding; a second copy of the flat export list. |
| [`sidewinder94/Logitech-LCD`](https://github.com/sidewinder94/Logitech-LCD) | C# wrapper; soft-button bit values. |
| [`mpc-hc/mpc-hc`](https://github.com/mpc-hc/mpc-hc), [`Aleksoid1978/MPC-BE`](https://github.com/Aleksoid1978/MPC-BE), [`mumble-voip/mumble`](https://github.com/mumble-voip/mumble) | Ship the public `lglcd.h` SDK header (documented API signatures and structs). |
| [`linkdata/LCDHost`](https://github.com/linkdata/LCDHost) | Open-source LCD host; prior art for replacing `LCore`. |

## MinGW / Windows

The proxy and server are built with MinGW-w64 and link only against Windows
system libraries (`kernel32`, `user32`, `gdi32`, `advapi32`, `msvcrt`).
