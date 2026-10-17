# The SH4ZAM idea, checked against what we have (2026-10-17)

Sheepdog01, 2026-10-17, from a chat outside the sessions: someone suggested a "SH4ZAM math pass" on any Halo CE port. SH4ZAM (Falco Girgis, gyrovorbis/sh4zam, MIT) is the Dreamcast's hand-written SH4 math library, grown out of the GTA3 and Vice City DC ports. The chat then listed fifteen PSP levers "of a similar nature" (drop-in, hardware-specific, broad). Sheepdog01 recognised most of them. This note checks each against the docs, so the master session only has to look at what is left. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference.

**Verdict:** every one is already built, measured or ranked in our docs. Three leave a small open item, none of them a headline lever. No code or idea is taken from SH4ZAM itself; it only prompted the check.

## Why a math pass is not the lever here

- On the Dreamcast, the SH4's scalar FPU is slow and GCC rarely emits `FTRV`/`FIPR`, so hand assembly is the difference between running and not.
- On the PSP, the tick spends 54–56% of its cycles waiting on memory, two thirds of that on instruction fetches [M, `plan_memory_stalls.md`]. A math pass cuts instructions, and cutting instructions has barely moved the clock (the ME pose job: −2.0% on b30 [M]).
- The VFPU has been in since b37 (`psp_vfpu.c`, matrix product, self-checked against the FPU); `vfpuskin` was flat on b30 and is kept for a30 only (`levers_next_b138.md`). The ME has no VFPU, so VFPU code must stay off any path the ME may run.

## The fifteen, and where each already lives

| Idea from the chat | Where it lives | Status |
|---|---|---|
| VFPU math pass (the SH4ZAM equivalent) | `psp_vfpu.c`; `vfpuskin`; research M (branch-free 4-wide bounds tests) | built; skin retry on a30 only; M proposed |
| Fast trig and 1/√x | single-precision pass (b19), `-fno-math-errno` makes `sqrtf` one instruction | done; `vsin`/`vrsq` unused [D] |
| Scratchpad for hot data | `psp_scratch.c` (b108) | built: 15.1 of 16 KB draw-only, ticks use none; sharing it in time is a ride-along |
| Hot/cold struct splits | research check 10, N (`pahole` audit); P2 and X5 (compact arrays + `cache 30` prefetch) | in progress as X5 |
| Tuned memcpy/memset | E15 (`cache 0x18` for whole-line writes), N5 (the tick's memsets) | see open item 2 |
| Uncached writes for GE data | `psp_ge.c` lists (uncached or `cache 0x18`); research R (VFPU write-through) | built; R proposed, low |
| Hot/cold function ordering | `PSP_TEXT_LAYOUT=coldfirst`, PGO `.text.hot.*`, TG | in tree, off; ~1.5% of the tick [U]; last in P1 |
| `-Os` cold, `-O2` hot | `psp_build.py` | done |
| Allegrex instructions (`-march=allegrex`) | `psp_build.py` | done; see open item 3 |
| Hidden `double` maths | b19, b112 (`fast_ftol`), E16 (the remainder, < 0.5 ms a tick [U]) | done; see open item 1 |
| 64-bit and divide-heavy integer maths | GCC turns constant divisors into multiplies [U] | nothing to do without a hot site |
| GE bounding-box test | `halo-psp-feasibility.md` (~1 µs a test [M], pays only for clusters and big objects) | researched; the renderer session's call |
| GE hardware skinning | `psp_ge.c` skinned records, weights by slot | built |
| DMA for bulk copies | `psp_tex.c` (`sceDmacMemcpy` for texture uploads); `big_levers.md` | built for textures; blocks its caller unless on another thread |
| Bake-time precompute, update by type | `texpack.py`, `stencilbake.py`, TB (#6); research H, `nwbatch` (shelved: data arrived cold) | ongoing; `nwbatch` retry narrowly per `levers_next_b138.md` |

## Open items

**1. A guard against `double` coming back with each engine pin** (a check, not a gain)
- psp-gcc doesn't narrow `(float)sqrt(x)`, `floor`, `ceil` or `fmod` to the float versions (`engine_performance.md` §1, item 2) [D]. A double `sqrt` costs 2,390 cycles against 31 [M].
- Every ChupathingyCE pin can bring new double calls in silently. Nothing in `tools/` counts them per build [D].
- **Proposal:** the release check lists the functions in `halo.elf` that call `__adddf3`, `__muldf3`, `__divdf3`, `__extendsfdf2`, `__truncdfsf2` or double `sqrt`/`floor`/`fmod`, and flags any in the hot set. `-Wdouble-promotion` on the hot units gives the source lines when it fires.
- **Cost:** a short script over `psp-objdump`; no runtime cost.

**2. E15 and N5: the whole-line writes and the tick's memsets** (data side of the memory-stall plan)
- `cache 0x18` halves whole-line fills on hardware (172 → 355 MB/s [M, v7b]); only `psp_ge.c` uses it. The engine's `memset`/`memcpy` are newlib's (not wrapped) [D].
- memset is 3.4% of instructions [E]; the tick's memsets are 238 M instructions over the bench [E]. No ceiling in ms yet.
- **Before building:** add memset and memcpy cycles to P1.1's per-pass table, by caller. Under the lever line (~2.5 ms a tick on b30), it stays filler.

**3. Are `min`/`max`/`movz`/`movn` emitted?** (an open question since the B15–B17 research)
- `-march=allegrex` is set, but no one has looked at our output (`research_blocked_list_index.md`, open questions) [D].
- **Check:** `psp-objdump -d halo.elf | grep -cE '\s(min|max|movz|movn)\s'`. If zero, the toolchain isn't using them, and the 2025 upstream GCC patch series (`halo-psp-feasibility.md`) is the follow-up to read.

## Status (2026-10-17, the master)

- **Item 3, answered:** `-march=allegrex` is used. b137's strict build (`7e52cd8`) holds `min` 382, `max` 391, `movz` 659, `movn` 649, `ext` 1,594, `ins` 1,057, `seh` 2,518 [D, `psp-objdump -d halo.elf`]. Nothing to follow up.
- **Item 1, built:** `tools/doublecalls.py` lists every function that calls a double helper, marks those the profile put in `.text.hot`, and with `--baseline tools/doublecalls_hot.txt` (b137's 18 hot ones) fails when a new one appears; it runs on each release build's `halo.elf`. b137 [D]: 184 functions call one, 18 of them hot. Most hot ones are a float passed to a log line's printf on a cold path; worth a look in the tick: `biped_update_physics` (`__divdf3` ×3, `__floatunsidf` ×6), `collision_get_features_in_sphere` (`__divdf3` ×3, `__floatunsidf` ×4), `vehicle_export_function_values` (`__divdf3` ×2, `__gtdf2`), `actor_look_decode_direction` (`__extendsfdf2` ×35), `halo_matrix` (×4). Recon's per-pass table says whether any runs per tick.
- **Item 2:** joins P1.1's per-pass table (memset and memcpy cycles by caller), recon's; under the lever line it stays filler.

## Sources

- SH4ZAM: github.com/gyrovorbis/sh4zam; Falco Girgis, FOSDEM 2026, "Accelerated Linear Algebra on the Sega Dreamcast with GCC".
- Ours: `plan_memory_stalls.md`, `levers_next_b138.md`, `optimization_ranking.md` (E15, E16, N5, X5), `engine_performance.md`, `halo-psp-feasibility.md`, `research_blocked_list_index.md`.
