"""Render the raw LCD framebuffer capture to PNGs so the screens can be identified.

`stream_*.bin` in the payload directory is a dump of raw 320x240 BGRA pixels,
not protocol frames - a 307224-byte stride lands mid-image and yields chunks
whose "headers" are colour values. The real frame is 307200 bytes. Only the
first message in such a dump carries a 24-byte header, so try both alignments
and keep whichever produces images that are not uniformly one colour.

Frames are written to `screens/` and are the evidence for how many distinct
screens GW2 shows; the user identifies them by eye.
"""
import hashlib
import struct
import sys
from pathlib import Path

RAW_FRAME = 320 * 240 * 4          # 307200, no header
MSG_FRAME = RAW_FRAME + 24         # 307224, header + pixels
HEADER = 24

PAYLOADS = Path(r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\payloads")


def looks_like_frame(buf: bytes) -> bool:
    """A real LCD image has many distinct colours; a misaligned cut rarely does."""
    seen = set()
    for off in range(0, len(buf), 4 * 97):        # sparse sample, BGRA
        seen.add(buf[off:off + 3])
        if len(seen) > 24:
            return True
    return len(seen) > 8


def to_rgb(buf: bytes) -> bytes:
    out = bytearray(len(buf) // 4 * 3)
    for i in range(0, len(buf), 4):
        b, g, r = buf[i], buf[i + 1], buf[i + 2]
        j = (i // 4) * 3
        out[j:j + 3] = bytes((r, g, b))
    return bytes(out)


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    """Minimal PNG writer - this interpreter has no PIL."""
    import zlib

    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)                                  # filter type 0
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


def main() -> int:
    name = sys.argv[1] if len(sys.argv) > 1 else "stream_0_1ef0.bin"
    path = PAYLOADS / name
    if not path.is_file():
        print(f"no such capture: {path}")
        return 1
    data = path.read_bytes()

    out = PAYLOADS / "screens"
    out.mkdir(exist_ok=True)
    for old in out.glob("*.png"):
        old.unlink()

    # Find the alignment that yields plausible images, then render in order.
    best = None
    for skip in range(0, 64):
        n = (len(data) - skip) // RAW_FRAME
        if n < 2:
            continue
        good = sum(looks_like_frame(data[skip + i * RAW_FRAME:
                                           skip + (i + 1) * RAW_FRAME])
                   for i in range(n))
        if best is None or good > best[1]:
            best = (skip, good, n)

    skip, good, n = best
    print(f"{path.name}: {len(data)} bytes")
    print(f"alignment: skip={skip} stride={RAW_FRAME} -> {n} frames, "
          f"{good} look like real images")

    try:
        import PIL  # noqa: F401
        have_pil = True
    except ImportError:
        have_pil = False
    print("PIL available" if have_pil else "PIL absent, using built-in writer")

    digests: dict[str, list[int]] = {}
    for i in range(n):
        chunk = data[skip + i * RAW_FRAME: skip + (i + 1) * RAW_FRAME]
        if not looks_like_frame(chunk):
            continue
        key = hashlib.sha256(chunk).hexdigest()[:12]
        digests.setdefault(key, []).append(i)

    print(f"{len(digests)} distinct images\n")
    for n_img, (key, members) in enumerate(sorted(digests.items(),
                                                  key=lambda kv: kv[1][0]), 1):
        chunk = data[skip + members[0] * RAW_FRAME:
                     skip + (members[0] + 1) * RAW_FRAME]
        rgb = to_rgb(chunk)
        fn = out / f"img{n_img:02d}_at{','.join(map(str, members))}.png"
        if have_pil:
            from PIL import Image
            Image.frombytes("RGB", (320, 240), rgb).save(fn)
        else:
            write_png(fn, 320, 240, rgb)
        print(f"  {n_img:2d}: {fn.name}")

    print(f"\nwritten to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
