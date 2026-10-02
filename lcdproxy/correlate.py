"""Pair captured frames with the pipe traffic around them, to find what triggers
a screen change.

The frames and the message log describe the same session from two sides. Each
stream file holds the frames published for one pipe handle, in order, and the
proxy log records a timestamped line for every message on every handle. Pairing
frame i with the i-th frame-sized write on its handle means that for each frame
we can print exactly which messages GW2 sent in the gap before it - and those
are the candidates for whatever caused the screen to change.

Screen labels are the ones identified by eye from the contact sheet.
"""
import re
import struct
from pathlib import Path

PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")
PROXY = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\gw2proxy.log")
W, H = 320, 240
RAW = W * H * 4

# Tile numbers as identified on all_frames.png (1-indexed, 8 per row).
LABELS: dict[int, str] = {}
for t in (1, 8, 11, 14, 35, 38, 43, 48, 51, 56, 59):
    LABELS[t] = "LOGO"
for t in (22, 26):
    LABELS[t] = "combat log"
for t in (6, 7, 21, 27):
    LABELS[t] = "character sheet"
for t in (5, 20, 28):
    LABELS[t] = "WvW"
for t in (4, 19, 24, 29, 30, 31, 32, 33, 34):
    LABELS[t] = "Current Map"

# Tile order as printed by build_sheet.py: streams in name order, frames in order.
TILES: list[tuple[str, int]] = []
for path in sorted(PAYLOADS.glob("stream_*.bin")):
    data = path.read_bytes()
    for i in range(len(data) // RAW):
        TILES.append((path.stem, i))

LINE = re.compile(
    r"(?P<clock>\d\d:\d\d:\d\d\.\d+) \+(?P<ms>\d+)ms "
    r"(?P<op>RP|R|C|W|CREATE)\b"
    r"(?: h=(?P<handle>[0-9a-f]+))?"
    r"(?: n=(?P<n>\d+))?"
    r"(?: got=(?P<got>\d+))?"
    r"(?: head=(?P<head>[0-9a-f]+))?")


def main() -> int:
    events = []
    for line in PROXY.read_text(errors="replace").splitlines():
        m = LINE.search(line)
        if m:
            events.append(m.groupdict())
    print(f"{len(events)} log events\n")

    # Frame-sized writes, in order, per handle.
    frames_by_handle: dict[str, list[int]] = {}
    for idx, ev in enumerate(events):
        if ev["op"] == "W" and ev["n"] and int(ev["n"]) == RAW + 24:
            frames_by_handle.setdefault(ev["handle"], []).append(idx)

    frames_per: dict[str, int] = {}
    for stem, _ in TILES:
        frames_per[stem] = frames_per.get(stem, 0) + 1

    print("frames captured vs frame writes seen in the log\n")
    for stem, count in frames_per.items():
        got = len(frames_by_handle.get(stem.split("_")[-1], []))
        print(f"  {stem:<18} {count:2d} frames captured, "
              f"{got:2d} frame writes logged")

    print("\nper-frame: screen, and the messages since the previous frame\n")
    for t, (stem, idx) in enumerate(TILES, 1):
        label = LABELS.get(t, "World Completion")
        idxs = frames_by_handle.get(stem.split("_")[-1], [])
        if idx >= len(idxs):
            print(f"  tile {t:2d} {stem}#{idx:<2d} {label:<16} (no matching log write)")
            continue
        at = idxs[idx]
        prev = idxs[idx - 1] if idx else None
        if prev is None:
            print(f"  tile {t:2d} {stem}#{idx:<2d} {label:<16} (first frame on this handle)")
            continue
        gap = []
        for ev in events[prev + 1:at]:
            op, n, head = ev["op"], ev["n"], ev["head"]
            if op == "W" and n and int(n) != RAW + 24:
                gap.append(f"W n={n} {head}")
            elif op in ("RP", "R"):
                gap.append(f"{op} n={ev['n']} got={ev['got']} {head}")
            elif op == "C":
                gap.append(f"CONNECT h={ev['handle']}")
        shown = "; ".join(gap) if gap else "(nothing between frames)"
        print(f"  tile {t:2d} {stem}#{idx:<2d} {label:<16} {shown}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
