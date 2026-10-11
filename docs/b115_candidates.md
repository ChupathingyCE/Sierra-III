# Build 115: candidates (2026-10-11, a draft for the next-steps talk)

What comes after b114, gathered from b114's hardware tests (`hardware_b114.md`) and the day's recon (`big_levers.md` §0, §4, §8, §9; `streaming.md`). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator or estimate, **[D]** read in the code, **[U]** inference.

Progress on this list shows on the **Sierra-III performance** dashboard (an artifact in Sheepdog01's artifact list), in its Levers and Next pages.

**Built as b115** (`build_b115.md`): the instruments, `overlayfreefar` and the presets. The two exact changes in §3 are out (below).

Everything ships behind a keep word. Anything that changes what the player sees is Sheepdog01's call, one at a time, after an A/B. **This list is a draft:** the order and the cut are for Sheepdog01 to settle.

## Decisions and priorities (Sheepdog01, 2026-10-11)

- **Targets** (`frame_rate_targets.md`):
  - primary: a solid 30 ticks and 30 frames a second;
  - minimum: 15 fps at 30 ticks.
  - Raising the floor speeds up Sheepdog01's testing, which is the slowest part of the loop.
- **Priority: align the workload to the pipeline** (`big_levers.md` §0). Every unit works on its own stage, and reads from the memory nearest to it.
- **Benchmarks:** the beach is the most accessible. Two Betrayals (Covenant against Flood, plus flying) is the stress case that will need every lever.
- **Draw distance:** in the back pocket, off. 45 would suit b30's landmark section and may suit the large maps (B9).

## 1. Carried from b114

| Item | Kind | Notes |
|---|---|---|
| `overlayfreefar` (the ocean's depth, pushed back by the far end) | fix; Sheepdog01's A/B | Built on branch `water-depth` (a20f63d), checked in the emulator (`hardware_b114.md`, "The ocean") |
| The water's tint (white at the start; partly loaded) | fix | A tint per shader, baked from the cube map's mean colour and the shader's stage constants |
| A **play** preset without `hwprofile` and `gebusy` | test tool | Preset 7, the only one without them, had the smallest spikes. Writing the log holds the stick for up to 803 ms (`streaming.md` §2) |
| The looks of tests 7–8: the pistol zoom overlay offset and stretched; tracers as long streaks; the plasma pistol's bolt like a cloud | visual | Needs a reference to aim at (Linux build screenshots of the same moments, Sheepdog01). The effects bake's half-size 16-colour textures are a likely part; native DXT for effects (S6) is the candidate fix |

## 2. Instruments (keep words, off by default)

| Word | What it reports | Where it runs | Source |
|---|---|---|---|
| `iostats` | Per heartbeat, for each stream (textures, geometry, animations, sound, log writes): reads, bytes, time queued against reading, worst read, needed now or prefetched. Also the stick lock's waits by the thread holding it, and the sound cache's loads split into re-reads and streaming | PSP | `streaming.md` §5 |
| `gereads` | The GE's vertex and index bytes by buffer and class; the top buffers by reads per byte; the fit curve for 200–1,000 KB of VRAM | emulator first, PSP too | `big_levers.md` §0, "Measure first" |
| `gebusy` with layer marks | The GE's own time per layer (level, models, see-through, decals, effects, HUD) and its starved time | PSP, bench presets only | same |
| `vramtex=N` | The VRAM texture cache cut to N KB (the rest goes to RAM) | PSP, the cinematic A/B | same; the swap's first step |
| `edramcheck` | The ME writes a pattern over eDRAM below its stack at start; the heartbeat has it checked. Shows whether anything else writes eDRAM during play | PSP | `big_levers.md` §0, "The ME's eDRAM, checked" |

## 3. Exact changes

| Item | Saves | Check first |
|---|---|---|
| ~~The script-node array sized to the map~~ | nothing: the start-up's table is let go of before the first map, and the map's nodes are in the tag window, sized since b106 [E, b115] | dropped |
| `aiming_screen_apply`'s two software-double `fmod`s → `x - truncf(x)` (E16) | small [U] | **held back:** the emulator's state differed from tick 0 with it (`build_b115.md`) |

## 4. A/Bs for Sheepdog01 (one preset each)

- **`vfpuskin` on the cinematic** (E24): already built. The cabin's marines beside the camera are its best case.
- **`vramtex`** on the cinematic, against the same preset without it.
- **`overlayfreefar`** on the flight (the ocean).

## 5. In the lab (no hardware)

- ~~A `gereads` batch along the bench route, every stretch.~~ Done: models ~600 KB of VRAM, textures ~400, the level the rest (`build_b115.md`).
- b30's sound census (`snd_census.py`), and a stats pass of `texpack.py` (b30's textures by source format and chosen depth; results only).
- The emulator traces and the replay tool for the cache curves (`streaming.md` §5): design first.

## 6. Next, but not b115

- **`vrammodels`:** the top model buffers copied into VRAM, sized by `gereads`, timed by `gebusy`'s layers.
- **L1 step 4, the overlap:** two record buffers, spinning hand-overs. Needs a day with the PSP at hand (`big_levers.md` §4).
- **S1, the single input stage** (`streaming.md` §3): design first.
- **Track B:** a hands-free route preset and a fast preset for Sheepdog01's runs.
- **Automating PSP tests:** to explore. It means building things; how much is open.
- **Halo_bench tests:** VFPU state on thread switches (E20), and the posing routines on the VFPU (X13).

## 7. Sheepdog01's side

- Tag b114 and add its zip to the `build-archive` Release (laptop).
- Linux build screenshots of the moments in tests 7–8, as the reference for the looks.
