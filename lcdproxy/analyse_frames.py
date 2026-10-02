"""Split a captured LCore frame stream into frames and group them by content.

The goal is to find out how many *distinct* screens GW2 actually puts on the
LCD, without touching the running game. Each frame is a 24-byte header
followed by 320x240 BGRA pixels; only the pixel data is hashed, so frames that
differ solely in the header's sequence/counter fields group together.
"""
import hashlib
import struct
import sys
from pathlib import Path

FRAME_BYTES = 307224
HEADER_BYTES = 24
WIDTH, HEIGHT = 320, 240

PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")
OUT = PAYLOADS / "unique"


def frames(data: bytes):
    for off in range(0, len(data) - FRAME_BYTES + 1, FRAME_BYTES):
        yield off, data[off:off + FRAME_BYTES]


def describe(frame: bytes) -> str:
    head = struct.unpack_from("<6I", frame, 0)
    return " ".join(f"0x{v:08x}" for v in head)


def main() -> int:
    target = sys.argv[1] if len(sys.argv) > 1 else "stream_0_1ef0.bin"
    path = PAYLOADS / target
    if not path.is_file():
        print(f"no such capture: {path}")
        return 1

    data = path.read_bytes()
    total = len(data) // FRAME_BYTES
    print(f"{path.name}: {len(data)} bytes = {total} whole frames "
          f"({len(data) % FRAME_BYTES} trailing)")

    OUT.mkdir(exist_ok=True)
    groups: dict[str, list[int]] = {}

    for i, (_, frame) in enumerate(frames(data)):
        pixels = frame[HEADER_BYTES:]
        key = hashlib.sha256(pixels).hexdigest()[:16]
        groups.setdefault(key, []).append(i)

    print(f"\n{len(groups)} distinct screens out of {total} frames\n")
    for n, (key, members) in enumerate(sorted(groups.items(),
                                             key=lambda kv: kv[1][0]), 1):
        first = members[0]
        off = first * FRAME_BYTES
        head = describe(data[off:off + FRAME_BYTES])
        print(f"  screen {n}: frames {members}")
        print(f"            content {key}  header {head}")
        (OUT / f"screen{n:02d}_frames{'_'.join(map(str, members))}.bin") \
            .write_bytes(data[off:off + FRAME_BYTES])

    print(f"\nper-frame dumps written to {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
