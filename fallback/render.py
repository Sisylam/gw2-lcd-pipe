from __future__ import annotations
from datetime import datetime


class Framebuf:
    def __init__(self, w: int = 320, h: int = 240):
        self.w = w
        self.h = h
        self.data = bytearray(b"\x00" * (w * h * 3))

    def set_rgb(self, x: int, y: int, r: int, g: int, b: int):
        if x < 0 or x >= self.w or y < 0 or y >= self.h:
            return
        i = (y * self.w + x) * 3
        self.data[i] = max(0, min(255, b))
        self.data[i + 1] = max(0, min(255, g))
        self.data[i + 2] = max(0, min(255, r))

    def fill(self, r: int, g: int, b: int):
        for y in range(self.h):
            for x in range(self.w):
                self.set_rgb(x, y, r, g, b)

    def rect(self, x, y, w, h, r, g, b, filled=False):
        if filled:
            for yy in range(y, min(y + h, self.h)):
                for xx in range(x, min(x + w, self.w)):
                    self.set_rgb(xx, yy, r, g, b)
            return
        for xx in range(x, min(x + w, self.w)):
            self.set_rgb(xx, y, r, g, b)
            if y + h - 1 < self.h:
                self.set_rgb(xx, y + h - 1, r, g, b)
        for yy in range(y + 1, min(y + h - 1, self.h)):
            self.set_rgb(x, yy, r, g, b)
            if x + w - 1 < self.w:
                self.set_rgb(x + w - 1, yy, r, g, b)

    def draw_text(self, x, y, s: str, r=255, g=255, b=255):
        for ch in s:
            self._char(x, y, ch, r, g, b)
            x += 6
            if x > self.w - 6:
                x = 4
                y += 10
            if y > self.h - 10:
                break

    def _char(self, x, y, c, r, g, b):
        font5x7 = {
            " ": [],
            "0": [0x3E, 0x41, 0x41, 0x41, 0x3E],
            "1": [0x10, 0x20, 0x40, 0x7F, 0x00],
            "2": [0x72, 0x49, 0x49, 0x49, 0x46],
            "3": [0x22, 0x41, 0x49, 0x49, 0x36],
            "4": [0x18, 0x14, 0x12, 0x7F, 0x10],
            "5": [0x27, 0x45, 0x45, 0x45, 0x39],
            "6": [0x3C, 0x4A, 0x49, 0x49, 0x30],
            "7": [0x40, 0x47, 0x48, 0x50, 0x60],
            "8": [0x36, 0x49, 0x49, 0x49, 0x36],
            "9": [0x06, 0x49, 0x49, 0x29, 0x1E],
            ".": [0x00, 0x60, 0x60, 0x00, 0x00],
            ",": [0x00, 0x30, 0x30, 0x10, 0x00],
            ":": [0x00, 0x24, 0x24, 0x00, 0x00],
            "-": [0x08, 0x08, 0x08, 0x08, 0x08],
            "+": [0x08, 0x08, 0x3E, 0x08, 0x08],
            "[": [0x3E, 0x41, 0x00, 0x00, 0x00],
            "]": [0x00, 0x00, 0x41, 0x3E, 0x00],
            "(": [0x1C, 0x22, 0x41, 0x00, 0x00],
            ")": [0x00, 0x00, 0x41, 0x22, 0x1C],
            "%": [0x12, 0x08, 0x04, 0x02, 0x09],
            "/": [0x40, 0x20, 0x18, 0x06, 0x01],
            "A": [0x3F, 0x44, 0x44, 0x44, 0x3F],
            "B": [0x7F, 0x49, 0x49, 0x49, 0x36],
            "C": [0x3E, 0x41, 0x41, 0x41, 0x22],
            "D": [0x7F, 0x41, 0x41, 0x41, 0x3E],
            "E": [0x7F, 0x49, 0x49, 0x49, 0x41],
            "F": [0x7F, 0x48, 0x48, 0x48, 0x40],
            "G": [0x3E, 0x41, 0x41, 0x49, 0x2E],
            "H": [0x7F, 0x08, 0x08, 0x08, 0x7F],
            "I": [0x41, 0x41, 0x7F, 0x41, 0x41],
            "J": [0x20, 0x40, 0x41, 0x3F, 0x01],
            "K": [0x7F, 0x08, 0x14, 0x22, 0x41],
            "L": [0x7F, 0x01, 0x01, 0x01, 0x01],
            "M": [0x7F, 0x20, 0x10, 0x20, 0x7F],
            "N": [0x7F, 0x10, 0x08, 0x04, 0x7F],
            "O": [0x3E, 0x41, 0x41, 0x41, 0x3E],
            "P": [0x7F, 0x48, 0x48, 0x48, 0x30],
            "Q": [0x3E, 0x41, 0x41, 0x21, 0x5E],
            "R": [0x7F, 0x48, 0x48, 0x28, 0x17],
            "S": [0x32, 0x49, 0x49, 0x49, 0x26],
            "T": [0x40, 0x40, 0x7F, 0x40, 0x40],
            "U": [0x3F, 0x01, 0x01, 0x01, 0x3F],
            "V": [0x3F, 0x01, 0x02, 0x04, 0x38],
            "W": [0x3F, 0x01, 0x0E, 0x01, 0x3F],
            "X": [0x63, 0x14, 0x08, 0x14, 0x63],
            "Y": [0x60, 0x10, 0x0F, 0x10, 0x60],
            "Z": [0x43, 0x45, 0x49, 0x51, 0x61],
            "a": [0x1C, 0x22, 0x22, 0x14, 0x3E],
            "b": [0x7F, 0x12, 0x22, 0x22, 0x1C],
            "c": [0x1C, 0x22, 0x22, 0x22, 0x10],
            "d": [0x1C, 0x22, 0x22, 0x12, 0x7F],
            "e": [0x1C, 0x2A, 0x2A, 0x2A, 0x18],
            "f": [0x08, 0x7E, 0x09, 0x09, 0x00],
            "g": [0x18, 0x25, 0x25, 0x25, 0x3E],
            "h": [0x7F, 0x08, 0x10, 0x10, 0x0E],
            "i": [0x00, 0x22, 0x5E, 0x20, 0x00],
            "j": [0x00, 0x40, 0x20, 0x10, 0x3E],
            "k": [0x7F, 0x04, 0x0A, 0x12, 0x20],
            "l": [0x3F, 0x20, 0x20, 0x20, 0x00],
            "m": [0x3E, 0x20, 0x18, 0x20, 0x3E],
            "n": [0x3E, 0x10, 0x20, 0x20, 0x1E],
            "o": [0x1C, 0x22, 0x22, 0x22, 0x1C],
            "p": [0x3E, 0x14, 0x22, 0x22, 0x1C],
            "q": [0x1C, 0x22, 0x22, 0x14, 0x3E],
            "r": [0x3E, 0x08, 0x10, 0x10, 0x08],
            "s": [0x10, 0x2A, 0x2A, 0x2A, 0x04],
            "t": [0x10, 0x3E, 0x10, 0x10, 0x00],
            "u": [0x3C, 0x02, 0x02, 0x04, 0x3E],
            "v": [0x3C, 0x02, 0x04, 0x08, 0x30],
            "w": [0x3C, 0x02, 0x0C, 0x02, 0x3C],
            "x": [0x22, 0x14, 0x08, 0x14, 0x22],
            "y": [0x38, 0x05, 0x05, 0x09, 0x3E],
            "z": [0x22, 0x26, 0x2A, 0x32, 0x22],
        }
        pat = font5x7.get(c.upper(), [])
        for row, colmask in enumerate(pat):
            for col in range(5):
                if colmask & (0x10 >> col):
                    self.set_rgb(x + col, y + row, r, g, b)


    def to_ppm(self, scale: int = 1) -> bytes:
        """Binary P6 PPM, nearest-neighbour upscaled. Feeds tkinter.PhotoImage."""
        w = self.w * scale
        h = self.h * scale
        row = bytearray()
        for x in range(self.w):
            i = x * 3
            r, g, b = self.data[i], self.data[i + 1], self.data[i + 2]
            row += bytes((r, g, b)) * scale
        header = f"P6\n{w} {h}\n255\n".encode("ascii")
        return header + bytes(row) * (h)


