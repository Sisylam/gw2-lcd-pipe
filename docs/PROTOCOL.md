# Protocol notes — GW2 LCD out of LCore

**Last updated:** 2026-10-02 09:20
**Status:** **SUCCESS — replacement server drives GW2 with LCore stopped, and the
view buttons work.** The viewer's `<` / `>` cycle GW2's screens through the
pipe; a `0x0702` press is followed by a new 307224-byte frame ~26-29ms later,
exactly as with LCore. The viewer is now a native Rust executable
(`viewer/`); the Logitech DLLs live in `third_party/logitech/` (gitignored).
Next: deploy the third-party-loading proxy and verify, then LGS removal and
HVCI.

### What actually made the buttons work (2026-10-02)

The opening is **five** entries, not six. The old `replay_table.inc` had a
sixth `0x0702 = 0x200` entry that was a *button press* captured by accident
during the old two-second capture, not handshake state. Replaying it at startup
put GW2's button state at `0x200` before the user touched anything, so real
presses were ignored (and GW2 emitted spurious `0x0804` / `0x0803` / `0x0830`
re-negotiation traffic). Dropping it fixed the buttons, the frame header word 3
(`0xCCCCCCCC` -> `0x80000002`), and the spurious client traffic.

Button bits are little-endian: `[<] 0x100`, `[>] 0x200`, `[ok] 0x400`,
`[^] 0x1000`, `[v] 0x2000`, `[menu] 0x4000`. See the corrected sections below.

---

## Objective

Remove the obsolete `LCore.exe` so Microsoft Defender's memory-integrity feature
can fully activate, while preserving GW2's native 320x240 LCD stream for
`Local\GW2LCDShim` and casting to other displays (ESP32, phone, monitor, ...).

The blocking dependency: GW2 talks to `LCore.exe` over the named pipe
`\\.\pipe\LGLCDPIPE-00000001`. No third-party project implements that pipe.

---

## Read this first — what the earlier version of this file got wrong

This file previously described the client header as `length, type` with types
`0x0831 / 0x0802 / 0x0801 / 0x0800`. **That was wrong and cost three hangs.**

Corrections, all verified against captured bytes:

- The client header is `length, **sequence**, type` — a per-message sequence
  number sits between length and type. Reading `type` at offset 4 shifts every
  field by one word.
- Client types are `0x0831` (negotiation), `0x0803` (short), `0x0830` (region),
  `0x0806` (frame). The old values were off by one field because of the shift.
- Server replies include **`0x0702`** (page/view state), which the old file did
  not list at all.

---

## Protocol

The pipe is a **message-mode** duplex pipe. Each `WriteFile` queues one whole
record; each `ReadFile` dequeues exactly one.

### Client -> server (GW2 writes)

```
u32  length      total bytes including this field
u32  sequence    increments per client message (0,1,2,3,...)
u32  type
...  payload
```

| Size | Type | Meaning |
|-----:|-----:|---------|
| 1084 | `0x0831` | opening negotiation; contains Unicode `Guild Wars...` text |
| 16 | `0x0803` | short message, payload `0x80000002` |
| 20 | `0x0830` | region/page state, payload `0x80000002`, state |
| 307224 | `0x0806` | 24-byte header then 320*240 BGRA frame |

The frame is 307224 bytes because it is 320*240*4 plus a 24-byte header.
`stream_0_1ef0.bin` (8,602,328 B) parses as 31 concatenated messages.

### Server -> client (LCore replies — what we owe GW2)

| Size | Type | Meaning |
|-----:|-----:|---------|
| 16 | `0x0900` | config, e.g. `(16, 0x0900, 0, 10)` and `(16, 0x0900, 1, 0x80000002)` |
| 32 | `0x0703` | device/handshake state, e.g. `(..., 0, 1, 2, 0, 0, 0)` and `(..., 0, 5, 0, 0, 0, 0)` |
| 12 | `0x0900` | keepalive; third word counts up `0,1,2,3,...` one per message |
| 20 | `0x0702` | page/view state `(20, 0x0702, 0, 0x80000002, 0x200 or 0)` |

### The verified opening sequence

**Five** replies, captured verbatim, in wire order. This is the whole startup
handshake and it is now in `replay_table.inc`:

