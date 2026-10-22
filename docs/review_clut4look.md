# Review: clut4ship, the CLUT4 look set on by default

Branch `clut4ship` off w140 fb5d1c9: `clut4look` 63dd5e9 (the bake option), then on by default. Bake only: the runtime is unchanged. A look change, so Sheepdog01's call, made on 2026-10-21 at 17:17 after the side-by-sides: it ships, on by default, with an option to bake without it. Labels: [M] PSP, [E] emulator or bake, [D] code, [U] inference.

## The change

Beyond the exact set (`review_clut4exact.md`), the bake now writes a lightmap or HUD sheet at 4 bits when one 16-colour palette over its whole chain holds CLUT8's look under the CLUT4W census's strict bar (`design_clut4w.md`). Half the bytes in the packs and in the VRAM texture cache, and the GE samples CLUT4 fastest. It is lossy, so the bar decides texture by texture; anything that misses stays CLUT8.

## Key lines

**`tools/texpack.py`**
- `bake_one`: after the CLUT8 chain (and after `clut4_exact`, which takes the exact ones first), `clut4_look()` for a texture still at 8 bits whose class is `lightmap` or `ui/hud`, but not the meters' grid (its entries are positions along a meter), the multipurpose maps (the change colour reads 256 entries) or the showcase packs. ui.map is left out entirely (`CLUT4_LOOK` is false for it): the main menu keeps every colour.
- `clut4_look`: one palette (`make_palette(..., 16, cls)`) over every level's pixels; each level is mapped to it (`nearest`) and measured against CLUT8's own error.
- `look_errors`: the error as the census measures it, at the screen's 5/6/5 bits: colour PSNR and worst step where the texel can be seen; for alpha, a cut-out by the texels whose alpha test turns over, any other by its alpha PSNR and worst.
- `look_passes`, the strict bar: every level within 1 dB of CLUT8's PSNR or over 45 dB, no texel more than 8 (one 5-bit step) past CLUT8's worst, the same for alpha, and at most 0.1% more cut-out texels flipped.
- The texture keeps CLUT8's chain (same level count and sizes), so the look at a distance moves only by the colours. It carries no pack flag 2: `psp_tex_average` takes CLUT4's own sample, as for any CLUT4 texture.
- `clut4_look_report`: each pack's `clut4 look` lines (counts, KB, lowest level dB per class, how many missed), which the bake echoes (no names).
- `--no-clut4-look` turns it off.

**`bake/bake.py`**
- `--no-clut4-look` (also `--no-clut-look`, for the lab's option check, which takes letters only) passes `--no-clut4-look` to texpack. `--clut-look`, the switch from before it was the default, is still accepted and does nothing, so older run specs keep working.

## The A/B behind Sheepdog01's call [E]

From `memory_ledger.md` §3 (bake c4look1, lab sierra-bake 39675794133; batch 39680280258, one EBOOT, hands off, a shot every 5 s of game time to 100 s, plain twice as the control). Pixels differing per frame, 480 x 256:

| Pair | Pixels differing, mean (worst shot) | Largest channel step | PSNR, worst shot |
|---|---|---:|---:|
| c20 plain vs plain (control) | 0.00% (0.00%) | 0 | identical |
| c20 track: plain vs look | 0.07% (0.33%) | 17 | 61.6 dB |
| c20 main pack: plain vs look | 0.03% (0.16%) | 9 | 65.1 dB |
| b30 plain vs plain (control) | 0.00% (0.02%) | 16 | 72.2 dB |
| b30 track: plain vs look | 0.02% (0.05%) | 16 | 68.5 dB |

c20's differences are faint dither-pattern changes on the lit walls; b30's sit inside the control's own spread. Sheepdog01 (17:17): "very imperceptible".

## Figures: the all-maps bake [E]

Lab sierra-bake **39686965976**, bake **b140y** (bundle local:dcf41d8, all 24 maps, `--multiplayer`; 24 of 24 baked, 0 errors). Textures switched and KB saved by the look set, beyond the exact set, main pack / asset track's pack (the shipped `assettrack`, half-size lightmaps):

| Map | Main | Track |
|---|---|---|
| a10 | 50, -165 | 36, -60 |
| a30 | 19, -62 | 17, -57 |
| a50 | 33, -135 | 32, -87 |
| b30 | 14, -31 | 13, -30 |
| b40 | 25, -99 | 19, -71 |
| c10 | 33, -411 | 19, -78 |
| c20 | 48, -523 | 39, -137 |
| c40 | 27, -224 | 21, -63 |
| d20 | 47, -170 | 31, -86 |
| d40 | 39, -93 | 25, -42 |
| ui | none (the showcase keeps every colour) | |
| multiplayer (13) | 15-21, -56 to -59 | 15, -56 |

- The HUD sheets are the same 13 textures (30 KB) on every map; the rest is lightmaps. On the track pack the campaign saves 42-137 KB a map.
- Lowest level PSNR of any switched texture: 45.0 dB (lightmaps) and 45.1 (HUD); medians per pack 45.1-61.8 and 52.9-57.1. 47-362 tried a pack missed the bar and stay CLUT8.
- c20 and b30 match the A/B's bake c4look1 exactly (c20 -523 / -137, b30 -31 / -30), so the A/B saw what ships.
- The exact set is unchanged beside it: every pack reads back 0 differ.

## Proof

- **In play** (lab sierra-batch c4ship1, **39692676257**, emulator x3d-psp-emu; one EBOOT, clut4ship dcf41d8, on b140y vs b140x; hands-off b30 to exitat210 and a10 to exitat300 on the asset track; `fixedticks fixedseed statecrc nomouth`; all 4 played) [E]:
  - Play and both random seeds identical look vs exact at every tick: a10 9,000, b30 6,300 (`tools/statecmp.py`).
  - a10's whole state is identical at every tick. b30's differs outside play ("objects", from tick 2,996), as b30 does between two runs of one EBOOT on one bake (c4x1's off vs off control, 39634384439: the same, from tick 4,934).
  - Texture streaming, look vs exact: b30 loaded 3,389 vs 3,425 KB, largest frame 2,402 vs 2,413 KB, untextured frames 25 vs 25; a10 3,158 vs 3,160 KB, 13 vs 13.
- **Builds:** none needed. The change is in the bake alone (`bake/bake.py`, `tools/texpack.py`, docs); every runtime file is w140 fb5d1c9's.
- **The look:** Sheepdog01's side-by-sides above. No frame check here: the pixels change by design.

## Not covered

- The PSP itself hasn't drawn these packs yet; the GE takes CLUT4 lightmaps and HUD sheets as it takes the exact set's [D], and the emulator draws them [E].
- How much of the saving becomes fewer untextured frames is up to play: the hands-off openings bind few of the changed lightmaps (ledger §3).
