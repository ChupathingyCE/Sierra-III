# Big levers: what can close the gap (2026-10-10)

Sheepdog01 asked for the changes that would **significantly** raise the tick rate and the frame rate, tested, instead of more small gains. This doc sizes the structural levers against b108's hardware runs, says what each needs, and records the first tests (b112). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference.

## 1. The gap

Targets (Sheepdog01): 30 ticks a second everywhere, which means 2 ticks plus one frame's drawing in 66.7 ms; and 30 fps on average.

b108 run A on Sheepdog01's PSP (`hardware_b108.md`), ms a displayed frame [M]:

| Stretch | Frame | Ticks (≈2 a frame) | Engine drawing | GE layer CPU (`psp_ge.c`) | Rest |
|---|---:|---:|---:|---:|---:|
| Pelican flight | 261 | 120 | ~68 | 65 | 8 |
| Beach | 169 | 70 | 44 | 42 | 12 |

The flight's tick, window 4 [M]: `objects_update` 41.9 ms (bipeds 21.6 for 27 of them, vehicles 9.2 for 3, scenery 7.0 for 572 updates), `ai_update` 14.5 ms.

**Why small levers can't do it.** The beach needs ~2.5× and the flight ~3.9×. The open exact levers in `optimization_ranking.md` §3 add up to roughly 8–16 ms a tick, and most of those are ceilings. Each one is 1–3% of a frame.

**Where the time really goes.** The CPU waits on memory for 54–59% of its cycles, and two thirds of those misses are instruction fetches: the tick's hot code is ~59 KB against a 16 KB cache [M/E]. The Media Engine, a second CPU as fast as the main one (b90 [M]), is idle ~94% of the time.

So the big levers are: **use the second CPU**, **fetch less code**, and **cut the GE's reading of main RAM**.

## 2. The levers, ranked

| # | Lever | Ceiling | Exact? | Cost | First test | Status |
|---|---|---|---|---|---|---|
| L1 | **The ME builds the GE's lists** (the GE layer moves off the main CPU) | the GE layer's CPU: **42 ms (beach) to 65 ms (flight) a frame** [M], ~25% of each frame | yes | L | `gedefer` on one CPU, then on the ME | **steps 1–2 built (b112)** |
| L2 | **VRAM for the level's vertices** (X1) | ≈ 36 ms of drawing a frame [U] | yes | M | benchrun switch 8 against switch 3 (M3) | built since b103; **needs one hardware run** |
| L3 | **The next tick's AI on the ME while the main CPU draws** | `ai_update` 12–23 ms a tick in fights [M], one tick of the two a frame: 5–9% | if drawing and AI share nothing | L | reorder on one CPU, compare the game state every tick | reach measured; design below |
| L4 | **Profile-guided build** (GCC knows the hot path, packs it, moves the rest out) | unknown; aims at the instruction misses | yes | M | instrumented run in the emulator, cache-model A/B | blocked on memory, below |
| L5 | **The full tick/draw split** (the main CPU runs only ticks) | the frame becomes max(2 ticks, drawing) instead of their sum | yes, with a snapshot | XL | after L1 and L3 | the end state |

**L1 + L3 together** [U]: the beach 169 → ~115 ms a frame, the flight 261 → ~185 ms. That isn't the target, but it is the biggest exact bite there is, and it makes L5 possible.

**For tick rate, the gate stays the tick itself.** Even with drawing free, 30 ticks a second needs a tick under 33 ms; the flight's is 60. L5 is what makes "tick ≤ 33 ms" the whole condition (today it is "2 ticks + drawing ≤ 66.7 ms"). The tick's own big costs are bipeds (~0.8 ms each a tick [M]) and vehicles (~3 ms each [M]): those need their own structural work (data-oriented loops X5, collision walks X9), on top of these.

## 3. The ceiling bench (b112)