| # | Bytes | Type | Payload |
|--:|------:|------|---------|
| 1 | 16 | `0x0900` | `0, 0x0a` |
| 2 | 32 | `0x0703` | `0, 1, 2, 0, 0, 0` |
| 3 | 16 | `0x0900` | `1, 0x80000002` |
| 4 | 32 | `0x0703` | `0, 5, 0, 0, 0, 0` |
| 5 | 12 | `0x0900` | counter `2` |

The opening is delivered **two replies per client message**: `seq=0 NEGOTIATE`
gets replies 1+2, `seq=1 SHORT` gets 3+4, `seq=2 REGION` gets reply 5. After
that the server sends one reply per client message (a keepalive).

There is **no** sixth entry. An earlier table carried a sixth `0x0702 = 0x200`
because a button happened to be pressed during the two-second capture that
produced it; replaying it at startup broke GW2's button state. The clean
single-press capture shows no `0x0702` before the physical press.

---

## The one rule that matters: one reply per read

**Answer every completed read with exactly one message. No bursts, no timers.**

The first server sent four handshake messages at the first read, then two per
cycle. GW2 dequeued messages it had not asked for, the framing desynchronised,
and its main thread blocked on the next read — reported by Windows as
`AppHangB1` with a black screen, three separate times.

There was never a missing message type. It was a queue discipline violation.
LCore honours message-mode strictly: one session showed 49 reads answered by 48
replies, one for one.

`handle_client()` now calls `reply_next()` exactly once per parsed message, and
the hand-built `send_handshake()` is gone. Past reply 6 the server sends
`{12, 0x0900, n}` with `n` incrementing from 3 — LCore's keepalive counts up, and
the old frozen `0` answered out of order. `gen_replay.py` derives the resume
point from the last captured keepalive rather than hardcoding it.

### A read the server cannot see — the idle poll

"One reply per client write" is only half the contract, and on its own it
deadlocks. The captured trace:

```
R n=560        read #1 posted
W n=1084       negotiation written
RP got=16      our reply satisfies read #1
R n=560        read #2 posted
(silence)      GW2 blocks here, and sends nothing more
```

GW2's `ReadFileEx` goes straight to the pipe and never reaches the server, so
**reads are invisible from this side**. GW2 waits for a second, server-initiated
message and will not issue another request until it gets one. The server sat
there for 30s with `R560=2, W307224=0, RP=1` while GW2 waited at character
select, and no frame was ever coming.

So **an idle client is a blocked client**. When nothing has arrived inbound for
one poll interval, the server calls the same `reply_next()` and walks the
captured opening in order. That is also why LCore's 12-byte `0x0900` is a *poll
response* and not a timer artefact — its counter advances by exactly one per
message because it answers one read at a time.

Interval is `LGLCD_IDLE_MS` (default 200ms, logged at startup). It only fires
when nothing is inbound, so it tracks GW2's consumption instead of running ahead
of it; that is what prevents the backlog that caused the original black screens.

### Result — verified with LCore stopped

```
seq=0 NEGOTIATE 1084  ->  replay[0] 0x0900 16
        (idle poll)   ->  replay[1] 0x0703 32
seq=1 SHORT      16   ->  replay[2] 0x0900 16
seq=2 REGION     20   ->  replay[3] 0x0703 32
        (idle poll)   ->  replay[4] 0x0900 12
        (idle poll)   ->  replay[5] 0x0702 20
seq=3..6                 -> keepalive(tail), counter 4,5,6...
```

- `LCore` process: **not running**
- reads / replies: **552 / 551** — one-for-one
- frames: five 307224-byte writes, client frame counter advancing `3,4,5,6,7`
- reconnect after a server restart: ~20s, **GW2 retries on its own**
- tail keepalives are sampled in the log (first 3, then every 64th) or a
  five-minute session buries everything under thousands of lines

### Shim content does NOT prove the server works

Worth stating because it nearly caused a false conclusion here. The hook
publishes to shared memory *before* the pipe write is attempted:

```c
static BOOL WINAPI hook_WriteFile(HANDLE h, LPCVOID b, DWORD n, ...) {
    note_write(h, b, n);                    /* shm publish happens here */
    return g_RealWriteFile(h, b, n, ...);   /* pipe write happens after */
}
```

So the shim lights up whenever GW2 *attempts* a frame, even against a dead pipe.
The real evidence that the server is healthy is the frame writes on the pipe
handle plus the one-for-one read/reply counts, not the shim.

