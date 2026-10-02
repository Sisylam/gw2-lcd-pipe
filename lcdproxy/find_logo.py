"""Find the GW2 logo frame and build a contact sheet of every captured screen.

The logo is a red/orange dragon on a white background, so it is easy to pick
out mechanically: a large near-white area plus a cluster of saturated
red/orange pixels. No other LCD screen is mostly white.

The contact sheet puts all screens in one image, in capture order, with their
index drawn on them, so the five screens can be identified by eye in one pass.
"""
import hashlib
import struct
import zlib
from pathlib import Path

W, H = 320, 240
RAW = W * H * 4
PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")
SCREENS = PAYLOADS / "screens"

# Minimal 3x5 digit font, enough to label tiles.
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


def score(buf: bytes) -> tuple[int, int]:
    """Return (near-white pixels, red/orange pixels)."""
    white = hot = 0
    for i in range(0, RAW, 4):
        b, g, r = buf[i], buf[i + 1], buf[i + 2]
        if r > 200 and g > 200 and b > 200:
            white += 1
        elif r > 140 and r - g > 45 and r - b > 70:
            hot += 1
    return white, hot


def stamp(img: bytearray, ox: int, oy: int, text: str) -> None:
    """Draw text as blocky 3x5 digits, scaled 2x, top-left of tile."""
    x = ox + 4
    for ch in text:
        glyph = FONT.get(ch)
        if not glyph:
            x += 8
            continue
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit != "1":
                    continue
                for dy in range(2):
                    for dx in range(2):
                        px, py = x + gx * 2 + dx, oy + 4 + gy * 2 + dy
                        if 0 <= px < W and 0 <= py < H:
                            j = (py * W + px) * 3
                            img[j:j + 3] = b"\xff\x00\xff"      # magenta
        x += 8


def main() -> int:
    data = (PAYLOADS / "stream_0_1ef0.bin").read_bytes()
    n = len(data) // RAW
    tiles = []
    for i in range(n):
        buf = data[i * RAW:(i + 1) * RAW]
        tiles.append((i, buf, to_rgb(buf), score(buf)))

    print("frame  white%   red/orange%")
    for i, _, _, (w, h) in tiles:
        print(f"  {i:2d}   {w * 100 / (W * H):5.1f}   {h * 100 / (W * H):5.1f}")

    best = max(tiles, key=lambda t: t[3][0])
    print(f"\nmost-white frame: {best[0]} "
          f"({best[3][0] * 100 / (W * H):.1f}% white, "
          f"{best[3][1] * 100 / (W * H):.1f}% red/orange)")

    cols, tw, th, pad = 5, 320, 240, 6
    rows = (len(tiles) + cols - 1) // cols
    sheet_w = cols * (tw + pad) + pad
    sheet_h = rows * (th + pad) + pad
    sheet = bytearray(b"\x20\x20\x28" * sheet_w * sheet_h)

    for idx, (_, _, img, _) in enumerate(tiles):
        ox = pad + (idx % cols) * (tw + pad)
        oy = pad + (idx // cols) * (th + pad)
        stamp(img, ox, oy, f"{idx:02d}")
        for y in range(th):
            src = y * tw * 3
            dst = ((oy + y) * sheet_w + ox) * 3
            sheet[dst:dst + tw * 3] = img[src:src + tw * 3]

    out = SCREENS / "contact_sheet.png"
    write_png(out, sheet_w, sheet_h, bytes(sheet))
    print(f"\ncontact sheet: {out}  ({sheet_w}x{sheet_h}, {cols} per row)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
