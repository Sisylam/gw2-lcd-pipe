# Method and legal posture

This note records *how* the protocol knowledge in this repository was obtained
and what it does and does not rely on. It is not legal advice.

## What this project is

A hobby / personal-use interoperability project. It lets Guild Wars 2 keep its
LCD output while the proprietary Logitech server (`LCore.exe`) is not running.
It is **not** intended for commercial use, sale, or redistribution, and it is
not affiliated with or endorsed by Logitech or ArenaNet.

## How the protocol was derived

Purely **black-box observation of a public inter-process boundary**:

- The transport is a named pipe, `\\.\pipe\LGLCDPIPE-00000001`. A passive proxy
  (`lcdproxy.c`) logs the bytes each side writes and reads. That is the same
  category of observation as capturing network traffic on an interface you are
  party to.
- No decompilation or disassembly of `LCore.exe` or `LgLcdApi.dll` was performed.
  The only static inspection was reading the DLL's **import table** (to know
  which functions are safe to hook), which is metadata, not code.
- No Logitech code, symbols, or strings were copied. All source here is original.

The reverse-engineering notes live in `docs/PROTOCOL.md`; the implementation
lives in `lcdproxy/`, `viewer/` and `shim/`. They are kept separate on purpose,
so the *observed behaviour* is documented independently of the code that acts on
it.

## The documented API

Logitech publishes the Logitech Gaming LCD SDK, which documents the client API
(the `lgLcd*` / `LogiLcd*` functions and the `GetInterface` entry point). The
**API surface is public and documented**. This project's client-side work is
intended to align with that documented API; the undocumented part is only the
*internal transport* between the client library and the server, which the
observation above describes.

## Third-party binaries

The genuine Logitech DLLs are **not** part of this repository and are not
redistributed. They are expected in `third_party/logitech/` (gitignored), taken
from a Logitech Gaming Software installation the user already has. See
`third_party/logitech/README.md`.

## Interoperability

Reverse engineering for interoperability is treated as lawful in a number of
jurisdictions (for example the US cases *Sega v. Accolade* and *Sony v.
Connectix*, and Article 6 of the EU Software Directive 2009/24/EC), but it is
fact- and jurisdiction-specific, and a product EULA may separately restrict it.
If you intend anything beyond personal use, seek qualified legal advice.