## View buttons — `Local\LGLCDCtl`

The viewer's view buttons used to change a local counter and nothing else. They
now drive a real `0x0702` soft-button notification through `Local\LGLCDCtl`.

A 32-byte named mapping, `Local\LGLCDCtl`, created by the server and written by
the shim:

| Offset | Field | Meaning |
|-------:|-------|---------|
| 0 | `u32 magic` | `0x31544347` (`"GCT1"`) |
| 4 | `u32 request` | bumped on every press; the server watches this |
| 8 | `u32 button` | direction: `BTN_NEXT` (`0x200`) or `BTN_PREV` (`0x100`) |
| 12 | `u32 served` | last request answered (server writes) |

### 0x0702 is a button notification, not a page selector

The last field of a `0x0702` is a single button bit, stored little-endian (see
the SETTLED table above). A button press is **two** messages - the bit, then
`0` about 130-250ms later as the release - and GW2 advances its view on the
pair. A lone press with no release is discarded.

Consequences for the implementation:

- `ctl_poll()` enqueues **press then release** (`BTN_*`, then `0`).
- A small `PQ_MAX 8` ring holds them. They are spent **one per reply** rather
  than sent back to back, because two messages at once is the queue discipline
  violation behind the original black screens.
- The six bits are `0x100`, `0x200`, `0x400`, `0x1000`, `0x2000`, `0x4000`.
  `[<]`/`[>]` are confirmed; the other four are corrections of the old
  byte-swapped table.

### Incident: button flood wedged GW2 (2026-10-01 21:39)

An early version of the delta logic sent `0x0702` press messages continuously and
**GW2 hung on a map loading screen**. Killing the server released it immediately,
so the flood is confirmed as the cause, not a correlation.

Root cause: `ctl_poll()` used

```c
LONG prev = InterlockedCompareExchange(&g_ctl_seen, seen, seen);
```

as if it were a read-and-set. It is not. `InterlockedCompareExchange` stores
**only when the comparison matches**, and passing `Comparand == Exchange == seen`
means that succeeds exactly when the value is *unchanged*. So a real change was
never recorded, every poll re-read it as a new press, and pairs were queued
faster than GW2 drained them.

The fix is `InterlockedExchange(&g_ctl_seen, seen)`, which always stores and
returns the previous value.

Two hard limits now exist because of this, and they must not be relaxed:

- **One outstanding pair maximum.** A press and its release are the only
  unspent messages permitted. Further clicks are dropped and logged
  (`a press is already pending`).
- `delta` is clamped to `1`.

The general lesson, and the reason the limit is not "tune it up if it looks
dropped": **0x0702 presses are not safe to send at pipe rate.** The capture only
ever showed them at human speed. Nothing establishes what GW2 does with a
rapid press stream other than that it stops making progress.

### Screen changes ARE triggered by `0x0702` (earlier "no trigger" note was wrong)

An earlier version of this file correlated frames against the log and concluded
nothing preceded a screen change. That was a byte-order artefact: the `0x0702`
presses were mis-decoded (see the button section), so they did not look like
button events. The clean capture settles it: a `0x0702` press is followed by a
new frame ~26-29 ms later, every time, for `[<]`, `[>]` and `[v]`.

`[<]`/`[>]` cycle sequentially through the screens (they do not select a screen
directly); `[ok]`, `[^]` and `[menu]` do not change the screen. Screens also
change on GW2's own initiative - zoning, WvW, opening the character sheet.

| Screen | Frames (tile numbers) |
|--------|-----------------------|
| GW2 logo (zone/loading) | 1, 8, 11, 14, 35, 38, 43, 48, 51, 56, 59 |
| World completion | the remaining 33 |
| Current map | 4, 19, 24, 29-34 |
| Character sheet | 6, 7, 21, 27 |
| WvW | 5, 20, 28 |
| Combat log | 22, 26 |

The logo frames are trivially detectable: 82.7% near-white with 4.6%
red/orange, and nothing else on these screens is mostly white.

### What the shim can and cannot tell us

- It is a live view of GW2's screen: GW2 writes the frames and the proxy
  publishes them, so it reflects what GW2 rendered.
- It is **not** a window into GW2's internal state, and because the proxy
  publishes *before* the pipe write, the shim lights up even against a dead
  server. So the shim alone cannot prove the server is healthy; the pipe frame
  writes and the read/reply counts are the real evidence.
