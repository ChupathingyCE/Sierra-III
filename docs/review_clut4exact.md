# Review: clut4exact, the exact CLUT4 set

Branch `clut4exact` off w140 17984a1. An exact speed-up (CLAUDE.md: pre-approved): the pictures don't change. Labels: [M] PSP, [E] emulator or bake, [D] code, [U] inference.

## The change

The bake writes a texture at 4 bits wherever its 8-bit texels, over every level, use 16 palette words or fewer. CLUT4 then draws it texel for texel as CLUT8 did, from half the bytes, and the GE samples CLUT4 fastest (2.5-3.6 ns a pixel against CLUT8's 4.4-10.7 [M], `design_vrbtex.md` §2). The census behind it is `design_clut4w.md` ("the exact set"). The bake's own 16-colour rule leaves these textures at 8 bits: it skips lightmaps, level, sky and detail maps, keeps HUD sheets at 8 bits since build 120, and doesn't try maps under 32 texels wide.

Lossy 16 colours (the rest of CLUT4W's strict set: lightmaps and HUD sheets that need refitting) is not in this change: that is Sheepdog01's look call.

## Key lines

**`tools/texpack.py`**
- `bake_one`: after the CLUT8 chain is made, `clut4_exact(made, cls, words, level_ix)` for every 8-bit texture but the meters' grid (its entries are positions along a meter), the multipurpose maps (the change colour reads 256 entries) and the showcase packs. The levels are CLUT8's own: same count and sizes, so the look at a distance is the same too.
- `clut4_exact`: the palette words the levels use, deduplicated as stored (two 8-bit colours that round to one 5650 word count once). Over 16: unchanged. Entry 0's word comes first when the top level has padding, because `psp_tex_average` samples padding bytes (index 0).
- The proof, per texture, in the bake: `read_levels` unswizzles both packings from their bytes as the GE reads them, and every level's texels must give the same words; `runtime_average` works out `psp_tex_average`'s colour for both and they must match. A texture that fails stays at 8 bits and is counted ("differ").
- `palette_words`, `pack_level`: the palette and level packing that `bake_one` did inline, as functions both packings share.
- Pack flag 2 marks these textures (the directory's flags byte; bit 0 is still alpha).
- `--no-clut4-exact` (texpack and bake; the bake also takes `--no-exact-clut`, for option checks that take letters only) keeps them at 8 bits.
- Each pack logs `clut4 exact, <pack> pack: N textures ..., read back: N the same, D differ`, and the classes; the bake echoes them (no names).

**`psp/src/psp_tex.c`, `psp_tex_average`**
- Why it's needed: object lighting (the surface colour under an object) and the flashlight use a texture's average, taken from a sample of its top level's bytes. At 4 bits the same sample step lands on other texels, so the average would move by a unit or so: a change in every frame with objects on such a surface.
- A texture with flag 2 is sampled where its CLUT8 bytes were: each CLUT8 offset is turned back into its texel (the 16 x 8 swizzle) and read from the 4-bit data; CLUT8's padding bytes read entry 0. The colour is then the one CLUT8 gave.
- `c4xavgoff`: CLUT4's own sample instead (a tiny look change; for checking only).
- Older packs carry no flag 2, so they read as before; an older EBOOT ignores the flag and takes CLUT4's own sample.

## Figures [E: bake b140x, lab sierra-bake 39631575121, 24 of 24 maps]

Textures switched and KB saved, main pack / asset track's pack; every one read back the same (0 differ in every pack):

| Map | Main | Track |
|---|---|---|
| a10 | 50, -148 | 51, -107 |
| a30 | 42, -69 | 43, -69 |
| a50 | 57, -140 | 55, -96 |
| b30 | 28, -42 | 28, -42 |
| b40 | 47, -108 | 46, -71 |
| c10 | 47, -63 | 46, -52 |
| c20 | 52, -334 | 47, -95 |
| c40 | 48, -76 | 48, -72 |
| d20 | 63, -142 | 65, -117 |
| d40 | 46, -81 | 45, -56 |
| ui (not the showcase) | 34, -94 | |
| multiplayer (13) | 37-42, -66 to -78 | the same |

- By class: HUD sheets 31-57 KB a map (14-27 textures), lightmaps up to 297 KB (c20's main pack, 28 pages), a few level maps (a10 43 KB, d20 32), and small detail, sky and effect maps.
- The pack files shrink by exactly these amounts against the same bundle with the set off (bake b140x0, 39631627361): a10 -148 / -107 KB, b30 -42 / -42, ui -94; the showcase packs are unchanged.
- Against the census's exact column (a10 -112, a30 -88, b30 -66, c20 -344, d20 -127): the census judged colours at the screen's bits and counted shared texels once, this counts the stored words over every level and each texture baked [U for the gap].

## Proof

- **The bake with the set off is the old bake:** `--no-clut4-exact` from this branch's bundle (b140x0, lab 39631627361) and w140 17984a1's own bundle (b140w, 39635431059) give the same 15 files byte for byte (a10, b30, ui: sizes and SHA-256). The packing moved into functions and changed nothing else [E].
- **In play** (lab sierra-batch c4x1, 39634384439, emulator; the same EBOOT on b140x and on b140x0 twice; hands-off b30 to exitat210 and a10 to exitat300 on the asset track, b30 on the main pack; `fixedticks fixedseed statecrc framecrc nomouth`) [E]:
  - Play and both random seeds identical on vs off at every tick: a10 9,000, b30 6,300 (both packs).
  - a10's pictures: on vs off differ at 11 of 4,489 frames, the same as off vs off (11). The set-on run matches one of the two off runs at 4,485 frames; its other 4 are within that repeat noise (M6 #2's frames in flight).
  - b30's pictures can't decide it: off vs off already differ at 1,314 of 3,148 frames (on vs off 1,346), mostly in the top-right tiles.
  - Texture streaming: the same untextured frames on and off (a10 13, b30 25 on the asset track).
- What the pictures can't show, the bake shows for every texture: the texels and the average are the same (above).

## Not covered

- CLUT4 levels under 16 texels wide, and CLUT4 top levels under 32, are new to the packs. The runtime's strides (`psp_tex_bind`, `bind_block_fill`) take any width, and the emulator draws them the same; the PSP itself hasn't drawn one yet [U].
