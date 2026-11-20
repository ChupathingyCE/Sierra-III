# Recon 134: sizing the big levers before building them (2026-10-16)

Sheepdog01's check-in on 2026-10-16 found the last week's builds chasing levers too small to see on the PSP, against a code layout that swings the tick ±5–20% from build to build. It named the big levers:
1. the display list built on the Media Engine one frame behind (frames in flight);
2. the object loop split into phases whose code fits the 16 KB instruction cache;
3. the map-specific drawing costs (a30's near-eye triangles, d20's light pass);
4. b30's own work.

This recon sizes them before any is built. Everything here is measurement: the instruments change nothing the game plays or draws, and they build only with `PSP_RECON=1` (on branch `recon134`, never merged into a release build), because a hook in hot code costs even switched off (`docs/shelf.md`).

Labels: **[M]** measured on a PSP · **[E]** emulator or estimate · **[D]** read in code · **[U]** inference.

## Where b30's frame goes today [M]

Sheepdog01's PSP-1000, b132, the b113 route, two runs (lab 38822408622), ms a frame; the steps sum to the clock within 1%:

| | Cutscene | Pelican ride | Landing | Beach | Bunker |
|---|---:|---:|---:|---:|---:|
| Frame | 167 | 260 | 210 | 145 | 93 |
| Tick (two a frame) | 98.0 | 112.2 | 88.2 | 75.8 | 56.9 |
| Drawing, main CPU | 61.4 | 136.4 | 112.4 | 56.1 | 23.5 |
| of it, models | 49.5 | 99.4 | 76.7 | 26.1 | 6.8 |
| GE busy (alongside) | 33.1 | 75.1 | 80.6 | 31.5 | 27.3 |
| of it, change-colour and glow passes | 7.7 | 24.9 | 27.8 | 7.4 | 7.2 |

Inside the tick (PSP-3000, b133 with nwbatch off, lab 38881473541), ms a tick: bipeds 19–28, vehicles 3–9 (three of them), scenery 2–7 (almost all of it sleep checks of objects already asleep), weapons 1–3; AI 9–14.

## The static reads [D]

