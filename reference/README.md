# Reference screenshots

Renders of Guild Wars 2's own LCD output, used to identify which screens the
game shows and to compare against captured framebuffers.

- `*_color.png` are 320x240 - the G19S colour path.
- `*_mono*.png` are 160x43 - the mono G15/G13 path.

They are **not** from the Logitech SDK: the Logitech Gaming Software install
ships no matching images (only Arx/applet icons).

## Provenance

Downloaded on 2026-10-01 from the **GW2SDK wiki page "Logitech-LCD"**, authored
by Steven / GitHub user **`sliekens`**:

- https://github.com/sliekens/gw2sdk/wiki/Logitech-LCD
- mirror: https://gitea.sliekens.dev/sliekens/gw2sdk/wiki/Logitech-LCD

The images are embedded in that page as GitHub user attachments uploaded by
`sliekens` (`private-user-images.githubusercontent.com/1583241/...`). They are
therefore **not** this machine's captures - which is why the world-completion
percentages do not match this account. (The earlier claim that
`lcdproxy/analyse_screens.py` produced these files was wrong; that script
renders *this* machine's `stream_*.bin` dumps instead.)

## Licence

The imagery is Guild Wars 2 LCD content and is copyright ArenaNet; the
screenshots were captured and published by a third party. It is kept here for
analysis and reference only, is **not** covered by this project's MIT licence,
and should be removed if its origin cannot be established. See
`docs/INTEROP.md` for the project's stance on third-party material.
