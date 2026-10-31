# The whole-frame layout plan: every system placed by unit, memory, rate and budget

2026-10-30, hardware session (Sheepdog01's ask, 12:34-12:54: "look at the whole, find a spot for everything"; the brief is the lab's `coord/handoff/layout_plan_2026-10-30.md`). A plan on paper: nothing here is built or run for it. Sheepdog01 reads and rules before any rewrite starts.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Every ms is per frame drawn unless it says "a tick". "15/30" is 15 frames and 30 ticks a second; "30/30" is 30 and 30.

## 0. The short version

The frame has three lanes that matter: the main CPU (ticks, plus whatever drawing it keeps), the ME (the drawing's command building, visibility, sound, rays) and the GE. **Option C's shape, two lanes in parallel (ticks on the main CPU, drawing off it), is the spine, but C's full form (the ME draws everything) can't be built yet, because the ME is already the contested lane.** At w142 the ME carries 18-61 ms a frame on the intros and 70-82 ms on b30's flight [M]. That already fills a 15/30 budget on a10, a30, b30 and d20, before AIME or C's walk asks for any of it. So the critical path is **making the ME's drawing cheap** (prebuilt lists CALLed, not copied; the level as baked lists), behind **a scheduler that keeps the ME's deadlines** (today's music hangs and the AIME fault are failures of the ME's lane, not of placement: a job with no deadline, a runtime CPU test that a stack can fool, and an ME stack found where the plan doesn't put it). Then the room it frees is spent per map and per role: on AIME where the AI is heavy (c20, d20, then b30), on C's walk where drawing binds (c10, a10, a30), and on C's walk alone for a co-op client, which runs no AI. Beside all of that, on the main CPU only, **the tick has to fall 30-40% on b30 and a few ms on c10 and d20** even if every frame's drawing left it. That work is code shape (the big updates split into passes that fit the instruction cache), hot data (the scratchpad, the object datum's hot fields) and posing batched on the VFPU. Collision, tag indices and object slots stay as the host has them in every role. With the compromises left off: a10, a30, a50 and c20's intros are already at or within a few ms of 15/30 [M]; c10 gets there with C's walk on the ME [U]; d20 needs its tick cut a few ms more than the plan sizes [U]; **b30's 15/30 has no sized route yet**: phase 4's sized tick cuts come to a few ms a tick against the -13 to -19 b30 needs, and the largest measured tick lever, the bus toll (the GE reading main RAM while the tick misses, §2), has no lever behind it until phase 0's census of what the GE reads says which bytes could move or shrink; and 30/30 isn't promised on any heavy map (it needs every phase, the ME's drawing ~3x cheaper and a GE cut on a30 and b30).

## 1. The goals as lane budgets

**A frame today runs its ticks, then records its drawing, one after the other on the main CPU** (`rank_pipelined.md` §9 in the lab [M]). The ME replays the records into the GE's lists beside the next frame's ticks (gemeon, geinflight); the GE draws.

| Goal | Main CPU, serial (today's shape) | Main CPU, two lanes (C's shape) | ME | GE |
|---|---|---|---|---|
| 15/30 (66.7 ms a frame, 2 ticks) | 2 ticks + record + rest ≤ 66.7 | 2 ticks + rest + the snapshot's copy ≤ 66.7: **a tick ≤ ~31 ms** | ≤ **50 ms** (75% of the frame) | ≤ ~60 ms (90%) |
| 30/30 (33.3 ms, 1 tick) | 1 tick + record + rest ≤ 33.3 | 1 tick + rest + copy ≤ 33.3: **a tick ≤ ~26 ms** | ≤ **25 ms** | ≤ ~30 ms |

- **The ME's ceiling is 75% busy, not 100%** [U, from two measurements]. With MUSICSTREAM's decode in the audio job, the ME went from 49% to 63% busy and every render job lengthened 35.0 -> 40.8 ms, because audio is served inside them (sierra-hw 41108776634 [M]). Moved to its own lowest-priority slot, it starved near 71% and two PSPs hung (41123795043 PSP-1000, 41133871215 PSP-3000 [M]; the second hang's cause isn't found). The ceiling rises to 85% only once the scheduler (RW1, §9) shows its deadlines holding at that load on a PSP.
- **The snapshot's copy** is 3.5 ms a frame on c10 for 144 KB on the main CPU (snapfieldfast, 40958783351 [M]); b30's would be larger.
- **The GE's 90%** [U]: it's a pipeline fed ahead, but its main-RAM reads slow the tick (§2), so a GE that runs flat out costs the main CPU too.

## 2. Where each map stands, lane by lane

The intros at w142 (PSP-3000, tm-plain 40566969386, strict, two runs agreeing within 0.1-3.4 ms [M]), with b30's flight from the lane trace (40714069887, 40718998656 [M]):

| Map | Frame | Ticks a frame (a tick) | Record | GE busy | ME busy | ME ms | Main CPU, 15/30 serial | Tick vs C's 15/30 (≤ 31) | ME room, 15/30 (≤ 50) | ME room, 30/30 (≤ 25) |
|---|---|---|---|---|---|---|---|---|---|---|
| a10 | 63.0 | 28.0 (14.0) | 30.1 | 27.6 | 68% | 42.8 | 63 ✓ | ✓ | +7 | -18 |
| a30 | 67.1 | 23.3 (11.5) | 38.6 | 45.8 | 69% | 46.3 | 67 ≈ | ✓ | +4 | -21 |
| a50 | 44.0 | 20.8 (16.0, 1.3 a frame) | 18.3 | 24.0 | 42% | 18.5 | 55 at 2 ticks ✓ | ✓ | +31 | +6 |
| b30 intro | 130.4 | 84.2 (44.4) | 43.2 | 61.1 | 47% | 61.3 | 135: **-68** | **-13 a tick** | **-11** | -36 |
| b30 flight | 155-167 | 65-92 | 61-100 | 77-88 | 45-53% | 70-82 | **-90 or more** | **-19 a tick** (flight tick ~50 [M]) | **-20 to -32** | -45 to -57 |
| c10 | 64.7 | 33.5 (29.1, 1.15 a frame) | 24.6 | 26.9 | 34% | 22.0 | 89 at 2 ticks: **-22** | ✓ just | +28 | +3 |
| c20 | 67.3 | 44.8 (22.4) | 16.8 | 19.0 | 41% | 27.6 | 67 ≈ | ✓ | +22 | -3 |
| d20 | 96.8 | 65.8 (32.8) | 22.8 | 20.9 | 48% | 46.5 | 97: **-30** | **-2 a tick** | +3.5 | -21 |

(ME ms = busy share x frame [U from M]. "Rest" is 3.0-8.3 ms a frame and is in the serial column. The heavy stretches are worse than the intros: c20's swarm 116 ms with a ~46 ms tick, d20's fly-through 108 with 74 ms of ticks [M, `hardware_big_moves.md`].)

**What the table says:**
- **Ticks are the gap on b30, d20 and c10** [M]: b149's hand play on the PSP-1000 ran b30 at 15.1 ticks a second, a10 16.9, a30 25.7. On b30 even a frame with no drawing on the main CPU leaves a 44-50 ms tick against the 31 C allows. On d20 the two ticks alone already fill the 15/30 frame.
- **The record is the gap on a10, a30 and c10** [M] (hardware_utilization.md §1). Moving it off the main CPU pays most there.
- **The ME has no room on b30 and almost none on a10, a30 and d20 at 15/30, and none anywhere but a50 and c10 at 30/30.** Every plan that puts more on the ME starts by taking something off it.
- **The GE is over 30/30's budget on a30 and b30** and over 15/30's on b30's flight (77-88 ms [M]). It has room on the rest.
- **The memory wait** is 47-70% of the main CPU's cycles on the intros [M, hwprofile], and the VFPU under 1% [M]. So the tick's levers are code shape and data placement first, maths second.
- **The bus toll on the tick:** with nothing drawn, b30's tick falls 25-34% (34.4 -> 25.8, 31.8 -> 18.6, 25.0 -> 16.8 ms by view, PSP-1000, 40635686041 [M]); c20's 16% (24.6 -> 20.6, 40638429108 [M]). Most of it is the GE reading main RAM while the tick misses (55-94% of the rise, hardware_utilization.md §4 [M]). The ME's own traffic costs the tick only 1.4-6.7% [M].

## 3. Is Option C the spine?

**Yes as the frame's shape; no as "the ME draws everything", and not first.**

Why the shape: under the serial frame, 30/30 needs a tick plus a record under 33 ms, which no heavy map comes near (a10 14 + 30, c10 29 + 25 [M]). Only two lanes in parallel let each lane have the whole frame. c10 needs it even for 15/30: its tick (29 ms) fits C's 31, but two ticks and a record (89 ms) don't fit 66.7.

Why not the full form now:
1. **The ME has no room for the walk.** The objects pass is ~20 ms of b30's record and ~14.5 ms of c10's [M]. Added to today's ME load it would put b30 at 80-100 ms and a10 and a30 near 60, all over 15/30's 50 (§5).
2. **The snapshot needs a reader and copy-on-wake first** (hardware_utilization.md §4, "Read against the code" [D]): nothing reads it yet, and the field list has holes for a walk that runs beside the ticks.
3. **The walk is cheap on the main CPU's VFPU and dearer on the ME.** The node products are 1.3-1.9 ms a frame on b30 there, ~2.5-5 ms on the ME [M x U]. That's a small price against 20 ms moved, so it doesn't decide the placement; the ME's room does.
4. **C keeps the GE's main-RAM reads beside the tick,** so its ceiling on b30 is ~29-40 ticks a second, not 60 (40635686041 [M arithmetic]).

**What C's shape needs first, in order:** the ME scheduled with deadlines that hold (RW1); the ME's per-frame drawing cut (RW2 prebuilt lists, RW3 the level's baked lists); then the walk moved, map by map, where §5's room appears: c10 and c20 first, a10 if RW2 lands at its upper end, a30 and b30 only with a further cut. The model walk stays on the main CPU wherever the ME has no room, and that is a placement, not a fallback (the 10-28 rule). **For a co-op client, C's full form is the natural layout:** its ME has no AI to run, so the room AIME takes in single player goes to the walk.

## 4. The placement table, per role

SP = single player, H = co-op host, Cl = co-op client. "Main" = the main CPU, with its VFPU. "-" = doesn't run in that role.

### The tick

| System | Unit (SP / H / Cl) | Memory | Rate | Why, and what changes [label] |
|---|---|---|---|---|
| Input, player control | Main / Main / Main | main RAM | every tick | the netcode's own word; nothing to move [D] |
| AI thinking (perception, situation, emotion, decision loop, cover search) | **ME** (AIME 2a) / **ME** / - | the job's data and stack in eDRAM (592 KB, 1,456-2,048 KB); actor and prop datums in main RAM | every tick, aicad3 resting half (on by default in w142 [D]); AIFAR off | branchy integer work with its own rays and no reader until the next tick's control steps: the ME's kind of job (`hardware_big_moves.md` #2). Its ME value is **unmeasured** [U]: the c20 pairs never ran 2a (the ME faulted, §9 RW1). b30 only after RW2 frees room |
| AI control (move, look, fire, unit control) | Main / Main / - | main RAM | every tick | the units read it inside the same tick [D] |
| AI rays (line of sight) | ME ring / ME / - | main RAM | every tick | 1-3% of the ME [M]; inline in AIME's job when that lands |
| Level scripts | Main / Main / - | main RAM | every tick | small; the host sends their effects to clients [D] |
| Biped and unit updates | Main / Main / Main (remote players too) | object pool in main RAM; hot fields packed (OBJHOT) | every tick | **split into sub-passes that fit the I-cache** (RW5): biped_update is 299 code lines a call and unit_update 384, against the 256 the cache holds by itself [E, phase0] |
| Biped and vehicle physics, collision sweeps | Main / Main / Main | collision BSP's top in the scratchpad (SCRATCHBSP [M]); tag window in main RAM | every tick | memory-bound walks, one dot a node: placement, not maths (hardware_utilization.md §3). **Exact in every role**: the host checks hit reports against its own collision |
| Skeletons: animation decode, posing, node matrices | Main (VFPU) / Main / Main | node blocks in main RAM | every tick; undrawn ones lazily (VISLAZY, PSC, NMSKIP [D]) | ~21% of b30's tick [M, brief]. **Batched VFPU pass** (RW6): many bones a pass with `vqmul`/`vmmul`, "plays the same", proven by side-by-sides and an outcome census; LOCAL to every role (hardware_utilization.md §6), so it ships to co-op with a `co-op:` note. Not the ME: it has no VFPU and the readers sit inside the same update (NW2 lost, 40843585564 [M]) |
| Projectiles, weapons, items | Main | main RAM | every tick | small [E] |
| Scenery, machines, devices | Main | main RAM | every tick; still ones skipped (WS, PSC [M]) | done |
| Effects, particles, contrails (tick side) | Main / Main / Main | main RAM | every tick; FX15 (pairs them on the two ticks) off, Sheepdog01's A/B | the point physics already on the ME (mepoints [M]); the rest spawns effects on collisions and stays on the main CPU [D] |
| c10's weather | **ME** (WXME) | its own array | every tick | self-contained end to end; c10 4.7-5.1 ms a frame [M]; fits c10's ME room (§5) |
| Lights relink, sleep checksums, damage, garbage collection | Main | main RAM | every tick | small or shared state [D] |
| Sound's game side (scheduling, obstruction rays) | Main | main RAM | every tick | rays ≤ 0.55 ms [E]: under every gate |
| Netcode: sockets | - / Main / Main | buffers allocated at the game's start (§7) | every tick | system calls: the main CPU only (the ME has no kernel [D]) |
| Netcode: packet build and parse, prediction, corrections | - / Main / Main | the same buffers | every tick | small and tied to the tick's state [U]; a host's per-client sends are its largest piece and unmeasured [U] |
| Checkpoints | Main (stage 1 copy) | main RAM; CPA off (the eDRAM went to speed, Sheepdog01 07:38) | at saves | the save hitch is a player-felt cost but out of this plan's lanes |

### The frame (drawing)

| System | Unit (SP / H / Cl) | Memory | Rate | Why [label] |
|---|---|---|---|---|
| Visibility (clusters, objects) | **ME** (mevisland, on) | mevis's state in main RAM; **gathered spheres in eDRAM** (MEVGATHER, ~20-44 KB) | every frame | exact and on by default [M]; the main CPU still waits 2.8-3.8 ms a view at the collect (40845560977 [M]); mevis reads 45-93 KB a call against the ME's 16 KB cache, so a gathered copy should shorten it [U] |
| Lights' preprocess | Main | main RAM | every frame | a plan input to the walk; joins C's walk later |
| Level pass | ME replay today -> **baked per-cluster lists, CALLed** (RW3) | the lists in main RAM (the GE can't read eDRAM) | baked once; per frame only the calls | the level keeps 0.1-70% of its words frame to frame [E pdc1] because its cut-up and index copies are made per frame; baked lists keep it all |
| Model walk (render_model, parts' decisions, node products for drawing) | **Main** (SP, H); **ME** where §5's room allows (C's walk, SNAPC); **ME** (Cl) | the snapshot (192 KB) in the heap, or eDRAM for a client | every frame | §3 |
| Records -> GE lists (the replay) | ME | records in main RAM (in-flight 224 KB) | every frame | **cut by RW2**: a part whose record matches last frame's is CALLed from a retained block, its matrices patched, not rebuilt or copied |
| Skinning | GE (hardware bones) | vertices in VRAM where they fit | every frame | done [M] (~42 ns a weight a vertex) |
| Shadows, see-through queue, decals, HUD | ME replay; HUD/decals as small jobs | main RAM | every frame | ~0.6 ms main [M]; SMALLME parked (0.45% of b30's frame [M]) |
| Particles and contrails render | Main | main RAM | every frame | reach firmware paths [D] |
| Lens flares | Main | GE readback | every frame | needs the GE's answers |
| Window begin (sync, vblank, VRAM preload) | Main | - | every frame | waiting, not work |
| Texture uploads, model buffers into VRAM | DMA / Main | VRAM 2 MB: display 832 KB, textures 616, models 616 [M] | on demand | VRAM is spoken for: choosing what earns it, not finding more |

### Sound and streaming

| System | Unit | Memory | Rate | Why [label] |
|---|---|---|---|---|
| Mixer, ADPCM voices | ME (audio job, hard deadline) | sound cache in main RAM (1.5-1.8 MB) | per audio block | 2.6 ms a block [M]; the VME's best candidate once it computes (§9 RW7) |
| Music decode (MUSICSTREAM, Tremor) | ME, in its own bounded slice (RW1) | Tremor's pool and tables in eDRAM, 1,184-1,456 KB | per block | +2.65 ms a block [M]; off by default until RW1 holds it; **or** music baked to the mixer's own ADPCM (MUSADPCM, a listening A/B), which removes the decoder from the ME |
| Stick reads (sound, geometry, textures, animation pages) | Main (reader threads above the game's priority) | main RAM caches | on demand | the async-priority rule [M] |
| Geometry cache scans | ME | entry table in `.bss` | every frame | MEPLACE (its bytes cut) was null [M] |

## 5. The ME's load per role, summed against its capacity

ME ms a frame: today's load [M, §2], then each placement's ask [U unless marked]. "Cap" is 75% of the frame at the goal (§1).

### At 15/30 (cap 50 ms)

| Map | Today | RW2 + RW3 (cut) | + AIME 2a (SP, H) | + C's walk (Cl, or SP where room) | SP total | Client total |
|---|---|---|---|---|---|---|
| a10 | 42.8 | -10 to -15 | +1 to +2 | +15 to +19 | ~44-54 with the walk: on the line | ~43-52: on the line |
| a30 | 46.3 | -10 to -15 | +1 | +19 to +24 | ~51-61: the walk doesn't fit | ~50-60: over |
| b30 intro / flight | 61 / 70-82 | -15 to -25 | +10 to +24 | +25 | intro ~46-70, flight ~55-91, without the walk: over | intro ~61-71, flight ~70-92: over |
| c10 | 22.0 | -6 to -9 | +0.5 | +14.5 to +18 | ~28-35 ✓ with the walk | ~28-34 ✓ |
| c20 | 27.6 | -3 to -6 | +4 to +10 | +8 to +11 | ~34-46 ✓ with both | ~30-36 ✓ |
| d20 | 46.5 | -4 to -8 | +6 to +15 | +10 to +12 | ~45-58 with AIME alone: on the line | ~49-55: on the line |

- **The cuts:** RW2 is the census's removable share (61-70% of the models' non-matrix words [E pdc1]) less the key and the patching. PDC's copies cost the ME what building did, so the copy form saved nothing (c10 pair 3, 41129823447 [M]: copying words 3.5 ms, replay 2.1, counts and hash 1.1, deciding 0.9, key 0.4). A CALL to a retained block writes ~3 words a part instead of the part's words [D, pdc_plan's step 1], so most of the 3.5 + 1.1 goes. The figures above take a third to a half of the replay's models share as removable [U]. RW3 is the level's ME time (b30 7.1 ms a frame on the ME [M, 6b]; c20's level is 8.4 ms of the GE [M]) less the calls [U].
- **AIME** is the pass after a frame's last tick on the ME at 0.9-1.5x the main CPU's time: b30 ~10-12 ms a frame movable today (up to ~24 under L15), c20 ~4-5, d20 ~6-7, c10 ~0.5 (`hardware_surveys.md` [U on M x E]).
- **C's walk** is the objects pass (b30 ~20 ms [M], c10 ~14.5 [M], a10 and a30 taken as half their record [U]) x 1.0-1.25 on the ME [U, mekinds 0.80x for model_geometry, +15% under overlap, the node products 2x slower].
- **Audio** is in "today". Its share is per second, not per frame: ~7% of the ME's time [U from M: 9.9 ms of audio and ring jobs in a ~150 ms b30 frame]. At 30 frames a second it's ~2.3 ms a frame.

### At 30/30 (cap 25 ms)

Only a50 and c10 fit today's load. After RW2 and RW3, c10 (~13-17 with AIME) and a50 fit; c20 fits only without AIME (~22-25). **C's walk at 30/30 doesn't fit any map's ME** on these figures. So 30/30 needs either the walk to stay on a main CPU whose tick is far smaller than today's (a tick + record ≤ 33), or an ME whose per-frame drawing falls ~3x. RW2's full reach (the kept blocks plus kept bone sub-lists for still objects, pdc_plan step 3) is the only route on the table to the second. **30/30 is not a target this plan can promise on any heavy map** [U].

### Per role

- **Single player:** as the SP column. The ME's room goes to AIME where the AI is heavy (c20, d20; b30 only after the cuts), to C's walk where drawing binds (c10; a10 only at the cuts' upper end), and nowhere on a30 or b30 until RW2 is measured.
- **Co-op host:** SP's load plus the extra actors (`coop_enemies` "per_player": 1.5x the squads at 2 players [D]) and the remote players' bipeds (~0.8 ms a biped a tick [M]). So the host's ME is SP's, plus ~50% more of AIME's ask at 2 players [U]. A PSP host is on paper only (multiplayer_plan.md §8): it is the worst case for memory (§7), not a near-term target.
- **Co-op client:** no AI, no scripts. Its tick on b30's flight was 46 - 12 = ~34 ms (b112 [M]; less today), plus every remote player. Its ME carries no AIME, so C's walk fits on c10 and c20 at 15/30 and sits on the line on a10 and d20; a30 and b30 stay over their cap with the walk.

## 6. The eDRAM map

The ME's eDRAM runs to the full 2 MB on the PSP-1000 (the ME's stack began at 2,039 KB and nothing below 1,792 KB changed, bench-hw 41115505618 [M]). Only the ME and DMA reach it [D]. Its contents don't survive a sleep: it's rebuilt.

| KB | SP and host (planned) | Client (planned) | Today (w142) |
|---|---|---|---|
| 0-192 | left as found | left as found | the same |
| 192-208 | recon's bus scratch (recon builds only) | the same | the same |
| 208-256 | the ME's private state (`meedramstate`, if its pair pays) | the same | opt-in |
| 256-320 | **MIST's reserve** (the 1000's syscall table is in 256 KB+). **Its address is pinned before PDC lands at 320**: if the table sits higher, PDC's 240 KB moves above it, so the order is MIST's pin, then PDC's claim [U] | the same | reserved, unpinned |
| 320-560 | PDC's tables and stores (~240 KB) [D] | the same | in the heap (research) |
| 560-750 | L1, the level pass's positions (~190 KB) | the same | plan |
| 750-800 | MEVGATHER, visibility's gathered spheres and references (~20-44 KB, double) [U] | the same | new |
| 800-1,184 | free (~384 KB): RW2's key tables if they leave the heap | **C's snapshot (192 KB)**, filled by the ME's own gather | free |
| 1,184-1,456 | MUSICSTREAM's decoder (~270 KB); free if MUSADPCM replaces it | the same | decided 2026-10-30 |
| 1,456-1,504 | AIME's job data (48 KB) | - (free) | decided 2026-10-30 |
| 1,504-2,048 | the ME's stack, sized for AIME's job (544 KB) | the ME's stack, 128 KB at 1,920-2,048: **1,456-1,920 free (464 KB)** | decided 2026-10-30 |

- **SP's claims come to ~1,450 KB** of the ~1,856 above 192 KB, with ~384 KB free (and 270 more if MUSADPCM wins).
- **The client** keeps everything but AIME, so its walk's snapshot fits in eDRAM, read off the bus. In single player the snapshot stays in the heap (192 KB fits the heap's largest block on b30, c10 and a10 [E walkr2]) unless the 800-1,184 room is left free for it.
- **CPA** (1,089 KB) stays out (Sheepdog01 2026-10-30 07:38). **R2/SNDEDRAM** stays rejected (frame +7-14% [M]).
- The lab's `edram.md` is the register: each row here goes there before it's built.

## 7. The other memories, per mode

**The scratchpad (16 KB)** [D, w142 `psp_scratch.c`]: the level collision BSP's top nodes (SCRATCHBSP, ~9.7 KB, on [M: -0.3 ms a tick]) and the profiler's slots and front table (6,656 B). The models' corner table went to the heap with SCRATCHBSP, so the brief's 15.1-of-16 KB split is a b108 figure. **The profiler's tables take the scratchpad in every build, play builds included** (`psp_profile_tables_take`, called from ge_initialize unconditionally [D]). **PROFHEAP:** in play builds the tables go to the heap and their 6.6 KB go to the tick's hottest data: more BSP slots, or the hottest stack frames (STACKHOT: stacks are 10.4% of b30's tick D-misses [E]). Under C's full form the drawing never reads the scratchpad (the ME can't reach it [D]), so it is the tick's alone.

**VRAM (2 MB)** [M, w142 log]: display 832 KB, texture cache 616, model buffers 616. All spoken for. Display lists in VRAM would remove the GE's list reads from main RAM (0.25 MB a frame on b30 [E bus7]) but the ME, which writes them, can't see VRAM [D]: they don't fit C's shape, so they're dropped from the plan.

**Main RAM, per mode** [D, U]:

| Mode | Tag window | Heap | Netcode | Notes |
|---|---|---|---|---|
| Campaign (SP) | 9 MB | 5,952 KB; b30 peaked at 5,047 [M, b111] | none | PDC's 240 KB, mevis's ~150 KB (state ~80 and a 72 KB stack) and the in-flight 224 KB are inside it |
| Multiplayer | 6 MB | 4,352 KB | modules 574 KB outside the heap [M]; buffers in the heap | measured on a stock map: heap top 3,528 KB in play [E] |
| Co-op (host or client) | 9 MB | from one pool with the window | **~1.3-1.9 MB of buffers allocated at the network game's start, freed at its end** (MODEMEM) | ~1.6 MB to find against SP (multiplayer_plan.md §8 [U]); the eDRAM's freed AIME room (client) can't take net buffers, since the main CPU does the I/O |

A unified EBOOT follows from MODEMEM: the campaign stops paying for the netcode while it isn't playing. Its size is measured only when multiplayer resumes (it needs a multiplayer compile).

## 8. What moves where, and why

| # | Move | From -> to | Worth [label] | Roles | Gate |
|---|---|---|---|---|---|
| 1 | The ME's scheduler, entry points and stack (RW1) | - | 0 ms direct; it is what every ME placement below stands on, and it fixes two open failures (music, AIME's fault) | all | music and AIME jobs run on a PSP at ≥ 75% busy with no missed audio block |
| 2 | Prebuilt lists CALLed (RW2) | ME's building -> retained blocks | ME -6 to -9 ms (c10), -10 to -15 (b30, a10, a30) [U] | all | a c10 pair (§11) where the ME binds: the cinematic |
| 3 | The big updates split into passes (RW5) | Main -> Main, fitted to the I-cache | unsized [U]: the I-cache is ~37% of the tick's cycles, ~86% of it capacity [E tg141]; FVP, the first split, gave -0.5 ms on b30's flight tick [M] | all | one sub-pass at a time, each a PSP pair (the cache rule) |
| 4 | AIME 2a | Main -> ME | b30 -10 to -12, c20 -4 to -5, d20 -6 to -7 a frame before L15 [U] | SP, H | RW1's entry points and the ME's stack answered, then c20's pair; b30 after #2 |
| 5 | Posing batched on the VFPU (RW6) | Main FPU -> Main VFPU, a pass of many bones | -1 to -3 ms a tick on b30 [U: ~21% of the tick x ~30% issuing x the VFPU's 2-2.6x] | all (plays the same; `co-op:` note) | emulator side-by-sides + outcome census, then a b30 flight pair |
| 6 | The level as baked lists (RW3) | ME's replay -> CALLs | ME -2 to -7 [U] | all | gestream exact; then a c20 pair |
| 7 | C's walk (SNAPC) | Main -> ME, per map | main CPU -14.5 (c10), -15 to -20 (a10) a frame [M x U] | Cl first; SP on c10 and c20, a10 if room | §3's order; the ME's room in §5 measured after #2 |
| 8 | Visibility's gathered spheres (MEVGATHER) | mevis's scattered reads -> a gathered copy in eDRAM | -1 to -3 ms of the collect's wait [U] | all | an exact build, then a b30 pair |
| 9 | c10's weather on the ME (WXME) | Main -> ME | c10 -2.8 to -3.1 (-3.9 to -4.3 with the sprites) [U on M x E] | all | the dead walk's census, then a c10 pair |
| 10 | The scratchpad for the tick (PROFHEAP) | profiler -> heap; 6.6 KB to the tick | -0.3 to -1 ms a tick [U, by SCRATCHBSP's -0.3 for 9.7 KB] | all | a one-build pair (a keep word) |
| 11 | The datum's hot fields packed (OBJHOT) | 5 lines -> 3 | ~1.5-2% of b30's tick D-misses [U] | all | a cache-model count, then a pair |
| 12 | Music out of Tremor (MUSADPCM) | ME decode -> the mixer's ADPCM | ME -2.65 ms a block [M x U] | all | a listening A/B (Sheepdog01), then a pair |
| 13 | What the GE reads from main RAM, by kind and timing (GEREADS, phase 0) | a census, then at most one move into VRAM or a smaller form | the bus toll is 25-34% of b30's tick [M]; the share any one kind can win is unknown [U] | all | the census names a kind read while the ticks run |

**What stays where it is, and why:**
- **The model walk on the main CPU** in single player until §5's room is measured: the ME is the scarcer lane.
- **Collision on the main CPU, exact, everywhere:** memory-bound, and the network game needs the host's answers. Its levers are placement (SCRATCHBSP, a start grid at bake, 8-byte nodes) [U].
- **Node matrices off the ME** (NW1, NW2, MEPOSE, BW, PNC): the readers sit inside the same update and the ME's FPU is slower than the VFPU (NW2 c20 tick +0.8 ms [M]).
- **The DMA controller** keeps the bulk copies it already makes: every copy this plan adds is a gather, not a block (`hardware_big_moves.md`).
- **The GE:** only RW2's and RW3's CALLs change what it reads. The GE lane's own cuts (needed on a30 and b30 for 30/30) are out of this plan's scope [U]; the catalogue's are GL1/GL2 (measured), CP's exact slice (~2 ms of b30's GE [E]) and VRAM residency.

## 9. What's rewritten

**RW1. The ME's scheduler, its entry points and its stack** (lever MESCHED, new). Today's failures are the ME's, so they're fixed where they run (the 10-27 rule):
- **Job classes with deadlines:** audio (hard, per block), ring jobs and rays (soft, 10 ms), the drawing's replay (chunked, mechunk's gate), and background jobs (music decode, AIME) in bounded slices. A slice that would make the next audio block late yields first.
- **Admission:** a background job starts only when the frame's expected load leaves the ceiling's room, and is counted (late, refused, starved) in the log.
- **Preemption, if cooperative yields don't hold:** the ME's own timer interrupt exists and the port already uses it under `mehalt` (mcidclan's IP7 timer [D]). A timer that preempts a background slice for the audio job removes the "one long job makes audio late" class outright.
- **No runtime CPU test on the hot paths (option a).** AIME 2a's pairs faulted because the ME's sp was in main RAM, so every `sp >> 27` test took the ME for the main CPU and ran a VFPU instruction on it (41132036291, 41142311296 [M]). The fix is not a better test. Every ME job enters through entry points of its own, and the shared maths it reaches (matrix4x3_multiply, the quaternion routes) is called in its FPU form there, explicitly, through those entry points or an ME build of the shared maths. So no code on either CPU asks which CPU it is on, and nothing costs a test at run time. The identity check survives only as a **debug check that runs on the ME** (in a debug-tools build), where reading CP0 is allowed. The AIME helper is building this now; this is its design, not its code.
  - **Why not `mfc0 $22`:** mcidclan's library reads the CPU's number from CP0 register 22 (`me-lib.h` 83-107 [D]), but his samples run in kernel mode. The game's main CPU runs in user mode: the crash dumps' Status 60008613 has KSU = user and CU0 off (41142311296 runs 2 and 3 [M]), so a CP0 read there is code 11, the very fault being fixed. The ME is in kernel mode with CU0 on (Status 30000002 [M]). No one `mfc0` test serves both CPUs.
  - **Rejected, option b: a word only the ME writes, read by both.** A shared word says what the ME is doing, not which CPU is reading it, so the main CPU reading it while a job runs gets the ME's answer: racy. A per-CPU word works only where each CPU sees its own memory, and here both see the same main RAM (the eDRAM is the ME's alone, but the main CPU can't load from it [D]). It also keeps a test on the hot paths, which (a) removes.
  - **Its proof is on a PSP:** one c20 run, keep alone, the ME's jobs live (the emulator runs no ME code, so it can't fault there) [D]. A clean run with the ME never stopped is the proof; it's a fault test, not a timing, so one run is enough (§12).
- **The ME's stack: an open hazard.** At the fault, the ME's sp was 0x09fbf860-888, in main RAM beside the main thread's own stack at 0x09fbe- (41142311296 [M]), not in the eDRAM room the plan gives it (1,504-2,048 KB). If the ME runs there, it writes over the main thread's oldest frames, and **the main CPU's bus errors after the ME stopped (runs 2 and 3) fit that** [U]. The AIME helper is finding where the job gets that sp. Until that's answered, RW1's job classes have nothing safe to stand on: the slices, the preemption and the 75% ceiling all assume each job's stack is where the eDRAM map says. Checked against it too: whether the ME's stack was where we thought on the music hangs' runs (41123795043, 41133871215), since a stack in main RAM would explain a hang whose first fix held once and failed once [U].
- **The music hang's cause found first:** its first fix held once and failed once [M]. RW1's counters (each slot's longest run, each audio block's slack) are what name it.

**RW2. Prebuilt lists, CALLed** (PDCCALL, a slice of PDC). PDC's own step 1 as designed, not step 3b's copy: a part whose record matches last frame's (key: the record's bytes less its node matrices) is CALLed from a retained block after a CALL to this frame's bone sub-list. Blocks live in a heap arena (~200 KB on b30 at most [E], the GE can't read eDRAM, VRAM is full), freed only once the GE has finished the last frame that called them (geinflight tracks it). The key and its tables can move to eDRAM (only the ME reads them). Exact by construction: identical records make identical words.

**RW3. The level as baked lists** (BLEVEL, new; B's level pass, `b_level_pass.md` on its branch). Per-cluster lists made once at load (or at bake), CALLed per visible cluster, with the per-frame parts (fog, the near cut) patched. It takes the level's replay off the ME.

**RW4. The snapshot contract** (SNAPC, a slice of L5). C's steps 0-4 (hardware_utilization.md §4): the walk reads through a snapshot accessor by absolute index, copy-on-wake, the field list completed until the serial check shows no live read, visibility collected before the hand-off, walkapart's side set by which entry point the walk came in through (RW1's entry points), not a global, and the ME's bounded wait with the main CPU walking the snapshot only if the ME is dead.

**RW5. The big updates in passes that fit** (FISSB, new; LPH's real form). biped_update and unit_update split, inside each function, into sub-passes over every biped or unit, each under ~200 code lines so it stays in the I-cache across objects. One sub-pass is built and priced at a time, on the PSP (LPH's cache-model check failed: it scored +11.5% where the real batch lost 34% [E vs M]). Exact if the sub-passes keep each object's reads before writes in order: proven by play and both seeds.

**RW6. Posing as a batched VFPU pass** (SKELBATCH, new). The quaternion and matrix chain for many bones at once in the VFPU's own forms (`vqmul.q`, `vdot.q`, `vrsq`), aligned temporaries (the engine's floats are 4-aligned and `ulv.q` corrupts on the 1000 [D]). Not bit-exact: plays the same, proven by side-by-sides and an outcome census. LOCAL to every role (hardware_utilization.md §6): the host checks only movement and hit reports, within units of tolerance.

**RW7. Sound off the ME's CPU** (two options; neither is the first step). MUSADPCM (new): music baked to the mixer's own ADPCM, so the ME decodes it like any voice. And the VME, once it computes: the mix is fixed-point streaming work, the VME's kind. The VME is present and answers on the PSP-1000, but computes nothing yet (round 1, 41115505618; round 3's start failed, 41142248633 [M]). It gets **0 ms in every budget** until a sample computes on the 1000.

**RW8. Memory by mode** (MODEMEM, new). The netcode's buffers allocated at a network game's start and freed at its end; the tag window and the heap from one pool sized per mode; co-op planned as the worst case. On paper until multiplayer resumes.

## 10. The multiplayer boundary, move by move

Halo's netcode isn't lockstep (hardware_utilization.md §6 [D]): what must match is the host's collision answers, the datum indices of objects placed at map load, tag indices and object slots.

| Move | Touches what the server consumes? | Verdict |
|---|---|---|
| RW1, RW2, RW3, MEVGATHER, PROFHEAP | no (drawing, scheduling, placement) | safe in every role |
| RW4 (C's walk) | no: drawing only; slots and indices untouched | safe; the client's natural form |
| RW5 (passes) | the same results in the same order | safe, exact |
| RW6 (posing) | node matrices are LOCAL; hit reports bounds-checked | safe with a `co-op:` note; a systematic bias would go unseen (by design) |
| OBJHOT | layout only; slots and indices kept | safe |
| AIME, aicad3, AIFAR | the AI runs on the host only | a host option in co-op; nothing on a client |
| WXME, FX15 | local effects | safe |
| L15 | the tick rate | **campaign only**: a client must tick at the host's 30 Hz |
| MODEMEM | the join token and the map version unchanged | safe |
| Collision forms | the host's answers | **exact in every role**; no non-exact form is planned |

## 11. The order of work

Each step is gated on its proof. Steps in one phase can run side by side when they touch different lanes.

**Phase 0, records and one census.**
- The stale rows below fixed (§13).
- The ME's ms a frame per map read from runs already on the results branch (the intros' tm-plain, the b30 lane traces), so §5's "today" column is complete for the heavy stretches.
- **The GE's main-RAM reads on b30's flight, by kind** (GEREADS, new). b30's tick falls 25-34% with nothing drawn, and 55-94% of that rise is the GE reading main RAM while the tick misses (§2 [M]): the largest measured tick lever, with nothing behind it once VRAM display lists are dropped. RW2 and RW3 cut what the ME writes, not what the GE reads. The bus census over b30's 90 s route already gives the totals: the GE reads ~1.4 MB of vertices, ~1.4 MB of texels and ~0.25 MB of lists a frame, the level copies ~1.0 MB of the vertices (bus7 40884427736, bus8 40894428250 [E]). What it doesn't give:
  - **the flight alone** (game time 300-2000), where b30's tick is ~50 ms;
  - **by kind:** display lists, model vertices, level-copy vertices, indices, dynamic vertices (decals, particles, the HUD), textures, **CLUTs**, and anything else, each split by where it lives (heap, `.bss`, the texture RAM cache);
  - **when:** the bytes read while a tick runs against those read while the main CPU records or waits. Contention is about timing (levelcompact halved the level's bytes and gained nothing, three times [M]), so a kind read mostly outside the ticks is worth less than its bytes.

  Spec: one emulator batch on the bus census (the halo-local patch's item 23, the lab's `bus.py`), b30's route to the flight's end, with the kind and tick-overlap splits added where `bus.py` lacks them [U: whether it splits CLUTs and indices today]. **At most one PSP pair, and only if needed:** if the census names one kind that VRAM could take by giving up something less used (the texture cache's best third holds 189-270 KB; the model buffers run full [M]), a b30 flight pair `"- | <that word>"` prices it; if no kind stands out, no pair. Its answer is what decides whether b30 has a tick route at all.

**Phase 1, the ME's floor.** RW1, in order: the ME's stack answered (where AIME's job gets its sp, and whether the music runs had the same), then the ME's own entry points with no runtime CPU test (both are the AIME helper's, in progress), then the job classes and the counters, then the music hang's cause. The classes go on only after the stack's answer. Proof: one c20 run on a PSP for the entry points (§12 ask 0); the emulator for exactness and the counters; one PSP pair under load (ask 1).

**Phase 2, ME room** (after phase 1's entry points and stack; beside phase 4). RW2, then RW3, then MEVGATHER. Each frees the ME's time; each is measured on the map where the ME binds (c10's cinematic, then b30).

**Phase 3, spending the room, map by map** (after phase 2's pairs). AIME 2a on c20 and d20 (SP, host), then b30 if §5's measured room allows; WXME on c10; C's walk (RW4) on c10 first, then c20 and a10 where the measured room allows, behind its keep word, and for a client by design.

**Phase 4, the tick** (main CPU only; independent of phase 1, so it starts now, beside phases 1-3, and its PSP asks 5 and 6 can run while RW1 is built). Its sized cuts don't close b30's gap (§0): GEREADS (phase 0) is what may. PROFHEAP, RW6 (emulator side-by-sides first), RW5 (one sub-pass at a time), OBJHOT; the collision start grid and 8-byte nodes after their censuses.

**Phase 5, Sheepdog01's A/Bs, off by default.** L15, FX15, AIFAR, PRIO60, RENDERTICK are built behind keep words (Sheepdog01 2026-10-29 11:59). In co-op the AI ones are host options; L15 stays campaign only.

**Phase 6, research.** The VME (computing at all, then the mix); MUSADPCM's listening test; MODEMEM's sizes when multiplayer resumes.

## 12. The PSP asks

Each is one A/B pair (two sides, two runs a side: 4 runs) on the shortest stretch that holds the effect, unless a reason is given. None is queued: each waits for its build to pass its emulator proof and compile. **Asks 5 and 6 don't depend on phase 1** (main CPU only): they can take the PSPs while RW1 is built.

| # | Ask | Map, stretch | Keep sets | Why the PSP | Waits on |
|---|---|---|---|---|---|
| 0 | RW1's entry points (one run, not a pair: a fault test, not a timing) | c20's route | keep alone, the ME's jobs live | the emulator runs no ME code, so the fault can't show there | the AIME helper's fix; the ME's stack answered |
| 1 | RW1 under load | b30's route to the flight (where the ME is busiest) | `"- | musicstream"` on the RW1 build | only the PSP runs the ME: deadlines, late blocks, the hang | RW1 built; exact in the emulator |
| 2 | RW2 | c10, the cinematic (150-570) | `"- | pdccall"` | the ME binds there; the copy form lost there [M] | RW2 exact on four maps |
| 3 | AIME 2a, after the entry-point fix | c20's route | `"aime aime1b | aime aime1b aime2a"` (another thread's spec) | the ME's value is unmeasured | ask 0 clean |
| 4 | RW6 | b30's flight | `"- | skelbatch"` | the VFPU's speed isn't in the emulator | side-by-sides + census |
| 5 | PROFHEAP (**independent of phase 1**) | b30's flight | `"- | profheap"` (one build) | a cache change: priced on the PSP | exact (it changes no state) |
| 6 | RW5, first sub-pass (**independent of phase 1**) | b30's flight | `"- | fissb1"` (one build) | a cache change | play + seeds |
| 9 | GEREADS's one move, only if the census names a kind | b30's flight | `"- | <that kind's word>"` | the toll is contention, which the emulator doesn't time | phase 0's census |
| 7 | RW3 | c20 hands off (its level keeps 70% and costs 8.4 ms of GE) | `"- | blevel"` | GE and ME time | gestream exact |
| 8 | MEVGATHER | b30's route to the landing | `"- | mevgather"` | the ME's cache | exact |

**Not asked:** a depth run of anything. C's walk (RW4) gets its b30 pair only after ask 2 says how much room the ME has. The VME stays on its own bench (bench-hw, the vmeprobe thread).

## 13. Records this plan found out of date

Checked against w142 (d367e0a) [D]:
- **RSO is on by default** (`rsooff` turns it off, psp_ge.c; FIXES_RSO in every build). levers.tsv says "not merged". decisions.md (2026-10-22 08:03) is right.
- **PRS is on by default** (`percall` turns it off; FIXES_PRS in every build). levers.tsv says "shelved". decisions.md (02:45) is right.
- **aicad3 is on by default in w142** (`keep_default_add("aicad3")`, `aicad3off` off, psp_platform.c). The parked-lever sweep's check called it "built, off".
- **The scratchpad's split** in the brief (the corner table 8.4 KB) is b108's: SCRATCHBSP took that room in w142.
- **CSTPOOL is closed** (null on the PSP-1000) but `edram.md` still lists it "waiting on Sheepdog01".
- **NW's parent row** still says "proposed" though its slices were judged (the sweep's correction).

## 14. What this plan doesn't know

- **Where the ME's stack really is** [M at the fault, U why]: 0x09fbf8xx in main RAM at AIME 2a's fault; RW1's classes wait on the answer.
- **What the GE reads on b30's flight, by kind and by timing** [U]: GEREADS (phase 0); until then b30's 15/30 has no sized route.
- The ME's value of AIME 2a, of RW2 and of C's walk [U]: every ME figure in §5 is a sum of shares, not a measurement.
- The ceiling of 75% [U]: RW1's pair sets it.
- RW5's worth [U]: one function's split, measured, before anything is planned on it.
- The heavy stretches' ME loads for a10, a30, d20 [U]: phase 0 reads them.
- A co-op host's netcode cost [U]: nothing measured.
- Whether the GE's cuts for 30/30 on a30 and b30 exist without a compromise [U].

## Sources

- **Lanes and loads:** lab findings `timemap_lanes` (sierra-hw 40566969386), `lanes_per_frame` (40714069887, 40718998656), b149's hand play rows; `hardware_utilization.md` §1-§8; `hardware_big_moves.md`; `hardware_surveys.md`; the lab's `rank_pipelined.md` §9.
- **Today's lessons:** music (41108776634, 41123795043, 41133871215), PDC 3b (41116735742, 41123563741, 41129823447), AIME 2a's faults (41132036291, 41142311296), the VME (41115505618, 41142248633).
- **C:** the lab's `coord/handoff/render_ownership_2026-10-27.md` §4 and §6b; `mevis_landing.md`; hardware_utilization.md §4.
- **PDC:** `pdc_plan.md` (research/pdcreuse).
- **eDRAM:** the lab's `findings/edram.md`; `aime_me.md`.
- **Multiplayer:** `multiplayer_plan.md` §4, §8; hardware_utilization.md §6.
- **The scratchpad:** `psp/src/psp_scratch.c`, `psp/src/psp_platform.c` (`psp_profile_tables_take`), `psp/src/psp_ge.c` (ge_initialize).
