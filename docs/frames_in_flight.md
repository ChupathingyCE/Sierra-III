# Frames in flight: the display list built on the ME one frame behind (design, 2026-10-16)

**Status (2026-10-17):** F0 and F1 built on branch `inflight` (`015c964`, keep word `geinflight` with `gedefer`; F1 adds `gemeon`), off by default. Play exact against the controls, no system call in the ME's work [E]. On the PSP-3000 (b30's b113 route, twice) it runs clean but **loses**: the main CPU's drawing −23 to −53%, the frame +3 to +25% [M]. The ME is the limit: the frame's period is the ME's frame time plus the main CPU's recording, and the ME takes ~2–3× the main CPU's time for the same GE work, against the 1.25× assumed in the estimate below [U]. **Corrected since [M]:** on its own the ME runs the GE layer's records at 0.45–0.92× the main CPU's time (`mekinds`, PSP-3000 b30 and PSP-1000 c20); the 2–3× was the overlap and the shared bus, not the ME's speed (the lab's `levers.tsv`, L5). Next, in order: the ME's time by record kind; recording frame N while the ME draws N−1 (two record buffers); splitting the frame (skinned models on the main CPU). Details: the lab's handoff of 2026-10-17 (recon). Recon 134 sized it (`recon_134.md`, on branch `recon134`). It continues `big_levers.md` §4 (L1, step 4) and is the first half of L5, the full split between the tick and drawing.

**Who builds it:** the GE layer (`psp_ge.c`) is the renderer session's, and builds are the master session's. This doc goes to them through Sheepdog01.

Labels: **[M]** measured on a PSP · **[E]** emulator · **[D]** read in the code · **[U]** inference.

## 1. The idea

Today each frame runs as a line on the main CPU: two ticks, then the engine's drawing walk, then the GE layer turning its calls into the display list. The GE draws behind the list as it fills.

In flight, the main CPU stops before the GE layer. It writes the GE layer's calls down (the recorder, `gedefer`, already does this) and goes straight on to the next frame's ticks. The Media Engine (ME) carries out the written-down frame meanwhile, and the GE draws behind the ME:

```
today       main  | tick | tick | engine walk + GE layer (N) | tick | tick | ...
            GE                   |  draw N, behind the list   |

in flight   main  | tick | tick | sync | engine walk (N+1), recorded | hand over | tick | tick | sync | ...
            ME    |  GE layer (N) ...  |            idle            |  GE layer (N+1) ...
            GE      |  draw N, behind the ME  |                       |  draw N+1 ...
```

The main CPU's frame becomes two ticks plus the engine's walk. The GE layer's 17–72 ms a frame on b30 leaves it [M]. The frame is then the longest of three lanes (main CPU, ME, GE), not their sum.

## 2. What it's worth

### Why L1 found no gain, and why this is different

L1's overlap (`gemeoverlap`, b118) was **3–6% slower** on the PSP-3000 (lab 38359689577), for two reasons:
- the ME carried out a frame's halves while the main CPU waited for the frame to end, so the GE started late (the frame end waited 35 ms against 0.8 ms);
- the ME built lists ~25% slower than the main CPU [M].

In flight, neither costs the main CPU anything. The ME's work and the GE's lateness both happen during the next frame's ticks, which the main CPU spends ticking.

### Each b30 stretch, recomputed

The figures are ms a frame. Sheepdog01's PSP-1000 on b132 (lab 38822408622) [M]: the frame, and the GE layer's own CPU time from its entry-point timers (the log's "renderer CPU us a frame").

The new lanes [U]:
- **main CPU:** the frame less the GE layer, slowed by 5–12% (mebus's range when the ME loads main RAM beside it);
- **ME:** the GE layer × 1.25 (L1's rate), plus audio at ~14% of the frame (10–18% busy, b114);
- **GE:** unchanged.

| | Cutscene | Pelican ride | Landing | Beach | Bunker |
|---|---:|---:|---:|---:|---:|
| Frame today [M] | 167 | 260 | 210 | 145 | 93 |
| GE layer on the main CPU [M] | 33 | 60 | 72 | 30 | 17 |
| Main CPU in flight [U] | 141–150 | 210–224 | 145–155 | 121–129 | 80–85 |
| ME in flight [U] | ~61 | ~105 | ~111 | ~54 | ~33 |
| GE busy [M] | 33 | 75 | 81 | 32 | 27 |
| **Frame in flight [U]** | **141–150** | **210–224** | **145–155** | **121–129** | **80–85** |
| Change | −10 to −16% | −14 to −19% | −26 to −31% | −11 to −17% | −9 to −14% |

**This corrects the lever table's −25% for b30.** That figure came from a coarser split of the drawing time. The GE layer's own timers give −14 to −19% on the ride and −9 to −31% across b30. It is still the largest exact cut on the list: making sleepers wake on writes (WT), the next-largest exact lever, is worth 3–5% of a b30 frame.

**What it doesn't count** [U]: the main CPU's instruction cache without the GE layer's code in it. The engine's walk and the ticks may run faster for it.

**At the target:** the main CPU's lane is all that counts while the ticks are slow. Once ticks shrink, the ME's lane can become the limit: 66.7 ms holds a GE layer of ~45 ms with audio, and the ride's is 60, the landing's 72. Two things lower it:
- cheaper draws (the renderer session's precompiled lists);
- streaming within the frame (phase F3).

## 3. The design

### 3.1 One sync point a frame

Before it records frame N+1's first call, the main CPU waits until:
1. the ME is done with frame N (a spin on its done word, as the ring does: no sleeps, X3);
2. the GE is done with frame N (`sceGuSync`, as the frame end does today);

then it shows N (the swap) and services N's wanted lists (texture loads, level copies, model conversions).

The wait costs nothing whenever the ME's lane fits inside two ticks, which it does on every b30 stretch above. This one point keeps everything else simple:
- **One display list.** Each frame's list starts only after the GE has finished the last one, so the list needs no ring and no second 768 KB.
- **One copy of the GE layer's state.** Only the ME writes it between hand-over and sync; only the main CPU writes it between sync and hand-over.
- **The frame's dynamic triangle lists stay as they are.** The engine's walk writes them while the ME is idle.

**Where it goes:** at the first call of the next frame, not at window begin. Something is recorded before window begin every frame: on b30 the recorder ran its waiting calls there 300 times in 300 frames (recon134a) [E]. F0 lists which calls those are.

### 3.2 The frame's boundaries

Recon134a found three calls that make the recorder run what it holds early [E]. Each becomes a recorded call or moves to the sync:

| Call | What it does now | In flight |
|---|---|---|
| `psp_ge_window_begin` | Starts the list, chooses the near plane (`near_plane_auto` reads the level), sets fog, clears, resets the VRAM models after a map change | **Split.** The main CPU makes the near-plane decision and the VRAM reset when the call is recorded, and records the result. The ME carries out the rest (`sceGuStart`, fog, clear) |
| `psp_ge_frame_end` | Flushes the transparent queue, resolves TAA, makes the VRAM model copies, writes back, `sceGuFinish`, waits for the GE, logs, screenshot and overlay, swap | **Split.** The ME does the list's end: the transparent flush, `taa_resolve`, `ge_kinds_frame_end`, `sceGuFinish`. The main CPU does the rest at the next sync (the ME can't write VRAM, so `vm_frame_end` stays on the main CPU) |
| `psp_ge_text_changed` | The game writes new glyphs into the font bitmap that waiting text calls will sample | **Waits** for the frame in flight to finish before the main CPU writes. It's rare: zero drains on b30's opening [E] |

Two more cases make the recorder run what it holds early: a model with more than 64 nodes (none on b30 [E]) and a record too large for the buffer. Both keep today's behaviour: the main CPU waits for the ME to finish the frame in flight, then runs the call itself.

### 3.3 What the records point to

The recorder copies what each call's arrays hold. A few kinds of data it keeps only as pointers, since the data is only rewritten once the frame ends (the comment above `DEFER_WORDS`) [D]. In flight, the next frame's ticks now run while the ME reads them:

| Kept as a pointer | Rewritten | In flight |
|---|---|---|
| The level's vertices | Never, while the map is loaded | Safe |
| Geometry cache blocks | When evicted, after `RECENT_FRAMES` (2) frames without use | Check: the frame in flight may need a third (`psp_geo.c`) |
| The level's copies (the arena) | Let go for room at the frame end | Safe: room is made only at the sync |
| The frame's dynamic triangle lists | By the engine's walk | Safe: the walk runs after the sync |
| **Decal vertices** | **In the tick** | **Hazard: copy them into the record** (words to be sized in F0) |
| Textures | Evicted after 60 frames unused (`psp_tex.c`) | Safe |

The list isn't proven complete. F0's test finds any pointer this table misses.

### 3.4 The record buffer and memory

- **A frame on b30 is ~1,780 calls, ~100 KB** (recon134a). With today's 64 KB buffer, the recorder ran early ~1.5 times a frame because the buffer filled [E].
- **One 128 KB buffer** replaces today's 64 KB one: **+64 KB**. Two buffers aren't needed, because the sync point keeps the main CPU from recording while the ME reads (§3.1).
- **A frame that fills it** runs as a full buffer does today: the main CPU waits for the frame in flight, hands over what it holds, waits for it, and records on. That is exact, only slower for that frame. The log counts these frames.
- **Room is the hard part.** Free user RAM was 138 KB at worst on b132's b30 [M].
  - The candidate source: the display list's headroom. Its peak is 624 KB of 768 KB on b30, and 298–441 KB on the other five recon maps (recon134a) [E].
  - Shrinking the list needs an overflow check first, since it has none (build 19).
  - The master session decides where the 64 KB comes from.

### 3.5 The ME's time

The render job now lasts a whole frame (60–90 ms on the ride), so nothing else waiting for the ME may queue behind it:
- **Audio** is already mixed inside the GE layer's replay every few records (`psp_me_serve_audio`, b113). Audio comes first on the ME (Sheepdog01, 2026-10-14).
- **The ring's jobs** (perception's rays, the slant measure) are queued by the next frame's ticks while the render job runs. Waiting behind it would stall the tick. The replay must run a waiting ring job between records, as it runs audio. Each job's inputs are the tick's state, which the replay never touches, so this is exact [U]: check it with the ring's own write rules (`media_engine.md`). The fallback: `meraysoff`, with the rays on the main CPU, while it's built.
- **The render job's timeout** needs a progress word, not a fixed limit (b112 Test 3: a 40 ms limit stopped the ME [M]). b113's step 4b may already have built one; check before building.
- **Later, AI on the ME (L3)** competes for the same time. The ME's lanes above leave 20–110 ms a frame free on b30 today [U]. At the target there is little left, so the two must be sized together.

### 3.6 Keeping the two caches straight

`gemeoverlap` calls `sceKernelDcacheWritebackInvalidateAll` after the ME has written. That throws away the main CPU's whole working set, and may be part of why L1 lost (`research_application.md` W5).

In flight:
- **At hand-over,** the main CPU writes back its whole data cache. That is cheap, and its lines stay valid.
- **The main CPU reads nothing the ME writes,** except a small mailbox on lines of its own: the wanted lists, the log counts, the stats and the stall address. At the sync it drops those lines by range, not the whole cache.
- **The ME drops its own cache as each job starts** (it does already) and writes the list's lines back before passing a stall address on.
- **Checking it:** the `PSP_MAIN_SIDE` marks and the emulator's write trace (`mewrites`) already list every line both CPUs write.

### 3.7 Showing the frame, and input lag (Sheepdog01's call)

Phase F1 swaps at the sync point, so frame N appears after frame N+1's ticks: **about one frame more of input lag**. Compared with today:
- at 15 fps, +50–67 ms;
- on today's ride, ~+200 ms.

Phase F2 swaps as soon as the GE finishes, through a small display thread on the main CPU woken by the GE's finish signal. That cuts the extra lag to the ME's and GE's time (~35–110 ms on b30 today [U]).

The lag is felt in play, so it counts as a change Sheepdog01 judges. The feature ships off by default behind its keep word, and the A/B on the PSP-1000 is how he decides.

## 4. Build and test order

Keep word `geinflight` (with `gedefer`). Off, nothing changes.

1. **F0, the order alone (built, branch `inflight`):** `geinflight` writes the frame's end and the next frame's first window down too, so the main CPU carries frame N out at the next frame's first call, after N+1's ticks: the order the ME version will run in, with no work moved. Both calls are carried out whole here (their splits are F1's), the decal vertices are copied into the record, and the buffer is 128 KB. The log's `geinflight` line gives the KB a frame written down (average and fullest).
   - Lab batch `inflight135a`: `gedefer` against `gedefer geinflight` on eight maps, with `fixedticks fixedseed statecrc framecrc` (`framecrc` hashes each finished picture; `tools/framecmp.py` and `tools/statecmp.py`, on the renderer branch, compare two runs), and b30 plain twice as the control.
   - Passes when every picture and every tick's state is the same. A difference is something the next frame's ticks changed under a waiting frame: a pointer §3.3 missed.
   - The ME's own checks, `gemesim` (system calls) and `mewrites` (lines both CPUs write), come with F1, once the work moves.
2. **F1, the ME for real (built, branch `inflight`):** `gedefer gemeon geinflight`.
   - **The splits:** the frame's end and its window's begin are each two parts. The list's part (the last draws and the finish; the camera and the GE's state) is written down and carried out with the frame. The main CPU's part runs at the main CPU's turn: the window's (the near plane's choice, the level's origin, the list's start, a system call) when it is written down, the end's (the GE waited for, the picture shown, the frame's reads and logs) once the GE has the whole frame.
   - **The hand-over:** as the frame's end is written down, the main CPU writes its cache back and hands the whole buffer to the ME's render slot, and goes on to the next frame's ticks.
   - **The GE behind the ME:** on the ME, each stall address is written back and published uncached; a main-CPU thread above the game's looks every millisecond while the frame is out and passes it on.
   - **The wait:** at the next frame's first call (or a call that runs at once), the main CPU waits for the job, passes on the last stall address, drops its whole cache once (what the ME wrote read fresh), and runs the end's main part, the swap included. If the ME failed, the list is finished where the main CPU last saw it and the main CPU carries the frames out from there.
   - **Ring jobs inside the frame's job:** every 32 records the frame's job mixes a waiting audio block and runs the ring's waiting jobs (perception's rays from the next ticks), so neither waits behind a whole frame. Whether the rays are better on the ME or on the main CPU (`meraysoff`) under geinflight is for the PSP to say.
   - Checks: in the emulator, `gemesim` (the job simulated at the hand-over) with `mewrites` (lines both CPUs write while the frame is out) and `mestrict` (system calls in its work); on the PSP, `sierra-hw` `keepsets="- | gedefer gemeon geinflight" repeat=2`, b30's route, read with `tools/abreport.py`. Then d20 and a30.
3. **F2: less lag.** The display thread swaps at the GE's finish.
4. **F3: the ME starts sooner.** The ME starts on the frame's first half while the main CPU still records (gemeoverlap's halves, with the sync rule kept). Only once the ME's lane is the limit.

Multiplayer: the GE layer compiles into the multiplayer build too (the lab's `mpcheck` batch). The simulation is untouched, so play against ChupathingyCE's servers doesn't change.

## 5. Open questions

- **What is recorded before window begin each frame** (F0 lists it). This decides where the sync point goes.
- **The decal records' size** on decal-heavy stretches (b30's beach, a30).
- **Whether geometry blocks need `RECENT_FRAMES` 3.**
- **Where the 64 KB comes from** (the master session).
- **The extra frame of input lag** (Sheepdog01, after F1's A/B).
