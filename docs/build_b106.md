# Build 106: The Silent Cartographer fits, and the frame gets cheaper (2026-10-08)

Built overnight without hardware, from b105 run 1 (build_b105.md, last section) and the emulator. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory, 13 descriptors), **[D]** read in the decomp, **[U]** inference.

**How to read the emulator numbers.** PPSSPP models no caches, and runs the tick about 4× faster than the PSP-1000 (psp_hardware_reference.md §5). Use them for *where* time goes and *how much it shrank*, not for the PSP's frame rate. The fight on b30's beach isn't repeatable from build to build (the AI's choices depend on the frame timing), so whole-frame A/Bs late in a run carry ±10–20% noise; the component figures (a step's time, a counter) are the solid ones.

## 1. b30 on the PSP-1000's memory

The tag window is 9 MB (0x09600000–0x09F00000). b30 needed 11.34 MB with b102's recipe (overnight_b102.md).

- **Animation frame data out of the tags** (invader-build `-M`, needs `-G`). Each animation's frame data goes to the front of the map's `.pgeo` pack, laid out on 8 KB pages; its pointer becomes `0x0C000000 + offset`. The game's two readers (the frame reader and the three keyframe readers) ask `psp_animation_data` for exactly the bytes they read: from the tags, or through the animation cache (`psp/src/psp_anim.c`). [E] b30: **2,494 KB of frame data** moved out (3,202 KB with its page padding). The cache is 512 KB in 8 KB pages, 368 KB of it in the tag window's unused end and 144 KB from the texture RAM cache. `tools/anim_cache_sim.py` replayed a 9-minute trace of b30's frame-data reads (`animtrace` keep word) to pick those sizes: about 214 misses and 1.8 MB read in 9 minutes.
- **The script node table sized to the map** (invader-build `-Y 512`). [D] The Xbox's maps carry the engine's maximum, 19,001 nodes (380 KB), in the scenario's tag data; b30 uses 4,534 (invader-build's count; the table is then 5,046 nodes, 101 KB). A `-Y` map carries those plus 512 spare (the console's expressions take nodes while they run; `hs_node_gc` hands them back every 16 ticks). `hs_allocate` used to accept only the full-size table, and otherwise put a full-size one in the heap and handed the map's (tag memory) to `free()`; it now takes any table that agrees with its own header.
- **Result [E]:** b30's tags are **5.04 MiB** (5,282,112 bytes) and the map loads and plays: the Pelican drop, 35 actors, scripts through the beach. **Maps baked with the new bundle need b106**; b106 still loads older bakes (The Library and Blood Gulch, run tonight). The shipped bundle, run here on a folder of the disc's maps, reproduced the tested b30 files byte for byte (`.map`, `.pgeo`, `.pstn`, both `.ptex`).
- **Soak [E]:** the shipped build on b30 with the scripted play input (deathless) for 26,969 ticks: no crash, heap flat at 4,522 KB in use, 0 failed requests.
- **The other maps [M, the bake's build step alone]:** with the same flags, a10, c10, c40, d20 and d40 fit the window too (with c20 and b30: seven of the ten). a50 (0.20 MiB over), b40 (0.53) and a30 (0.57) don't yet. campaign_window_fit.md has the table.

## 2. Performance

**Whole frame, b30's beach route [E]** (same map, same route: the player placed on the beach at 110 s, looking along it; no input). Early b106 (build h, before tonight's performance work) against the build shipped:

| Game ticks | Tick ms, early → now | Frame ms, early → now | fps |
|---|---|---|---|
| 600–1500 (landing) | 26.6 → 23.6 (−11%) | 69.8 → 42.5 (−39%) | 8.1 → 11.3 |
| 1500–3000 | 25.6 → 22.2 (−13%) | 74.8 → 40.6 (−46%) | 7.8 → 11.7 |
| 3600–4500 (the fight) | 20.2 → 16.2 (−20%) | 52.5 → 20.8 (−60%) | 10.5 → 21.3 |
| 6000–9000 | 11.0 → 9.2 (−17%) | 47.6 → 19.4 (−59%) | 13.7 → 29.6 |
| 9000–12000 | 10.8 → 8.3 (−23%) | 38.2 → 17.2 (−55%) | 16.1 → 30.0 (the emulator's cap) |

The emulator's frame is lighter than the PSP's, so the PSP's percentages will be smaller; what matters is that each cost below is gone or shrunk.

### 2.1 Waits and wasted CPU outside the game's own code

- **The cache file thread now runs above the game thread** (`psp_cache_thread_priority`, priority 0x1C; `readerslow` in keep.txt undoes it). It reads sounds, the second BSP and the loader's tag reads. [E] pthreads gave it 0x3C, below the game thread's 0x20. The PSP never shares the CPU between threads, so a finished read waited for the game thread to block before the next could start, holding the stick's lock meanwhile: halo_bench v10's rule ("any streaming runs above the game thread") broken. [M, b105 run 1] That thread spent 647 s in 7,468 stick calls, **87 ms a call** for reads of about 30 KB the stick does in about 4 ms. Expect fewer sound cut-outs and shorter stalls behind the stick's lock.
- **Condition variables that sleep** (`psp_libc.c`; `pthread_cond_*` replaced through `--wrap`). [D, the linked library's code] pspdev's pthreads waits on a condition by polling: a look, then `sceKernelDelayThread(100)`, again and again. [E] The cache file thread woke 4,000 times a second for 5% of the CPU; with the new waits it wakes about 3 times a second for 0.0%. Without this, the priority change above would have handed that 5% to a poller interrupting the game.
- **The renderer's timers.** Each clock read is a system call (0.56 µs on the PSP [M, b105's "one timer reading"]). [E] Clock reads were 8% of the game thread at the beach, 6.4 of it the decals' five reads per decal. Decals now time one call in 16 and count it 16 times. Clock reads: **8.0% → 2.1%**. `notimers` turns the per-step, per-object and renderer timers off entirely (the heartbeat's tick and frame times stay on): [E] worth another 3–10% of frame rate, so the profile windows cost that much when on.

