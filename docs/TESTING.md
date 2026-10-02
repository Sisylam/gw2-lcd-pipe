# Testing

Automated tests run with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\test.ps1
```

Close GW2 first: the clean-DLL test shares `Local\GW2LCDShim` and
`Local\LGLCDCtl` with any running instance. Build `lcdproxy\lgpipe_server4.exe`
first (`lcdproxy\build-server.ps1`) so the server mock has something to talk to.

## What is covered

| Test | File | Covers |
|------|------|--------|
| Client DLL | `clean/test_cleanapi.c` | Loads `LgLcdApi.clean.dll`, checks the `GetInterface(5)` slot layout, drives `init`/`connect`/`open`, publishes a colour **and** a mono frame and checks the shim header and the BGRA→RGB conversion, then pokes the control channel and checks the soft-button callback fires with the right device and bit. No GW2, no Logitech, no server. |
| Viewer units | `viewer/src/shim.rs`, `viewer/src/ctl.rs` | Shim header parsing (magic/version/size/active, colour + mono pixel extraction, short-buffer rejection) and the button-bit constants. Run via `cargo test`. |
| Server mock | `lcdproxy/test_server.c` | A mock GW2 client connects to `lgpipe_server4.exe`, sends the opening negotiation and two frames, and checks every reply is a well-formed, length-prefixed message. |

The mono path matters because not every supported panel is colour: the G19/G19s
is 320x240 colour, while the G15/G510/G13 are 160x43 mono. The shim carries both
(`active=1` colour, `active=2` mono) and the tests exercise both.

## What is not automated

- The real GW2 handshake and the `DEVICE_ARRIVAL` notification flow - needs the
  game (see `docs/PROTOCOL.md`).
- The physical/virtual LCD actually showing the frames - needs eyes or the
  viewer.
- Anything requiring `LCore.exe` or the genuine Logitech DLLs.

These are exercised manually, as recorded in `docs/PROTOCOL.md`.

## Linting

```powershell
powershell -ExecutionPolicy Bypass -File scripts\lint.ps1
```

Runs `gcc -Wall -Wextra` over the C sources, `cppcheck`, `cargo clippy -- -D
warnings`, and `cargo fmt -- --check`. cppcheck and the Rust tools are skipped
with a notice if not installed.

## CI

- `.github/workflows/ci.yml` — on push/PR: installs MinGW, builds the server,
  runs the tests and the linters on `windows-latest`.
- `.github/workflows/codeql.yml` — CodeQL code scanning for C/C++, Rust and
  Python (`build-mode: none`, so no build is required).

