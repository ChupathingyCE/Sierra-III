# The GE audit: how the port feeds the GE, and what is left to squeeze (work in progress)

Question: take a look at the GE, how we're using it, and how we can squeeze more performance from it. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference. The tables below are from existing logs; the new measurements are in "Next" and `docs/review_perf-ge.md`.

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

1. **GL1, the geometry cache's lookup by address (built, `geoscan` off, `geoindexcheck`).** `resident_entry` answers every pass of every part; its 64-slot table misses some questions and scans ~350 entries (8 KB): 2.4% of b30's drawing phase in the cache model, the 12 KB `entries` table the phase's second most-missed data. Now a sorted 2 KB key snapshot searched by halving, verified by `entry_holds`, so answers are the scan's. **Proven on the emulator:** `geoindexcheck` on b30's route compared 171,109 answers, 0 differed; play and seeds identical on d20, c20, a50. **Cost in the cache model:** `resident_entry` 2.34 -> 1.27 ms a frame on b30, **-1.07 ms** (fetches -61%, D misses -68%) [E].
2. **GL2, the Xbox vertex shaders' constants left unmade (built, `vshconstants` off).** The null renderer stores vertex constants nothing reads (no GL device on the PSP); each model draw still computed its node constants and lighting constants. **Proven on the emulator:** play and seeds identical on d20, c20, a50; a10 clean under the strict interpreter. **Cost in the cache model on b30:** `constants_store` 0.78 ms a frame and `rasterizer_set_model_skinning` 0.19 ms gone, **-0.96 ms**, up to -1.25 ms if the `memcmp` that left the profile was `constants_store`'s [E, U]. Together GL1+GL2: about -2.0 ms a frame in the model; the run's own frame budget lines show -1.8 ms in the frame and -0.85 ms (-1.2 ms in the heavy stretch) in the GE renderer's CPU. Read as 1-2 ms a frame on b30 in the model; **on the PSP-3000 the drawing phase gains 2.4 ms (cinematic), 5.2 (flight) and 6.1 ms (landing) a frame, more than the model, and nothing it can show on the beach** ("The PSP A/B" below) [M].
3. **BB, bindblocks (ported from the renderer branch, `bindblocks`, opt-in).** Exact on the emulator before. ~11 us a bind, part of 2.6 ms of model binds on b30's flight [M]. **PSP A/B: the drawing phase moves -0.8 ms in the flight, +0.1 cinematic, +0.2 landing, -0.9 beach, every one inside the repeats' own spread: no saving shows** [M]; at 11 us a bind and a few binds a part the most it could take is a fraction of a millisecond.
4. **Skinning plans' indices in VRAM** (for the VRAM-bytes lever #1's owner): the GE reads ~280-340 KB a frame from the heap on b30 and a30 [E, gereads], most of it bending batches' index lists, re-read every pass.
5. **Bone sub-lists as deltas, node matrices once a part:** consecutive batches share slots; `halo_matrix` is ~1% of drawing [E]. Small.
6. **GE part timers:** ~6-8 clock reads a part (0.56 us each on the PSP-1000) ~1.3 ms a frame on b30; covered by E2 (`lean`, Sheepdog01's call).

Not redone (measured before): EM, MB (instruction misses barely cost the PSP: gedefer), X2 (retired), statememo, listcachedall (hung the GE), DB/DC, levelcompact, texel bytes.

## The PSP A/B [M, PSP-3000, b30's route to game time ~4,480, `keepsets="- | geoscan vshconstants | bindblocks | geserial" repeat=2`, `tools/abreport.py`]

Two runs a side, one build (b139's bake), `nearauto gebusy` on. Figures are ms a frame in each stretch of game time (mean of the two runs; both runs in brackets where it matters). `-` is the defaults, GL1+GL2 on. "Real" is `abreport.py`'s verdict: the gap is larger than the spread inside either side, and over 0.5% of the figure.

| Drawing phase, ms a frame | cinematic 150-570 | flight 300-2,000 | landing 2,000-2,700 | beach 2,700-end |
|---|---:|---:|---:|---:|
| `-` (GL1+GL2 on) | 58.3 | 107.8 | 125.2 | 47.4 |
| `geoscan vshconstants` (GL1+GL2 off) | 60.7 | 113.0 | 131.3 | 47.7 |
| old minus new | **+2.4 (4.1%), real** | **+5.2 (4.8%), real** | **+6.1 (4.9%), real** | +0.3, noise |
| `bindblocks` minus `-` | +0.1, noise | -0.8, noise | +0.2, noise | -0.9, noise |
| `geserial` minus `-` | **-17.4 (-29.9%), real** | **-15.6 (-14.5%), real** | **-13.8 (-11.0%), real** | **-6.5 (-13.7%), real** |
| *frame by the clock: `-`* | 149.7 | 212.9 | 214.0 | 127.5 |
| *frame: old minus new* | +2.7 (1.8%), real | +5.1 (2.4%), real | +10.6 (4.9%), real; tick +2.4 of it | +2.0, noise |
| *frame: `geserial` minus `-`* | +4.8 | +42.6 | +56.5 | +11.5 |

- **GL1+GL2 save on the PSP, and more than the cache model said** (model: 1-2 ms a frame in b30's heavy drawing; PSP: 5-6 ms of the drawing phase in the flight and landing) [M]. The model's clock (71 cycles a miss) probably undercounts what a miss costs while the GE competes for the bus [U]. The frame by the clock follows the drawing phase (the frame is CPU-bound); the landing's frame gap (+10.6) also carries a tick gap (+2.4) that these changes don't touch, and the `-` side's tick is the lowest in all four stretches (by 0.3-2.4 ms), so the drawing phase is the clean reading. On the beach the gap (+0.3 ms) is inside the spread (1.1 ms); the saving doesn't show there, where drawing is only 47 ms.
- **bindblocks: no saving shows** beyond the spread in the drawing phase. The cinematic's frame gap (-2.9 ms, both runs 146.8) is flagged real but its drawing and tick gaps are noise, so that gap isn't credited to the binds. It stays opt-in; being exact, it is harmless to leave on, but this A/B gives no reason to.
- **geserial: the bound on every "fewer GE bytes" lever.** With the GE held back until the CPU has finished the list, the CPU's drawing phase runs 17.4 / 15.6 / 13.8 / 6.5 ms a frame faster: **that is what the GE's concurrent reads of main RAM cost the CPU, and the most any lever that cuts those bytes (GL3's index lists in VRAM, DXT, moving textures and vertex copies to VRAM) could recover**: 11-14% of the drawing phase in the heavy stretches, 6-7% of the frame there (flight 7.3%, landing 6.4%), 11.6% in the cinematic (little else is drawn, so the GE's share is large), 5.1% on the beach. A lever removes a fraction of the bytes, so it recovers about that fraction of the bound at best [U]. The frame itself gets longer under `geserial` (+4.8 to +56.5 ms) because the GE's busy time is then in series: that part is the measurement, not a cost. The bound agrees with the earlier GE-serial readings (`memory_traffic.md`: overlap slowed the CPU by 16-18 ms at the spawn view).

## Next

1. Done: GL1 and GL2 proven and costed on the emulator (above, `docs/review_perf-ge.md` "Evidence"). `resident_entry` still costs 1.27 ms a frame on b30 (its last-locked and slot-table paths and the snapshot's rebuilds, not the scan): counting calls by path would size a further saving [U].
2. Done: the PSP job (above). GL1+GL2 save 2.4-6.1 ms a frame of drawing in three of four stretches; bindblocks shows nothing; `geserial` bounds the "fewer GE bytes" levers at 6.5-17.4 ms a frame of drawing (14-16 ms in the flight and landing).
3. GL3 (skinning plans' index lists in VRAM) belongs with the VRAM-bytes lever #1; GL4 and the part timers wait on E2 (`lean`).

The code's walk-through: `docs/review_perf-ge.md`.
