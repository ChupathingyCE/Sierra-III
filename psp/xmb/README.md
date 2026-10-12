# XMB menu art and sound

The XMB is the PSP's home menu. When Sierra-III is highlighted there, the firmware shows art and plays a sound it reads straight out of `EBOOT.PBP`. No game code runs at that point, so every file is a fixed format.

## The files

| File | Size / format | Shown as | Here |
|---|---|---|---|
| `ICON0.PNG` | 144×80 | The tile in the game list | `campaign/`, `mp/` |
| `ICON1.PMF` | 144×80, PMF video | Animated tile, replaces ICON0 on hover | not yet |
| `PIC0.PNG` | 310×180, transparent | Overlay drawn on the background | `campaign/`, `mp/` |
| `PIC1.PNG` | 480×272 | Full-screen background (static only) | `campaign/`, `mp/` |
| `SND0.AT3` | ATRAC3 | Loops while the entry is highlighted | local only, see below |

`ICON1.PMF` is the only slot that can move. The background is always a still image.

The campaign build (`PSP/GAME/halo`) and the multiplayer build (`PSP/GAME/halomp`) are separate XMB entries, so each has its own set. `psp_build.py` picks the folder by `PSP_MULTIPLAYER` and packs whatever is there. A missing file is simply left out.

## Art

- What is here now are placeholders drawn by `make_placeholders.py`. Re-run it to regenerate them.
- Real art replaces the PNGs in place, at exactly these sizes. Keep the layered masters (at 2–4× size) wherever you draw; only the exported PNGs live here.
- Export plain 8-bit PNGs: non-interlaced, no metadata. The XMB can show a blank tile for anything fancier.
- Art must be ours. Nothing from the game's files or Microsoft's branding goes in this repository.

## Sound

A clip of the game's music is game data, so it never gets committed (`.gitignore` drops `*.at3` and `psp/xmb/local/`). Each person makes their own from their own copy:

1. Cut a short, seamlessly looping clip and save it as 44.1 kHz stereo `.wav`.
2. Encode it to ATRAC3 `.AT3`. Sony's `at3tool` (from the official SDK, runs under Wine) is the known-good encoder; it isn't ours to redistribute. The open-source `atracdenc` encodes ATRAC3 too, but check its output on hardware before trusting it. Keep the file small: around 500 KB is the community rule of thumb [U].
3. Put it in `psp/xmb/local/` and build with `PSP_XMB_SND0=psp/xmb/local/SND0.AT3`.

An EBOOT built with `PSP_XMB_SND0` carries that clip, so its zip stays on your machine. It does not go in a Release, including `build-archive`.
