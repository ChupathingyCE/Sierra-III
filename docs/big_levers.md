# Big levers: what can close the gap (2026-10-10)

Sheepdog01 asked for the changes that would **significantly** raise the tick rate and the frame rate, tested, instead of more small gains. This doc sizes the structural levers against b108's hardware runs, says what each needs, and records the first tests (b112). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference.

## 0. The priority: align the workload to the pipeline (Sheepdog01, 2026-10-11)

**High priority, because it decides both targets** (`frame_rate_targets.md`). Today the frame runs as a line: the main CPU ticks, then builds the GE's lists, then the GE draws.
- The main CPU waits on memory for ~58% of its cycles.
- It spends 42–65 ms a frame building lists (b108 [M]).
- The GE works 25–30% of the clock (b113 [M]). Its own time, 79 ms in the flight and 46 on the beach, is already over a 30 fps frame (33.3 ms), and in the flight over a 15 fps one.
- Its main-RAM reads double the CPU's miss time while it works.
- The Media Engine mixes audio, 10–18% busy in b114's flight and beach [M], and is otherwise free.

The aim is a pipeline: each unit works on its own stage of a different frame, and each reads from the memory nearest to it. The levers already ranked below are its pieces: L1 step 4 (the ME builds lists while the main CPU runs on), L2/X1 (VRAM for the GE's reads), L3 (AI on the ME), L5 (the full tick/draw split).

**The Memory Stick is a stage too:** what's streamed in play, how to make it arrive ahead of need without stopping the game thread, smaller asset bakes, and how to measure the headroom and balance the caches are in `streaming.md` (S1–S11).

### The memory pools, b114 on b30 [M from the hardware log unless marked]

| Pool | Size | What's in it |
|---|---:|---|
| VRAM | 2,048 KB | two 16-bit framebuffers 544 KB, depth 272, motion-sensor target 16; texture cache 1,216 (set by binds per byte) |
| Program [D, `psp-size`] | 4.6 MB | code 2,762 KB, read-only data 462, data 146, zeroed 1,341 |
| Heap | 5,888 KB, peak 5,520; **147 KB of user RAM free at worst** | sound cache 1,792; level vertex copies (the arena) 1,056, peak 1,013 in use; display list 768, peak 601 in use; data arrays (script nodes 371, sounds 86, …); the shim's index buffer 192; rasterizer pool 96 |
| Game state | 3,412 KB, 3,347 allocated | Xbox-sized pools: actors 457 KB (peak 39 of 256), props 234 (255 of 768), objects 1,024 (494 used) |
| Tag window | 9,216 KB, full | tags 5,158; the 377 KB gap before the BSP holds 368 KB of the animation cache [D, `psp_anim.c`]; the largest BSP fills the rest |
| Stacks | 784 KB | main thread 281 of 512 at its deepest |
| Volatile (borrowed) | 4,096 KB | geometry cache 2,048 (`geo2`; b30's pack is 11,988), texture RAM cache 1,904, animation cache 144 |
| Scratchpad | 16 KB | the CPU's hot tables |
| ME eDRAM | 2 MB | the ME's own memory, which nothing else can see: me-core and the audio mixer |

b30's texture pack (7,060 KB) doesn't fit the 3,120 KB of texture caches, so textures stream as they're drawn.

### The proposed arrangement [U, under discussion]

The rule: each byte goes nearest its heaviest reader.
- **What only the GE reads, and reads often** → VRAM.
- **What the CPU reads constantly and rarely writes** → the scratchpad, then a cache-friendly layout.
- **What only the ME reads** → its eDRAM.
- **What several units share, or the ME writes** → main RAM.
- **Bulk** → the stick, streamed through caches.

| Pool | Today | Proposed | Why |
|---|---|---|---|
| VRAM: display | 832 KB | 832 KB | fixed |
| VRAM: textures | 1,216 KB | ~400 KB: the best third by binds per byte | Texels from RAM cost ±1 ms (Blood Gulch [M], stand-in textures). On b30 the best third of the VRAM set (383 KB) took 92% of its binds (b114 test 1 [M]) |
| VRAM: level vertices and indices | 0 | ~450 KB, 16-bit (`levelcompact`) | Blood Gulch [M]: 4.4 of the level's 4.8 ms recovered from VRAM. Compact vertices don't speed reads from RAM, but fit twice as many here |
| VRAM: models | 0 | ~350 KB: the shared cast at the LODs drawn | Blood Gulch [M]: the models' reading costs 8.5 ms. One marine model drawn ten times is read ten times |
| Heap: level arena | 1,056 KB | none: the GE-format copy moves to VRAM, made by the main CPU at the frame's end as today's conversions are; the positions the ME's near-plane cuts read go to **the ME's eDRAM**, copied there by the ME from the geometry cache (~190 KB at 16 bits) | The ME can't see VRAM and the main CPU can't see eDRAM, so data only the ME reads belongs in eDRAM (158 ns against 214 from main RAM [M]). Frees the arena's ~1 MB of heap |
| Texture RAM cache | 1,904 KB | ~2.7 MB | Takes the textures VRAM gives up, paid for by the arena, the pools and debug data. The caches together stay above 3 MB: one frame bound 2.43 MB of textures (b114 test 1 [M]) |
| Game state | 3,412 KB | sized to every map's peaks (~0.5 MB less) | Needs peaks from all ten maps, Two Betrayals' fights included |
| ME eDRAM | audio | plus the ME's job code and ME-only data (L1's list state, a collision copy for L3's line-of-sight tests) | 158 ns against 214 ns from main RAM [M, `halo-psp-feasibility.md`] |

**The rules this follows:**
- **The ME can't see VRAM.** Under L1 the lists, and anything the ME reads to build them, stay in main RAM. So E14 (the display list in VRAM) conflicts with L1.
- **VRAM is written by the CPU or by DMA,** never by the ME. A buffer can't be overwritten until the GE has finished the frame that reads it.
- **The volatile partition is lost on sleep** (handled today: caches there forget on suspend).

### The DMA controller: a fast mover, not a worker

What we know (`halo-psp-feasibility.md`, halo_bench v6–v7 and v10 [M]):
- **Speed:** RAM to RAM at 181 MB/s against a CPU `memcpy`'s 76; into VRAM at 323 MB/s. 512 KB into VRAM takes 1.6 ms against 4.1–4.9 ms for the CPU.
- **It doesn't work into the scratchpad:** it reports success, but the data doesn't arrive.
- **The user call blocks the calling thread** until the copy is done (`sceDmacMemcpy`, `sceDmacTryMemcpy`). Run from another thread, it does go alongside the CPU.
- **It isn't free alongside the CPU.** While it copies, a CPU miss takes 499 ns instead of 218, and the DMA itself drops to 64 MB/s. That's worse than the GE's effect (424 ns).

**What that means:** a background copy costs the CPU about what copying it itself would. A copy is worth doing by DMA in a short burst while the CPU would wait anyway, or in place of a CPU copy that blocks: 3× faster, and the CPU's cache isn't filled with the copied bytes. Its uses here:
1. **Keeping VRAM stocked for the swap above:** the shared models, and refills of the visible clusters' vertices (256 KB in ~0.8 ms). It's already used for textures (`psp_tex.c`).
2. **Bulk moves:** the level arena's compactions (803 KB in two, in b114 test 1 [M]) and similar.
3. **Screenshot readback** for test runs: 272 KB out of VRAM.

**It only copies, it can't convert.** Today model vertices are converted in place and the level's copies are converted into GE format as they're made, both on the CPU. Baking both in the GE's own format (X21 for models; the level's GE streams in the pack) turns loading into a plain copy, so the stick fills RAM and DMA fills VRAM with the CPU out of it.

### Measure first (planned 2026-10-11)

Three measurements turn the proposal's sizes into numbers. Each is a keep word, so none changes a normal run.

1. **What the GE reads, by buffer (`gereads`; emulator first, PSP too).**
   - **How:** every draw already passes through one place, the `sceGuDrawArray`/`sceGumDrawArray` wrappers in `psp_ge.c`. With the keep word on, each draw adds its vertex and index bytes (count × the vertex format's size) to the buffer it reads: a model part in the geometry cache, a level stream in the arena, data inside the display list, or VRAM.
   - **What it reports, per heartbeat:**
     - bytes read a frame by class: level, each model type, decals and other in-list data, sprites, HUD;
     - the top buffers by reads per byte;
     - **the fit curve:** the share of the GE's vertex reads that would come from VRAM with 200, 400, 600, 800 and 1,000 KB, filled best-first.
   - **Caveat:** counted bytes assume the GE fetches a vertex for every index; any reuse inside the GE would make the real figure lower [U].
2. **The GE's time by layer (`gebusy` with layer marks; PSP only, bench presets only, since its interrupts cause hitches).**
   - **How:** a SIGNAL at each change of layer (level, models, see-through, decals, effects, HUD) on top of today's one every 16 draws. Each chunk's busy time goes to the layer it ran, and time the GE spent waiting for the list is counted apart.
   - **What it reports:** the GE's milliseconds a frame per layer, per stretch, and how long it sat starved.
3. **One thing moved, timed (`vramtex`, then `vrammodels`; PSP, the cinematic A/B).**
   - First a smaller VRAM texture cache, which shows whether texels are free on b30 as they were on Blood Gulch.
   - Then the top model buffers by reads per byte (the marines in the Pelican's cabin) copied into the room it frees.
   - The models layer's GE time with and without, divided by the bytes moved, gives what a KB of VRAM buys. That turns 1 and 2 into a forecast of the GE's floor after the swap.
   - These are also the first two steps of building the swap.

**How the results steer:**
- If the models layer dominates and a few buffers carry most of its reads: models first, as proposed.
- If the level dominates: the level's copies first, on the main CPU's path (gemeon off, so nothing yet needs the ME's positions).
- If the data inside the list dominates (decals copy their triangles into it every frame): VRAM can't hold that. The levers there are fewer decal draws, or a display list in VRAM on the main CPU's path.
- If a layer's time doesn't follow its bytes: it's fill or passes, and the levers are fewer passes and less overdraw.

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
| L2 | **VRAM for what the GE reads** (X1): the level's vertices, then the models' | Blood Gulch [M]: the level's vertices in VRAM recover 4.4 of the 4.8 ms the level's GE reading costs; the models' reading 8.5 ms; all GE reading 14 ms (32% of the drawing) | yes | M | **M3 done (b112):** switch 8 against switch 3 | the level's switch works; the design for campaign VRAM budgets next |
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

**Sheepdog01's PSP-1000, b30's Pelican flight (game time 90–160 s), 3 cycles, b112 (2026-10-10) [M]:**

| Switch | ms a frame | a tick | drawing a frame | the GE layer's CPU | saved: frame, tick, drawing |
|---|---:|---:|---:|---:|---|
| baseline | 274.6 | 46.0 | 173.9 | 90.0 | |
| AI off | 247.6 | 34.1 | 172.4 | 84.7 | +27.0, +11.9, +1.5 |
| objects frozen | 196.4 | 11.9 | 168.7 | 84.8 | +78.1, +34.1, +5.2 |
| effects off | 270.7 | 48.5 | 171.2 | 84.5 | +3.8, −2.5, +2.7 |
| models not drawn | 156.1 | 46.3 | 56.0 | 30.0 | +118.4, −0.3, +117.9 |
| level not drawn | 256.9 | 45.0 | 157.0 | 70.5 | +17.7, +1.0, +16.9 |
| GE layer skipped | 146.7 | 42.9 | 50.3 | 0.0 | +127.9, +3.1, +123.6 |

- **L1's ceiling is bigger than the GE layer's own time.** Skipping it saves 123.6 ms of drawing against its own 90.0 ms of CPU: about 34 ms more comes back to the engine's own drawing (its code and data stay in the caches, and the GE stops reading main RAM). The ME's version keeps the GE's reading, so its saving lies between the two: **90–124 ms of a 275 ms frame (33–45%)**.
- **Models are two thirds of drawing** (118 of 174 ms); the level 17 ms.
- **The tick is objects** (34 of 46 ms; AI 12). Effects cost nothing measurable.
- Each switch runs 10 s; the bench waited ~6 minutes of real time for its 90 s of game time (the game runs at about a quarter speed at 3.6 fps).

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

**Blood Gulch benchrun, b112, 4 views (2026-10-10) [M]**, ms of drawing a frame saved against the baseline (43.8):

| Switch | Saved |
|---|---:|
| GE skips the level | 4.8 |
| level vertices in VRAM | 4.4 |
| GE skips models | 8.5 |
| GE busy on VRAM only (it reads no main RAM) | 14.0 |
| stand-in textures, smaller textures | ≈ 0 |

By b103's rule the level's GE cost is its vertex fetches over the shared bus, and VRAM takes ~90% of it away; the models' vertices are next (8.5 ms here). Texels are still nothing.

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
2. **The rest of the entry points recorded** (b112, built): the level, see-through level surfaces, fog, detail maps, the sky, sprite groups, widgets, decals, shadows, dynamic lights, screen quads, flashes, blips and text. What still runs at once, after what is waiting: the frame's start and end, the calls whose answer the engine uses (the level vertex queries; `psp_ge_text_begin` too until it was given a predicted answer: the frame and the font's picture decide it on the main CPU, and if the list turns out full the replay drops the text's characters, as the engine would have), `psp_ge_text_changed` (it changes the font data a waiting character reads) and the map loads.
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

   **Checked (lab run 38059850176):** with every gate in, the ME-strict run lists only its own test call, so the work marked as the ME's makes no system call on the paths b30's opening runs. The calls that force a run are down to 2 a frame (the frame's start and end) plus the rare font update.
4. **Carried out on the ME**, streaming behind the recorder. The design (not built yet):
   - **Two record buffers:** the main CPU fills one while the ME carries out the other, so no line is written by one CPU while the other reads it. A buffer is handed over when it's half full, at a call that runs at once, and at the frame's end.
   - **Hand-over** (psp_me.c's slots, `media_engine.md`): the main CPU writes back its whole data cache (the records, and what they point to that the engine wrote: dynamic triangles, decal vertices), then bumps the request number; the ME drops its cache, runs `defer_drain` on the buffer with `psp_me_context` set, writes its cache back and sets the done number. The main CPU **spins** on the done number (X3: no sleeps), and before touching the GE layer's state again it writes back and drops its own cache, so it reads what the ME wrote.
   - **What only the ME writes while it runs:** the GE layer's state (`psp_ge.c`'s statics, the converted-buffer table, the arenas), pspgu's list pointer, the list itself, the wanted lists. The main CPU writes none of them until the ME is done; its own counters (records, drains) stay apart from the ME's.
   - **The GE's stall address:** the ME keeps the newest one (step 3's gate); the main CPU passes it on each time it sees a batch done, so the GE runs the list as it fills.
   - **Calls that run at once** wait for the ME to finish what was handed over. Each one is a stall, so the next work is cutting them: the log's "carried out for" line counts them by call (text blocks, the level vertex queries, the frame's start and end).
   - **The ME's time:** its cycle counter (CP0 $9, b89) for the entry points' timers.
   - **Expected cost:** two whole-cache operations a hand-over (~tens of µs each [U]) plus the tail the main CPU waits for at each call that runs at once. Against 42–65 ms a frame moved [M].

### Step 4's memory (2026-10-11) [D, U where marked]

Step 4 adds little memory. What it needs is rules about who writes what, and when.

| Data | Written by | Read by | Lives in |
|---|---|---|---|
| Records (calls, and copies of what they point to) | main CPU | ME | main RAM: two 64 KB buffers (one today). b114 test 5 filled the 64 KB buffer ~0.7 times a frame beyond the two forced runs [M] |
| The display list (768 KB, peak 601 used [M]) | ME | GE | main RAM (the ME can't write VRAM) |
| The GE layer's state, the converted-buffer table, the wanted lists | ME while it runs; main CPU only after it's done | both | main RAM |
| Texture, geometry and level-copy residency, model conversions | main CPU at the frame's end (gemesim's wanted lists) | ME, GE | main RAM and the caches; VRAM copies are made here too, so the VRAM swap (§0) fits |
| Positions for the near-plane cuts | ME | ME | eDRAM (§0) |

- **New memory for step 4:** the second record buffer (64 KB) and a small log ring. Nothing else, since conversions stay on the main CPU at the frame's end. **Free user RAM is 147 KB at worst** (b114 test 1), so room comes first. The cleanest source is the script-node array: 19,001 nodes × 20 B = 371 KB of heap, of which b30 uses 4,557 (`campaign_census.md`). Sized to the map, it frees ~270 KB; ≈L once nothing is shown to make nodes at run time (`asset_baking.md` §3.8).
- **Coherence:**
  - The main CPU writes back its data cache before each hand-over (the records and the arrays they point to).
  - The ME drops its cache at each hand-over, which also covers blocks the stick's DMA has just reloaded.
  - The ME writes back the list's lines before each stall address is passed on.
  - b112's Test 3 measured the hand-over at ~0.3 ms a run [M].
- **After step 4, frames in flight:** to let the GE draw frame N while the ME builds N+1:
  - **the display list becomes a ring, not two lists.** The ME writes ahead of the GE's read point, which it learns from signals like `gebusy`'s, and wraps with a jump. Doubling a 768 KB list wouldn't fit;
  - **"in use this frame" becomes "in use in the last two frames"** for textures and geometry blocks. Consecutive frames share most of their textures, so the working set grows little [U];
  - the records already copy what they point to, so the main CPU can tick N+1 while the ME works on N.

### Step 4a on the PSP (b112, Test 3) [M]

`halo_psp_b112me.zip`, keep words `nearauto assettrack hwprofile geo2 gedefer gemeon`, b30 from the start. In 4a the ME carries out the runs and the main CPU waits for each, so this measures whether the ME can do the work and what it costs, not a speed-up.

- **The ME carried out the GE layer's work:** 48 render jobs, then the one that stopped it. The ME's own time was 17.8 ms a job, worst 39.6 ms. The main CPU waited 18.1 ms a job, so the hand-over (two whole-cache operations and the polling) costs about 0.3 ms a job.
- **No loss from moving it.** Over the same stretch of b30's opening (game time 15–51), frames took 216–229 ms by the clock with the ME, against 227–262 ms in the ceiling run without `gedefer`. Two different runs, so this is within run-to-run noise.
- **What stopped it:** job 49 wasn't done within psp_me.c's 40 ms limit. The ME had finished job 48, and render jobs had already reached 39.6 ms, so the limit (set for ~1 ms audio jobs) is most likely too short [U]. The ME then stopped, and the main CPU ran everything, audio included, for the rest of the run, as designed.
- **Audio waited behind render jobs.** The ME runs one job at a time. Audio waited up to 38 ms, and 7 of 109 blocks were late, against 1 of 213 before render jobs started.
- **The level copies' wanted list overflowed:** 113 asked, 14 more than its 32 entries held.

For 4b (`b113_candidates.md` §2): a render job timeout of its own plus a progress word; audio mixed in between runs; a bigger wanted list; then the two-buffer overlap.

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

**The state check's reach [E]** (lab runs 38059921950 and 38059951107: the same build twice, `fixedticks statecrc`, no input; 38059978024 with `gedefer`). The play hash (what the game plays with) agrees between the two identical runs up to tick 1,325 and differs from tick 1,326 (44 s of game time) on. With `gedefer`, it also first differs at tick 1,326. So `gedefer` matches the game up to the point where two identical runs stop agreeing, and a state check can judge a change over b30's first 44 s only. Something outside the game's state drives play after that. The suspect [U]: the opening's dialogue. The scripts wait for a line to finish, and the mixer plays it in real time, so the tick a line ends depends on the emulator's speed. Next, find it: the first object that differs at tick 1,326, and the scripts' sound waits. **Further (2026-10-10, b113's `stateobjects1300`, two more identical runs) [E]:** this pair parted at tick 1,459, not 1,326, so the tick moves from pair to pair: the cause is timing, not a fixed event. Each object's play hash was the same in both runs for ticks 1,300 to 1,347. In every pair, the allocations "actor", "effect", "effect location" and "particle" first differ at the 30-tick checkpoint at tick 810, long before the play hash does. Suspects [U]: game logic that reads what was drawn (unseen objects skip work), and streaming that completes at host-timed moments in the emulator (sound, through dialogue timing). Next: per-object dumps from tick 780, and the effect records, in a pair.

**Found (2026-10-10, b113) [E + D]: the local random seed.** Every tick's records from tick 780 (`staterecords780`, lab runs 38083687659 and 38083717063): "effect" and "effect location" part first, at tick 806; "objects" at 808; "actor" and "prop" at 809; "particle" at 810. Each object's play hash was still the same through tick 827, so what parted first was the effects' own state. "object looping sounds" had already differed from tick 540, and "first person weapons" from tick 30.
- Why [D]: Halo keeps two random seeds. The global one starts from the game's options (0xDEADBEEF) and drives play. The local one starts from the clock at start-up (`random_math_initialize`: seconds, milliseconds, `rand()`). Effects draw from it unless their tag says "must be deterministic", and so do particles and the sound manager (pitch, permutations, looping sounds' detail periods). The sound manager draws as sounds start and loop, so its draws follow the audio and the stick's timing, not game time. Effects can create objects (debris, casings), and actors see objects, so the local draws reach play.
- **The fix for tests:** keep word `fixedseed` (with `fixedticks` and `statecrc`). The local seed starts at a constant when a map starts, and the sound manager draws from a seed of its own (`random_math.c`, `sound_manager.c`, `sound_definitions.c` in `psp_fixups.py`). Off, nothing changes. The per-tick state line now carries both seeds, and `statecmp.py` reports the first tick each differs.
- **Result (lab runs 38087279133 and 38087313399):** the two runs' **whole game state is identical at all 13,346 ticks** (445 s of game time): all 81 allocations at all 445 checkpoints ("first person weapons" and "object looping sounds" included), the play hash, and both seeds. Before, play parted at tick 1,326 or 1,459.
- **What it opens:** a change meant to be exact can now be checked over a whole run, not 44 s. That is the test L3 (the AI's reorder) and L5 need.
- **Its limit, across builds (b113's first uses) [E]:** two builds, or two sets of keep words that change the emulator's speed, still part, between ticks 1,326 and 1,357: b112 against b113 set back to b112's behaviour (`weaksum renderstates256`); b113's defaults against that; `gedefer` against none. Even the extra logging of `staterecords1316 stateobjects1316` moved one pair's first difference from 1,326 to 1,357 (lab runs 38092178378 and 38092205732). In that pair the first object to differ is a **plasma pistol bolt** at tick 1,357; every other object is the same through tick 1,363. So something b30's opening plays with still follows how fast the emulator runs, and the same binary agrees with itself only because the emulator times one binary the same way every run.
- **The suspect [U]:** drawing feeding back into play. The bolt leaves from the Elite's gun marker, and an object's nodes may be worked out fully only when it's drawn; what is drawn depends on which geometry and textures have arrived (`psp_geo.c`, `psp_tex.c`: a frame's reads are capped), and with `fixedticks` the reads land at different game times when a build runs at a different speed. **Next (b114, `b113_candidates.md` #25):** a test word that makes every geometry and texture read complete in the frame that asks for it, then the same cross-build pair again.
- Until then, judge a change across builds over b30's first 44 s (tick 1,326), and within one build and one set of words over the whole run.

### The reach, mapped (b114, 2026-10-11) [D]

`tools/mewalk.py` on b114's disassembly (`psp-objdump -d`, the symbol table to rejoin split functions). It walks every direct call from `ai_update` and flags what the Media Engine can't run: firmware calls (locks, file reads, the clock, the GE), VFPU instructions, and calls through pointers that a static walk can't follow. Error and debug paths are cut off at `halt_and_catch_fire`, `system_exit`, `display_assert`, `error`, `platform_log`, `console_printf`, `terminal_printf` and `ai_debug_update`. An assert on the ME would stop the job and let the main CPU report it.

- **What the AI can reach:** 986 functions, 386 KB of code. With the error paths included it's 1,501 functions and 630 KB.
- **The steady path is nearly clean.** From `actors_update` (perception, decisions, looking, moving), the firmware is reached three ways only:
  - `actor_decision_loop` → `sprintf`, building the "infinite decision loop" error message. That's an error path.
  - `actor_erase` → object deletion. That's a rare path.
  - every line-of-sight ray → `collision_test_vector` → `psp_sphere_of` → `psp_keep`. The keep word is cached in a static after the first call, which the static walk can't see. A run would.
- **The rare paths, by entry point** (each walked on its own):

| Entry point | Reaches | What the ME can't do there |
|---|---|---|
| `actor_place` (placing actors) | 485 functions, 191 KB | object cleanup → deletion → posing → animation reads from the stick (`psp_anim_lock` → `sceIoRead`); `terminal_shows` → the config file (fopen, the TOML parser); `sprintf` → malloc |
| `unit_enter_seat` (getting into a vehicle) | 802 functions, 303 KB | weapon readied or put away → the texture cache (`cache_file_read`), sound requests (the unzoom sound), keep-word reads through marker lookups |
| `ai_conversation_update` (dialogue) | 197 functions, 89 KB | 22 firmware functions, through dialogue sounds |
| `encounters_update` | 699 functions, 273 KB | debug prints, and placement through the encounter rules |
| `actor_erase` (inside `actors_update`) | | object deletion, as for placement |

- **VFPU:** only `vfpu_multiply`, reached through node matrices during object deletion. The ME has no VFPU.
- **Calls the walk can't follow:** 70 sites in 28 functions. They are qsort's comparator in perception, the object types' delete callbacks, the actor-type dispatch, encounter rules, and libc's stream internals. Each needs a look by hand.

**What this means for L3:** run the steady path on the ME, and send the four entry points (placement, seat entry and exit, dialogue start, erasure) back to the main CPU as requests, the way `gemeon`'s wanted lists already work.

**Confidence comes in three layers:**
1. **This static map.** It covers every direct call, but not calls through pointers.
2. **A dynamic check on real routes:** the emulator's ME-strict mode, as `gemesim` does for the GE layer. It catches the cached keep words and every pointer call actually taken.
3. **Exactness:** the whole game state compared tick by tick with the AI reordered before drawing. That waits on #25, because drawing still changes play.

### The two-CPU budget (b114 test 1) [M + arithmetic]

Even split perfectly between the main CPU and the Media Engine, the work has to fit 2,000 ms of CPU a second. At the minimum target (15 fps, 30 ticks a second), the work a second is 30 × tick + 15 × drawing:

| | Ticks | Drawing | Total | Against two CPUs |
|---|---|---|---|---|
| Cutscene | 1,815 | 1,328 | 3,143 ms | 1.6× over |
| Flight | 2,076 | 2,340 | 4,416 ms | 2.2× over |
| Landing | 1,374 | 1,122 | 2,496 ms | 1.25× over |
| Beach | 1,056 | 1,259 | 2,315 ms | 1.16× over |
| Bunker | 393 | 668 | 1,061 ms | fits (0.53×) |

- **The split can't be perfect, because only some work can move.** On the beach, moving both the AI and the GE layer would put 990 ms a second on the ME, just under its whole second. The main CPU would keep 1,330 ms a second, which is still a third too much.
- **So the moves and the cuts have to come together.** In actor-heavy scenes, the AI and the GE layer compete for the one Media Engine.

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

**`gedefer` on (lab run 38059883490, same route) [E].** The replay moves the GE layer's work from the drawing phase to the frame end, so compare the two phases together, per frame (2,921 frames off, 2,826 on):

| Drawing + frame end, a frame | `gedefer` off | on | Change |
|---|---:|---:|---:|
| Instruction misses | 125 k | 99 k | −20% |
| Data misses | 73 k | 78 k | +6% |
| Instructions fetched | 13.1 M | 13.7 M | +4% (the recording) |

The tick doesn't move (2.75% → 2.70% data misses). Writing the calls down and carrying them out in runs keeps each half's code in the instruction cache, as step 1 predicted. At ~216 ns a miss and an instruction a cycle, that's about −5.5 ms of misses against +2.7 ms of recording and data misses: **roughly −3 ms a frame on the main CPU, before the ME takes anything** [E]. Only the PSP gives the real figure: a `gedefer` A/B in the ceiling bench's flight. **The PSP's answer (b112 runs 1 and 2, 2026-10-10) [M]: no change**, 260.0 against 260.6 ms a frame by the clock over the flight (`b112_candidates.md`, results). The instruction-cache saving the model predicts doesn't show on the PSP, or the recording's cost (about 2 ms a frame of the GE layer's CPU) eats it. `gedefer` is neutral on its own; its value is as the step before the ME.

The objects pool is 20% of the tick's data misses, the object spheres 3.7%, the collision BSP walks (`collision_bsp_test_vector_recursive`, `bsp3d_test_sphere_recursive`, `bsp2d_test_sphere_recursive`) 15% together. Drawing's: `model_geometry` 8.8%, `render_model` 5.5%, the geometry cache's lookup `resident_entry` 4.1% (its `entries` table: E6).

## 8. Tests running and next

- **Emulator ceiling bench with the GE layer switch** (b112).
- **Cache-model A/B, `gedefer` off and on** (lab `cache=1`: the emulator's cache models on the interpreter, each phase's misses in `cache.txt`).
- **For Sheepdog01's PSP:** the ceiling bench on b30, and benchrun on Blood Gulch for switch 8 (L2). Steps in the build notes. Done (§3), and Test 3 (`gemeon`, §4 "Step 4a on the PSP").

**Update 2026-10-11, build 114 (`build_b114.md`):**
- **L1:** step 4's overlap (the ME building the lists while the main CPU runs on) is still to build; it needs a day with the PSP at hand. b114's PSP test 5 runs `gemeon` with b113's step-4b fixes.
- **Smaller drawing levers built as switches** for the PSP's A/B: `listcachedall` (X18), `statememo` (X6), `lean` (E2: −8 to −12% of the frame in the emulator's bench), `drawdistN` (B9: small on b30).
- **L3's blocker (#25):** the first difference between b114 and b114 with `gedefer` is a standing Elite's aim at tick 1,320, and the object mark counter differs from tick 1,204: the engine's render-side work runs differently when the GE calls are deferred. Not streaming, not the VFPU.
- **The lab runs up to ten emulators at once** (`docs/testing_pipeline.md`): about five times the night's emulator work.
