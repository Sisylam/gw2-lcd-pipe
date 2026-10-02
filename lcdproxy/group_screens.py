"""Group captured LCD frames by screen layout, across every stream capture.

Frames from one screen are never byte-identical - the minimap animates, numbers
tick - so exact hashing reports every frame as unique. Instead each frame is
reduced to a coarse grayscale signature (an average hash of a 16x12 grid) and
frames are clustered by how far apart their signatures are. That separates
"same screen, slightly different" from "genuinely different screen".

The GW2 logo is detected separately: it is overwhelmingly white, which nothing
else on these screens ever is.
"""
import struct
import zlib
from pathlib import Path

W, H = 320, 240
RAW = W * H * 4
GW, GH = 16, 12
PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")


def gray_signature(buf: bytes) -> list[int]:
    """Mean brightness of each cell of a 16x12 grid."""
    cells = []
    for gy in range(GH):
        for gx in range(GW):
            x0, x1 = gx * W // GW, (gx + 1) * W // GW
            y0, y1 = gy * H // GH, (gy + 1) * H // GH
            tot = n = 0
            for y in range(y0, y1, 3):
                row = y * W
                for x in range(x0, x1, 3):
                    i = (row + x) * 4
                    tot += (buf[i + 2] * 299 + buf[i + 1] * 587 + buf[i] * 114) // 1000
                    n += 1
            cells.append(tot // max(n, 1))
    return cells


def is_logo(buf: bytes) -> bool:
    white = 0
    for i in range(0, RAW, 4 * 31):
        b, g, r = buf[i], buf[i + 1], buf[i + 2]
        if r > 200 and g > 200 and b > 200:
            white += 1
    return white > len(range(0, RAW, 4 * 31)) * 0.4


def dist(a: list[int], b: list[int]) -> float:
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def main() -> int:
    frames = []
    for path in sorted(PAYLOADS.glob("stream_*.bin")):
        data = path.read_bytes()
        for i in range(len(data) // RAW):
            buf = data[i * RAW:(i + 1) * RAW]
            frames.append((path.stem, i, buf, gray_signature(buf)))

    print(f"{len(frames)} frames from {len(list(PAYLOADS.glob('stream_*.bin')))} captures\n")

    logos = [f for f in frames if is_logo(f[2])]
    print(f"GW2 logo frames ({len(logos)}): " +
          ", ".join(f"{s}#{i}" for s, i, _, _ in logos))

    blanks = [f for f in frames
              if not is_logo(f[2])
              and all(v < 12 for v in f[3])]
    print(f"near-black frames ({len(blanks)}): " +
          ", ".join(f"{s}#{i}" for s, i, _, _ in blanks))

    rest = [f for f in frames if f not in logos and f not in blanks]
    print(f"\nremaining gameplay frames: {len(rest)}")

    clusters = []
    for f in sorted(rest, key=lambda f: (f[0], f[1])):
        for c in clusters:
            if dist(f[3], c[0][3]) < 22:
                c.append(f)
                break
        else:
            clusters.append([f])

    clusters.sort(key=lambda c: -len(c))
    print(f"\n{len(clusters)} distinct layouts (threshold 22):\n")
    for n, c in enumerate(clusters, 1):
        members = ", ".join(f"{s}#{i}" for s, i, _, _ in c)
        mean = sum(sum(f[3]) for f in c) / (len(c) * GW * GH) / 255 * 100
        print(f"  layout {n}: {len(c)} frames, mean brightness {mean:4.1f}%")
        print(f"            {members}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