- A frozen shim during the 2026-10-01 button tests was a real symptom: the
  phantom opening entry had broken GW2's state. Once that was removed, button
  presses produced frames again (confirmed 2026-10-02).

### Frame capture notes

- `stream_*.bin` are raw framebuffer dumps, not protocol messages. The real
  frame is 307200 bytes (320x240 BGRA); a 307224 stride lands mid-image and
  yields chunks whose apparent "headers" are colour values.
- Geometry is confirmed 320x240: the row-seam test scores 320 at ratio 1.12
  against 1.78 for the next candidate (`geometry.py`).
- Frames most likely carry a 24-byte header each (file size is exactly
  `28 * 307224 + 56`), so reading at a 307200 stride accumulates a 6px
  horizontal shift - which is why the contact sheet wraps progressively. This
  does not affect replay, which uses `payload_307224.bin` intact.

### SETTLED 2026-10-02: the G19S button channel

The button bit is the last 4 bytes of a `0x0702`, stored **little-endian**. The
proxy log dumps raw bytes in memory order (`lcdproxy.c`, `"%02x"` per byte), so a
real `[>]` press ends in the bytes `00 02 00 00` = `0x00000200`.

An earlier version of this file read that 8-hex-char group as a big-endian
number, recorded `0x00020000`, and concluded the old `0x100`/`0x200` constants
were "off by eight bits". That was backwards. A re-parse of the captures shows
the real values are:

| Button | `0x0702` word 4 | Screen changed? |
|--------|-----------------|-----------------|
| `[<]`  | `0x00000100`    | yes - cycles to previous view |
| `[>]`  | `0x00000200`    | yes - cycles to next view |
| `[ok]` | `0x00000400`    | no visible change |
| `[^]`  | `0x00001000`    | no visible change |
| `[v]`  | `0x00002000`    | no visible change |
| `[menu]` | `0x00004000`  | no visible change |

All six bits were confirmed end-to-end on 2026-10-02 through the viewer
against the replacement server: the server logged the correct bit for each
button, and exactly `[<]` / `[>]` changed the displayed view. (`[v]` once
produced a frame write but no visible view change - treat it as a non-view
action.) `[<]` and `[>]` cycle sequentially; they do not select a screen.

Every press is followed ~130-250 ms later by `0x0702` word 4 = `0`. That is the
release, and it is required: a press with no release is discarded.

Clean single-press capture against real LCore (2026-10-02 01:00:30), the model
the server now reproduces:

```
RP got=20  0x0702 = 0x00000200   GW2 reads LCore's press
R  n=560
W  n=307224                      GW2 writes a new frame (~18 ms later)
RP got=12  0x0900                keepalive
R  n=560
RP got=20  0x0702 = 0x00000000   release (~110 ms later)
```

### Message direction (settled, and the earlier "backwards" claim was wrong)

Every hook lives inside GW2's process, so every logged `W` is GW2's own write:

- **GW2 -> server:** 307224-byte frames. GW2 renders its screen and pushes the
  pixels; LCore consumes them.
- **Server -> GW2:** small control messages - `0x0702` (buttons), `0x0900`
  (keepalive), `0x0703`, `0x0900` config. GW2 reads these.

The replacement server was **never** sending frames - its replay table contains
only small control messages. The earlier "the replacement server has this
backwards, and that invalidates `replay_table.inc`" note was incorrect; the
premise of the table is fine. The display froze for a different reason (the
phantom sixth opening entry, now removed).

Because GW2 writes the frames, the viewer is a genuine live view of GW2's
screen. It is still not a way to observe GW2's internal state.

### There is no second channel

A build of the proxy that also logs `CreateFileMappingW`, `OpenFileMappingW`,
`MapViewOfFile`, `CreateEventW` and `OpenEventW` (2026-10-02) showed that
`LgLcdApi.dll` imports only `CreateEventW`/`OpenEventW` (not the mapping calls)
and that every event it creates is anonymous (`name=(null)`). The only named
object is `LGLCDPIPE`. So button events genuinely travel on the same pipe as
`0x0702` - there is no second pipe, shared section, or named event to find.

### Disproved hypotheses — do not revisit

- "A missing `config#2` message is what holds GW2 hostage." The 32-byte `0x0703`
  is real and is in the table; adding it alone did not fix the hang.
