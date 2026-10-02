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

## MinGW / Windows

The proxy and server are built with MinGW-w64 and link only against Windows
system libraries (`kernel32`, `user32`, `gdi32`, `advapi32`, `msvcrt`).
