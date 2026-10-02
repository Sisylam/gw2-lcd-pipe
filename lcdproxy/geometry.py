"""Find the true pixel geometry of the captured frames.

The frames were being rendered as 320x240, but the contact sheet came out with
the right edge wrapped onto the left, which means the real row width is not 320.
A frame is always 307200 bytes of BGRA, so the true width and height satisfy
width * height == 76800; only the split is unknown.

The test is objective. For a candidate width, compare how different the last
pixel of a row is from the first pixel of the next row (the "seam") against how
different horizontally adjacent pixels are within rows (the "gradient"). At the
correct width the seam is unremarkable, because consecutive rows really are
consecutive. At a wrong width the seam cuts through the middle of the image and
stands out sharply. The lowest ratio wins.
"""
import struct
import zlib
from pathlib import Path

BYTES = 320 * 240 * 4
PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\Temp_opencode_placeholder")
PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")
CANDIDATES = (240, 256, 272, 288, 300, 304, 320, 336, 352, 368, 384, 400,
              416, 432, 448, 464, 480, 512, 544, 576, 640)


def luma(buf: bytes, i: int) -> int:
    return (buf[i + 2] * 299 + buf[i + 1] * 587 + buf[i] * 114) // 1000


def seam_ratio(frame: bytes, width: int) -> tuple[float, float, float]:
    stride = width * 4
    height = BYTES // stride
    step = max(1, height // 60)
    seams, grads = [], []
    for y in range(0, height - 1, step):
        base = y * stride
        nxt = (y + 1) * stride
        for x in range(0, width - 1, 7):
            a, b = base + x * 4, nxt + x * 4
            seams.append(abs(luma(frame, a) - luma(frame, b)))
        for x in range(0, width - 7, 7):
            a, b = base + x * 4, base + (x + 1) * 4
            grads.append(abs(luma(frame, a) - luma(frame, b)))
    s = sum(seams) / max(len(seams), 1)
    g = sum(grads) / max(len(grads), 1)
    return s, g, (s / g if g else float("inf"))


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


def render(frame: bytes, width: int, path: Path) -> None:
    stride = width * 4
    height = BYTES // stride
    out = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            i = y * stride + x * 4
            j = (y * width + x) * 3
            out[j] = frame[i + 2]
            out[j + 1] = frame[i + 1]
            out[j + 2] = frame[i]
    write_png(path, width, height, bytes(out))


def main() -> int:
    data = (PAYLOADS / "stream_0_1ef0.bin").read_bytes()
    # Frame 0 of this file is the 16-byte SHORT message, so pixels start after it.
    for skip, label in ((0, "skip 0 (assume pixels from byte 0)"),
                        (16, "skip 16 (after the SHORT message)"),
                        (24, "skip 24 (after a 24-byte header)")):
        frame = data[skip:skip + BYTES]
        if len(frame) < BYTES:
            continue
        print(f"\n{label}")
        results = []
        for w in CANDIDATES:
            if BYTES % (w * 4):
                continue
            s, g, r = seam_ratio(frame, w)
            results.append((r, w, s, g))
        results.sort()
        for r, w, s, g in results[:5]:
            print(f"  width {w:4d} x height {BYTES // (w * 4):4d}"
                  f"   seam {s:6.2f}  gradient {g:6.2f}  ratio {r:5.3f}")

    best_w = results[0][1]
    out = PAYLOADS / "screens"
    out.mkdir(exist_ok=True)
    for skip, tag in ((0, "s0"), (16, "s16")):
        render(data[skip:skip + BYTES], best_w, out / f"geom_{tag}_{best_w}.png")
    print(f"\nrendered best candidate width={best_w} to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
