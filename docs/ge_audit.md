# The GE audit: how the port feeds the GE, and what is left to squeeze (work in progress)

Question: take a look at the GE, how we're using it, and how we can squeeze more performance from it. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference. Unfinished: the measurements below are from existing logs; nothing new has been measured on the PSP yet.

## Where things stand [M, b136 ladder on the PSP-1000 and b139's on the PSP-3000]

The frame is CPU-bound and in series; the CPU waits for the GE 0.2-1.5 ms a frame and the GE idles 50-77% of the clock. The GE layer's CPU ("the GE renderer's CPU"), ms a frame:

| map | all | level draws (passes) | model parts | decals | lights | shadows |
|---|---:|---:|---:|---:|---:|---:|
| a10 | 33.6 | 8.1 | 7.5 | 0.3 | 14.8 | 1.3 |
| a30 | 61.3 | 13.6 (8.7) | 35.1 | 1.7 | 0 | 6.6 |
| b30 | 65.4 | 10.1 (7.4) | 38.8 | 8.4 | 0.8 | 2.4 |
| c20 | 36.8 | 17.4 (11.2) | 6.1 | 0 | 3.6 | 0 |
| d20 | 62.2 | 22.4 (17.3) | 3.0 | 3.1 | 31.7 | 0 |

b30's model parts, ~300 a frame, ~130 us each on the PSP: geometry 52 us (most of it the near corner loops), passes (two-sided, change and glow) 31, bind+state+lighting 23, the buffer lookup ("convert") 11, node matrix 7.

## What the GE already does in hardware [D]

- **Skinning:** every bending part is drawn with the GE's 8 bone matrices (batches planned per part, `skin_plan`); the CPU bends only corners near the eye, to cut them. Not a lever.
- **Lighting:** models use the GE's lights (2 directional, 2 point). The level's dynamic lights stay on the CPU: the GE's falloff can't match Halo's (`recon_134.md`); a look change.
- **Near plane and guard band:** `GU_CLIP_PLANES` is on, but a piece clipped at the near plane lands past the GE's guard band, so the CPU must still cut near triangles (tcut, NC, NR, CL). Not a lever.
- **Bounding-box test (BBOX):** saves GE work only; the CPU still writes the object's words. The engine's sphere culling already drops what it would. Small.
- **Formats:** textures are already swizzled CLUT8/CLUT4 with mips; 16-bit level vertices (`levelcompact`) measured null three times; models are 99% drawn from VRAM copies. DXT1 would halve CLUT8's texel bytes, but texels from RAM measured ~0 (stand-in textures) and it is not lossless.

So the GE's hardware is used; what is left is the CPU feeding it, and the bus.

## Squeezes found, ranked [E]/[U]

1. **GL1, the geometry cache's lookup by address (built, `geoscan` off, `geoindexcheck`).** `resident_entry` answers every pass of every part; its 64-slot table misses most questions and scans ~350 entries (8 KB): 2.4% of b30's drawing phase in the cache model, the 12 KB `entries` table the phase's second most-missed data. Now a sorted 2 KB key snapshot searched by halving, verified by `entry_holds`, so answers are the scan's. Size: ~1-3 ms a frame on b30 and a30 [U]; exact.
2. **GL2, the Xbox vertex shaders' constants left unmade (built, `vshconstants` off).** The null renderer stores vertex constants nothing reads (no GL device on the PSP); each model draw still computed its node constants and lighting constants. `constants_store`, `rasterizer_set_model_skinning`, `_lighting`, `_point_light`: ~1.8% of b30's drawing phase [E]. Exact.
3. **BB, bindblocks (ported from the renderer branch, `bindblocks`, opt-in).** Exact on the emulator before; the PSP A/B never ran. ~11 us a bind, part of 2.6 ms of model binds on b30's flight [M].
4. **Skinning plans' indices in VRAM** (for the VRAM-bytes lever #1's owner): the GE reads ~280-340 KB a frame from the heap on b30 and a30 [E, gereads], most of it bending batches' index lists, re-read every pass.
5. **Bone sub-lists as deltas, node matrices once a part:** consecutive batches share slots; `halo_matrix` is ~1% of drawing [E]. Small.
6. **GE part timers:** ~6-8 clock reads a part (0.56 us each on the PSP-1000) ~1.3 ms a frame on b30; covered by E2 (`lean`, Sheepdog01's call).

Not redone (measured before): EM, MB (instruction misses barely cost the PSP: gedefer), X2 (retired), statememo, listcachedall (hung the GE), DB/DC, levelcompact, texel bytes.

## Next

The bus tax that remains, measured: a PSP A/B with `geserial` (the CPU's drawing with and without the GE overlapping) bounds every "fewer GE bytes" lever. Then GL1 and GL2 proven (statecrc, draw hash, `geoindexcheck` 0 differed), cache-model A/B, and one PSP keepsets job: `- | geoscan | vshconstants | bindblocks | geserial`.
