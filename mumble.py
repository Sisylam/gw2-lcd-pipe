# Windows-only MumbleLink reader
# Reads the Win32 named section \\BaseNamedObjects\\MumbleLink
# Layout: https://wiki.guildwars2.com/wiki/API:MumbleLink (uiVersion 2)

import ctypes
from ctypes import wintypes
import struct


KERNEL32 = ctypes.windll.kernel32
FILE_MAP_READ = 0x0004
S_NAME = "MumbleLink"
TOTAL_SIZE = 5460

# 64-bit safe prototypes: without these, ctypes defaults restype to c_int and
# truncates handles/pointers, causing an access violation.
KERNEL32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
KERNEL32.OpenFileMappingW.restype = wintypes.HANDLE
KERNEL32.MapViewOfFile.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.DWORD,
    ctypes.c_size_t,
]
KERNEL32.MapViewOfFile.restype = ctypes.c_void_p
KERNEL32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
KERNEL32.UnmapViewOfFile.restype = wintypes.BOOL
KERNEL32.CloseHandle.argtypes = [wintypes.HANDLE]
KERNEL32.CloseHandle.restype = wintypes.BOOL

# Offsets (LinkedMem, current GW2)
UI_VERSION = 0
UI_TICK = 4
AVATAR_POS = 8  # float x,y,z
NAME = 44  # wchar_t[256]
IDENTITY = 592  # wchar_t[256]
CONTEXT_LEN = 1104
CONTEXT = 1108
DESCRIPTION = 1364

# Context offsets
CTX_MAP_ID = 28
CTX_MAP_TYPE = 32
CTX_SHARD_ID = 36
CTX_INSTANCE = 40
CTX_BUILD_ID = 44
CTX_UI_STATE = 48
CTX_COMPASS_WIDTH = 52
CTX_COMPASS_HEIGHT = 54
CTX_COMPASS_ROT = 56
CTX_PLAYER_X = 60
CTX_PLAYER_Y = 64
CTX_MAP_CENTER_X = 68
CTX_MAP_CENTER_Y = 72
CTX_MAP_SCALE = 76
CTX_PROCESS_ID = 80
CTX_MOUNT_INDEX = 84


class MumbleData:
    def __init__(self):
        self.valid = False
        self.ui_version = 0
        self.ui_tick = 0
        self.avatar_x = 0.0
        self.avatar_y = 0.0
        self.avatar_z = 0.0
        self.name = ""
        self.identity = ""
        self.identity_json = {}
        self.context_len = 0
        self.map_id = 0
        self.map_type = 0
        self.shard_id = 0
        self.instance = 0
        self.build_id = 0
        self.ui_state = 0
        self.compass_width = 0
        self.compass_height = 0
        self.compass_rotation = 0.0
        self.player_x = 0.0
        self.player_y = 0.0
        self.map_center_x = 0.0
        self.map_center_y = 0.0
        self.map_scale = 0.0
        self.process_id = 0
        self.mount_index = 0

    def __repr__(self):
        return (
            f"MumbleData(valid={self.valid}, tick={self.ui_tick}, map={self.map_id}, "
            f"identity='{self.identity_json.get('name') or self.identity[:20]}')"
        )


def _read_wide(buf, off, length_chars):
    """Read null-terminated UTF-16LE from buf starting at off, up to length_chars."""
    end = off
    max_bytes = off + length_chars * 2
    for i in range(off, max_bytes, 2):
        if i + 1 >= len(buf):
            end = i
            break
        lo = buf[i]
        hi = buf[i + 1]
        if lo == 0 and hi == 0:
            end = i
            break
        end = i + 2
    if end <= off:
        return ""
    chunk = bytes(buf[off:end])
    try:
        return chunk.decode("utf-16le")
    except UnicodeError:
        return ""


def read() -> MumbleData:
    md = MumbleData()
    try:
        h = KERNEL32.OpenFileMappingW(FILE_MAP_READ, 0, S_NAME)
        if not h:
            return md
        v = KERNEL32.MapViewOfFile(h, FILE_MAP_READ, 0, 0, TOTAL_SIZE)
        if not v:
            KERNEL32.CloseHandle(h)
            return md
        buf = (ctypes.c_char * TOTAL_SIZE).from_address(v)
        b = bytes(buf)

        md.valid = True
        md.ui_version = struct.unpack_from("<I", b, UI_VERSION)[0]
        md.ui_tick = struct.unpack_from("<I", b, UI_TICK)[0]

        ax, ay, az = struct.unpack_from("<fff", b, AVATAR_POS)
        md.avatar_x, md.avatar_y, md.avatar_z = ax, ay, az

        md.name = _read_wide(b, NAME, 256)
        md.identity = _read_wide(b, IDENTITY, 256)
        try:
            import json

            md.identity_json = json.loads(md.identity)
        except Exception:
            md.identity_json = {}

        md.context_len = struct.unpack_from("<I", b, CONTEXT_LEN)[0]

        c = CONTEXT
        md.map_id = struct.unpack_from("<I", b, c + CTX_MAP_ID)[0]
        md.map_type = struct.unpack_from("<I", b, c + CTX_MAP_TYPE)[0]
        md.shard_id = struct.unpack_from("<I", b, c + CTX_SHARD_ID)[0]
        md.instance = struct.unpack_from("<I", b, c + CTX_INSTANCE)[0]
        md.build_id = struct.unpack_from("<I", b, c + CTX_BUILD_ID)[0]
        md.ui_state = struct.unpack_from("<I", b, c + CTX_UI_STATE)[0]
        md.compass_width = struct.unpack_from("<H", b, c + CTX_COMPASS_WIDTH)[0]
        md.compass_height = struct.unpack_from("<H", b, c + CTX_COMPASS_HEIGHT)[0]
        md.compass_rotation = struct.unpack_from("<f", b, c + CTX_COMPASS_ROT)[0]
        md.player_x = struct.unpack_from("<f", b, c + CTX_PLAYER_X)[0]
        md.player_y = struct.unpack_from("<f", b, c + CTX_PLAYER_Y)[0]
        md.map_center_x = struct.unpack_from("<f", b, c + CTX_MAP_CENTER_X)[0]
        md.map_center_y = struct.unpack_from("<f", b, c + CTX_MAP_CENTER_Y)[0]
        md.map_scale = struct.unpack_from("<f", b, c + CTX_MAP_SCALE)[0]
        md.process_id = struct.unpack_from("<I", b, c + CTX_PROCESS_ID)[0]
        md.mount_index = struct.unpack_from("<B", b, c + CTX_MOUNT_INDEX)[0]

        KERNEL32.UnmapViewOfFile(v)
        KERNEL32.CloseHandle(h)
        return md
    except Exception:
        return md


def is_in_world(md: MumbleData) -> bool:
    if not md.valid:
        return False
    return md.ui_tick > 0 and md.context_len >= 48