def render_char_panel(st) -> Framebuf:
    fb = Framebuf(320, 240)
    fb.fill(12, 14, 18)
    cd = st.char
    y = 6
    fb.draw_text(6, y, cd.name or "- no character -", 255, 255, 255)
    y += 11
    fb.draw_text(6, y, f"Level {cd.level}  {cd.race}  {cd.profession}", 200, 205, 215)
    y += 13
    fb.rect(4, y, 312, 1, 70, 76, 88, filled=True)
    y += 6
    fb.draw_text(6, y, f"World  {cd.world or '-'}", 190, 225, 255)
    y += 11
    fb.draw_text(6, y, f"Zone   {cd.map_name or '-'}", 190, 225, 255)
    if cd.guild:
        y += 11
        fb.draw_text(6, y, f"Guild  {cd.guild}", 190, 225, 255)
    y += 13
    fb.rect(4, y, 312, 1, 70, 76, 88, filled=True)
    y += 6
    fb.draw_text(6, y, f"Deaths {cd.deaths}", 170, 175, 185)
    fb.draw_text(120, y, "In world" if cd.in_world else "Not in world", 170, 175, 185)
    y += 13
    fb.rect(4, y, 312, 1, 70, 76, 88, filled=True)
    y += 6
    fb.draw_text(6, y, "ATTRIBUTES", 120, 128, 140)
    y += 11
    fb.draw_text(6, y, "Power -    Precision -    Toughness -", 150, 150, 150)
    y += 11
    fb.draw_text(6, y, "Concen -   Condition -    Healing  -", 150, 150, 150)
    y += 11
    fb.draw_text(6, y, "Health -   (not exposed by API)", 110, 112, 120)
    y += 13
    fb.rect(4, y, 312, 1, 70, 76, 88, filled=True)
    y += 6
    status = []
    status.append("MUMBLE OK" if st.has_mumble else "MUMBLE NO")
    status.append("API OK" if st.has_api else "API NO")
    fb.draw_text(6, y, "   ".join(status), 90, 175, 95)
    y += 11
    fb.draw_text(
        6, y, datetime.now().strftime("%H:%M:%S") + f"   tick {cd.tick}",
        120, 124, 134,
    )
    return fb

