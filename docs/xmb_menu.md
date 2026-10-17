# XMB menu: art, sound and the stick tree

Plan, 2026-10-12. Nothing past the placeholders is built yet.

## What goes where

The XMB shows five optional files packed into `EBOOT.PBP`. Each comes from a different place, decided by who owns it:

| Slot | Content | Source | Path |
|---|---|---|---|
| `ICON0.PNG` | Still tile: the Sierra-III mark and the build's name | Ours (`icon0.py`) | Committed, `psp/xmb/<build>/` |
| `ICON1.PMF` | The ring, spinning (silent loop) | Footage Sheepdog01 records from PC ChupathingyCE | Encoded once by Sheepdog01, shipped |
| `PIC0.PNG` | Empty (see Layout) | n/a | Not packed |
| `PIC1.PNG` | The Pillar of Autumn, with the game title baked in | Footage still from PC ChupathingyCE + our layers | Shipped |
| `SND0.AT3` | A clip of the score | The user's own disc | Added by the bake, never shipped |
| title text | "Halo: Combat Evolved…" | `PSP_TITLE` → PARAM.SFO | Build |

Why the split:
- Recorded footage is what Microsoft's Game Content Usage Rules cover; anything shared carries their notice (see the README).
- The score is extracted game data, and its ownership is not public (the composers' 2020 royalty suit against Microsoft settled on undisclosed terms). The bake reads it from the user's disc, the same way maps are made. This is the decomp-port precedent (e.g. Ship of Harkinian): share code, generate assets locally.
- Bake-first is reversible. Music can always be shipped later; a shipped copy can't be taken back.

## Layout (revised 2026-10-12, after a hardware preview)

The game title sits at the top (x 152–328, y 14–73), centred on the screen's true centre (x 240). The bottom rail carries the title's amber rule in the same place relative to its rail (x 152–327, y 228–234); the Sierra-III mark that sat there (y 217–264) moved to the tile (2026-10-17, Sheepdog01). The middle half of the screen (y 68–204) is left to the wallpaper, the tile's row included.

- **Baked into PIC1, not PIC0.** Where the XMB draws PIC0 isn't documented in what we checked [U]; PIC1 fills the 480×272 screen, so baking the title and mark into it puts every pixel where we drew it.
- **Why not above and below the tile** (the first plan): on hardware, the other titles in the list sit there, and scrolling moves them through that whole column [M].
- **Margins**: about 12 px to the list column and 13 px to the clock.
- **The highlighted tile** sits at about (24, 101), 146×81 [M, from a photo, ±4 px].

## The tile (2026-10-17)

`psp/xmb/icon0.py` draws both builds' ICON0 from `sierra_mark.svg`: smoked glass (the patch of that build's PIC1 under the tile, at its measured spot (25, 101), darkened 45%: `GLASS_DIM`, `GLASS_BLUR`), the first placeholder's 2 px border in the mark's amber (`#c29b63` → `#a06e26`), the mark as it sat on the wallpaper (PSP under the III, 120 px wide), the title's amber rule under SIERRA up to PSP (it ends where the III starts), and the build's name below it in Audiowide at 13 px with the title's steel, tracked a little tighter than COMBAT EVOLVED so MULTIPLAYER fits the mark's width. The two entries differ only in that name.

