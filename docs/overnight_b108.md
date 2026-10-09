# Overnight 2026-10-09: memory waits, spare hardware, the last three maps

Sheepdog01's goals for the night: find effective uses for the scratchpad and the parallel compute (the VFPU, the Media Engine), chase further memory gains, and watch memory bandwidth (why the CPU waits on memory, and what can be done). The main menu if time allows. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator or estimate, **[D]** read in the code or the tags, **[U]** inference.

## 1. Sheepdog01's b107 run with `hwprofile` and GE serial (b30, the beach) [M]

Run `b107_b30_hw`: landing and beach fight, with GE serial switched on twice at standstill (66 s and 112 s). `tools/hwreport.py` and the per-phase lines:

| | Tick | Drawing | Frame end |
|---|---:|---:|---:|
| Share of the CPU's cycles | 43% | 54% | 1% |
| Waiting on memory | 54% | 59% | 37% |
| I-cache misses per M cycles | 5,200 | 2,700 | 4,050 |
| D-cache misses per M cycles | 2,860 | 1,690 | 910 |

- **The whole run: 54% of the CPU's cycles wait on memory.**
- **The GE's busy time** (GE serial: the frame end's wait is then the GE's whole time): 32 ms a frame (median), 40 (mean), against frames of ~108 ms. **The GE is busy about a third of the time; the CPU is the limit.**
- **The GE's own memory traffic slows the CPU's drawing:** with GE serial (the GE runs only after the CPU has finished the frame's list), drawing's memory wait falls from 59% to 43%. The tick's doesn't change (54%): the GE is idle during the tick either way.
- **The misses explain the wait.** At the measured cost of a miss (216 ns, 72 cycles, with the bus quiet; 424 ns when the GE reads main RAM, psp_hardware_reference.md), 6,300 misses per million cycles is 45–90% of the cycles. About two thirds of the misses are instruction fetches (tick: 5,200 of 8,060).

So the CPU is not short of bandwidth so much as **latency-bound**: it stops on each miss. Two levers follow:
1. **Fewer misses.** Above all instruction misses: the hot code is far larger than the 16 KB I-cache (b102: half the fetches from 39 KB of code, nine tenths from 278 KB [E]).
2. **Cheaper misses.** A miss costs twice as much while the GE reads main RAM; less GE main-RAM traffic during the CPU's drawing (vertices and textures in VRAM) cuts it (b102 benchrun: "GE busy on VRAM only", memory wait 56% → 40% [M]).

## 2. Why the CPU waits: the emulator's cache model, by name [E]

`tools/cachemap.py` (new) names every miss the emulator's cache model counts (16 KB 2-way caches, 64-byte lines, as the PSP-1000's): by the data it touched (the program's variables, the game state's allocations, the tags in the window) and, for data misses, by the function whose load or store missed. Run: b30's beach, the interpreter from a savestate at 235 s, b108f (before tonight's fixes).

**The model agrees with the hardware.** In the tick, instruction misses are 64% of all misses (model: 130.7 M of 204.8 M) against 65% on Sheepdog01's PSP (5,200 of 8,060 per M cycles). So its "where" can be trusted, though its counts are not time.

**Instructions (two thirds of the misses): too much hot code, not a few bad functions.**
- The tick's hot code: half its fetches come from 5 KB of code, nine tenths from 59 KB, against a 16 KB cache. These are capacity misses.
- The profile is flat: the worst function (`object_compute_node_matrices`) has 3.2% of the tick's instruction misses; the top 30 together about a third.
- Reordering the code was measured in b102: about 3% of the misses. What helps is less code on the hot path:
  - `-Os` on the hot code (the C/D runs, below);
  - less work per tick (fewer objects updated: the sleep system);
  - code moved to the Media Engine, which has caches of its own (section 5).

**-Os against -O2 in the model: inconclusive.** Instruction misses per fetch fell with `-Os` (tick 1.61% → 1.28%, drawing 0.99% → 0.90%), but the two runs played different stretches: the `-Os` build ran fewer ticks per emulated second, so its savestate landed earlier in the fight, and the fetch counts per tick aren't comparable. The question stays with the hardware A/B (the same stretch, `hwprofile`, both EBOOTs).

