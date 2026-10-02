"""Reader for the LogitechLcdShim shared-memory framebuffer.

The shim publishes GW2's rendered LCD frame in the named section
Local\\GW2LCDShim: a 48-byte header followed by the pixels (320x240x3 for the
colour G19 panel, 128x48x1 for monochrome).
"""

import ctypes
from ctypes import wintypes
import struct

KERNEL32 = ctypes.windll.kernel32
FILE_MAP_READ = 0x0004
SHM_NAME = "Local\\GW2LCDShim"
MAGIC = 0x32313047
VERSION = 1

KERNEL32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
KERNEL32.OpenFileMappingW.restype = wintypes.HANDLE
KERNEL32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
KERNEL32.MapViewOfFile.restype = ctypes.c_void_p
KERNEL32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
KERNEL32.UnmapViewOfFile.restype = wintypes.BOOL
KERNEL32.CloseHandle.argtypes = [wintypes.HANDLE]
KERNEL32.CloseHandle.restype = wintypes.BOOL

HDR_FMT = "<12I"
HDR_SIZE = struct.calcsize(HDR_FMT)
MAX_PIXELS = 320 * 240 * 3


class ShimFrame:
    __slots__ = ("width", "height", "bpp", "active", "sequence", "connected",
                 "init_count", "text_count", "update_count", "rgb", "mono")

    def __init__(self):
        self.width = 0
        self.height = 0
        self.bpp = 0
        self.active = 0
        self.sequence = 0
        self.connected = 0
        self.init_count = 0
        self.text_count = 0
        self.update_count = 0
        self.rgb = b""
        self.mono = b""


class Shim:
    def __init__(self):
        self.h = None
        self.view = None
        self.last_seq = -1

    def open(self) -> bool:
        h = KERNEL32.OpenFileMappingW(FILE_MAP_READ, 0, SHM_NAME)
        if not h:
            self.close()
            return False
        v = KERNEL32.MapViewOfFile(h, FILE_MAP_READ, 0, 0, HDR_SIZE + MAX_PIXELS)
        if not v:
            KERNEL32.CloseHandle(h)
            self.close()
            return False
        self.h = h
        self.view = v
        return True

    def close(self):
        if self.view:
            KERNEL32.UnmapViewOfFile(ctypes.c_void_p(self.view))
            self.view = None
        if self.h:
            KERNEL32.CloseHandle(self.h)
            self.h = None

    def read(self) -> ShimFrame | None:
        if not self.view:
            return None
        raw = ctypes.string_at(self.view, HDR_SIZE)
        (magic, version, width, height, bpp, active, sequence,
         connected, init_count, text_count, update_count, _res) = struct.unpack(
            HDR_FMT, raw)
        if magic != MAGIC or version != VERSION:
            return None
        f = ShimFrame()
        f.width = width
        f.height = height
        f.bpp = bpp
        f.active = active
        f.sequence = sequence
        f.connected = connected
        f.init_count = init_count
        f.text_count = text_count
        f.update_count = update_count
        if active == 1 and width and height:
            n = width * height * 3
            f.rgb = ctypes.string_at(ctypes.c_void_p(self.view + HDR_SIZE), n)
        elif active == 2 and width and height:
            f.mono = ctypes.string_at(
                ctypes.c_void_p(self.view + HDR_SIZE), width * height)
        return f

    def is_new(self, f: ShimFrame) -> bool:
        if f.sequence == self.last_seq:
            return False
        self.last_seq = f.sequence
        return True