- "GW2 opens the pipe write-only." It opens it `GENERIC_READ | GENERIC_WRITE`
  (`acc=c0000000`).
- "`ERROR_NO_DATA` (232) is a client limitation." It was a server-side
  synchronous-write bug on a `FILE_FLAG_OVERLAPPED` handle.

---

## Capture integrity — why the table has only 5 entries

The payload directory holds **65 dumps that are two spliced sessions**, not one:

| Run | Time | Reply indices | Contents |
|-----|------|--------------:|----------|
| 1 | 20:29:08 -> 20:39:40 | **6..64** (59 msgs) | long gameplay session; its first 5 replies were lost when the payload dir was cleared mid-run |
| 2 | 20:53:39 -> 20:53:41 | **1..6** (6 msgs) | short run; the only run starting at 1 |

Sorting by filename produced run 2's six replies, then run 1's reply 6 (a
keepalive), then run 1's replies 7..64 — a 65-message sequence that **never
occurred on the wire**. That is the desync, and it would have reintroduced the
hang for reasons that had nothing to do with the protocol.

`gen_replay.py` therefore splits runs on index discontinuity and **refuses to
emit a table unless one run starts at reply 1 with no gaps**. It picked run 2's
six replies. The 59-message run is intact on disk and can be used later, but it
is a *tail*, not an opening.

Run 2's sixth reply (`0x0702 = 0x200`) was a *button press* caught in the
two-second window, not handshake state. The real opening is the first **five**
replies; replaying the sixth at startup broke GW2's button state (see the top of
this file). `replay_table.inc` now carries five entries.

### To get a full table

Clear `%TEMP%\opencode\payloads`, then start LCore and GW2 fresh and let the
session finish **without clearing the directory mid-run**. The proxy numbers
replies from 1 per process lifetime and dumps up to 64, which is enough for the
startup plus a short session. Re-run `py gen_replay.py` afterwards.

---

## The capture crash, and the fix

The first in-process capture attempt crashed GW2 twice (`Gw2-64.exe`,
`c0000005` at `fffffffffffffff8`). The dump pinned the cause exactly — the
faulting instruction was:

```
cmp dword [rdx-18h], <magic>     ; rdx = 10h
```