Keep word `ceilingbench` (b30, from 90 s of game time, standing still). Each heartbeat window switches one big part of the game off, three cycles, and prints benchrun's `bench: RESULT` lines. `tools/benchparse.py --by-switch` averages them; its "tick" column is a tick's own cost (a faster frame needs fewer ticks a frame, so the frame's tick time drops with any saving).

| Switch | What it turns off | What it measures |
|---|---|---|
| AI off | `ai_update` | AI's cost (and the objects work the actors cause) |
| objects frozen | `objects_update` | every object's update |
| effects off | effects, particles | their cost in tick and drawing |
| models not drawn | `render_objects`, shadows | models' drawing, engine and GE layer together |
| level not drawn | the level's passes | the level's drawing |
| GE layer skipped | every draw handed to `psp_ge.c`, dropped on arrival | **L1's ceiling**: the GE layer's CPU plus the GE's reading; the engine's own drawing stays |

**Emulator, b30, 3 cycles (run 38050134153) [E]:**

| Switch | ms a frame | a tick | drawing a frame | saved: frame, tick, drawing |
|---|---:|---:|---:|---|
| baseline | 76.8 | 14.3 | 45.3 | |
| AI off | 66.6 | 10.2 | 44.0 | +10.2, +4.1, +1.3 |
| objects frozen | 48.2 | 2.7 | 42.6 | +28.6, +11.5, +2.7 |
| effects off | 75.2 | 14.2 | 45.3 | +1.6, +0.1, +0.0 |
| models not drawn | 41.5 | 13.0 | 22.8 | +35.3, +1.3, +22.5 |
| level not drawn | 66.9 | 12.9 | 39.6 | +10.0, +1.3, +5.7 |

- In the emulator, objects are ~80% of a tick and models half of drawing. The GE layer ("rend", the renderer's CPU) is 25.5 of the 45.3 ms of drawing (56%).
- **Drawing feeds back into the tick:** with models not drawn, a tick costs 1.3 ms less. Unseen objects skip work (their node matrices), so drawing and the tick are coupled. L3's audit must cover this.
- The emulator's milliseconds aren't the PSP's (it runs ~2–3× faster); the shares are what transfer. The hardware run gives [M].

## 4. L1: the ME builds the GE's lists

### What it is

The engine draws by calling `psp_ge.c`'s ~40 entry points (`psp_ge_draw_level`, `psp_ge_model_begin`, `psp_ge_draw_model`, `psp_ge_queue_transparent`, `psp_ge_decal`, …). Behind them, our code turns each call into GE commands: it converts and caches vertices, bends skinned parts, picks textures, sets state and lighting, cuts triangles at the near plane. That is the 42–65 ms a frame.

The plan: the main CPU **writes the calls down** as the engine makes them, and the ME **carries them out**, building the list while the main CPU goes on with the engine's drawing and then the next frame's ticks.

### Why this one first

`tools/mewalk.py` walked everything the entry points can reach in b112's build [D] (lab run 38051515740):
- **337 functions, 188 KB of code, no calls through pointers.** It's our own code, and the walk sees all of it.
- **One function with VFPU code:** `model_geometry`'s `vfpuskin` path (keep word, off). The ME takes the FPU path.
- **23 firmware calls, all behind a handful of gates:**
  - texture loads: `psp_tex_bind` → `load_into` → stick reads and DMA;
  - geometry loads: `psp_geo_lock` → async stick reads;
  - the heap: `model_convert` → `malloc`/`free` (its lock);
  - keep words: `psp_keep` (a semaphore);
  - the clock for the profiler: `sceKernelGetSystemTimeLow`;
  - cache write-backs: `sceKernelDcacheWritebackRange`;
  - the GE's stall address: `sceGuDrawArray` → `sceGeListUpdateStallAddr`;
  - the debug screen (only when the stick lock waits long) and `taa_resolve` (temporal blend, settled off).

  Each has an ME-side answer: textures and geometry made resident by the main CPU when the call is written down; an arena of the ME's own for converted vertices; keep words copied once a frame; the ME's cycle counter; the `cache` instruction (the ME's own cache, which is the one to write back); stall addresses passed through an uncached word for the main CPU to hand on.

The AI (L3) by comparison reaches 969 functions and 389 KB, 69 calls through pointers, and firmware through actor placement, dialogue sound, seat entry, animation streaming and config reads.

### The steps

1. **`gedefer` (b112, built):** the model path's six calls are written down with every array they point to copied, and carried out in order when the 64 KB buffer fills, when any other call that writes the list arrives, or at the frame's end. Everything still runs on the main CPU and the list comes out the same. It proves the recording is complete, and it reorders the CPU's work: the engine's half for a run of models, then this file's half, so each half's code stays in the instruction cache longer. That may be a speed-up of its own [U]; the cache-model A/B measures it.
2. **The rest of the entry points recorded** (b112, built): the level, see-through level surfaces, fog, detail maps, the sky, sprite groups, widgets, decals, shadows, dynamic lights, screen quads, flashes, blips and text. What still runs at once, after what is waiting: the frame's start and end, the calls whose answer the engine uses (`psp_ge_text_begin`, the level vertex queries), `psp_ge_text_changed` (it changes the font data a waiting character reads) and the map loads.
3. **The gates moved** so the carrying-out reaches no firmware (mewalk's report goes to 0). The walk from `defer_drain` itself (lab run 38054673819 [D]: 377 functions, 206 KB, no calls through pointers) gives the list:

   | Gate | Reached from | The ME-side answer |
   |---|---|---|
   | texture loads (`load_into`: stick reads, DMA) | `psp_tex_bind` | the main CPU makes each key resident when the call is written down; a texture drawn this frame is never evicted (`psp_tex.c`) |
   | level copies' streaming (`psp_geo_lock`: async reads) | `level_stream_get` | the same, when the level draw is written down |
   | the heap's lock (`malloc`/`free`) | `model_convert` | the converted buffers and skinning plans in an arena of the ME's own, as the level copies' arena (b111) |
   | the keep-word cache's lock (on a cache miss only) | `psp_keep` | on the ME, a miss reads keep.txt's words without filling the cache |
   | the log (file writes, the debug screen) | `platform_log` | a ring the main CPU writes out |
   | the clock (`sceKernelGetSystemTimeLow`) | the entry points' timers | the ME's cycle counter |
   | `sceKernelDcacheWritebackRange` | `psp_ge_shadow_begin` and others | the `cache` instruction on the ME's own lines |
   | the GE's stall address | `sceGuDrawArray` (text, via the stall wrapper) | an uncached word the main CPU hands on |
   | temporal blend (`taa_resolve`: VRAM address, write-backs) | `psp_ge_blips_begin` | settled off; the ME refuses it |
   | engine collision (`overlay_on_a_wall`'s rays) | `psp_ge_queue_level_transparent` | done when the call is written down |
   | VFPU (`vfpuskin`) | `model_geometry` | off on the ME |

   **The check that the gates are closed** runs in the emulator, which runs no ME code: with `gedefer gemesim`, the carrying out is marked as the ME's work (`psp_me_context` holds the game thread's id), and the emulator's ME-strict check (`PSP_MESTRICT`, in the lab's patch) prints every system call that thread makes meanwhile, once per function and caller (`mestrict.txt` in the lab's results). Closed so far (b112), each behind `gemesim` so nothing changes without it:
   - the entry points' timers (`ge_clock` reads 0);
   - keep-word cache fills (skipped);
   - texture loads, level copies and model conversions: the work asks, and the main CPU does them at the frame's end (`psp_tex_frame_end`, `psp_ge_frame_end`), so they're there next frame; the surface or part waits a frame, as for anything still on its way;
   - the table sweep (waits for the main CPU);
   - the log (`ge_log`: a count per message, written out at the frame's end);
   - write-backs (`ge_writeback`: the cache instruction);
   - the stall address (kept, then passed on when the run ends);
   - temporal blend and VFPU skinning (never in the ME's work).

   - the see-through level surfaces' wall test (heap for its answers, and the engine's collision rays, which read the game's data while the main CPU may move it): an unknown answer counts as "on a wall" for a frame and is asked at the frame's end.

   Still open: whatever the ME-strict run still finds.
4. **Carried out on the ME**, streaming behind the recorder. The design (not built yet):
   - **Two record buffers:** the main CPU fills one while the ME carries out the other, so no line is written by one CPU while the other reads it. A buffer is handed over when it's half full, at a call that runs at once, and at the frame's end.
   - **Hand-over** (psp_me.c's slots, `media_engine.md`): the main CPU writes back its whole data cache (the records, and what they point to that the engine wrote: dynamic triangles, decal vertices), then bumps the request number; the ME drops its cache, runs `defer_drain` on the buffer with `psp_me_context` set, writes its cache back and sets the done number. The main CPU **spins** on the done number (X3: no sleeps), and before touching the GE layer's state again it writes back and drops its own cache, so it reads what the ME wrote.
   - **What only the ME writes while it runs:** the GE layer's state (`psp_ge.c`'s statics, the converted-buffer table, the arenas), pspgu's list pointer, the list itself, the wanted lists. The main CPU writes none of them until the ME is done; its own counters (records, drains) stay apart from the ME's.
   - **The GE's stall address:** the ME keeps the newest one (step 3's gate); the main CPU passes it on each time it sees a batch done, so the GE runs the list as it fills.
   - **Calls that run at once** wait for the ME to finish what was handed over. Each one is a stall, so the next work is cutting them: the log's "carried out for" line counts them by call (text blocks, the level vertex queries, the frame's start and end).
   - **The ME's time:** its cycle counter (CP0 $9, b89) for the entry points' timers.
   - **Expected cost:** two whole-cache operations a hand-over (~tens of µs each [U]) plus the tail the main CPU waits for at each call that runs at once. Against 42–65 ms a frame moved [M].

### What stays on the main CPU

Engine calls into collision from the GE layer (`overlay_on_a_wall`'s rays for see-through level surfaces), the near plane's sphere test at the window's start, and texture/geometry residency. All happen when the call is written down or at the frame's start.

## 5. L3: the next tick's AI on the ME

**The idea:** the AI for a frame's first tick needs only the state after the last tick, which is ready when drawing starts. If drawing and the AI share no data, the ME can run the AI while the main CPU draws, and the tick then skips it.

**What the reach says** [D]: the per-actor core (perception, decisions, movement) is integer and FPU work, but the walk also reaches:
- `actor_place` → object garbage collection → object deletion, config reads;
- `ai_conversation_update` → scripted sound → the sound cache (stick reads);
- `unit_enter_seat` → weapon readying → the texture cache;
- marker lookups → `psp_keep` (a keep-word read per lookup: a cost on the main CPU too, E19).

So the AI needs a design where the rare paths (placing actors, dialogue, seats) either run on the main CPU or are deferred. And the ceiling bench shows drawing feeds back into the tick (unseen objects skip work), so the first test is the reorder on one CPU with a check of the whole game state every tick: if the state is identical with the AI moved before drawing, the data is independent.

`game_tick`'s prelude before `ai_update` (allegiance decay, unit timers, the data-array check) would move with it.

## 6. L4: profile-guided build

GCC with a profile moves every block that never runs out of the hot path, lays out branches by how they go, and inlines by real call counts. It's aimed straight at instruction misses, which are two thirds of all misses.

**The blocker** [M, lab run 38051632134]: instrumented with `-fprofile-arcs`, the program grows from 4.56 MB to **9.25 MB** (text 3.27 → 6.01 MB, data 0.15 → 1.10, bss 1.36 → 2.58) and the heap falls from 5,952 KB to 1,152 KB. The profile run can't fit in the PSP-1000's layout. The way through:
- the 64 MB layout (`PSP_BIG_RAM`), emulator only, with the game state moved above the tag window so the program and heap get 0x08804000–0x09600000;
- the instrumentation on the hot units only (about +2.8 MB [U]);
- the `.gcda` files dumped by the game itself (`__gcov_dump` on a timer) to the memory stick, brought back by the lab, and fed to a `-fprofile-use` build.

The use build is an ordinary build (no size cost), testable in the cache model, then on the PSP.

## 7. The cache model on b30's opening (b112, `fixedticks`) [E]

Lab run 38054938344: the emulator's cache models on the interpreter, b30 from the start, 5,842 ticks (194 s of game time) in 45 minutes.

| Phase | Data misses | Instruction misses | Per tick / frame |
|---|---:|---:|---|
| tick | 247 M (2.75%) | 449 M (1.59%) | 42 k data + 77 k instruction misses a tick |
| drawing | 213 M (1.92%) | 362 M (0.96%) | 73 k + 124 k a frame (2 ticks a frame) |

At the PSP's ~216 ns a miss [M, `psp_hardware_reference.md`] that's ~26 ms of waiting a tick and ~43 ms a frame's drawing [U]. The tick's hot path: half its fetches from 5 KB of code, nine tenths from 59 KB (the same 59 KB as before).

Where the tick's data misses go, three new suspects (all exact levers if fixed):
- **The sleep rule's checksums** (`psp_checksum_rules_for`, `psp_checksum_span`: 6% of the tick's data misses; `psp_sleepy_update` 1.3% of its instruction misses). The byte-identical rule hashes each candidate's bytes every tick.
- **The game thread's stack** (13% of the tick's data misses, 9% of drawing's). Deep calls with large local buffers (the collision walks' feature lists) push everything else out of the 16 KB cache.
- **`memset`** (3%): structures cleared whole (`path_state_new`, X20, is one).

The objects pool is 20% of the tick's data misses, the object spheres 3.7%, the collision BSP walks (`collision_bsp_test_vector_recursive`, `bsp3d_test_sphere_recursive`, `bsp2d_test_sphere_recursive`) 15% together. Drawing's: `model_geometry` 8.8%, `render_model` 5.5%, the geometry cache's lookup `resident_entry` 4.1% (its `entries` table: E6).

## 8. Tests running and next

- **Emulator ceiling bench with the GE layer switch** (b112).
- **Cache-model A/B, `gedefer` off and on** (lab `cache=1`: the emulator's cache models on the interpreter, each phase's misses in `cache.txt`).
- **For Sheepdog01's PSP:** the ceiling bench on b30, and benchrun on Blood Gulch for switch 8 (L2). Steps in the build notes.
