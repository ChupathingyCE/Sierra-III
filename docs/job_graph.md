# What depends on what: the job graph (ledger 6)

Each job of a campaign frame on each unit (the main CPU, the Media Engine, the GE, the DMA engines): what it reads and writes, its deadline, whom it waits on and who waits on it, where the hand-over happens, and the measured waits. Ledger 6 of `redesign_recon.md`; it is the graph a placement or an overlap must respect. Read it beside `frame_timeline.md` (ledger 3: how long each unit is busy and waiting), `exact_state.md` (ledger 7: which of this state the multiplayer host reads) and `capacity_model.md` (ledger 2: each job's cost).

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code, [U] inference.

**Sources.** The code at master/tock162 19c02bbc (the timeline's build, 64647bb5, is its ancestor; tock162 adds `aimeaudiolook`); the default keep words from `psp_platform.c`'s `keep_defaults` and `keep_levers` (§0). Four read sweeps (lab or-job 41962249052 the ME's jobs, 41962277130 the drawing's path, 41962304174 the frame's order, 41962282643 audio). Their citation checks weren't clean (lines moved, some quotes misread, one default misread), so every hand-over, collection, bound and default in the tables below was re-read in the code at 19c02bbc [D]; line numbers are at that commit. Measured waits: `frame_timeline.md`'s runs (lab sierra-hw 41941407089 b30, 41942245562 c20, 41941380156 d20, 41942251606 b40) and B60's pair (41960614644).

## 0. What runs by default

On in a default campaign build (`keep_defaults`, `keep_levers`, and each job's own gate) [D]:
- **The drawing recorded and replayed on the ME, one frame in flight:** `gedefer`, `geinflight` (with `inflightkb224`: a 224 KB record buffer), `gemeon`, `mechunk`, `geshow3`, `mepoll` (the forwarding thread looks every 4 ms, `gefwd`), `flipwait`, `mememo`, `geoindexme`.
- **The ME's other jobs:** audio (unless `nomeaudio`, `psp_dsound.c`), the AI job `aime2a`, poses ahead (on the ME unless `poseaheadoff`: `psp_fixups.py` 22347-22350), points (unless `nomepoints`) with `mepointsclaim`, perception rays (`psp_me_rays_wanted`: on unless `meraysoff`), visibility made on the ME (`mevisland`, built unless `PSP_MEVISLAND=0`), the sound tier in the eDRAM (`sndedram`, `sndtierback`), and the looks inside long jobs (`lightringlook`, `aimeringlook`, `aimepathlook`, `aimeobslook`, `aimeaudiolook`).
- **Not in a default build or run:** music (`PSP_MUSIC=1`), `mevis` (the measurement; `PSP_WALKAPART`), `soundedram` (the whole sound cache in eDRAM), `sndtierdma`, `geshow2` alone, `gemeoverlap`; the measurement jobs (`ME_JOB_EDRAM`, `ME_JOB_VME`, `ME_JOB_RINGTEST`, `ME_JOB_BUSLOAD`).

## 1. The frame's order on the main CPU

The phases are `psp_phase`'s marks (`psp_fixups.py` 1154-1180, `psp_platform.c` 3500), which `frame_timeline.md`'s tick, drawing, other and frame end rows count [D].

| # | Step (engine function) | Phase | ME jobs handed over (→) or collected (←) here |
|---|---|---|---|
| 1 | The loop's rest: input, `player_control_update`, `game_engine_update_non_deterministic` (`main.c`'s `main_loop`) | other | — |
| 2 | **Each tick** (`game_time_update` → `game_tick`, 1-2 a frame on the heavy maps) | tick | |
| 2a | `game_tick`'s first step | tick | ← **the AI job** of the frame before (`psp_aime2a_tick_begin`, `psp_fixups.py` 42231, anchor `game.c#aime2a`) |
| 2b | `ai_update` → `actors_update` | tick | → **perception rays** at its start (`psp_perception_queue_done` → `psp_merays_hand_over`, 18577); ← each job when the first actor asks for one of its rays (`psp_merays_collect`, 18612), the rest at `psp_perception_end` (`psp_merays_settle`) |
| 2c | `objects_update` (posing, node matrices, the objects' updates) | tick | ← this tick's **poses** (`psp_pose_ahead_take`/`_collect`, 21436); → next tick's poses at its end (`psp_pose_ahead_make`, 21610, anchor `objects.c#poseahead`) |
| 3 | After the frame's last tick | other | → **the AI job** (`psp_aime2a_frame_end`, 42184, anchor `game_time.c#aime2a`, before `game_frame`) |
| 4 | `game_frame`: `particles_update`, `contrails_update`, `particle_systems_update`, `game_sound_update`, ... | other | → **points** in `particles_update` before its loop (`psp_points_hand_over`, 31685); ← at `contrails_update`'s end (`psp_points_end`, 31973) |
| 5 | **Drawing** (`main_game_render` → `render_frame` → `render_window`), the GE's calls written into the record | drawing | ← **the replay** of the frame before, at the first GE call (`psp_ge_window_begin` → `inflight_sync` → `defer_wait_me` → `psp_me_finish`, `psp_ge.c` 6395, 1863); under `mechunk`, → the frame's records in pieces as they are written (`chunk_publish`) |
| 5a | `render_window`: `structure_visibility_compute` | drawing | → **visibility** (`psp_mevisland_push`, 47846, anchor `render.c#mevisland`) |
| 5b | `render_sky`, `first_person_weapon_render_update` | drawing | (visibility runs on the ME meanwhile) |
| 5c | `lights_preprocess_scene` | drawing | ← **visibility** (`psp_mevisland_collect`, 47906) |
| 5d | `render_objects` and the rest of the record | drawing | the AI job is out, and yields to the ring between actors |
| 6 | **Frame end** (`psp_ge_frame_end`, `psp_ge.c` 24806) | frame end | → **the replay** (`DEFER_FRAME_END` recorded, `inflight_hand_over` → `psp_me_submit(ME_SLOT_RENDER, ME_JOB_GE)`, 2127; under `mechunk` the final mark); the show of the frame before (geshow3: `show3_wait_finish`) and the swap |

So a frame is a pipeline [D]: frame N's ticks run while the ME replays frame N-1 and the GE draws it; N's record waits for that replay at its first GE call; N's AI pass runs on the ME through N's `game_frame`, record, frame end and N+1's loop rest, and is collected at N+1's first tick. The order is the design's (`psp_ge.c` 344-351: "the ME carrying out frame N while the main CPU ticks N+1").

## 2. The jobs

| Job (unit) | Handed over (function, file) | Collected or waited for | Reads | Writes | Deadline, bound and fallback | Waits on | Waited on by | Measured [M] | |
|---|---|---|---|---|---|---|---|---|---|
| **The ticks** (main CPU) | — | — | the game state; the AI job's actors once collected; poses ahead; the rays' answers | the game state (units, objects, actors, effects, node matrices) | none of their own: the clock must never run fast (`exact_state.md`) | the AI job (2a), each ray job, the poses job | the replay's next collection is after them; the AI job's hand-over is after the last | 68-87 ms a frame b30, 35.5 c20, 65.0 / 42.2 d20, 11.0 b40; on ring jobs 0.03-0.41 ms a frame | [D] [M] |
| **`game_frame`** (main CPU) | — | — | particles, contrails, the sound manager's state | particles, contrails; the mixer's command ring (`mix_command`, `psp_dsound.c` 3459) | none | the points job (≤ 10 ms, or none under `mepointsclaim`) | — | in "other": 2.6-9.2 ms a frame | [D] [M] |
| **The drawing's record** (main CPU) | — | — | the frame's game state: node matrices (up to 64 a model copied into the record, `defer_nodes`), decals (copied), level vertex and index arrays (by pointer) | the record buffer (`defer_buffer`, 224 KB by default) | a record over 16 KB runs as it comes; a full buffer drains on the main CPU | **the previous replay** (`inflight_sync`, 500 ms bound); visibility (50 ms); slant parts (10 ms) | the replay (its input) | working 13.4-42.9 ms a frame; asleep 0.1-3.5, **17.1 d20 flashlight** | [D] [M] |
| **The frame end** (main CPU) | — | — | the record's end | the hand-over; the swap's pointers | geshow3's wait for the GE's finish: 200 × 2 ms, then shown without it (`psp_ge.c` 6012-6025) | the GE's finish of the frame before (geshow3) | — | 0.1-0.2 ms; frameend's wait for the GE 0-2.0 ms a frame | [D] [M] |
| **The replay** (ME, `ME_SLOT_RENDER`, `ME_JOB_GE`) | `inflight_hand_over` → `psp_me_submit`, `psp_ge.c` 2127, at the frame end (whole cache written back first, 2105); `mechunk`: pieces during the drawing | `defer_wait_me` → `psp_me_finish`, `psp_ge.c` 1863, at the next frame's first GE call | the record and everything it points at (the engine's arrays, pspgu's state, textures) | the display list (768 KB, one allocation; geshow3 puts the next frame in the part the GE isn't reading), pspgu's and `psp_ge.c`'s state, the newest stall address (`INFLIGHT_STALL`, uncached) | **500 ms** (`psp_me.c` 2620) → the ME stopped, the main CPU finishes the frame; must end inside the next frame's ticks and rest or the record waits | the record's hand-over; audio served at its yields (every record in flight); the ring served in the light pass (`lightringlook`) | the next record; the GE (its chunks) | 17.3-76.5 ms a frame; longest job 9.5-55.7 ms | [D] [M] |
| **Audio** (ME, `ME_SLOT_AUDIO`, `ME_JOB_AUDIO`; the audio thread) | the audio thread (priority 0x12, above the game's): `psp_meaudio_run` (`psp_dsound.c` 2868) → `psp_me_run`, `psp_me.c` 4513, blocking | in `me_run_slot`'s look loop (first look 0.2 ms) | the voice list (uncached), the voices' packets (main RAM or the sound tier in eDRAM) | the 512-frame output block, the voices' state | **a 23.2 ms block** (512 frames at 22,050 Hz, `MIX_FRAMES`); **40 ms** (`psp_me.c` 2467) → the ME stopped for the session and the mix made on the main CPU (`psp_dsound_mix_list`, 2929) | the ME being free: served first in the ME's pass, at every yield of a long job (`me_audio_now`), and at the AI job's looks once a block has waited ~4 ms (`aimeaudiolook`, `me_audio_due`, `psp_me.c` 886) | `sceAudioSRCOutputBlocking` (the next block) | 4.7-20.2 ms a frame; worst wait c10 placed 8.6 (10.5-11.4 the run's worst) on, 27.7 (32.6-34.2) with `aimeaudiolookoff` | [D] [M] |
| **The AI job** (ME, `ME_SLOT_AI`, `aime2a`) | `psp_aime2a_frame_end`, `psp_fixups.py` 42219, after the last tick, before `game_frame` (whole cache written back) | `psp_aime2a_tick_begin`, 42273, at the next `game_tick`'s start: sets the abort word and waits | actors, props, encounters, the collision log, the world (`psp_aime_think`: the pass after the frame's last tick) | the actors it thinks for (written out a line at a time, `psp_me_lines_out`); its marks in tables of its own (`PSP_AIME_ME_SIDE`) | **20 ms** after the abort word (`PSP_AIME2A_BOUND_US`, 42927) → the ME stopped; then every later pass on the main CPU | the ME between other jobs; it yields between actors (the word, audio, the ring, a waiting replay) and at ~1 ms looks inside an actor | the next tick | 2.5-21.4 ms a frame **by difference [U]**; its own time and wait never print (the ME's report line is cut at 512 bytes) | [D] [U] |
| **Poses ahead** (ME ring, `ME_JOB_POSES`) | `psp_pose_ahead_make` → `psp_me_push_poses`, `psp_fixups.py` 21610, at `objects_update`'s end | `psp_pose_ahead_collect` → `psp_me_wait`, 21436, at the next tick's posing (held: also before the ring reuses the entry, or the animation cache moves a page) | the models' and animations' tags (resident pages only) | the pose arena only | **10 ms** (`RING_DEADLINE_US`, `psp_me.c` 2712) → the poses decoded on the main CPU | the ring's turn (after audio; inside long jobs at their looks) | the next tick's posing | in "ring jobs" 0.7-3.9 ms a frame; main CPU's ring waits ≤ 0.41 | [D] [M] |
| **Points** (ME ring, `ME_JOB_POINTS`) | `psp_points_hand_over` → `psp_me_push_points`, 31685, in `particles_update` | `psp_points_end`, 31973, at `contrails_update`'s end; an unbegun job isn't waited for (`mepointsclaim`) | particles' and contrails' points, the BSP, water, wind, the breakable surfaces (inputs compared bit for bit before an answer is used) | the answers; its own word (uncached) | 10 ms → the main CPU's loop has made every point already | the ring's turn | `contrails_update`'s end | as above | [D] [M] |
| **Perception rays** (ME ring, `ME_JOB_PERC_RAYS`) | `psp_merays_hand_over` → `psp_me_push_perc_rays`, 18577, at `actors_update`'s start | `psp_merays_collect` (18612) at the first ray asked; `psp_merays_settle` at `psp_perception_end` | the level and objects anywhere in RAM | the answers only | 10 ms → the job's rays cast on the main CPU; five object writers wait for the rays first (`psp_object_epoch`) | the ring's turn | `actors_update`'s line-of-sight tests | as above | [D] [M] |
| **Visibility** (ME ring, `ME_JOB_VISIBILITY`, `mevisland`) | `psp_mevisland_push`, `psp_fixups.py` 47846, in place of `structure_visibility_compute` | `psp_mevisland_collect`, 47906, before `lights_preprocess_scene`: polls `psp_me_ring_done` | the BSP, the objects, the view | its state block (visible clusters, surfaces, objects), copied back by the main CPU | **50 ms** (`PSP_ML_LATE_US`) → the main CPU makes its own; a cut walk is thrown away the same way | the ring's turn | `lights_preprocess_scene` | in the drawing; not split out | [D] |
| **Slant** (ME ring, `ME_JOB_SLANT`) | `slant_hand_over`, `psp_ge.c` 9963: the back part of a draw's angle sort | `slant_collect` → `psp_me_wait`, 10052 | the draw's vertices and indices | its bucket bytes | 10 ms → the part sorted on the main CPU | the ring's turn | that draw | **not on the ME's own replay** (`PSP_ME_WORK()` returns first): only draws made on the main CPU | [D] |
| **Sound tier copies** (ME, `ME_JOB_SOUNDCOPY` on `ME_SLOT_PROBE`) | `psp_sound_upload_poll`, `psp_dsound.c` 2372, from the sound cache's idle (staging written back first) | polled in the same function (2114: "never waited for") | 60 KB pieces staged in main RAM by the stick reader | the sound tier in the eDRAM (`ME_EDRAM_SNDTIER`, 656 KB) | 250 ms once begun, 2 s to begin (`TIER_JOB_LATE_US`, `_WAIT_US`) → the pieces given up and re-read; three failures close the tier | the ME being free: a plain slot job, it doesn't yield, **and the ring refuses pushes while it is out** (`psp_me.c` 3350) | nothing waits | not separated | [D] |
| **The GE** | the forwarding thread ("ge forward", priority 0x10) passes each new stall address (`ge_stall_pass`, `psp_ge.c` 2024) every 4 ms (`mepoll`); the collection passes the last one (1888-1893) | the frame end's finish (geshow3) | the display list, vertex arrays, textures, the frame buffers (eDRAM) | the colour (two, 272 KB each) and depth buffers | none of its own; at most two frames out with it (`psp_ge.c` 514) | **the ME's next chunk** (it stalls inside an unfinished list) | geshow3's show of the frame before | busy 16.7-83.5 ms a frame; stalled on the ME 0.5-4.9, **47.2 d20 flashlight** | [D] [M] |
| **The show** (forwarding thread) | geshow3: the display set once the GE reaches the frame's finish (`show3_finish_callback`, `psp_display_release`) | — | — | the display's buffer | `flipwait`: the set waits for the vertical blank | the GE's finish | the next frame's drawing into the freed buffer | b40: 5.0 ms a frame asleep in "other" (the display [U]) | [D] [U] |
| **Texture DMA** (DMAC) | `psp_tex.c` 1957-1963: staging (32 KB) written back, `sceDmacMemcpy` into the texture arena | synchronous | the stick's texture pack via staging | the texture arena | on failure `memcpy` through the uncached view | the stick read | the draw binding the texture (`mechunk` stops publishing at a texture not marked this frame) | not separated | [D] |

The only DMA engine use in a default frame is the texture staging copy [D]; every other hand-over between units is a cache write-back (`sceKernelDcacheWritebackAll` before a slot job, the ring's `psp_me_writeback`, the ME's `meLibDcacheWritebackInvalidateAll`) or the GE reading what the CPU wrote back. `sndtierdma` (the sound tier by DMACplus) is off: it hung the PSP-3000 with the ME busy (`psp_dsound.c` 1479-1481).

## 3. How the ME orders its jobs

`meLibOnProcess` (`psp_me.c`) serves the slots in index order, audio (0), render (1), probe (2), AI (3), music (4), and drains the ring right after audio; music runs only when nothing else waits [D]. A slot job runs to its end except where it yields:
- **The replay** serves audio at every record in flight and the ring inside the light pass (`psp_me_ring_look`, at most once every ~1 ms of the ME's clock, `INNER_GAP_CYCLES`).
- **The AI job** yields between actors (the abort word, audio, the ring, a waiting replay, the word again) and looks every ~1 ms inside an actor (`aimeringlook`, `aimepathlook`, `aimeobslook`), mixing a block there once it has waited ~4 ms (`aimeaudiolook`).
- **A ring job** yields only to audio (`psp_me_ring_step`).
- **A sound copy** doesn't yield, and while it is out the ring refuses every push: each ring job's caller then does the work on the main CPU.

So every edge into the ME is bounded: audio 40 ms (then the ME stops), ring jobs 10 ms (then the main CPU does the work), visibility 50 ms, the AI job 20 ms from the abort word, the replay 500 ms, the copies 250 ms [D]. The only unbounded waits on a default path are the GE's own (`sceGuSync` outside geshow3, in pspgu) [D, CAN'T TELL inside pspgu].

## 4. The critical path a frame

From `frame_timeline.md`'s waits [M], with the order above [D]. A frame's length is the longest of three loops: the main CPU's own work, the ME's work between one record's hand-over and the next record's first call, and the GE's drawing behind the ME's chunks.

**b30 (all four stretches): the main CPU alone.** The path is ticks → `game_frame` → record → frame end: on the flight 87.3 + 5.3 + 37.1 + 0.1 = 129.8 of a 132.8 ms frame. The replay (69.8 ms) starts at the frame end and must end before the next record, a window of the next frame's ticks and rest (~92.6 ms) that it shares with audio (17.9 ms a frame, ~12.5 of it in that window [U]) and the ring jobs (3.9): ~6-7 ms of slack [U], and the record waits only 0.3 ms for it. The GE draws inside the ticks (75.6 ms) and stalls 2.6. The waits that matter: none over 2 ms (frameend 1.8 on the flight, 0.8 landing). What would lengthen it: the AI job's ~17-21 ms [U] lands in the same ME window as the next replay only when it overruns the record, which the timeline can't show while its line is cut.

**d20, swamp walk: the main CPU alone.** Tick 65.0 of a 96.3 ms frame; the replay 24.2 ends well inside the next ticks (wait 0.0), the ME is 32 ms a frame idle.

**d20, flashlight on: the ME.** The path is record (26.6) → frame end → **replay 76.5 + audio 14.1 + ring 1.4 on the ME (94.2 of a 92.3 ms frame)** → the next record, which waits **18.1 ms** a frame (drawing asleep 17.1). The GE sits **47.2 ms** a frame inside unfinished lists waiting for the ME's next chunk, so the ME's chunk rate, not the GE, sets the drawing's pace. The main CPU's own loop (tick 42.2 + rest 6.2 + record 26.6 ≈ 75) is ~17 ms shorter than the frame. The longest replay job is 55.7 ms: one frame's replay outlasts the next frame's ticks on its own. Every other wait is under 2.2 ms.

**c20 (for contrast): the main CPU, the ME 2 ms behind.** The replay (39.9) already outlasts the next ticks and rest (~42 ms less audio's 4.7 and the ring's 2.6), so the record waits 2.2 ms a frame: c20 is where the path crosses from the main CPU to the ME next.

**The edges that carry time, by size:**

| Edge | Bound [D] | Measured [M] |
|---|---|---|
| The GE on the ME's next chunk | none (the GE stalls) | 0.5-4.9 ms a frame; **47.2 d20 flashlight** |
| The next record on the replay (`inflight_sync`) | 500 ms | 0.0-0.4 b30 and d20 walk; 2.2 c20; **18.1 d20 flashlight** |
| The frame end on the GE | 400 ms (geshow3) | 0.0-2.0 |
| Audio on the ME | 40 ms, then the ME stops | c10 placed: worst 10.5-11.4 with `aimeaudiolook`, 32.6-34.2 without (B60) |
| The tick on a ring job | 10 ms, then the main CPU | 0.01-0.41 |
| The next tick on the AI job | 20 ms after the word | unmeasured (the report line is cut) |
| Visibility at its first reader | 50 ms | not split out |

## 5. The overlaps the design must respect

Each job that runs beside the main CPU has a window, and the state written in it [D]:
- **The replay across the next frame's ticks:** the ticks rewrite node matrices and decals, so the record copies both (up to 64 nodes a model; a bigger model runs as it comes). The level's vertices and the frame's dynamic triangle lists are kept by pointer, on the claim that they "stay put until the frame ends" (`psp_ge.c` 219-222, written for the record before frames went in flight; the geometry cache's blocks are held for `RECENT_FRAMES`): no guarantee was found that the next frame's ticks don't rewrite a dynamic list before the replay reads it [U, open].
- **The AI job across `game_frame`, the record, the frame end and the next loop's rest** (`player_control_update`, `game_engine_update_non_deterministic`): guarded by the actors written out line by line, the ME's marks kept in tables of their own, a whole-cache write-back before and write-back-invalidate after, and `aime2acheck`'s pool hash. It must be collected before `units_update` and `game_allegiance_update` write what it read.
- **Poses ahead across the drawing and the next tick's start:** they read only tags (resident animation pages; the cache settles the ME before moving one) and write only the arena.
- **Points across `particles_update`'s and `contrails_update`'s own loops:** a two-word handshake moves the line the ME must not cross; inputs compared bit for bit before an answer is used.
- **Rays inside `actors_update`:** the five object writers that change what a ray reads wait for the rays first.
- **Visibility across `render_sky` and the first-person weapon's update:** nothing there writes what visibility reads (`psp_fixups.py` 47800-47805).

None of these writes state the multiplayer host checks except, through the hit reports, the poses ahead (`exact_state.md` §3) [D].

## 6. Gaps

1. **The AI job's own time and its collection's wait never print**: the ME's report line is cut at 512 bytes (`frame_timeline.md`'s gap 1). Until it's split, the AI job's row and its edge into the next tick are [U].
2. **Visibility's wait at `lights_preprocess_scene`** (`psp_ml_wait_us`) is counted but isn't in the timeline's table.
3. **The level vertex arrays under the frame in flight** (§5): whether the next ticks can move a dynamic list before the replay reads it.
4. **pspgu's `sceGuSync` and the swap's vertical blank** aren't in the tree, so the unbounded GE waits and where b40's 5 ms a frame of sleep sits (the forwarding thread's `flipwait`, or the game's) are [U].
5. **The sound copies' and the texture DMA's time a frame** aren't separated in any instrument.

## Keeping it current

A new ME job, a moved hand-over or collection, a changed bound, or a frame in flight more or less: update §1-3 in the same commit, and §4 when `frame_timeline.md` is re-run.

Started 2026-11-04.
