"""End-to-end test of the shim: load it, drive it like GW2 would, read the frame back."""
import ctypes
import sys
import time

sys.path.insert(0, r"R:\SYSTEM\Users\Sisyphos\Documents\Default Project")
import lcdshim

DLL = r"R:\SYSTEM\Users\Sisyphos\Documents\Default Project\shim\LogitechLcd.dll"

d = ctypes.WinDLL(DLL)
print("loaded", DLL)

# x64: declare with enough params to accept whatever GW2 passes
P = ctypes.c_ulonglong
d.LogiLcdInit.restype = ctypes.c_uint32
d.LogiLcdIsConnected.restype = ctypes.c_uint32
d.LogiLcdColorSetBackground.restype = ctypes.c_uint32
d.LogiLcdColorSetBackgroundUDK.restype = ctypes.c_uint32
d.LogiLcdColorSetText.restype = ctypes.c_uint32
d.LogiLcdColorSetTitle.restype = ctypes.c_uint32
d.LogiLcdUpdate.restype = ctypes.c_uint32
for fn in ("LogiLcdInit", "LogiLcdIsConnected", "LogiLcdColorSetBackground",
           "LogiLcdColorSetBackgroundUDK", "LogiLcdColorSetText",
           "LogiLcdColorSetTitle", "LogiLcdUpdate"):
    getattr(d, fn).argtypes = [P, P, P, P, P, P]

print("IsConnected ->", d.LogiLcdIsConnected(0, 0, 0, 0, 0, 0))
d.LogiLcdInit(0, 0, 0, 0, 0, 0)
d.LogiLcdColorSetBackground(0, 0, 40, 0, 0, 0)

# a red diagonal
for i in range(320):
    d.LogiLcdColorSetBackgroundUDK(i, i, 0xFF0000, 0, 0, 0)
# a green block
for x in range(10, 60):
    for y in range(10, 40):
        d.LogiLcdColorSetBackgroundUDK(x, y, 0x00FF00, 0, 0, 0)

txt = ctypes.create_string_buffer(b"Level 36 Human Guardian")
d.LogiLcdColorSetText(10, 100, 255, 255, ctypes.addressof(txt), 0)
title = ctypes.create_string_buffer(b"Divinity's Reach")
d.LogiLcdColorSetTitle(10, 120, 200, ctypes.addressof(title), 0, 0)
d.LogiLcdUpdate(0, 0, 0, 0, 0, 0)

time.sleep(0.2)

s = lcdshim.Shim()
if not s.open():
    print("FAIL: could not open shared mapping")
    sys.exit(1)
f = s.read()
print(f"active={f.active} {f.width}x{f.height} bpp={f.bpp} seq={f.sequence} "
      f"texts={f.text_count} updates={f.update_count}")
if f.active != 1 or not f.rgb:
    print("FAIL: no colour frame")
    sys.exit(1)

px = f.rgb
def at(x, y):
    i = (y * f.width + x) * 3
    return tuple(px[i:i+3])

# (200,200) and (100,100) sit on the diagonal, so pick off-diagonal points
print("bg(200,150)   =", at(200, 150), "expect (0, 0, 40)")
print("diag(100,100) =", at(100, 100), "expect (255, 0, 0) red")
print("block(20,20)  =", at(20, 20), "expect (0, 255, 0) green")

ok = at(200, 150) == (0, 0, 40) and at(100, 100) == (255, 0, 0) and at(20, 20) == (0, 255, 0)
print("PIXEL CHECK:", "PASS" if ok else "FAIL")
s.close()

log = r"C:\Users\Sisyphos\AppData\Local\Temp\opencode\gw2lcd.log"
print("--- shim log ---")
try:
    with open(log, encoding="utf-8", errors="replace") as fh:
        print(fh.read()[-1200:])
except Exception as e:
    print("no log:", e)
