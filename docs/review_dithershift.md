# Review: the dither moved each pass (branch `dithershift`, on `glowmaps`)

Item 1 of the renderer session's next steps (`living/STATE.md`, 6b): the dark bands on the levels. Everything here is **opt-in** (a look call, decided by Sheepdog01 after the A/B). Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference or model.

## The mechanism

- **The frame is 16-bit**, so every pass that writes a pixel rounds it to 5 bits of red and blue (steps of 8 levels) and 6 of green. The GE's ordered dither adds a 4×4 pattern of offsets before that, so a smooth gradient becomes fine grain instead of contour bands.
- **b117 left blended passes undithered** (`hardware_b116.md`): a blended pass reads the pixel back and rounds it again, and with the same offset in every pass the roundings go the same way pass after pass. Layer on layer, a fixed checkerboard built (up to 140 levels under the Pelican's dust [M]).
- So the level's detail passes (two multiplies a pixel) and the sky's layers round without dither, and their steps show as bands. The likeliest cause left for the "oily" look (STATE 5c).
- **The fix:** dither those passes again, with the pattern **moved to a new place each pass**. A pixel then meets a different offset each time; its roundings stop agreeing, and no pattern can build.

## A second effect, found on the way: drift

PPSSPP **truncates** on the write and, for blending, **reads the 16-bit pixel back with its top bits copied into the low ones** (`GPU/Software/DrawPixel.cpp`, `Common/Data/Convert/ColorConv.h` at the pinned commit) [D]. Under that model:

- A pass that leaves a pixel as it was isn't neutral once dithered: a dark pixel (low bits 0) drops a step whenever its offset is negative; a bright one climbs. Moving the pattern stops the pattern, **not the drift**, and in the dark it makes it worse (every pixel eventually meets a negative offset). Model, grey 40 under 8 do-nothing passes: fixed pattern ends at 25, moved at 7 [U].
- So the moved dither goes only on the passes that change every pixel (detail, sky layers, and the lightmap, already dithered). Particles, shadows and decals (many thin layers) stay undithered. `dithershiftall` dithers every blend, for the emulator comparison only.
- **Truncation itself costs half a step a pass** (−3.5 levels on red and blue on average), dithered or not. The matrix is centred around 0, which suits a GE that *rounds*. For one that truncates, offsets 0…7 are right on average: `ditherlift`.

Model, a dark ramp (12–72) with two detail-like passes, against the exact result [U]:

| Variant | Mean error (levels) | Contour error |
|---|---:|---:|
| Today (passes undithered) | −8.4 | 2.61 |
| `dithershift` | −11.3 | 1.75 |
| `dithershift ditherlift` | +1.3 | 2.02 |

Whether the PSP truncates, and how it reads the frame back, has never been measured. `ditherprobe` measures both.

## The changes (`psp/src/psp_ge.c`)

- `dither_moves_make`: the matrix at its 8 distinct places (moved by (2, 2) it repeats). The order, `(0,0) (0,1) (2,0) (2,1) (1,0) (1,1) (3,0) (3,1)`, was searched so that any two passes in a row nearly cancel for every pixel (−4 then +2, −3 then +3, …), and eight give each pixel every offset once.
- `ge_dither_pass`: the next place, and the dither on. `ge_layer_dither` calls it under `dithershift` after a pass's blend is on: **the level's detail passes** and **the see-through draws flagged sky**. The lightmap pass (`ge_blend_dithered`) moves too, so its grain doesn't line up with the base pass's.
- `ge_dither`: each frame starts at place 0, so frame-locked shots repeat. `ditherlift` adds 4 to every offset (works with `ditherhalf` too).
- The GE's report line gains `dithershift: N passes dithered at a moved place`.
- `ge_dither_probe` (`ditherprobe`, at start-up, in the buffer not yet on screen): row 0 draws every value 0–255 as it is (where the codes step: truncate or round); rows 1–4 take one pass that ought to change nothing (times one; dithered −1; dithered +7; times white). One log line a row. What to read:

| Line | Truncating, low bits 0 | Truncating, low bits copied (PPSSPP) |
|---|---|---|
| row 0, red steps at | 8 16 24 32 | 8 16 24 32 |
| row 2 (−1), red down | 248 (every code but 0) | 24 (codes 1–3 only) |
| row 3 (+7), red up | 0 | most codes from 4 up |
| rows 1, 4 | 0 moved | 0 moved, if "times one" is exact |

A rounding GE shows red steps at 4 12 20 28.

**Cost:** a pass under `dithershift` adds the four matrix words (16 bytes of display list) and a dither switch; a few hundred passes a frame on b30 [U]. The probe runs once, only with its word.

## The build switch (`PSP_DITHERSHIFT=1`)

The shift's tests sit in `ge_blend` (every blend switch, every see-through draw), the level's detail loop and the see-through flush: hot paths, where code that does nothing still costs (b133's nwbatch hooks, c10 +7% while off). So they are built in only with `PSP_DITHERSHIFT=1`; as 0 (the default) every test folds away, and `dithershift` kept in such a build says so in the log and does nothing.
- `psp_build.py` passes the define to `psp_ge.c`'s own compile, which takes none of the engine's defines (so `PSP_DEBUG_TOOLS`'s three blocks in `psp_ge.c` have never been built in either: a separate question).
- **Measured on the object** (psp-gcc 15.2.0, `-O2`, function sizes against `glowmaps` 8b826c2) [D]: by default `ge_blend`, `psp_ge_draw_level_body` and `transparent_flush` are the same size; what's added is `ge_dither` +176 bytes (the lift's one-time setup; called twice a frame), `psp_ge_window_begin` +76 (the probe's call) and the probe itself, 1,092 bytes in `.text.unlikely`. With the switch, the three hot functions grow 20, 60 and 48 bytes.
- `ditherlift` and `ditherprobe` are always built: neither sits in a hot path.

## Keep words and presets

| Word | What | Default |
|---|---|---|
| `dithershift` | detail and sky passes dithered, moved each pass (needs `PSP_DITHERSHIFT=1`) | off |
| `ditherlift` | every offset +4 | off |
| `dithershiftall` | every blend dithered and moved (comparison; needs `PSP_DITHERSHIFT=1`) | off |
| `ditherprobe` | the GE's rounding and readback, logged | off |

Presets 16 (`dithershift ditherprobe`) and 17 (`… ditherlift ditherprobe`).

## Checks

| Check | Result |
|---|---|
| `psp_ge.c` compiles alone with psp-gcc 15.2.0, `-Wall` | the same warnings as before [D] |
| dither1 (lab 39009037745, at 4f20b16, before the switch): b30's flight and d20's dark corridor, frame-locked; sides base, `dithershift`, `+ditherlift`, `dithershiftall`, all with `ditherprobe` | [E] The fixed 4×4 pattern: b30 7.19 → **2.28** levels (shift), 2.52 (lift), 5.40 (all); d20 4.21 → 2.43, 3.22. Brightness against base: shift −0.6 / −0.85; **lift +6.9 / +4.3**, d20's corridor walls come up out of the black. `dithershiftall` mottles rock and grass into a hard checkerboard (the drift): comparison only. Strict interpreter clean; the multiplayer build draws. The probe: the emulator truncates and copies the top bits on readback (row 2: 24 down; row 3: 216 up), as read in its source |
| PSP A/B, b30, the same frames, base / shift / lift, `ditherprobe` on all | in flight on the PSP-3000 (lab 39010447206; the 1000 away): the probe's lines are the first measurement of the PSP's own GE |

What the emulator run measures, per side: the pattern's spread (the 4×4-periodic part, as `review_lookb124.md`), mean brightness against the undithered side, and the contour error along a dark gradient; the probe's five lines; the strict interpreter clean.