- **Glass, not a plate** (Sheepdog01, 2026-10-17): the tile fakes transparency over the wallpaper. Smoked, so the chrome holds up against the planet's limb, and so a few pixels' error in the measured position reads as a material edge rather than a jog. The wallpaper fades in only once the entry is highlighted; before that the tile sits on the XMB theme.
- **Drawn at 4× and shrunk.** The mark's outline is about 1 px at tile size; drawn directly it breaks up (the lesson from designing it at 2×).
- **Outline under fill.** The master asks for `paint-order="stroke"`, which cairosvg ignores; the script renders the outline and the fill as two passes so PSP's thin strokes keep their amber.
- **How PIC1 lost the mark** (the layered title master isn't kept): `backdrop.py` at seed 7 plus `compose.py`'s rail maths rebuilds the wallpaper's plate exactly (0 difference outside the titles) [D], so the sky under the old mark is the original sky. The rule under HALO was lifted against that plate and laid on the bottom rail, 174 rows down (the rails sit at y 115 and 463 at 2×).

### Dead zones

Where the XMB draws its own UI over PIC1, measured from photos of a PSP-1000 [M, ±4 px; each zone keeps a few pixels of margin beyond the measurement]. Nothing important goes in these; the final PIC1 leaves them to the wallpaper. `psp/xmb/deadzones.py OUT.png PIC1.png` draws them over any PIC1.

| Zone | x | y | When |
|---|---|---|---|
| Other titles above | 50–146 | 0–96 | Always; the list scrolls through this column |
| Highlighted tile | 18–176 | 96–188 | Always |
| Other titles below | 50–146 | 188–272 | Always |
| Category icon | 0–32 | 50–86 | Always |
| Corner mark | 0–18 | 0–18 | Always |
| List arrow | 4–18 | 128–146 | Always |
| Clock and battery | 336–480 | 10–34 | Always |
| Options button | 390–480 | 218–258 | After a moment on the entry |

Also from the hardware preview: the PSP-1000's LCD lifts near-black toward mid-blue, so the final art shouldn't lean on deep blacks.

## The game title

The name in our own type, never Microsoft's logo art (their Game Content Usage Rules allow referential titles; their logos are out).

- **HALO**: Krona One, turned into vector paths with two changes of ours: the H's crossbar moved down 238 units to sit at the cap height's middle, and the O redrawn as a true circle (radius 803, ring 317 units thick, matching the stems). Paths mean the title renders the same anywhere, with no font to load.
- **COMBAT EVOLVED**: Audiowide, set to the same width as HALO.
- **Between them**: a 3px steel rule, with an equal gap above and below (12px at 2×, measured from the fonts' cap heights).
- **Finish**: slate steel, a cool-grey chrome with an engraved inner shadow. Deliberately not a blue-white chrome.
- **Fonts** are SIL Open Font License: artwork made from them ships freely; the generator carries the OFL notice; no modified font file is redistributed.

## The stick tree

Today the user copies two things: the release's `PSP/GAME/halo/` and the bake's `halo/`. The bake will instead write one tree that mirrors the stick:

```
<output>/
  halo/maps/...               the baked maps (as now)
  PSP/GAME/halo/EBOOT.PBP     the release EBOOT, with SND0 added
```

The user drags the output's contents onto the stick root. That's one copy, and the music comes with it.

### How the bake adds SND0

Built 2026-10-12: `bake/lib/snd0.py`, run by `halo-psp-bake --eboot <release EBOOT.PBP>`.

1. Find the clip by **tag path**, not byte offset. Tag paths are the same across releases; offsets aren't. The spec is `[MAP:]TAG[@START][+LENGTH]` (map `ui` by default, 0 s, 30 s). A sound_looping tag gives its first track's start sound, then one pass of its loop.
2. Follow the music's chunk chain (each ~1.3 s permutation names the next) and decode it: Xbox ADPCM, the same arithmetic as the port's `psp_dsound.c`. A 22.05 kHz or mono sound is brought up to 44.1 kHz stereo.
3. Cut, fade in (0.02 s) and out (2 s, equal-power), and set the peak to −3 dBFS: the XMB plays it the instant the entry is highlighted.
4. Encode to ATRAC3 (LP2, 132 kbps, RIFF) with `atracdenc` (dcherednik/atracdenc, `cb17070`), bundled by `make_bundle.sh` when `ATRACDENC` names a built checkout, else taken from PATH. 30 s comes to about 484 KB.
5. Repack the EBOOT. A PBP is a header of eight offsets followed by its sections, so swapping SND0 means slicing the sections and writing a new header. It's plain Python with no PSP toolchain, and the program section is untouched. The copy goes to `<output>/PSP/GAME/<halo|halomp>/`.

**Best-effort:** with no clip chosen, no encoder, or no map, the bake says why and copies the EBOOT unchanged. Sound must never be why an install fails.

**Checked so far** [E]: the decoder against a round trip through an IMA encoder and against ffmpeg's Xbox ADPCM decoder (within 17 of 32767: ffmpeg rounds its steps differently); the cut, the fades and the peak; atracdenc's output (RIFF, format 0x0270, 384-byte blocks, which ffmpeg decodes back to 30.0 s); and the PBP repack on a synthetic PBP (every other section byte-identical). **Not yet checked:** reading a real map's music tags, and the XMB playing the result [U until each is run].

### The music, by tag path

From the campaign's own scripts ([Dwood15/HaloScripts](https://github.com/Dwood15/HaloScripts)) [D]. The scripts name tags, not the pieces they hold, so which track is which is still to be listened for.

| Level | Tags under `levels\<level>\music\` |
|---|---|
| a10 | `a10_01`–`07` |
| a30 | `a30_01`–`07` |
| a50 | `a50_01`–`11`, `_071`, `_072` |
| b30 | `b30_01`–`06`, `_031`, `_032` |
| b40 | `b40_01`–`08`, `_011`, `_041`, `_042`, `_061`, `_071` |
| c10 | `c10_01`–`07` |
| c20 | `c20_01`–`04` |
| c40 | `c40_01`–`11`, `_051`, `_101`, `_102` |
| d20 | `d20_01`–`06`, `_051` |
| d40 | `d40_01`–`09`, `_011`, `_012`, `_013` |

Also `sound\music\x20_music\x20_music_1`–`4` and the cinematics' `sound\sinomatixx\a30_*_music` and `a50_*_music`.

The main menu's theme isn't in any script: ui.map's only script moves the camera, and the music is set by the ui scenario itself. ui.map holds four multi-chunk music chains (11 MiB in all, 44.1 kHz stereo) [D]. `halo-psp-bake <disc> <out> --maps ui --list-music` names them, with their lengths.

## Open items

- [ ] Sheepdog01 records the ring loop (ICON1) from PC ChupathingyCE.
- [x] PIC1 committed for b117 (2026-10-12): `psp/xmb/campaign/PIC1.PNG`; the campaign's PIC0 dropped. The footage exception is in CLAUDE.md.
- [ ] PIC1 recapture: a larger ring, shot on the ultrawide; it reads lower quality than commercial PIC1s on hardware.
- [ ] Pick the clip: run `--maps ui --list-music` on the desktop, listen, and set `MUSIC` in `bake/bake.py`.
- [x] The bake step that cuts, encodes and packs SND0 (2026-10-12).
- [ ] Check on hardware that `atracdenc`'s ATRAC3 plays in the XMB [U until then].
- [ ] Encode `ICON1.PMF` (UMD Stream Composer under Wine; Sheepdog01's machine only).
- [x] Find the icon tile's real position (photo, 2026-10-12).
- [ ] Optional: refine the dead zones with a calibration grid shown as PIC1 on hardware.
- [x] Design the Sierra-III mark (2026-10-12); its vector master committed as `psp/xmb/sierra_mark.svg` (2026-10-17).
- [x] The mark moved from PIC1 to ICON0, on the placeholder's steel plate with an amber border (2026-10-17).
- [x] Export the PIC1 (footage + title + mark) and drop PIC0.
- [ ] Set `PSP_TITLE` for both builds.
- [x] Add the GCUR notice to README.md.
- [ ] More disc releases for `hashes.json` (only NTSC 01.10.12.2276 today).
