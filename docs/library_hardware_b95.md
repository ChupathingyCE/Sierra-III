# The Library on the PSP-1000: the BSP address bug (b95 → b96)

Follows campaign_window_fit.md. Labels: [M] measured.
## b95 on hardware: The Library killed the PSP-1000 at its first BSP read; fixed by rebuilding with `-T` (2026-10-06) [M]

**Symptom (Sheepdog01's b95 runs 1–2):** at the end of loading the log stops right after the texture pack opens; the heartbeat thread stops with it; no crash.txt; about 10 s later the PSP powers itself off. Same with the ME off (`nomeaudio`), so not the ME. Run 1 (ME on) also showed vertical white lines on the loading screen.

**Cause:** the c20 package came from the window-fit measuring build, which is built **without `-T 0x900000`** so that over-budget maps still build. Without it Invader places each BSP for the Xbox's 22 MB tag space: c20's four BSPs ended at **0x0AC00000** (c20_1 loaded at 0x0A889000), past the PSP-1000's last byte of RAM (0x0A000000). The game's read of the first BSP sent the Memory Stick's DMA to memory that doesn't exist. Blood Gulch and the other multiplayer maps were built with `-T 0x900000` (BSP ending at 0x09F00000, the window's top) and never had the problem.

**Why the emulator missed it:** PPSSPP headless hard-codes a PSP-2000 (64 MB), where 0x0A889000 is real memory. Our copy now takes `PPSSPP_FAT=1` for PSP-1000 memory; with it the old c20 fails at that read exactly as on hardware, and the rebuilt c20 loads and plays. **Every emulator run should use `PPSSPP_FAT=1` from now on.**

**Fix:** c20 rebuilt with `-T 0x900000` (Invader: tag space 8.99 of 9.00 MiB, 99.87%). Tag data is byte-identical apart from the four BSP addresses (now 0x09B89000, 0x09AD8000, 0x09A9E000, 0x09BA0000, all ending at 0x09F00000). The .ptex is unchanged; .pgeo and .pstn differ only in the map checksum in their headers. Shipped as `halo_c20fix.zip` (map, pgeo, pstn; 63 MB in three parts).

**b96** adds a load trace (psp_trace.c): from the geometry pack's opening to the first frame on screen (at most 400 lines), every Memory Stick open/seek/read, DMA copy, GE list and display change, logged before and after, with the breadcrumb steps between. The Library takes 66 lines; Blood Gulch's texture preload stops at the cap.

**Lesson for every map from now on:** a PSP map is built with `-X 0x09600000 -T 0x900000`. The window-fit script (no `-T`) only measures; check a shipped map's BSP addresses end at 0x09F00000.

## b96 runs 1–2: in-play death at tick 75 = the b95 stack report reading kernel memory; fixed in b97 [M]

**Symptom:** The Library loads (trace clean: 186 and 61 calls, all to real memory), plays ~8 s of its opening cinematic at 4–7 fps, freezes on the last frame, and the PSP powers off ~10 s later. No crash.txt. Run 1 had the ME off and the profiler on; run 2 the ME on and the profiler off: **same death at the same moment**, so neither is involved. Both logs end on the same two lines, `sound cache:` and `dynamic geometry, frame peaks:`, at game tick 75.

**Cause:** b95's high-water reports run at tick 75 (then every 150), and at tick 75 (then every 900) `stack_report()` also scans every thread's stack for its deepest use. The thread list includes the system's idle threads, whose stacks are kernel memory. On the PSP a user-mode read of kernel memory is an exception; the emulator doesn't protect kernel memory, so it printed `idle0 0/4; idle1 0/4` happily. b95 had never run on hardware past tick 75 before (its first hardware runs died at load on the BSP address).

**Why no crash report was seen:** the crash handler drew its report into the on-screen log's buffer (VRAM 0x1B0000), which is only displayed while loading. During play the screen shows the GE's frame buffers, so the report was invisible, and file writes inside the exception don't land. The handler then spins, and the PSP shuts down by itself after ~10 s.

**b97:**
- `stack_report()` scans only user-mode threads whose stacks lie in user RAM (0x08800000–0x0A000000).
- The crash handler draws its report three times: into the log buffer (load time), and into both GE frame buffers (VRAM 0 and 0x44000, cleared first), so a crash during play shows on screen.

**Lesson:** the emulator protects neither kernel memory (user-mode reads succeed) nor, by default, the PSP-1000's RAM size (fixed with `PPSSPP_FAT=1`). Anything that touches system structures must be tested on hardware, early.

## b97 run 1: The Library playable on hardware, then a hard freeze at the first shot (2026-10-06) [M]

**Progress:** the cinematic plays through and gameplay starts; the stack report now lists only our 8 threads (main 166 of 512 KB). About 48 s of game time, 6–10 fps.

**Freeze:** Sheepdog01 fired at the first infection form; the PSP froze and powered off. No crash.txt, and (to confirm with Sheepdog01) no crash report on screen, although b97 draws it onto the game's frame buffers: so not an ordinary exception in our code. During play the log is saved every 5 s, so the last seconds are lost. The emulator (PSP-1000 memory, warnings on) fires at the Flood for 3 minutes with no bad addresses.

**b98:** keep word / menu option `blackbox`: the load trace continues in play without the steps (Memory Stick opens/seeks/reads, DMA copies, GE list starts, display swaps: ~4 lines a frame plus I/O), and the heartbeat saves the log every 250 ms. The log batch grew from 8 to 32 KB.

**AI and load recon from the same run (profile window 8, 10.5 s, The Library's first area) [M hardware]:**
- `objects_update` **24.9 ms a tick**, `ai_update` **4.4 ms a tick**: game logic alone is ~31 ms a tick, over a 30 Hz budget before any drawing.
- **Bipeds 17.2 ms a tick for 30.2 bipeds** (type update 10.2, node matrices 4.4, damage 0.5): ~0.57 ms per body per tick. Most are infection forms: one swarm brain (actors peaked at 9, swarm components at 23) but each body is a full biped update.
- **Machines 6.5 ms a tick for 53.4** (the Library's doors, lifts and devices, all awake every tick): a cheap target.
- Drawing: structure lightmaps 16.2 ms a frame, models 11.0, lights 9.6, HUD widgets 9.2, sound update 11.0 (game_sound_update, once a frame).
- Sound cache 1,020 of 1,024 KB occupied, working set 932–968 KB in the last second, 159 loads (8.2 MB), 0 failures: full, but coping.

## b98 runs 1–2: the freeze is not the ME or the profiler (2026-10-06) [M]

- **Run 1** (ME on, profiler on, blackbox): froze at the end of loading, right after the BSP read and a heartbeat profiler line, in a stretch where the main thread was only computing (no traced call). Freezes at random moments point at something running on its own.
- **Run 2** (ME off, profiler off, blackbox): loads and plays; **froze again at the first shot at an infection form**, as in b97 run 1. So the ME and the profiler are cleared for this freeze. The last traced calls are ordinary: texture reads into the staging buffer, DMA copies into VRAM (0x040DDA00–0x041ABBC0, all inside the cache), two display swaps and two GE list starts, then nothing. The 250 ms flush window means the last frame or two are lost.
- The emulator, teleported to Sheepdog01's spot (-14.90 21.60 -4.55, looking +x) and firing for two minutes with warnings on: no bad addresses, no GE errors.
- **The very slow start-up in run 2 was the blackbox itself:** the log only batches after 20 s, so every traced line before then was its own Memory Stick write.

**b99:** the blackbox batches from the first frame and the game thread writes the batch at every frame's end (and when it is three quarters full), so a freeze loses at most its own frame; in play each breadcrumb step is logged the first time it runs (`new step`); option `crashplay` faults on purpose 20 s into play, to check the crash handler's report really shows on the game's screen.

## b99 runs 2–3 and b100: the Memory Stick as the suspect (2026-10-06) [M]

- **Run 2** (`noparticles`): froze the same way. Particles cleared.
- **Run 3** (Sheepdog01): a full magazine into a wall, no problem; then **a grenade**: froze on its landing or explosion, no Flood on screen. So not the infection form either. The last traced call before every freeze so far (b98 run 1 at load, b98 run 2, b99 runs 1–3): the game's loader thread reading the map, here two 64 KB reads (the grenade's sounds, by size), while the main thread streams textures and the heartbeat writes the log. Blood Gulch preloads everything and never does this; the emulator's file system has no timing.
- **Strict emulator run** (local PPSSPP patch, interpreter): kernel-memory and non-existent-memory accesses, misaligned loads and stores, FPU traps, from the program's own code. 45 s of firing from Sheepdog01's spot: **none** (the volatile partition, 0x08400000–0x08800000, is ours and was filtered out).
- **Thread priorities** (emulator log): callbacks 0x11, audio 0x12, heartbeat 0x18, main 0x20, the game's pthreads 0x3C (the loader) and 0x5C. The heartbeat stopping too means either a thread above 0x18 spun, or the system itself stopped: a Memory Stick driver hang fits the second.
- **b100:** every Memory Stick call (open, close, seek, read, the start of an async read, write, getstat) takes one re-entrant lock, so only one thread uses the stick at a time; `stick lock:` in the log counts the waits (emulator: ~250–375 waits per window, worst 10–23 ms). Keep word `stickparallel` turns it off.

## b100 run 2 and b101: the freezes were ordinary crashes, and the first one is found (2026-10-07) [M]

- **b100 run 1:** froze at the instant a grenade detonated; no input, **no sound** (the audio thread, priority 0x12, stopped too). The Memory Stick lock changed nothing, so the stick is cleared.
- **b100 run 2, `crashplay`** (a deliberate store to address 4 at tick 600): **froze exactly like every "hard freeze"**: no report on screen, no crash.txt, no sound, then the power-off. So the crash handler (pspsdk's `pspDebugInstallErrorHandler`) does nothing visible on 6.61/ARK-4 during play, and the freezes were most likely ordinary CPU exceptions all along. (Inference: the CPU stays in the exception with interrupts off, and the PSP's power controller turns it off after ~10 s.)
- **Strict emulator, grenades from Sheepdog01's spot (b100):** one report, `unaligned load of 4 bytes at 0943218d from pc 08984054` = `breakable_surface_extant` (breakable_surfaces.c), called from `breakable_surface_damage_area_of_effect`. `struct breakable_surface_globals` starts with a one-byte `boolean enabled`, then `byte breakable_surface_flags[bsp][32]`, which the code reads and writes as 32-bit words (`BIT_VECTOR_TEST_FLAG((long *)…)`). x86 doesn't care; on the PSP every such word access is an address error. Blood Gulch has no breakable surfaces in reach of a blast; The Library does.
- **b101 fix:** the flags array aligned to 4 bytes (psp_fixups.py, with a static assert on the offset).
- **b101 diagnostics:** keep word / menu option `crumbs` draws the last four steps, object updates (bar scenery, machines and sound scenery) and damage/effect entry points (projectile_detonate, area_of_effect_cause_damage, object_cause_damage, breakable_surface_damage_area, effect_new_from_object, with tag names) straight onto the displayed frame, so a frozen screen shows the last thing started. Costs ~2× the tick. The crash handler now paints a red band across the top of both frame buffers before anything else, to tell whether it runs at all.

## b101 run 1: the first campaign combat on the PSP-1000 (2026-10-07) [M hardware]

Grenades, the first Flood fight until Sheepdog01 died, and the checkpoint reload at the mission start: all worked. The breakable-surface alignment was the freeze. Load recon from the fight (window peaks):

| What | Measured | Limit / note |
|---|---:|---|
| Actors (brains) | peak 16 | of 256 |
| Swarm components (infection-form bodies) | peak 23 | of 256 |
| Props (what the actors know about) | peak 63 | of 768 |
| Objects pool | 467 KB | of 1,024 KB |
| `objects_update` | 28–39 ms a tick | the 30 Hz budget is 33 ms for everything |
| of it: bipeds | 19–25 ms a tick for 24–30 bodies | ~0.8–1 ms per body per tick in combat |
| of it: machines (doors, lifts, devices) | 7.5 ms a tick for ~61 | all awake every tick |
| `ai_update` | 5–8.5 ms a tick | |
| Tick in combat | 33–49 ms average | so 9–12 ticks a second; 4.5–6.5 fps |
| Sound cache | working set 1,008 of 1,024 KB; 1,191 loads (28 MB) in the run; **14 sounds failed for room** | too small for a fight |
| Heap | top 4,264 of 4,672 KB | b99–b101's logging buffers cost 64 KB of heap |
| Main stack | 166 of 512 KB | |

**Reading:** game logic alone is over budget in a fight, and bodies cost more than brains: the bipeds' update (type update, node matrices, damage) is the largest share, then the machines, then the AI. The sound cache is the first fixed-size pool a real fight overflows.

## b101 run 2: a long Library run with the ME and the profiler on (2026-10-07) [M hardware]

Sheepdog01 played from the intro cinematic through several checkpoints and the main elevator room: game time 11,809 ticks, **no freeze**. The ME mixed every block (5.8% busy, 0 failures) and the profiler ran throughout, so both are cleared for good; b98 run 1's freeze at the end of loading stays unexplained (seen once).

| What | Measured | Note |
|---|---:|---|
| fps (5 s windows) | median 6.6, p25 5.7, best 9.6 | |
| Tick | median 23 ms, p90 33 ms | |
| Tick, waiting on memory | ~56% of its cycles (58% late in the run) | I-cache misses ~6,600 per M cycles against D-cache ~1,850: instruction fetches dominate (inference: counts, not stall attribution) |
| Drawing, waiting on memory | 47–55% | |
| **Heap peak** | **4,660 of 4,672 KB** | 12 KB from running out; b99–b101's logging buffers took 64 KB of it |
| Geometry cache (1 MB) | 1,899 block loads (10.3 MB), 1,708 evictions, **375 frames drew with something missing**, worst frame 319 ms of reads | the flashing at area thresholds and the elevator room's reloads |
| Textures | 757 requests waited a frame, 22 frames drew something untextured (longest 10) | |
| Sound cache (1 MB) | **3,016 loads (99 MB streamed), 35 failed for room** | the sound cut-outs |
| Pools | actors 9, swarm components 25, props 38 | far under their limits |
| BSP switches | none in this stretch (one level load in the log) | still untested on hardware |

**Reading:** stability is there; what's short now is RAM. The geometry cache thrashes in big rooms (the flashing), the sound cache in fights (the cut-outs), and the heap has no room left. The 4 MB plan has to cover these caches, not only the tag window. The tick's memory waits are mostly instruction fetches, which points at code layout (hot and cold code apart, profile-guided ordering) as a tick lever beside the bus.