- **The display list can be built on the ME today.** `psp_ge.c` already runs on it (`gemeon`, `gemeoverlap`): its VFPU paths (`vfpuskin`, `seethroughvfpu`) are opt-in and skipped there (`PSP_ME_WORK()`), and the GE's stall address is forwarded by the main CPU. pspgum is the FPU build (`-lpspgum`).
- **The snapshot is mostly built too.** The recorder (`gedefer`) copies each model's node matrices into its own buffer as it writes the call down. What still reads live state, or runs at once, are eleven drain points: the level vertex queries (static data), window begin, frame end, the text and stencil calls, a model too big to defer, and the 64 KB buffer filling. A frame in flight needs the whole frame recorded: how many words a frame takes on b30 is batch item A9.
- **The GE's own lights can't match Halo's falloff.** The light pass lights level vertices as (1 − d²/r²)² × N·L. The GE's 1/(k0 + k1·d + k2·d²) fitted to it is off by up to 38/255 and leaves 37/255 of light at the radius, where Halo's falls to 0. Handing the level's lights to the GE would change the look: Sheepdog01's call, after the exact run culling below.
- **Normals are kept** (Halo's packed 11/11/10 form, 4 bytes a vertex, beside each level stream), but in an array of their own. The GE reads normals only inside the vertex, so GE lighting would need an interleaved copy of each lit surface.

## The instruments (keep words, with `PSP_RECON=1`)

| Keep word | Where | Logs | Sizes |
|---|---|---|---|
| `modelcensus` | `render_objects.c` (`FIXES_RECON`) | Every 5 s of game time: models drawn a frame by type and by size on screen, the definitions drawn most | b30's ride (~167 models a frame); the same pair runs on ChupathingyCE's desktop build through the lab's hooks, so the PSP's count can be held against the original's |
| `sleepcensus` | `objects.c` (`FIXES_RECON`) | Every 5 s: per tick, by type, sleepers checked, of them woken, objects checked on the way to sleep | Whether being told a sleeper changed could replace checking every sleeper every tick (3.8–6.8 ms a tick on b30) |
| `drawcensus` | `psp_ge.c` | Every 300 frames: near-eye work by model definition; distinct change-colour triples and their tinted copies' size; light-pass triangles in runs of 16 wholly beyond the light's radius, spot against point | a30's near-eye triangles, baking the tint, the light pass's exact run cull |
| `mebusN` | `psp_me.c` | Every 300 ticks: ticks with the ME loading main RAM against without (by a coin per tick), and the ME's rate during them against alone | How much the two CPUs slow each other: the real ceiling of lever 1. `mebus0` misses only; `mebus50` about the main CPU's own mix |
| `aimelookcensus` | `psp_me.c`, `psp_fixups.py` (`psp_aime_ring_yield`) | Every report window (`psp_me_report`): the AI job's looks at the ring on the ME (calls into `psp_aime_ring_yield` and yields between actors) by how each ended; the stretches between looks (longest and its step, histogram, time over 1 ms by step); ring jobs pushed, run and seen waiting while the AI job ran. ME list `rbtswr` | where the AI job's thinking runs without a look (`box_tick_schedule.md` §11, slice 2's step 0) |

**Their tables come from the heap, not statics** (B79) [D]: a census's tables are taken by `psp_recon_table` right after keep.txt is read (`psp_recon_tables_start`, `psp_platform.c`), only when its keep word is on; a census that finds no room logs `<census>: no room in the heap` and stays off. As statics they took ~121 KB of heap from every recon run, censuses on or off, and c20 has only a few KB to spare: a recon build ran its heap out there with no census on. Taken at first use instead, they found no room, because the level's loads fill the heap first. Moved: drawcensus (52 KB), stickwholecheck (32 KB), mebusN (16 KB, its address handed to the ME job in its args), nodereaders (8 KB), objcost and particlecensus (5 KB with their report). What recon still costs the heap is its code (~46 KB) and ~8 KB of small statics. New instruments follow the same rule.

A full heap no longer ends a run silently [E]: an exit that doesn't come through the game's own (`exit_game`) logs how and from where, and when the heap had refused requests a `heap: out` line with the newest refusal's size and caller (`psp_libc.c`, the `sceKernelExitGame`, `abort` and `__assert_func` wrappers). The first one it named: GCC's emulated thread-local storage (`__emutls_get_address`) takes a thread's block from the heap the first time it touches a `__thread` variable, and aborts when refused.

Two more come from existing switches: `PSP_TICK_STEPS=1` splits the tick by step (the vehicles' 9 ms, the bipeds' "other", AI by phase), and `gedefer` logs the recorder's words and drains (A9).

## What each answer decides

- **mebus:** if both CPUs keep most of their speed side by side, lever 1 goes first, at its ceiling of −19% to −41% a frame [U]. If they halve each other, it shrinks to the drawing-bound maps.
- **modelcensus on the PSP and the PC:** if the original draws fewer models on the ride, the port over-draws, and fixing it restores the original (on by default). If the counts match, b30's drawing needs cheaper draws (the renderer session's precompiled lists) or the tint baked.
- **drawcensus:** the light runs' share is d20's exact cut (ceiling ~7–11 ms a frame [U]); the tint triples' size says whether baking the tint fits in memory (−25 ms of GE time on b30's ride [U]); the near-eye definitions say what a30's 22 ms is.
- **sleepcensus:** a wake rate far below the checks makes change tracking worth its proof of every writer.
- **The tick steps:** what in the vehicles' and the bipeds' time is exact to skip, and which AI phases the far-actor question (Sheepdog01's call) would touch.

## What came back (2026-10-16)

The emulator batch (recon134a, lab 38920291358, counts and shares) [E], the PSP-1000 (lab 38925285127) [M], and ChupathingyCE's desktop build (pc-chupa 38927713634) [E].

| Question | Answer | So |
|---|---|---|
| Do the two CPUs slow each other? (mebus, b30, the PSP-1000) | The ME on scattered misses: the main CPU's tick +11.9% (nine windows, +6.4 to +13.3%), the ME −15%. With arithmetic between its loads: +5% and −10% | The bus has room. Lever 1 holds at −10% to −36% a frame (b30 −25%) [U], with the main CPU's share taken 12% slower |
| Can a whole frame be recorded? (gedefer, b30) | ~1,780 calls, ~100 KB a frame on the busiest stretch; drains only at frame end, window begin and HUD text changes | Two frame buffers of ~128 KB, and those three calls become the frame's boundaries |
| What are b30's ride's ~154 models? | 98 scenery (boulders 40, trees ~30, shrubs 13, crates 9), 26 bipeds, 25 weapons; 101 of them under 16 px on the PSP's screen | |
| Does the original draw fewer? | No. At 16:9 it draws 155.5 against the port's 153.8, the same scenery (98.2). At 4:3 it draws 120 (the narrower view) | No restoration to be had: b30's drawing needs cheaper draws (the renderer session's precompiled lists), not fewer |
| Can every tint be baked? | b30 has 231 distinct change-colour triples: 6.9 MB of tinted copies at 16 bits, 3.4 MB at 8; a30 and d20 ~3 MB | Not all: a cache of the most-drawn ones, sized by how often each is drawn |
| d20's light pass | 163 point-light draws a frame (no spotlights), 8,379 triangles; only 21% in runs of 16 wholly beyond the light | The exact run cull saves ~21% of the loop, not the half hoped for. Merging the lights that reach the same surface into one draw is the next question |
| Who wakes sleepers? | 0.00–0.26% of sleeper checks find a change (b30), 0–0.2% (a50, d20) | Being told on a write would remove nearly all of the checks' 3.8–6.8 ms a tick on b30, if every writer can be found |
| The near-plane load | b30: the Pelican's own cabin (5,742 rigid + 5,774 bending triangles a frame). a30: lifepod_crashed and the marines | The Pelican as a near layer of its own, as the first-person weapon is |

### The object access trace (objtrace134, 2026-10-16) [E]

The emulator's `PSP_OBJTRACE` (the lab's `halo-local.patch` item 17) with the keep word `objtrace`: every load and store into an object's data under the strict interpreter, counted by instruction. Eight maps, 120 s of game time each, b30 on the b113 route to 150 s (lab 38947079338).

| Question | Answer | So |
|---|---|---|
| Who reads other objects' data during an update? (A4) | 0.02–9% of object-data loads land in another family's data (b30 1.6%, a30 9%, c10 0.02%); 1–35 cross stores a tick. The readers are almost only bipeds, projectiles and vehicles: collision against scenery (`matrix4x3_inverse`, `matrix4x3_transform_point` on the target's nodes; `object_get_features_in_sphere`, `object_test_vector`) and other bipeds' positions. Scenery, machines, items and the rest read no other family | Object updates are nearly independent. The quiet types can be grouped and run back to back (lever TG); units and projectiles keep their order. Moving machines (lifts) are the case statecrc must check |
| Who writes a sleeping object from outside its own update? (WT) | Leaving out the object marker (`object_mark_function`: word 2, which the checksum already skips) and memory moves (the pool compacting, a slot reused): 0.002–0.65 changing stores a tick, from a short list: `object_get_cached_render_lighting`, `object_cause_damage`, `object_set_garbage`, `object_set_visibility`, `unit_control`, `object_new`, and connecting and disconnecting from the map on a BSP switch (d20) | Being told on a write is feasible: the writers are few and named. Hands-off runs miss scripts, the player and blasts, so the list needs a read of the code to be exact |
| Can a cache of the most-drawn tints work? | The 32 most drawn triples cover 93–100% of passes on a10, a50, b40, c10, c20 (350–900 KB at 16 bits), but 47% on b30, 63% on a30, 66% on d20 (~1 MB); b30 needs 128 (3.7 MB) for 98% | A tint cache helps most maps, not b30's ride |
| Do lights repeat a surface? | Not answerable by list: each light hands over a triangle list of its own. The census now counts repeated triangles by identity; next batch | |

### Lab notes from the runs

- The ME load first stopped the ME for the session: an audio block waited behind it past 100 ms. It now mixes audio inside the load, as the GE layer's replay does.
- Perception's rays (on the ME by default) took the ME back at once each tick; `meraysoff` gave the load whole ticks.
- The PC's trace mode asks for a 480×272 window, under the desktop build's 640×480 minimum, so it plays 4:3 at 1280×960. Frames mode (960×544, 16:9) fails at its shots step without a `shoteveryN` word.