**Data (one third):**

| Tick: where the misses land | | Drawing | |
|---|---:|---|---:|
| game state (the objects' 1 MB pool: 25%) | 43% | the program's own tables (renderer, geometry cache) | 41% |
| tag window (the level's BSP: 16%) | 30% | tag window | 19% |
| thread stacks (the game thread's: 9%) | 13% | game state | 13% |
| the program's variables | 12% | heap | 13% |

The functions that missed most: tick, `psp_checksum_span` 5.5% (the sleep system's checksums), the collision walks (vector 4.8%, 2D sphere 4.1%, 3D sphere 3.9%), `object_get_and_verify_type` 4.0%, `memory_pool_verify` 3.5%. Drawing: `model_geometry` 8.3%, `psp_geo_generation` 5.7%, `render_model` 4.9%, `psp_step` 3.4% (the profiler's breadcrumbs).

**The GE's traffic doubles the cost of each miss.** [M] With GE serial, drawing's memory wait fell from 59% to 43% (section 1). Vertices and textures in VRAM cut that traffic (b102 benchrun: memory wait 56% → 40%).

## 3. Fixed tonight (b108)

| Fix | Why | Evidence |
|---|---|---|
| Geometry cache lookup in O(1): last block, then a 64-slot table by address | `psp_geo_generation` scanned the 12 KB entry table for every model part: 5.7% of drawing's data misses | [E] gone from the top 40 in b108h |
| Pool checks off (`poolverify` turns them on) | `memory_pool_verify` walked every block header on every allocation and lookup: 3.5% of the tick's data misses | [E] |
| Profiler slots dense, with a 64-entry front table; then both in the scratchpad (b108n) | slots placed by hash, one cache line each: 2.2% of drawing's data misses. Dense alone didn't help (the front table missed as much as the slots saved) | [E] b108l; b108n moves them |
| `model_cut` (8,448 B) in the scratchpad | the near-cut corner table: 73,617 misses per KB in drawing | [E] 108j: "scratchpad: 8448 bytes at 00010000" |
| No mirror window | b30's mirror floors drew the whole frame from under the map | [M] Sheepdog01's b107 run; [E] fixed |
| Level end: no next map on the stick → the game exits with "level finished" | the error loop drew a frame and read a gone player's unit (bus error) | [M] Sheepdog01's b107 run |
| The local player's helpers only while a game is in progress | same crash, other readers | [D] |
| A full data array no longer searched again on every `datum_new` (ChupathingyCE's fix) | an array at its cap (props, effects, particles: the PSP's caps are lower) was walked end to end on each new datum | [E] `datum_new`'s misses −18% |
| -D, -Z: grass and unread tag bytes left out | a30, a50 and b40 fit the tag window | section 6 |
| No Bink movie started on the PSP (it has no Bink) | the main menu's intro movie took more of the texture cache than it has, before failing to open | [E] section 8 |

**Measured together** [E]: b108l against b108h from the same moment on the beach (594 against 593 frames, 1,011 against 1,012 ticks). b108h already had the geometry lookup; b108l adds the pool checks, the profiler's dense slots, `model_cut` in the scratchpad, the slim tag layouts and the `datum_new` fix. Two runs of the same build differ by about 3% (b108h with and without `vfpuskin`).

| Per tick / per frame | b108h | b108l | Change |
|---|---:|---:|---:|
| Tick: instructions | 5.10 M | 4.67 M | **−8%** |
| Tick: instruction misses | 78.4 K | 73.3 K | −7% |
| Tick: data misses | 44.7 K | 39.2 K | **−12%** |
| Drawing: instructions | 14.29 M | 13.56 M | −5% |
| Drawing: instruction misses | 134.1 K | 130.5 K | −3% (noise) |
| Drawing: data misses | 87.8 K | 79.7 K | **−9%** |

On the PSP a data miss in the tick costs about 216 ns (the GE is idle then), so 5,500 fewer a tick is roughly 1.2 ms a tick [U]; the hardware run says how much.

## 4. The scratchpad

16 KB beside the CPU, as fast as a cache hit, off the bus, never evicted. Nothing used it before b108.
- **Rule:** a table earns a place by its misses per KB in the model; one owner (the game thread); handed out once at start-up (`psp_scratch.c`); `noscratch` puts every piece back on the heap for the hardware A/B.
- **In now (b108n, 15.1 of 16 KB):** `model_cut` (8,448 B) and the profiler's slots and front table (6,656 B).
- **Ranked by the b108l model** (drawing, misses per KB): the profiler's front table 1,128 K/KB, `rasterizer_xbox_d3d_globals` 325 K/KB (2.2 KB), `hash_head` 169 K/KB (2 KB), the geometry cache's `entries` 159 K/KB (12 KB: 4.0% of drawing's data misses, the largest named table), the profiler's slots 101 K/KB, `model_cut` 50 K/KB.
- **Next:** `entries` with `hash_head` and the front table (14.8 KB) would save about 1.7 times what tonight's pair saves, but needs `model_cut` and the slots back on the heap; or cut the lookups (a lock re-finds its entry by hash each time). For b109, measured first.
- **Not candidates:** the big misses (objects, BSP, stack) are tables of hundreds of KB.

## 5. Parallel compute: the VFPU and the Media Engine

**VFPU (vector unit, same core).** It only helps arithmetic; the CPU's problem is memory.
- `vfpuskin` (near-corner skinning on the VFPU), model A/B on the beach [E]: the near-corner function's instructions per frame changed by less than run-to-run noise (−0.3%). Near corners are too few there to matter. It stays in the hardware A/B list, low priority.
- Bone matrices on the VFPU: about 1.5–2% of drawing at best [U].

**Media Engine (second MIPS core, 2 MB of its own eDRAM, its own 16 KB caches).** Audio runs there already (meaudio). Its real gift is its own caches: code moved there stops competing for the main CPU's 16 KB instruction cache.
- Limits: no VFPU, no coherent view of main RAM (the CPU must write its cache back before the ME reads), no system calls.
- Candidates, by their share of the tick on the beach [E]:
  - **rays against the level's BSP**: `collision_bsp_test_vector_recursive` is 3.5% of the tick's instruction fetches and 4.8% of its data misses (with `collision_test_vector`, 4.6% and 6.6%). The BSP is read-only tag data, so the ME can read it with no coherence work. The callers want answers at once, so the work has to be batched: the AI's line-of-sight rays (`actor_perception_update`) are the natural batch.
  - the collision sphere walks (`bsp2d`/`bsp3d_test_sphere_recursive`, 8% of the tick's data misses) belong to object physics and are harder to batch.
  - particles: small (the phase they run in has 2% of all misses).
- First step: one ray batch with a measured cost on both sides (copy in, run, copy out) before any design.

## 6. Memory: the last three maps (a30, a50, b40)

**Method.** `tools/hek_census.py` (new) walks a tag file with Invader's own definitions and gives the bytes under every block. Each map's tags were extracted from its original and rebuilt with the bake's flags and a 16 MiB tag space, so the build reports the real tag and BSP sizes; `tools/cachemap.py`'s tag reader then sizes each tag in the built map's window.

| Map | Over by (b107 bake) | Largest BSP in the window | Tags |
|---|---:|---|---:|
| a30 | 0.57 MiB | a30_b 4.44 MiB | 5.13 MiB |
| a50 | 0.20 MiB | a50_hangar 3.47 MiB | 5.73 MiB |
| b40 | 0.53 MiB | b40_b 3.13 MiB | 6.40 MiB |

The BSP levers the fit plan counted on (leaf surface references and subcluster indices at 16 bits, the 6-byte position copy) turn out to be in since build 68: what's left in the window is the collision (a30_b: 2.2 MiB, packed), the grass, and the render BSP's own tables.

**Two levers delivered** (the fork, the bake and the engine; [M] the build step, [E] the emulator):

| Lever | What | a30 | a50 | b40 |
|---|---|---:|---:|---:|
| **-D: detail objects left out** | the BSPs' grass, which the PSP never draws (`structure_render_detail_objects` returns at once since build 14; nothing else reads them) | −1.16 MiB (both BSPs; −0.64 on the largest) | 0 | 0 |
| **-Z: four blocks without the bytes the game never reads** | sound permutations' names and subtitle data (124 → 72 bytes), pitch ranges' names (72 → 40), AI squads' unused fields (232 → 144; the name stays, messages print it), firing positions' padding (24 → 20) | −0.44 | −0.50 | −0.60 |

| Map | Tag space with -D -Z [M] | Fits the 9.00 MiB window? |
|---|---:|---|
| a30 | 8.50 | **yes**, 0.50 MiB spare |
| a50 | 8.70 | **yes**, 0.30 MiB spare |
| b40 | 8.94 | **yes**, 61 KB spare |

**How -Z works, and old maps keep working.**
- The fork (`invader-build -Z`, needs `-G`) keeps only listed byte ranges of each element, moving pointers and tag-ID fields with them, and sets bits 0–3 of the geometry pack's header word 10.
- The engine's structs leave the same bytes out on the PSP (FIXES_108 `#layouts`), so the game reads the blocks as it always has. The five places that read a permutation's name print the tag's name or nothing.
- A map from an older bake is made slim in place as its tags load (`psp/src/psp_tag_layouts.c`, the same table, checked against the structs at compile time): each element moves down to its slim place, so a block keeps its address and count.
- [E] b108k, b30 from the b106 bake: "Xbox layouts made slim in place (bake formats 0): 7624 sound permutations, 1441 pitch ranges, 115 squads, 4470 firing positions"; the same AI peak (29 actors) and sound loads as 108j, no assert.
- [E] b30 from tonight's first -Z bake (permutations only, formats 1): the other three converted, same result.
- [E] b40 from the new bake (formats f): "slim layouts from the bake"; 10 minutes with nobody at the controls, the player killed and the level reverted to its checkpoint five times, no assert. Its BSP switches wait for a hardware run.

**Every campaign map now fits** [M] (the build step, b108 bake flags):

| a10 | a30 | a50 | b30 | b40 | c10 | c20 | c40 | d20 | d40 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8.52 | 8.50 | 8.70 | 8.19 | 8.94 | 8.38 | 7.31 | 8.43 | 8.46 | 8.17 |

(MiB of the 9.00 MiB window, the tags plus the largest BSP.)

## 7. ChupathingyCE: what moving would bring [D]

ChupathingyCE is a community build of OpenCE, which looks like halo-ce-universal's continuation (same `build-N` tags, same comments; inferred, not confirmed). It tracks OpenCE build-149 to 157; our base is build-58 plus one commit (network version 9 against 24).

**No CPU or memory gain for the PSP, probably a small loss.**
- What changed is mostly desktop and online: 64-bit pointers (compile to nothing on 32 bits), Custom Edition and HaloMD maps, network co-op, the game browser, hi-res HUD, menus, a tag validator.
- Hot paths got *more* checks: every BSP node visited (`collision_bsp.c`), per-surface checks in `structure_visibility.c`, keyframe and node checks in the animation code, the data arrays checked twice a tick, every tag schema-checked at load.
- About 35,000 more lines in `port/linux/game` the PSP build would compile: less heap.
- `pgo/` is clang profile data for x86, of no use to psp-gcc.
- One real fix, now taken into b108: `datum_new` no longer re-walks a full array (section 3).

**What a move would cost:** 33 of our 287 fixup anchors fail (21 files); the other 254 still match but sit in changed code and need review; 10 new capacity macros; stubs for the new platform dependencies; then re-profiling the hot paths.

**For GitHub:** the licence is CC0, the same as our base, so either base can be published. Our `#ifdef HALO_PSP` style suits upstream's rule that 32-bit builds stay byte-identical. The move is not needed to publish; choosing which upstream to fork from is a community question for Sheepdog01.

**The parity build: started, stopped on purpose.** `PSP_FIXUPS_CHECK=1 python3 psp_fixups.py TREE` (new) lists every anchor that fails rather than stopping at the first: 34 on ChupathingyCE (the 33 above and tonight's `datum_new` fix, which it already has). Working through them showed why this is more than re-anchoring:
- ChupathingyCE added checks of the map's data in the same code our fixups change, and they assume the Xbox's layouts. Two examples in `structure_visibility.c`: a material's vertex count taken as its compressed data's size over 32 bytes (our bake packs a vertex in 6, so most surfaces would be skipped), and surface indices checked as 32-bit values (ours are 16-bit). Its tag validator checks every tag at load against the stock layouts; our bake changes some on purpose (slim blocks, packed collision, external geometry), so it would need to know the PSP's [D, not run].
- So each of the 34 needs the new check rewritten for the PSP's layouts and re-tested, mostly in the renderer and the collision code, where a slip shows up as missing walls rather than as a compile error.
- Done as a sample: the portal hull cache (one of the six `structure_visibility.c` anchors), with a per-base alternative (`Either` in `/home/claude/psp_port_chupa`, a copy of the port tree; the shipped tree is unchanged).

**Recommendation:** publish from the current base (or as a fork of halo-ce-universal), and take ChupathingyCE's bug fixes one at a time, as tonight's `datum_new` fix. A move becomes worth it if their netcode is wanted for the PSP's co-op branch.

## 8. The main menu: a first look [E]

Booted with no map named (the game starts on `ui.map`, as the Xbox does), 5 minutes in the emulator:
- **It runs:** the HALO title, CAMPAIGN / MULTIPLAYER / SETTINGS and the version string draw; the camera pans slowly; 30 fps at **3.1 ms a frame** in the emulator, where a b30 frame takes about 45. A passive level leaves nearly the whole frame free for quality.
- **The background needs a reference to judge:** a large grey surface fills the left of the screen, with a diagonal edge and black below it, and 76-80 of the ring's triangles a frame are cut at the near plane.
  - The camera is right [D, E]: the menu's one script (`ring_loop`) moves it through 11 camera points within 2.5 units of the ring model, and the emulator's camera sat between the first two. A close surface is the design, not a bug.
  - Open questions: the black below the edge, and the white band across the ring from Sheepdog01's b106 run. Both need an Xbox shot of the same moment to compare.
- **Bink:** the menu starts its intro movie through Bink, which the PSP doesn't have. Before failing it took the movie's memory from the texture cache, more than the PSP's cache holds (asserts `xbox_texture_cache.c#319`, `lruv_cache.c#603`). Fixed from b108m: the movie isn't started, so nothing is taken.
- **Not checked yet:** the menus past the first (input), the attract mode, the transition into a level from the menu (`menutest` does Blood Gulch).

**The menu pass, as its own session:** reference shots → the near-plane cuts and the black below them → the white band → quality settings that only the menu uses (full-size textures, no level-of-detail drops, finer sky).

## 9. b108 on the PSP: the test plan

The build: **b108n** (heap 6,080 KB; `halo_psp_b108.zip`), and the same code at `-Os` for the optional A/B (heap 6,400 KB; `halo_psp_b108_os.zip`). New maps need the new bake tool; maps from older bakes still load.

[E] b108n: b30 from the old bake, 7 minutes, clean, the profile windows as before; b40 from the new bake with `noscratch`, clean; the main menu (b108m) with no Bink asserts.

1. **Bake every map with the b108 bake tool** (an hour or two), copy `halo/maps` to the stick, install the zip.
2. **b30 to its end** (`assettrack geo2 nearauto`, deathless): the mirror rooms (security room, map room) no longer turn the view upside down; at the end b30 should roll into **b40** (Assault on the Control Room) now that it's on the stick. Play b40 a while: its BSP switches are the part the emulator didn't reach.
3. **a30 and a50**, a few minutes each: they load, the opening plays.
4. Optional A/Bs, the same stretch twice with `hwprofile`, most promising first:
   - `sceneryhalf` (idle scenery checked every other tick): in the model it cut the tick's instructions and misses by 9% (`b109_candidates.md`);
   - the `-Os` zip against b108n (the instruction cache question; the model couldn't settle it). The `-Os` build's heartbeat reads "heap top ... of 6400 KB";
   - `noscratch` (the scratchpad's worth);
   - `notimers` (what the profiler's per-step timers cost: thousands of steps a second, each reading the clock).

Bring back `halo/log/` (`psplogs <name>`).
