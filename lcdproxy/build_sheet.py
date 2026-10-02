"""Build a labelled contact sheet of every captured LCD frame, and sweep the
clustering threshold to check whether one layout is really one layout.

The coarse signature in group_screens.py used a fixed distance threshold, which
could merge distinct screens that happen to share a layout. Here the threshold
is swept, so the number of clusters at each value shows whether the gameplay
frames have internal structure or are genuinely a single screen.

Every tile is numbered and the capture/index it came from is printed, so the
screens can be identified by eye and referred to unambiguously afterwards.
"""
import struct
import zlib
from pathlib import Path

W, H = 320, 240
RAW = W * H * 4
GW, GH = 16, 12
PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")
OUT = PAYLOADS / "screens"

FONT = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"),
    "7": ("111", "001", "010", "010", "010"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "111"),
}


def write_png(path: Path, w: int, h: int, rgb: bytes) -> None:
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xffffffff))

    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
                     + chunk(b"IEND", b""))


def to_rgb(buf: bytes) -> bytearray:
    out = bytearray(RAW // 4 * 3)
    for i in range(0, RAW, 4):
        j = (i // 4) * 3
        out[j] = buf[i + 2]
        out[j + 1] = buf[i + 1]
        out[j + 2] = buf[i]
    return out


def signature(buf: bytes) -> list[int]:
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
    step = 4 * 31
    samples = list(range(0, RAW, step))
    white = sum(1 for i in samples
                if buf[i + 2] > 200 and buf[i + 1] > 200 and buf[i] > 200)
    return white > len(samples) * 0.4


def dist(a, b) -> float:
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def stamp(img: bytearray, text: str) -> None:
    x, y = 5, 5
    for ch in text:
        glyph = FONT.get(ch)
        if glyph:
            for gy, row in enumerate(glyph):
                for gx, bit in enumerate(row):
                    if bit == "1":
                        for dy in range(2):
                            for dx in range(2):
                                px, py = x + gx * 2 + dx, y + gy * 2 + dy
                                j = (py * W + px) * 3
                                img[j:j + 3] = b"\xff\x00\xff"
        x += 8


def main() -> int:
    frames = []
    for path in sorted(PAYLOADS.glob("stream_*.bin")):
        data = path.read_bytes()
        for i in range(len(data) // RAW):
            buf = data[i * RAW:(i + 1) * RAW]
            frames.append({"src": f"{path.stem}#{i}", "buf": buf,
                           "rgb": to_rgb(buf), "sig": signature(buf),
                           "logo": is_logo(buf)})

    print(f"{len(frames)} frames\n")
    print("threshold sweep over non-logo frames:")
    play = [f for f in frames if not f["logo"]]
    for thr in (4, 6, 8, 10, 14, 22, 40):
        clusters = []
        for f in play:
            for c in clusters:
                if dist(f["sig"], c[0]["sig"]) < thr:
                    c.append(f)
                    break
            else:
                clusters.append([f])
        sizes = sorted((len(c) for c in clusters), reverse=True)
        print(f"  thr={thr:3d}: {len(clusters):2d} clusters, sizes {sizes[:8]}")

    cols, pad = 8, 6
    rows = (len(frames) + cols - 1) // cols
    sw = cols * (W + pad) + pad
    sh = rows * (H + pad) + pad
    sheet = bytearray(b"\x18\x18\x20" * sw * sh)

    print("\ntile map:")
    for n, f in enumerate(frames, 1):
        ox = pad + ((n - 1) % cols) * (W + pad)
        oy = pad + ((n - 1) // cols) * (H + pad)
        img = bytearray(f["rgb"])
        stamp(img, f"{n:02d}")
        for y in range(H):
            s = y * W * 3
            d = ((oy + y) * sw + ox) * 3
            sheet[d:d + W * 3] = img[s:s + W * 3]
        tag = "LOGO" if f["logo"] else ""
        print(f"  {n:2d}  {f['src']:<22} {tag}")

    out = OUT / "all_frames.png"
    write_png(out, sw, sh, bytes(sheet))
    print(f"\n{out}  ({sw}x{sh}, {cols} per row)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