### 2.2 Rendering

- **Decals.** b30's fight leaves 800–900 decals (bullet holes, blood, scorch marks) to draw a frame, most behind or beside the camera.
  - Quads wholly outside the view are found **before** the texture is bound and the display list is taken. Before, a decal with nothing on the screen still bound its texture (fetching it if it wasn't in VRAM) and reserved list room for every quad it then didn't use. [E] Decals were 131 KB of the beach's 282 KB list a frame; at 842 calls a frame, 828 had nothing on the screen.
  - The game's decal loop skips the Xbox's blend, shader and texture state on the GE path, and works out the GE's texture key only when the bitmap changes.
  - [E] decal CPU at 842 calls a frame: 3.6–3.9 → 2.5–2.7 ms; on the PSP, b105's Library fight drew 446 a frame at 10.9 ms [M], so this is a large lever there.
  - **Projected permanent decals off by default** (build 90's "on the level's triangles"). In every log since, on the PSP and in the emulator, it drew none that way ("0 drawn on the level's own triangles") and each fell back after a scan that cost 0.8 ms a frame at the beach [E]. The debug menu still turns it on.
- **Near models.** A bending part at the eye (a tree swaying over the player, two-sided leaves) had every corner tested again by each of its passes: [E] 24% of the game thread at one point on the beach. The first pass's lists are reused by the later passes of the same part (`near_memo`), and the corner verdict keeps all six plane bits, so triangles wholly outside one plane are dropped before their corners are skinned.
- **Bending corners near the eye on the VFPU, as an option** (`vfpuskin`; off by default). These corners are most of `model_geometry`, 9% of the beach's time [E]. Each batch's slot matrices are loaded once as VFPU matrices, and a corner is one `vhtfm4` per weighted slot, scaled and summed (`skin_vfpu_corner`). [E] Correct: 2,193,417 corners compared with the FPU (`skinvfpucheck`), **0 verdicts differed**. Not faster in the emulator (2–4% slower over the frame; its VFPU costs are its own), so it waits for an A/B on the PSP.
- **The near plane's choice ("nearauto").** The nearest model's limit is applied first and the level is tested only at the scales it leaves. [E] 1,177 µs → 1 µs a frame at the beach (the marines held the plane at x1, after up to four sphere queries of the level). The choice is the same: a sphere clear of the level leaves every smaller one about the same centre clear.
- **Objects' dynamic point lights aren't looked for** (`objectpointlights` looks again). [D] The game finds them only to pick the Xbox's per-vertex-light vertex shader; the GE lights a model with the ambient colour and the two distant lights. The dynamic lights' share of an object's light still reaches it through the lightmap sample, which looks for them separately. [E] 2.4% of the beach's time.
- **The change-colour and glow passes** (armour colours, self-illumination) use the GE's texture blend with the colour as the environment colour, and the mask's palette is cached by (texture, pass, count).

### 2.3 The game's own code

- **Sound obstruction** (`compute_sound_obstruction`: a collision test from the camera through the level, water, objects, scenery and machines for each sound, each refresh).
  - A sound past its maximum distance isn't tested: it is out whatever the test says (exact).
  - A result is reused for up to 4 ticks; older ones are tested again while the frame's 6 tests last, and reused past that (muffling that follows the camera 100–200 ms late).
  - [E] The Library: 153 tests a frame were of sounds past their reach; now about 1 test a frame. Its `game_sound_update: looping sounds` step: **2.7 → 0.8 ms a frame**. [M, b105] That step measured **11.5 ms a frame** on the PSP (median of 94 windows), so expect most of that back.
  - `game_sound_update` is split into four profile steps, and a `sound obstruction:` line reports tests, reuses and skips every 300 frames.
- **Lightning** (the Library's beams and arcs). [D] Each chain of each instance cleared its whole 4,097-point scratch array, **147 KB on the stack, every frame**, only so its checks would find unused points marked not valid. Only the points a chain uses are read, each written first, so only their flags are cleared now: the same points come out. [E] The Library's `widgets_render` step (lightning runs right after it): **1.41 → 0.31 ms a frame**. On the PSP that step was 8.2–8.5 ms a frame in b105 [M], and the memset also pushed 147 KB through the 16 KB data cache each time.
- **The keep-word cache.** `psp_keep` is asked about 13,000 times a second (per object, per draw); each ask was a `snprintf` under the formatting lock plus a search of keep.txt. [E] 6.6% of the game thread. The answers are remembered by the word (128 slots), dropped when keep.txt changes.
- **Lookups without frames.** `datum_get` (every object, light and cluster reference) and `tag_get` (every definition, shader and bitmap) carried large stack frames for their assert messages' buffers (160 bytes for `datum_get`): the messages moved to cold helpers, and the found-it paths have no frame. `datum_get` is inlined in the three files that call it most (cluster partitions, objects, object lights); the object lookups (`object_get`, `object_header_block_get`) lost their frame pointers. Inlining `datum_get` everywhere was tried and dropped: 48 KB of code, 64 KB of heap.
- **Debug work off on hot paths** (kept with `PSP_DEBUG_TOOLS=1`): `data_verify` on every data-array step, the collision usage log's clock reads per collision call (2% of b30's tick), the frame profiler's texture timer.
- **The sleep checksum** works through each object in spans between the words its rules leave out, instead of word by word. `checksumcheck` compares it against the reference; `slowchecksum` uses the reference alone.
- **Corpses sleep now.** [E] A dead unit's aim and look servo flips between two values every tick, so no corpse was ever byte-identical from tick to tick and none slept (b105 run 1's "0.0 corpses"). That range is left out of a corpse's checksum and keeps its last values while it sleeps. b30's beach: about 16 corpses asleep a tick. The rule's stages (dead, garbage, at rest, idle 2 s) are counted in the log.

## 3. Instruments

- **`threads (...)` line** (every 30 s, with the stick report): each thread's priority, CPU share and times woken. On the PSP this is the first look at what the threads cost.
- **Step-tagged PC samples** (emulator): `PSP_PCSAMPLE_STEP=<address of last_step>` makes the local PPSSPP patch record the running `psp_step` with each sample; `tools/pcprofile.py --steps` lists the steps by samples (every thread's), `--step TEXT` profiles one. This is what found the obstruction tests, the lightning memset and the point-light search, each hiding under another step's name. The patch is in `tools/ppsspp_local_patches.diff`.
- `tools/abcompare.py`: two runs' heartbeats side by side by game time. `tools/emu.sh`: the canonical emulator run (clears and copies `profile.txt` and the animation trace).
- Log lines: `decal calls with nothing on the screen`, `sound obstruction:`, `cache file thread: priority ...`, the corpse-sleep stages, the garbage collector's counts, `printfcount` (the busiest format strings).
- **The bake bundle:** `invader-build -h` had crashed ("Left column exceeded") since `-K`'s help text; the new options are `-M` (`--psp-stream-anims`) and `-Y <n>` (`--psp-script-spare`), and `-K` takes `<r,t>`. The bundle's `invader-psp-fork.patch` now includes the fork's changes not yet committed (the source tarball always had them).

## 4. Hardware test plan

1. **Bake b30 with the new bundle** (P14S): `./halo-psp-bake ~/Halo.iso ~/halo-psp-out --maps b30,ui`. Copy `~/halo-psp-out/halo` to the stick's root.
2. **Install b106**: `PSP/GAME/halo/` from the zip (EBOOT and PRXs).
3. **The Silent Cartographer**, keep words `assettrack geo2 nearauto` (the emulator's measurements used these), deathless. Play the landing and the beach fight, then up the beach a while. Look for:
   - it loads, and the Pelican drop plays;
   - no `HEAP FULL`, controls alive;
   - the log's `animations:` line and the `io:` lines' animation reads;
   - the `threads (...)` lines: the cache file thread at 0x1c, CPU shares;
   - fps and tick against b105's Library fight (5.0–6.7 fps).
4. **The Library**, same keep words, into the first Flood fight: the `game_sound_update` steps, `widgets_render`, `rasterizer_decals_draw` in profile.txt against b105 run 1, and the stick report's cache file thread time a call (87 ms in b105).
5. Optional: one run with `notimers` added, to see what the timers cost on the PSP (the profile windows read 0 then; the heartbeat's fps stays).

## 5. Not done, next

- The tick on b30: bipeds (physics, animation, node matrices), the AI, collision (#38's compact object broad-phase), the cluster partitions' reconnect (`reference_list_remove`).
- Game-state pools to their measured peaks (b30's beach: actors peak 29 of 256, 457 KB; props 118 of 768; hs threads 68 of 256): needs peaks from the other missions first.
- The scenery sleep checks every tick (2.3% of the beach's time): checking idle scenery every other tick would halve it, at a tick's delay before something done to it is noticed.
- c20 rebaked with `-M -Y`: its window would gain about 1.4 MB.
