# Review: the menu ring for b139 (`ringdetail`)

Sierra-III `ringdetail` on `w139` (d021f23). Main menu (ui.map) only: nothing here runs on another map. Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## 1. The model detail pass (`psp_ge.c`, `model_detail_pass`; `psp_fixups.py`)

**What:** the Xbox draws a model shader's detail map over its base: base × 2 × detail for the double multiply. The PSP has always folded the detail map's *average* into the base (`texpack.py`, `model_looks`) and dropped its pattern. On the menu, the pattern is now drawn too: the detail map over the frame with the GE's 2 × source × frame blend (`GU_DST_COLOR, GU_SRC_COLOR`), depth equal, last of the part's passes.

**Why it's exact enough:** the pack's detail map is scaled to an average of a half (or keeps the Xbox's own levels, which fade toward it), so 2 × detail is 1 on average and the folded base already carries the average: drawn over, the pass adds only the pattern. Drawn after the glow and its clamp, the product is the Xbox's (base × detail) × (lighting + glow).

**Key lines:**
- `psp_fixups.py`: on the menu only, `psp_ge_model_detail(key, u, v)` for a soso shader with a detail map, the double multiply, no mask, and its folded base in the pack (`base_key & 1`); otherwise key 0. Scale: map scale × detail scale, v × the v scale, after `rasterizer_xbox_models.c`'s vertex constants.
- `psp_ge.c`: `model_detail_pass` is cold and out of line; the model body calls it only when `psp_showcase_map` and a key are set. Its texture scale includes the vertices' own (`model_u_scale`), as the base's does. Each detail map's first draw is logged.
- Keep words: `nomodeldetail` (as before), `modeldetailbiasN` (N quarter levels coarser, a test).

**Proof:** ringdetail1 (39066581094) [E]: the outer ring's `detail corroded metal light` at 10×, 35–43 thousand pixels a frame changed; strict 0 sites; the multiplayer build plays. ringhw3 (39067393997) [M, PSP-3000]: A-B-A pixels a 4-frame run (median of 9) 83 with it, 70 without: inside the runs' spread.

## 2. The glow clamp off by default (`glow_map_pass`)

**What:** `glowclamp` turns the menu's glow clamp on; it is off by default (`glownoclamp` still means off).

**Why:** ringhw2 (39063835938) [M]: the clamp is what shows the shimmer on the ring's far band (median 67 with it, 41 without), and capping its level 1 or 2 levels coarser changed nothing (`glowclampbiasN`, tried and taken out). Sheepdog01's b138 run 3 with `glownoclamp`: "the menu ring looks fine". The clamp matched the PC's dimmer ring; that look is given up for the steadier picture, Sheepdog01's call.

## Tried and taken out

- **The menu's models' level by depth per pixel** (the level's slope mode, for finding 16, "worse at an angle"): ringhw4 (39068037719) [M]: twice the shimmer (194 against 87). The GE's own level stays.
- **`glowclampbiasN`**: no effect on the PSP (ringhw2).

## Still open

- Finding 16 (the ring's textures worse at an angle): no cure yet.
- The ring's reflection (the cube map the PC draws, masked by the multipurpose map's red): the bake drops cube maps, and the mask needs a second texture the GE can't take in the same pass.
