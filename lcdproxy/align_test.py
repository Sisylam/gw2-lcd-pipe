"""Decide whether each captured frame carries a 24-byte header, using a test
that is sensitive to horizontal alignment.

The seam test in geometry.py establishes the width (320) by comparing rows, but
it is blind to a uniform horizontal shift - shifting every row sideways changes
no row-to-row relationship. The reported distortion ("right edge wrapped onto
the left") is exactly a horizontal shift, so it needs its own test.

The logo is ideal for this: a centred dragon on a white field. In a correctly
aligned frame the white margin to the left of the artwork equals the margin to
the right. Shift the frame by k pixels and the two margins differ by k.

Two hypotheses are compared:
  A  stride 307200, pixels straight from the frame start (no header)
  B  stride 307224, pixels after a 24-byte header
"""
import struct
from pathlib import Path

W, H = 320, 240
RAW = W * H * 4
MSG = RAW + 24
PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")


def margins(buf: bytes) -> tuple[int, int, int]:
    """White margin left and right of the non-white artwork, over all rows."""
    lefts, rights = [], []
    for y in range(H):
        row = y * W * 4
        first = last = None
        for x in range(W):
            i = row + x * 4
            b, g, r = buf[i], buf[i + 1], buf[i + 2]
            if not (r > 235 and g > 235 and b > 235):
                if first is None:
                    first = x
                last = x
        if first is not None:
            lefts.append(first)
            rights.append(W - 1 - last)
    if not lefts:
        return -1, -1, 0
    return (round(sum(lefts) / len(lefts)), round(sum(rights) / len(rights)),
            len(lefts))


def main() -> int:
    data = (PAYLOADS / "stream_0_1ef0.bin").read_bytes()
    print("A: stride 307200, no header")
    for i in range(6):
        off = i * RAW
        if off + RAW > len(data):
            break
        l, r, rows = margins(data[off:off + RAW])
        print(f"  frame {i}: left margin {l:3d}  right margin {r:3d}  "
              f"skew {l - r:+3d}  ({rows} rows with artwork)")

    print("\nB: stride 307224, pixels after a 24-byte header")
    for i in range(6):
        off = i * MSG + 24
        if off + RAW > len(data):
            break
        l, r, rows = margins(data[off:off + RAW])
        print(f"  frame {i}: left margin {l:3d}  right margin {r:3d}  "
              f"skew {l - r:+3d}  ({rows} rows with artwork)")

    print("\nSkew should be ~0 when the alignment is right.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
