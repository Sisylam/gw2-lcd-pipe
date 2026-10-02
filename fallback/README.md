# fallback/ - alternative Python stack (not used)

An earlier, independent approach to driving a display from Guild Wars 2 data
without touching the LCD pipe: read the game's state from MumbleLink and the
public API, render it, and show it in a small Tk window.

It is **not** part of the shipped path - that is the clean-room client DLL
(`clean/`) plus the native viewer (`viewer/`) - and it is incomplete: the
map/POI/vista lookups still 404. Kept for reference.

| File | Role |
|------|------|
| `main.py` | Tk UI that ties the pieces together. |
| `state.py` | Game-state model (character, map, position) from MumbleLink + API. |
| `mumble.py` | Windows MumbleLink reader. |
| `gw2api.py` | Public API client; cache and `.env` key live at the repo root. |
| `render.py` | 320x240 framebuffer/renderer. |

Run with `python fallback\main.py` (needs the repo-root `.env` for the API key).