`18h` is `offsetof(rwrap_t, real_ov)`. Argument 2 arrived as `0x10` — a byte
count of 16, not a pointer. The runtime invokes completion routines in the
order this MinGW header documents, `void(DWORD err, DWORD transferred,
LPOVERLAPPED ov)`, and "correcting" it to the modern documented order is what
dereferenced a count as a pointer. (16 is also the size LCore uses to answer
GW2's 560-byte reads, so that number was a real observation, twice.)

The lesson is not the argument order — it is that the wrapper had no business
rewriting the caller's `OVERLAPPED` at all. The working capture build:

- passes the caller's buffer, `OVERLAPPED` and arguments through **unmodified**
- finds its own context by **matching the `OVERLAPPED` pointer** against a
  static locked table (`g_rcap[64]`), never by computing a pointer from an
  argument whose meaning it is guessing at
- reads the transferred count from `OVERLAPPED::InternalHigh`, which the kernel
  fills in regardless of completion order
- forwards the three arguments **verbatim**, so whichever ABI is in force stays
  in force
- if lookup fails it still forwards — worst case is a missing log line, not a
  lost notification

This build ran a full GW2 session without crashing.

---

## Server transport

`lgpipe_server4.exe`, 8 pipe instances, 64-bit, message mode:

- `PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | FILE_FLAG_OVERLAPPED`
- `write_msg` uses a real `OVERLAPPED` + event, waits, then
  `GetOverlappedResult`. A synchronous `WriteFile` with a NULL `OVERLAPPED` on a
  `FILE_FLAG_OVERLAPPED` handle is what produced `ERROR_NO_DATA` (232) before.
- `GetNamedPipeClientProcessId` filters phantom connections.
- `CancelIoEx` / `GetNamedPipeInfo` are loaded dynamically (see Build notes).

---

## Architecture

Passive proxy only. No generated trampolines, no executable stubs.

- CLSID `{FE750200-B72E-11d9-829B-0050DA1A72D3}` (ServerBinary, 64-bit
  `...\lcdproxy\LgLcdApiProxy.dll`) -> proxy, which loads the genuine
  `LgLcdApi.dll` and returns its real `GetInterface` table **unchanged**.
- The genuine DLL is resolved **relative to the proxy DLL**
  (`..\third_party\logitech\LgLcdApi.dll`), falling back to the stock Program
  Files path. The Logitech DLLs are copyrighted and are **not committed** - see
  `third_party/logitech/README.md`.
- Proxy IAT-hooks `WriteFile`, `WriteFileEx`, `ReadFileEx`, `CreateFileW`, and
  (in the observation build) `CreateEventW` / `OpenEventW` plus the mapping
  calls. All are pass-through; the last group exists only to prove there is no
  second IPC channel.
- Frames are tapped before LCore's mono downscale, so full-colour BGRA is
  preserved even though the physical G19 is monochrome.
- Published to `Local\GW2LCDShim` as 320x240, 24bpp, 230400 RGB bytes.
- The CLSID is global, so this must keep working for Mumble and bento too.

Physical G19 rendering is not required by the goal. `LCore.exe` is
Logitech Gaming Software and the last real reason it runs is GW2's LCD.

## Repository layout

The project is a git repository. `.gitignore` excludes secrets (`.env`), the
local API cache (`cache/`), build outputs (`*.dll`, `*.exe`, `target/`, ...),
logs, and - for copyright reasons - the Logitech binaries under
`third_party/logitech/`. `third_party/logitech/README.md` explains how to
populate that folder.

`viewer/` is a **native Rust replacement for `view_shim.py` + `lcdshim.py`**
(built with `viewer\build.ps1` to `viewer\target\release\gw2lcd-viewer.exe`).
It reads `Local\GW2LCDShim` and drives the six buttons through `Local\LGLCDCtl`.
The Python files are kept only for reference and are no longer needed at
runtime.

---

## Verified working

- Passive proxy, genuine interface passthrough, registry activation.
- Pipe capture, BGRA -> RGB conversion, shared-memory publishing.
- `Local\GW2LCDShim` 320x240 24bpp matches the physical G19 exactly.
- The viewer (`viewer/`) shows live GW2 output at 1:1 and rebuilds on
  `sequence` bump.
- Full LCore session captured: character select, gameplay, character switching,
  G19 view switching. Frame writes and the shim stayed live throughout.
- LCore is load-bearing: stopped, GW2's `CreateFileW` calls fail and it sends
  nothing. Restoring LCore does **not** trigger a reconnect — GW2 must restart.
- LCore command line: `"C:\Program Files\Logitech Gaming Software\LCore.exe" /minimized`
- `0x0702` correlates with view changes, so it is a server push, not a timer.
  It is now driven by the viewer's soft buttons.

## Resolved questions

- **No device selector needed.** GW2 already emits the 320x240 colour path via
  numeric family negotiation. The mono G19 is a downstream LCore concern.
- **No third-party help.** `g19daemon` (`github.com/mortendynamite/g19daemon`)
  and `G19LCD` (`github.com/endlessmind/G19LCD`) drive G19 over USB directly;
  neither implements `LGLCDPIPE`. `henninglive/logitech-lcd` still depends on
  Logitech software/SDK. USB-direct contradicts the casting goal and there is no
  spare hardware on hand.
- **GW2 does not reliably retry a failed open.** An earlier note here claimed it
  reconnects on its own after ~20s. That was wrong: two reconnects were observed
  at 16-20s, but GW2 was being actively used at the time (zoning, screen
  changes), and its LCD subsystem appears to reinitialise on in-game events. Left
  idle with the server down, GW2 goes completely silent — no reads, no writes, no
  further `CreateFileW` — so it never notices. **Restart GW2 to pick up a
  restarted server.**
- **Proxy timestamps.** Every log line carries a timestamp. The first attempt
  built the line and the timestamp into one buffer and truncated it; it now
  formats into a separate output buffer and tracks the total length.
- **The proxy log cannot be deleted while GW2 runs** — GW2 holds the handle open,
  so `Remove-Item` silently fails and the log keeps its old contents. Truncate
  or rename it instead, or just note the byte offset.

## Client API surface - which `GetInterface(5)` functions GW2 calls

Recovered empirically on 2026-10-02 by wrapping the real `GetInterface(5)`
table (instrumented proxy build: `build.ps1 -Wrap` -> `WRAP_TABLE`). The
wrapper copies the real table and swaps every entry that points into
`LgLcdApi.dll`'s own image for a logging thunk, preserving non-code entries,
so the game still sees a well-formed table.

- `GetInterface(5)` is called **once**; GW2 caches the returned table.
- The table holds **29 code pointers** (`T[00]`-`T[28]`). `T[29]` is `0` and
  `T[30]`/`T[31]` are adjacent `.rdata` bytes (`".?AVCPip..."`), not part of
  the struct.
- GW2 calls exactly **five** slots:

| Slot | Identity | Evidence |
|-----:|----------|----------|
| 0 | `lgLcdInit` | first call, before any pipe traffic |
| 4 | `lgLcdConnectEx` | arg is a context pointer; triggers `CreateFileW` on `LGLCDPIPE` + the 1084-byte handshake |
| 26 | `lgLcdOpen(ByType)` | context pointer; followed by the 16-byte `0x0803` open message |
| 24 | `lgLcdSetAsLCDForegroundApp` | `device=0x65`, `flag=0` then `1`; 20-byte `0x0830` message whose last byte is the flag |
| 13 | `lgLcdUpdateBitmap` | `device=0x65`, `bitmap=0x20655ce8f40`, `priority=0x80`; each call is followed by a 307224-byte frame write (13 calls = 13 frames) |

- Device handle is **`0x65` (101)**; GW2 reuses one bitmap buffer.
- `lgLcdReadSoftButtons` is **never called**: GW2 registers the
  `onSoftbuttonsChanged` callback through the open context instead (605
  background 560-byte reads, no matching `ReadSoftButtons` call).
- Pressing all six soft buttons delivered `0x0702` notifications (bits
  `0x100/0x200/0x400/0x1000/0x2000/0x4000`, each followed by a `0` release) but
  produced **no additional interface calls** - GW2 drives button handling
  entirely through the callback.
- The struct order does **not** match the flat-export order in the public
  `lglcd.h`, and no public mirror defines the version-5 struct (grep.app and
  WebSearch return zero hits for `lgLcdInterface` or the `*Func` typedefs).

Implication for a clean-room client DLL: only those five slots need real
implementations (init / connect / open / updateBitmap / setForeground); the
other 24 can be success-returning stubs, because GW2 never calls them. The
public `lglcd.h` (shipped in `mpc-hc`, `MPC-BE`, `mumble`) supplies every
signature and constant.

## Remaining work

1. ~~Confirm the buttons visually.~~ **DONE 2026-10-02**: all six bits confirmed
   through the viewer; `[<]`/`[>]` cycle GW2's screens with LCore stopped, the
   other four have no visible effect.
2. ~~Deploy the third-party-loading proxy.~~ **DONE 2026-10-02**: the active
   `LgLcdApiProxy.dll` loads `LgLcdApi.dll` from `..\third_party\logitech\`
   (falling back to Program Files); confirmed in the log (`real dll: ...`).
3. Consider tuning `LGLCD_IDLE_MS`. The idle timer is **required** (removing it
   wedges GW2 on a loading screen), but 200ms may be faster than LCore's sparser
   keepalives.
4. Decide whether the proxy stays in the loop. It feeds `Local\GW2LCDShim` and
   is not LGS, so it can stay.
5. Once stable: uninstall LGS 9.04.28 (interactive only, no quiet/modify
   string) and enable/test HVCI.
6. Parallel fallback stack (`main.py`, `mumble.py`, `gw2api.py`, `render.py`)
   still has 404s on `/v2/maps/{id}/poi` and `/v2/maps/{id}/vista`.
7. **Clean-room client DLL (optional).** A replacement for `LgLcdApi.dll`
   would drop the Logitech DLL dependency entirely. See the client-API section
   above: only five functions need real implementations, reusing the existing
   shim/control code.

## Known protocol gaps

Two client message types appeared later in a session that are not in the
captured opening, and the server answers both with a tail keepalive, which GW2
accepts:

| Size | Type | Header words |
|-----:|-----:|--------------|
| 16 | `0x0804` | `(16, seq=8, 0x0804, 0x04080000, 0x20000080)` |
| 12 | `0x0900` | `(12, seq=7, 0x0900, 0x00000700, 0x00000080)` |

`0x0900` is the same type the server uses for keepalives, so the type space is
shared in both directions. Worth capturing a longer session before assuming a
keepalive is always an acceptable answer.

The client's frame header also varies between connections: word 4 was
`0x20000080` on one handle and `0xCCCCCCCC` on the next. That looks like state
GW2 is echoing back, and the `0xCCCCCCCC` value was seen before in a context
that suggested a missing config reply. Unresolved.

---

## Build notes

Build only via the scripts; direct `gcc` needs `C:\w64\bin` on `PATH`.

- MinGW 4.8.2 with old headers. `CancelIoEx`, `GetNamedPipeClientProcessId`
  and `GetNamedPipeInfo` are **not declared** — use the existing dynamic
  `GetProcAddress` typedefs.
- The `ReadFileEx` completion signature in the old headers is unreliable.
  Do not reconstruct asynchronous operations; pass them through.
- Stop `lgpipe_server4.exe` before rebuilding; the exe is locked while running.
- `py gen_replay.py` regenerates `replay_table.inc`, then rebuild the server.
- Python gotcha already hit twice: `"%d" % n * 4` parses as
  `("%d" % n) * 4`. Parenthesise.

---

## Security state

- `HypervisorEnforcedCodeIntegrity Enabled=0` — HVCI is **not** currently on, so
  LCore is not blocking it right now.
- `VBS status=2`, `SecurityServicesRunning=0`.
- Defender exclusions could not be inspected without elevation.
- LGS 9.04.28 has no quiet/modify uninstall string. Keep it installed until the
  replacement server is proven.

---

## Build artefacts

| File | Bytes | State |
|------|------:|-------|
| `LgLcdApiProxy.dll` | 46328 | **active** proxy (observation build; logs mappings/events) |
| `LgLcdApiProxy.thirdparty.dll` | 47270 | loads `LgLcdApi.dll` from `third_party\`; deploy next |
| `lgpipe_server4.exe` | 54036 | **current** server: 5-entry handshake, buttons working |
| `viewer\target\release\gw2lcd-viewer.exe` | 309248 | native Rust viewer |

Earlier experimental proxy builds (`acc`, `capture`, `fullcap`, `rcap`, `safe`,
`ts`, `ts2`, `hooks`) and the old `lgpipe_server*.exe` were removed; rebuild from
source with `build.ps1` / `build-server.ps1` if needed.

---

## Relevant files

All under `R:\SYSTEM\Users\Sisyphos\Documents\Default Project\`.

| Path | Role |
|------|------|
| `lcdproxy\lcdproxy.c` | proxy + hooks + pass-through `ReadFileEx` capture + timestamps |
| `lcdproxy\lgpipe_server.c` | replacement pipe server, one reply per read |
| `lcdproxy\gen_replay.py` | builds `replay_table.inc` from captures, validates run integrity |
| `lcdproxy\replay_table.inc` | generated; hand-corrected to the 5-entry opening |
| `lcdproxy\build.ps1` | proxy build script |
| `lcdproxy\build-server.ps1` | server build script |
| `lcdproxy\registry.ps1` | registry status/restore; `registry-backup.json` is stock |
| `lcdproxy\lgpipe_server.log` | server diagnostics (gitignored) |
| `lcdshim.py` | Python shared-memory reader, used only by `shim\test_shim.py`; the viewer has its own Rust reader |
| `viewer\` | native Rust viewer: `src\main.rs`, `src\shim.rs`, `src\ctl.rs`, `src\win.rs`, `build.ps1` |
| `third_party\logitech\` | genuine Logitech DLLs (gitignored) + README |
| `README.md`, `.gitignore` | repo overview and exclusion rules |
| `C:\Program Files\Logitech Gaming Software\LCore.exe` | service being replaced |
| `C:\Program Files\Logitech Gaming Software\SDK\LCD\x64\LgLcdApi.dll` | stock real client DLL (also copied to `third_party`) |
| `%TEMP%\opencode\gw2proxy.log` | proxy pipe/session log |
| `%TEMP%\opencode\payloads\` | reply dumps + `stream_0_1ef0.bin` |
| `%TEMP%\opencode\lcore_baseline.txt` | saved LCore PID/command line |
| `S:\Programme\Guild Wars 2\Crash.dmp` | capture-build crash dump |
| `main.py`, `mumble.py`, `gw2api.py`, `render.py` | parallel fallback stack |

**Never expose `.env`** in the project root.
