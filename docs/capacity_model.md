# Workload and capacity (ledger 2)

Every job a frame does, as a **fixed cost per frame plus a cost per entity**, with the unit it runs on now and the units it could run on, so capacity can be read off (`redesign_recon.md`, ledger 2). Sections are added job family by job family; lighting is the first.

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference. Counts from the emulator are exact for the play they come from; its times are shares, not milliseconds.

## Ledger 2: lighting (started 2026-11-04)

Sheepdog01, 2026-11-04 13:44: today's lighting cost, split into models and world, before the GE's hardware lights (`hw_capabilities.md` C2) are chased. Source: the light census (keep word `lightcensus`, build switch `PSP_LIGHTCENSUS=1`, branch `recon/light-census` on tock161), the lab's `findings/measurements.tsv` rows labelled LC1, and the earlier lighting levers (`projects/sierra/tools/levercheck.py --ground lighting`).

### 1. The jobs

| Job | What it does | Unit now | Driven by | Known cost |
|---|---|---|---|---|
| Attached lights' relink | `object_connect_lights`: each object's attached lights reconnected to the map's clusters, once a frame (LD1) | main CPU, frame end | objects carrying lights | 0.9–4.5 ms a frame [M, objlight on w140]; 3.4–3.8% of the tick [E, §3] |
| The lighting lookup | `object_get_cached_render_lighting` and its refresh, per drawn object | main CPU, record lane | drawn objects | with the two preparations below |
| Static preparation | `lights_prepare_for_object_static`: lightmap samples, each a BSP ray down plus a lightmap read; 1 for a moving object, 5 for a still one | main CPU | refreshed objects | 0.1–1.1 ms a frame [M, objlight] |
| Dynamic preparation | `lights_prepare_for_object_dynamic`: the clusters' lights within reach, the two brightest kept | main CPU | drawn objects, each scene | 0.3–2.4 ms a frame [M, objlight] |
| Models' GE lighting | ambient + 2 distant + up to 2 point lights a vertex (`psp_ge.c`, `model_lighting`); a point light's reach and a spotlight's cone are worked out once for the model, at its sphere's nearest point [D] | GE | vertices drawn x lights on | unmeasured |
| Light selection | `lights_preprocess_scene`: the lights in view, their colours, lens flares | main CPU | lights in view | 0.4–0.7% of all cycles [E, §3] |
| The world pass, selection | `lights_render_diffuse`: per dynamic light its clusters and the level triangles it reaches | main CPU, record lane | lit lights x their surfaces | 8.1% of d20 swamp's drawing, 0.3–2.2% elsewhere [E, §3] |
| The world pass, vertex work | `psp_ge_light_draw_body`: per triangle the light's reach at each corner (falloff, cone, facing, gel), the spotlight's split near the light (FLC), the near cut, the depth bands, the list | ME (the replay) | triangles handed over | the ME's light draws 15.1 ms a frame on d20's swamp, 10.1 on a10's middle [M, LM] |
| The world pass, the GE | the lit triangles drawn again over the level, additive, depth equal, the surface's own texture | GE | lit triangles, their pixels | — |

**Models already use all four of the GE's lights.** Two are the distant lights from the lightmap samples, two are the object's point lights (b117) [D]. A dynamic light adds **no pass** to a model: it takes a GE light slot. What it doesn't get is its falloff or cone across the model: both are one number a model, so a Warthog lit at one end is lit all over by the reach at its nearest point [D].

### 2. The census (counts a frame) [E]

Tock161 68a2f134 plus the census (f8a97ca), lab sierra-batch 41904975882; play and both seeds identical to the base build every tick with the census off and on (b30 6,300 ticks, d20 2,700).

| Stretch | Lookups | Static preps (samples) | Dynamic preps | Point lights found (candidates) | Relinks (weapons') | GE: models, indices | Light-index products (point) |
|---|---|---|---|---|---|---|---|
| b30 opening + flight (0–60 s) | 98 | 16 (64) | 84 | 2.4 (2.4) | 99 (92) | 110, 78,272 | 158,911 (2,367) |
| b30 beach (60–100 s) | 192 | 27 (92) | 145 | 12.5 (15.0) | 102 (95) | 164, 93,349 | 193,608 (6,909) |
| b30 bunker, flashlight (100–210 s) | 42 | 8 (32) | 34 | 5.8 (5.8) | 26 (20) | 37, 25,254 | 59,064 (8,555) |
| b30 Warthog drive (90–127 s) | 204 | 28 (93) | 151 | 8.1 (8.2) | 102 (93) | 166, 93,801 | 194,610 (7,008) |
| d20 swamp, flashlight (50–90 s) | 16 | 4 (13) | 14 | 13.9 (16.1) | 28 (8) | 15, 9,243 | 29,566 (11,080) |
| c20 hands off (0–120 s) | 33 | 4 (10) | 22 | 1.8 (1.8) | 29 (5) | 24, 15,746 | 33,156 (1,663) |

**By kind, b30's beach:** scenery 97 models at 415 indices each, bipeds 28 at 961, **vehicles 2 at 8,789 each**, weapons 27 at 97, the first-person weapon 2,886. A vehicle costs the GE's lighting about 9 bipeds' worth a frame. Its CPU chain is small: one lightmap sample (it moves), one dynamic preparation, two relinks.

**The world's lights a frame (handed to the pass):**

| Stretch | Lights | Draws | Triangles | By carrier |
|---|---|---|---|---|
| b30 beach | 1.5 | 3 | 293 | weapons 154, effects 84, projectiles 55 |
| b30 bunker, flashlight | 0.8 | 10 | 1,561 | the flashlight 1,533 |
| d20 swamp, flashlight | 8.2 | 95 | 5,893 | machines' lights 3,244 (6.3 lights), the flashlight 2,153, scenery 431 |
| c20 | 0.8 | 15 | 651 | bipeds' lights |

**Where each handed draw goes** (`psp_ge_light_draw_body`, every way out counted; branch `recon/light-census2`; lab sierra-batch 41908365705, 41909289651, 41910690985; play and both seeds identical to the base every tick). These runs keep the replay on the main CPU (`gemeonoff`): the ME's copy of the counters doesn't reach the log [E].

| Stretch | Handed | No level copy (all waiting for room) | Through the loop | Nothing to draw |
|---|---|---|---|---|
| d20 swamp, flashlight | 95.8 | 77.4–81.5 (81–85%) | 18.5 | 6.1 |
| d20 hands off, heavy view (80–120 s) | 163.0 | 139.0 (85%) | 16.0 | 3.0 |
| b30 bunker, flashlight | 10.7 | 9.5 (89%) | 1.2 | 1.0 |

- **Every draw is accounted for.** Draws entered = draws handed, so no record is lost. The only early return taken is `level_stream_find` (no level copy). Light closed, frame closed, list full, pass off, texture bind, average and bad index are all 0 [E].
- **Each no-copy draw is on a material waiting for room for its copy.** The level skips its own draw of that material too (`GE: level copies short of room`): on d20's swamp ~29 level draws a frame, with 20–64 materials waiting. No light draw lands on a surface the level didn't draw that frame (stale 0): the selection hands over only drawn surfaces [E].
- **The emulator is short of copy room; the PSP isn't [M against E].** The PSP-1000 on d20's hands-off view (sierra-hw 41888761035, tock159 LTO+PGO, the same keep words) logs no material waiting and draws ~110 light draws a frame (33,000 every 300 frames). The emulator on the same view draws ~13. The 2 MB geometry arena (`geo2`) changes nothing (lab 41910210206): the copies' room is the heap's 800 KB arena plus the 256 KB level carve. The default build in the emulator skips more than `gemeonoff` (d20 63,200 level draws over the run, b30 71,744; lab 41909748434). Whether a tock161 PSP log shows it is unchecked [U].
- So **the emulator's world-pass totals on d20 are about 8x low**. Its per-draw ratios stand [U on E].

**The pass's work per light, a frame (what reached the loop)** [E, lab 41908365705 and 41910690985]:

| Stretch, light | Triangles looked | Cone cut | Reach cut | Lit | Split → pieces | Kept + cut | KB to the GE |
|---|---|---|---|---|---|---|---|
| d20 swamp, the flashlight | 191 | 54 | 68 | 14 | 54 → 143 | 157 + 0 | 11.1 |
| d20 swamp, machines' lights (6.3) | 495 | — | 329 | 166 | — | 136 + 21 | 10.0 |
| d20 swamp, scenery (flames, white lights) | 116 | — | 57 | 59 | — | 50 + 4 | 3.6 |
| d20 hands off, machines' lights (10) | 1,468 | — | 1,019 | 449 | — | 379 + 150 | 30.2 |
| b30 bunker, the flashlight | 57 | 7 | 47 | 0 | 3 → 3 | 3 + 0 | 0.2 |
| c20, the monitor's light | 23 | — | 20 | 2.7 | — | 2.6 + 0.1 | 0.2 |
| b30 beach, Warthog drive (flashes, grenades) | < 2 | — | < 2 | 0 | — | 0 | 0 |

Per draw through the loop, d20's machines look at ~92 triangles: 69% are cut for reach, 31% lit. On the PSP, with every handed draw drawn, d20's hands-off view would put **~8,400 triangles a frame** through the loop (lit ~2,600, ~170 KB to the GE). That is the handed count times the emulator's ratios [U on E].

**What the counts say:**
- **Two point lights a model are enough.** Candidates within reach exceed two by 0–2.5 a frame on every stretch (b30 beach 15.0 against 12.5 kept, d20 16.1 against 13.9).
- **Most relinks are of lights never drawn.** On b30 weapons' attached lights are 92–95 of ~100 relinks a frame, while under one weapon light a frame reaches the pass. That is DAL's ground (dark attached lights; held for an a10 picture mismatch) and LSAME's.
- **The models' CPU chain is driven by scenery and bipeds**, not vehicles: b30's beach takes 92 lightmap samples (BSP rays) and 145 dynamic preparations a frame, 97 of each for scenery alone.
- **The world pass is d20's and the flashlight's.** On b30 outside the flashlight the pass gets under 300 triangles a frame, and under 2 of them reach the loop. d20's machines and the flashlight hand it ~5,900 on the swamp and ~8,400 on the hands-off view.
- **Under b30's flashlight the pass is ~nothing.** Of 1,578 triangles handed a frame, 57 reach the loop and none is lit. The rest are on materials waiting for copy room in the emulator, so the PSP's figure is unknown.

### 3. The price [E]

The cache model's cycles in each lighting entry, inclusive of what it calls, as a share of all the run's cycles (lab sierra-batch 41904126863, tock161 68a2f134, `spliteach`, the replay on the main CPU; read with the lab's `projects/sierra/scripts/lc2read.py` and the build's link map). These are shares, not milliseconds.

| Stretch | Models (static prep, dynamic prep, point samples) | Relinks (`light_reconnect_to_map`) | Scene (`lights_preprocess_scene`) | World selection (`lights_render_diffuse`) | World pass (`psp_ge_light_draw_body`) | All |
|---|---|---|---|---|---|---|
| b30 route, to 194 s | 2.65% (static 4.86% of drawing) | 1.31% (3.78% of the tick) | 0.57% | 0.26% | 0.13% | 4.92% |
| b30 Warthog drive | 2.61% (static 4.63% of drawing) | 1.39% (3.78% of the tick) | 0.60% | 0.17% | 0.00% | 4.76% |
| d20 swamp | 1.10% (static 2.48% of drawing) | 1.46% (3.36% of the tick) | 0.66% | 2.59% (8.14% of drawing) | 1.69% (5.29% of drawing) | 7.50% |
| c20 hands off | 0.95% | 0.88% (1.92% of the tick) | 0.40% | 0.67% (2.22% of drawing) | 0.09% | 2.99% |

**Models against world.** On b30 the models' chain outweighs the world about 7:1. Its biggest piece is the static preparation's lightmap samples, a BSP ray each, driven by scenery and bipeds (§2). On d20's swamp the world outweighs the models about 4:1, and the selection on the main CPU costs more than the pass itself. Two caveats on d20:
- In the emulator only 15–19% of the handed draws reach the pass's loop (copy room, §2). The selection's share isn't cut by that; the pass's is, so on the PSP the pass is several times this [U].
- In the default build the pass runs on the ME, so its share leaves the main CPU. There it is the ME's light draws: 15.1 ms a frame on d20's swamp [M, LM].

**Vehicles.** The drive's shares are b30's. A vehicle adds one lightmap sample, one dynamic preparation and two relinks a frame (§2), so vehicles cost the CPU's lighting nothing measurable. Their cost is the GE's: 8,789 indices a vehicle a frame, times its 2–4 lights.

**Relinks.** They are 3.4–3.8% of the tick on b30, d20 and the drive, and 1.9% on c20. On b30, 92–95 of the ~100 relinks a frame are weapons' attached lights, while under one weapon light a frame reaches the pass. So the relinks of lights never drawn are about 3.5% of b30's tick, if a relink costs the same whatever its carrier [U on E]. That's DAL's and LSAME's ground. `projects/sierra/tools/levercheck.py DAL LSAME`:
- **DAL [stopped]**, held: dal 0f3b381 isn't picture-exact on a10 (81 systematic frames of 4,490). Revive when the missing a10 light is found and fixed, so pictures are exact, and a gain is shown.
- **LSAME [open]**, sized: ceiling 1.65 ms a frame on b30's flight, ~1.0 late cinematic, 0.6 d20 swamp [objl1].

The cache model's 3.8% of b30's tick sits in line with LSAME's 1.65 ms. This census adds who the relinks are for (weapons) and that the pass never sees them [E].

### 4. What it implies (a sizing, not a build)

**The GE's lights for models' dynamic lighting [D, E].** They already carry it: the four slots are full (2 distant + 2 point), and the census almost never finds more than two point lights in reach. What's left to gain is look, not capacity. `sceGuLightSpot` and the GE's attenuation would vary a light across a model per vertex (the flashlight's cone on a Marine, a Warthog lit at one end), where today it's one value a model. But the GE's attenuation is `1/(k0 + k1 d + k2 d²)` and its cone `cos^n`, not Halo's `(1 - d²/r²)²` and linear cone (recon_134: the fitted form is off by up to 38/255 and leaves 37/255 at the radius), so it would be a look change for Sheepdog01's A/B.

Its cost is the GE's per-vertex time for a spot or attenuated point light, against today's unattenuated point light, on the point-lit indices: 1,700–11,000 a frame on these stretches [E], 8,555 under b30's flashlight. The PSP pair in §5 measured today's point lights at no measurable GE time [M], so the per-vertex price of a hardware light on models is small. The open cost is the look.

**The GE's lights for the world: LM [E].** One draw per surface with up to four GE lights, in place of one draw per light, is lever LM (sized, not started). The census gives its count. d20's machine lights hand over 63 (swamp) to 163 (hands off) draws a frame for 6.3–10 lights, ~10–16 draws a light. On the PSP's hands-off view ~110 are drawn a frame [M, sierra-hw 41888761035]. The GE's falloff can't match Halo's (above), so LM's merged draw keeps the per-vertex reach on the ME and sums the lights into one colour: a draw-count lever, not a hardware-light one.

**A projected-texture pass for the world [D, U].** Today's pass already is a GE redraw of the lit triangles. What sits on the ME is the per-vertex light (reach, cone, facing, gel) and, for the flashlight, the split that rounds the falloff and cone on big triangles (FLC). A projected pass would move the cone and the falloff into textures the GE reads through the texture matrix (the old `lightconetex` path did the cone), leaving the ME a cull and an index copy. Its price has three parts:
- **What it removes from the ME.** The census says what the loop does: 69% of the triangles it looks at are cut for reach and 31% lit (d20's machines, both views). The flashlight's are cut by cone (28%) and reach (36%), and 54 a frame are split into 143 pieces. On d20's hands-off view that's ~8,400 triangles a frame on the PSP [U on E]. A projected pass keeps the cull (or the GE draws all ~8,400), so it removes the per-corner falloff, facing and colour on the ~31% lit, plus the split. LMC (static lights' reach kept across frames: d20 swamp ~85% of light-frames are static [E]) removes the same per-corner work without a second GE pass, and is ranked first.
- **What it adds on the GE.** The GE has one texture a pass, so keeping the surface's own texture (B7, a restoration) ~~takes a second pass over the lit pixels into the destination alpha~~ **can't be done with a second pass into the destination alpha: the frame buffer is 5650, with none (corrected 2026-11-10, research; `psp_ge.c` `sceGuDrawBuffer(GU_PSM_5650, …)`). A projected pass keeps the surface only as its average colour (the old `lightconetex` look, B7 given up), or makes the light term in a buffer of its own that shares the depth buffer's stride (~278 KB of VRAM; the lab's `projects/sierra/research/opportunities/light_pass.md`, option C).** The facing term needs normals in the level's vertices (kept apart today, 4 bytes a vertex).
- **Where it could pay.** Only where the ME binds: d20's heavy view, where the main CPU waits 14–17 ms a frame for the ME's render job (`cpu_cost_map.md` §1). On b30 under the flashlight the loop sees ~57 triangles a frame and lights none, so there's nothing to win.

A projected pass is a compromise (a look change) unless the textures reproduce Halo's falloff to the 1/255. By the rule for compromises it is catalogued, not built.

**Vehicles.** Their lighting is GE work: 8,789 indices a vehicle a frame, times 2–4 lights. A cheaper vehicle light (fewer lights on its far parts, or the distant lights only) is a GE question for a PSP pair, not a CPU lever.

**The emulator's copy room.** Until the emulator's copy room matches the PSP's, its world-pass totals on d20 (and b30's flashlight) undercount, and every emulator drawing figure on d20 is short of the level surfaces the PSP draws. Per-draw and per-triangle ratios stand.

### 5. Still open

- ~~**The GE's per-vertex price.**~~ **Answered [M, PSP-3000, lab sierra-hw 41914079168, tock161 3a104119, d20 hands off, `- | nopointlights` x2]:** the GE's busy time is 38.8 / 38.8 ms a frame with the models' two point lights and 38.8 / 38.7 without, from game time 1,800. The frame moves -0.7 to -1.0% in the opening and flight (the flight's -0.8 ms is real, mostly the tick's -0.3 ms of the dynamic preparation), and the heavy second half is in the noise. So the GE's hardware lighting costs it nothing measurable per vertex at d20's load. A GE spot or attenuated light on models would be priced by its look, not by GE time.
- ~~**Is the copy shortage the emulator's, or tock161's?**~~ The emulator's (B58 in the lab's bugs.md). Its simulated sound tier takes 656 KB of heap that the PSP keeps in eDRAM: the heap after d20's load is 5,782 KB taken in the emulator, 5,132 KB on the PSP. The fix (the ME's eDRAM mapped in the lab's emulator) is in progress, 2026-11-04.
- The world pass's ME time on today's build: LM's 15.1 ms (d20 swamp) is from an earlier build.

## Ledger 2: the jobs' fixed and per-entity costs (started 2026-11-04)

The master, 2026-11-04 17:45: each job's fixed cost a frame plus its cost per entity, the emulator's censuses first, then a few PSP runs with the counts varied, and first answers to the capacity questions. Branch Sierra-III `recon/capacity2` (cbf0b5e = `recon/b58-edram` b5c3b403, the emulator's B58 fix, plus the recon-only keep word `aiplaceallN`: every encounter of the level placed at once, by its own `ai_place`, at N s). Rows in the lab's `findings/measurements.tsv`, metrics `capacity_*`. The price is read against `cpu_cost_map.md` §1 (the tick is 37–58% of the heavy maps' clock) and its §4 lines "Doing less work" and "Work moved to the ME".

**The budget.** 30/30 is one tick and one frame in 33.3 ms on each unit: the main CPU (tick + drawing + the rest), the ME (the AI job, audio, the replay, visibility) and the GE.

### 1. The runs

- **The census** [E]: lab sierra-batch 41937949354 (`cap2b`): b30, a30, c20 routes and d20's swamp; a50, b40 and c10 hands off, each without and with `aiplaceall30`. Tick steps (`PSP_TICK_STEPS=1`, `stepsall`, `timerson`) grouped into jobs; counts from `workcensus` (objects updated by type, actors, collision vectors, node matrices) and `objcost` (per type and definition). Counts are exact; times are the emulator's JIT ms. (41937378999 ran the same without `timerson`: counts only. Note: every build starts with `notimers`, so a step census needs `timerson`.)
- **Placing every encounter steps the awake count only where the encounters are near.** On a50 and b40 the actors updated a tick don't move (28.0 and 10.0 on both sides): encounters placed away from the player sleep, and a sleeping actor costs nothing measurable a tick [E]. On c10 the count goes from 0 to 14.1 actors a tick (bipeds 2 → 9, weapons 1 → 7, collision vectors 7 → 16). So c10 is the stepped stretch.
- **The PSP pair** [M]: sierra-hw 41941016273, PSP-1000, the clean build (no recon switches, timers off, AIME and the replay on the ME as shipped), c10 hands off to game time 4,500, `- | aiplaceall30`, one run a side. Read from game time 1,800 (cumulative budget lines differenced).
- **An instrumented pair first** (sierra-hw 41938919389, PSP-3000, tick steps and `timerson`): **the ME faulted at start on both sides** (Cause code 8, EPC 08af9cc0), so the AI, the replay, point physics and audio all ran on the main CPU, and its timers cost ~1.9× the clean tick. Its per-job split, its particle and its voice figures are used below as upper bounds for the main CPU; its totals are not. That the fault comes from the step timers' clock calls in ME-run code is a guess [U]; it's unreproduced in the clean build. Named since: B59 in the lab's bugs.md (below, round 2).
- **Round 2** (branch `recon/capacity3` 4245e9a7 = capacity2's + two recon-only fixes + `aiviewN`: the player put at an encounter's first firing position facing its first squad; `aiviewplace` also places it, so both sides see the same view, one with the actors in it). Emulator check sierra-batch 41943600284 (c10: 6 Covenant in view, models drawn 326 → 451 a frame) and 41944718485 (b30 `drivetest`, which needs `PSP_DEBUG_TOOLS=1`). PSP-3000 clean pair sierra-hw 41947176349: c10 `nomeaudio aiview30 | + aiviewplace`, one run a side, read from game time 1,800. `nomeaudio` on both sides: without it the placement stopped the ME for the session (41945216680, the audio job past its 40 ms deadline; B60). PSP-1000 instrumented run sierra-hw 41947183527: b30 hands off with `drivetest`, `objcost` per type, the ME running throughout.
- **The ME's share of a job** [M]: the `ME: running` lines' busy time less the audio, replay and ring jobs' own time, with the work nested in a job's yields added back once. With no actor awake it reads 0.04–0.06 ms a tick, so the rest is the AI job.
- **B59, the instrumented ME fault** [D, M]: EPC 08af9cc0 was `sceKernelGetSystemTimeLow`'s system-call stub, called from `psp_step` (ra +0xe4). With `PSP_TICK_STEPS=1` the step markers sit in `actor_update` and perception, which AIME runs on the ME, where a system call is an exception. Fixed for recon builds: `psp_step` returns at once on the ME. Debug-tools builds had a second such path, the collision usage log's performance counter; fixed the same way. Proof: 41947183527's ME ran to the end.
- **Round 3** (branch `recon/capacity4` c547f42f = capacity3's + `drivetest` on the clean build: `-DPSP_DRIVE` through `PSP_CFLAGS` opens the drive alone, without the other debug tools; `drivequiet` drops its per-tick lines; `drivenone` is its control side, the AI erased on the same schedule and no Warthog). Emulator check sierra-batch 41949607338 (the drive makes its Warthog where it did before and plays 901 steps) and 41950017834 (b40: `aiviewplace` adds no actor there, a1_cov is already in play). PSP-3000, the clean build with the drive, no PGO, bake w157m: sierra-hw 41950823554, b30 hands off, `drivetest drivequiet` with `drivenone` against the drive, one run a side, read over game time 2,900–3,600 (the drive's two circles); sierra-hw 41950932611, b40 `aiview30` (the player at a1_cov's first firing position facing its squad), one run, read over 1,200–2,950. Read from the heartbeat lines (tick, drawing) and the `ME: running` lines (each job's own time) [M].

### 2. Per entity: the table

| Entity | Main CPU, a tick | Main CPU, drawing a frame | ME | Fixed part | Source |
|---|---|---|---|---|---|
| **Actor in play** (thinking, its biped and weapon updated and posed, collision, perception refreshes, props, lights) | **1.32–1.43 ms** [M] | **0.55 ms** when on screen [M] | **thinking 0.29–0.32 ms a tick; replay 0.78 ms a frame when on screen** [M] | AI ~3.3 emu ms a tick with no actor [E] | Off screen: PSP-1000 pair 41941016273, tick 17.55 → 27.61 ms for 7 actors (round 1 divided by 14.1 actor updates, 2 a biped a tick: 0.71 ms was per update). In view: PSP-3000 pair 41947176349, tick 18.8 → 26.7 ms, drawing 22.0 → 25.3 ms a frame, the ME's replay 24.3 → 29.0 ms a frame, its AI job 0.04 → 1.98 ms a tick, for 6 actors. `aicad3` (half-rate thinking) on, as in the default |
| ↳ of it by job | AI's main-CPU side ~35%, biped/unit ~28%, posing ~20%, objects ~14%, lights ~4% [U on M × E] | | | | cap2b c10's job shares on the same pair |
| **Asleep actor** (placed away from the player) | ~0 [E] | 0 | 0 | | a50, b40: counts unchanged |
| **Biped update** alone (`objcost`, per update) | 208–1,437 emu µs, median ~450 → ~1.3 ms [U on E] | | | | cap2b; the factor 2.9 below. d20's Flood 1,437 |
| **Vehicle, driven or flying** | **driven Warthog +8.7 ms a tick clean** [M] (+6.4 on the right-hand circle, +10.4 on the left; 6.0–9.2 instrumented was the Warthog's own update alone); a Pelican flying 4.7–9.3 instrumented (~2.5–4.9 clean), hovering 1.7–3.4 (~0.9–1.8) [M instrumented, /1.9 U]; 0.013–0.020 asleep [M, w139] | **Warthog +1.75 ms a frame clean** [M]; 0.5–2.6 ms a vehicle [M, w139 `objcost`] | driven Warthog: its sounds **+1.3 ms an audio block** (3.73 against 2.42 ms a 23.2 ms block, ~+1.9 ms a 30 fps frame); the replay unchanged within the view's change (66.3 against 68.4 ms a frame) [M] | | Clean: PSP-3000 sierra-hw 41950823554 (`drivetest drivequiet`, `drivenone` against the drive): tick 12.04 → 20.69 ms, drawing 50.66 → 52.41 ms a frame. The pair prices everything driving brings, not the vehicle's update alone: the seated player, the Warthog's physics and collision, its effects and sounds, the camera moving over the beach. Instrumented: PSP-1000 sierra-hw 41947183527 (`objcost`, timers on), the Pelicans below divided by 1.9 [U]. w139's 3.4–3.9 ms driven (fixsim-hw1 39395866364) is within today's range: UVB and the tick passes haven't moved the driven Warthog measurably. Emulator 765–3,161 emu µs an update (cap2b) |
| **Projectile** | ~0.4 ms an update instrumented, **~0.2 clean** [M instrumented, /1.9 U] | ~0 | | | 41947183527, b30's beach: 1.0 ms a tick for 2.5 updates; the emulator's 0.13–0.32 (cap2b) agrees |
| **Weapon** (lying or held) | 3–38 emu µs → ≤ 0.1 ms [U on E] | | | | cap2b |
| **Particle** (alive) | **14.6 µs a frame** (28.6 an update) + 158 µs fixed [M, upper bound: point physics on the main CPU] | | on the ME by default (point physics) | 158 µs | 41938919389, 11 windows, 4–92 alive |
| **Sound voice** | | | **0.28 ms a frame** (192 µs a 23.2 ms block) + 0.19 fixed [M, mixed on the main CPU; the ME's own split agrees: 57 cycles a voice-frame mixing, decoding beside it] | 129 µs a block | 41938919389, 81 blocks, 1–13 voices; `audio_split_b30_flight` |
| **Light** | ledger 2's lighting section above | | | | |

**The exchange rate.** The emulator's tick grows 3.5 JIT ms for c10's 14.1 actors where the PSP-1000's grows 10.06 ms: **×2.9** reads the emulator's per-entity slopes as PSP-1000 ms on today's build [U]. Each [U on E] figure above uses it.

**What's fixed, what scales.** With no actor awake, c10's tick is still 17.5 ms on the PSP-1000 and its drawing 28 ms: 237 scenery updates a tick (~32 emu µs each, ~7.5 emu ms a tick) and 221 node-matrix calls are the map's own load, not its fighters'. Each map's fixed load is its scenery, its BSP and its effects; the census rows give it per map.

### 3. First answers to the four questions

All against 33.3 ms on each unit, at one tick a frame. **An actor in view costs ~1.9 ms of the main CPU (1.32 its tick, 0.55 its drawing) and ~1.1 ms of the ME (0.32 thinking, 0.78 its share of the replay); off screen ~1.4 and ~0.3.** The main CPU binds first, but not by much: on c10's view the ME's replay alone is 24–29 ms a frame, so a heavy map's ME is the next wall (B60 is it reached). **On a light map both walls are near already** (round 3, b40's first fight, PSP-3000, 3–5 actors in play): tick 14.5 ms and drawing 15.9 ms a frame (the main CPU ~30.4 ms at 30/30), and the ME 89% busy at 27.9 fps: the replay 22.4 ms a frame, audio 4.11 ms a 23.2 ms block (~5.9 ms a 30 fps frame), the rest ~2.4 ms; no AI job on the ME there [M].

| Question | First answer | Weakest assumption |
|---|---|---|
| **Firefight waves** | Measured on b40's first fight (round 3) [M]: with its own 3–5 actors the main CPU uses ~30.4 ms of 33.3 and the ME ~30.7 (replay 22.4 + audio 5.9 + ~2.4), so **~1–2 more actors in view fit on either unit, ~5–7 in all**; less those actors, the arena's own load is ~8–10 ms of tick, ~13–14 of drawing and ~19–20 ms of replay [U on M: 1.4 ms of tick, 0.55 of drawing and 0.78 of replay an actor taken off]. A driven Warthog is ~10.4 ms of the main CPU (≈ 5–6 actors in view). Round 2's ~7 in view assumed a light map's tick of ~3.6 ms, which b40's fight doesn't have; its replay sits right at the ~20 ms that answer allowed. On c10's own ground none fit at 30/30. | That b40's first fight stands for an arena (one view, one run; whether all its 3–5 actors are on screen isn't counted) |
| **Co-op 4 / 16** | A remote player is a biped updated, posed and drawn without thinking: ~0.86 ms a tick (65% of 1.32) + 0.55 drawing ≈ **1.4 ms of the main CPU and ~0.8 of the ME** (its replay). 4 players add ~5.6 ms (17%) and ~3 ms of the ME; 16 add ~22 ms (67%) and ~12.5 ms of the ME. The heavy maps are already over 33 ms alone (b30's tick 35.5 ms, tock160 ladder), so co-op 4 waits on the campaign's own cuts; 16 needs remote players far cheaper than an AI biped on both units. | The network's own cost a player (unmeasured); the remote biped priced as the AI biped's non-thinking share (65%, emulator shares) |
| **A 128-player client** | 127 remote bipeds at ~0.86 ms a tick each is ~109 ms a tick: **not at today's per-biped cost**. It fits only if far players cost tens of µs (position only: no posing, physics or collision) and drawn players are capped near ~10 (~14 ms of the main CPU, ~8 ms of the ME's replay). The memory side is unchecked: today's actor pool is 128 (R4) and the multiplayer build's 8. | That a far remote player can skip posing and physics and still be correct for the server (`multiplayer_recon.md`: the client's floats are corrected) |
| **Custom content** | Room is 33.3 ms less the map's own load: today only the light maps (b40, c40, d40: 27–30 fps on tock160's ladder) leave any. 1 ms of the main CPU buys **~0.7 actors off screen or ~0.5 in view, ~70 particles, ~5 projectiles, or a tenth of a driven Warthog**; 1 ms of the ME ~3.5 voices or ~1.3 actors' share of the replay. | The Pelican and projectile figures are instrumented ones divided by 1.9 (the Warthog's is clean since round 3) |

### 4. Still open

- ~~**The drawn actor on today's build.**~~ Answered (round 2): 0.55 ms of main-CPU drawing and 0.78 ms of the ME's replay a frame [M].
- ~~**The ME's AI cost per actor.**~~ Answered: 0.29–0.32 ms a tick [M], from the `ME: running` lines (no new instrument needed).
- ~~**Vehicles, projectiles on today's build**~~ Answered: projectiles ~0.2 ms an update (instrumented); the driven Warthog clean in round 3, +8.7 ms a tick and +1.75 ms a frame (§2): round 2's ~3.2–4.8 was the vehicle's own update scaled, and too low as the price of a drive.
- ~~**A light map's replay and drawing on today's build.**~~ Answered (round 3, b40's first fight): replay 22.4 ms a frame, drawing 15.9, tick 14.5 [M]; the firefight answer above uses them.
- **b30's beach is ME-bound as well as CPU-bound** (seen in round 3's pair, not asked): with the AI erased the ME's replay is 66–68 ms a frame, and ~91 ms with the beach's AI (game time 1,800–2,600), against the main CPU's 50–56 ms of drawing [M].
- ~~**The ME fault under `timerson`**~~ B59, fixed for recon builds (§1).
- **B60: placing an encounter beside the player on c10 stopped the ME** (the audio job past 40 ms, on the clean build). Which job held the ME isn't known; the replay at 29–35 ms a frame there is the likely one [U]. A fight that brings this many actors into view at once on a heavy map could do it in play.
- The fixed loads per map on the PSP: one budget run a map gives them (the ladder's lines hold tick and drawing; the job split is the emulator's).

## Ledger 8: memory per entity (2026-11-04)

The redesign's ledger 8 (`redesign_recon.md`): the pool slots and bytes each entity takes, beside ledger 2's time, and how many of each the room ledger 4 found would hold (`memory_ledger.md` §0). Labels as above; every figure is [E] unless marked.

### 1. The runs

- **Build:** recon/ledger8 6bdbfb22 = recon/capacity4 (tock162 64647bb5 + B58's emulator fix + `aiplaceallN`, `aiviewN`/`aiviewplace`, `drivetest`) + **objbytes**: each poolpeaks report is followed by the objects pool summed by object type (count, bytes asked, bytes held with each block's 24-byte header and rounding, least and most of one). `PSP_RECON=1 PSP_POOLS_XBOX=1` (the game state's arrays at the Xbox's sizes, so nothing caps), `-DPSP_DRIVE=1`, no PGO, bake w157m. Recon only, never for merging. tock162's later head c3f3dba5 (ledger 4's) changes no array or pool.
- **Batch l8a** (lab sierra-batch 41960587740, 10/10 ok to game time ~3,675, 0 strict sites; `poolpeaks memcensus nomeaudio`, hands off, `fixedticks fixedseed`): entity counts varied on one build. c10 with no actors placed, one encounter placed in view (`aiview30` against `aiview30 aiviewplace`: 6 Covenant beside the player) and every encounter (`aiplaceall30`: 39 encounters, 42 more actors living); b30 and d20 hands off against every encounter (b30 +55 actors, d20 +12); b30 `drivetest` against `drivenone` (both erase the AI at 90 s; the Warthog alone). Each pair is read at the 120 s report as live slots and object bytes, placed side less control (the 60 s report agrees within a few slots).
- **The heap was the same on both sides of every pair:** heap top 5,216 KB (c10), 5,224 (b30), 5,108 (d20) with or without the actors or the Warthog; voices at most 5 -> 18 with the drive, same heap. An entity takes no heap of its own: its memory is the game state's fixed arrays and objects pool (sized at start), plus per-tag caches (sound, geometry, VRAM) that every instance of a kind shares. This build's heap is 5,228 KB (the Xbox's arrays take 536 KB more game state than the shipped 5,764), so b30's 62 failed heap requests on all four of its runs are the build's, not the entities'.

### 2. Per entity: slots and bytes

| Entity | Objects pool (KB, held) | Game-state slots it takes (bytes a slot) | Bytes in all | Heap |
|---|---|---|---|---|
| **Actor in view** (c10, 6 Covenant beside the player) | biped 4.17 KB + 1 weapon 1.01 = **5.18 KB** | actor 1 (1,828 B); object headers 2 (12); lights 3.3 (124); props 3.7 (312: its records of the units it perceives); pursuits 4.7 (40); render states 1.7 (256); light and object cluster refs 5 + 5 (12 each), collideable 1 + 1 (12); looping sounds 0.7 (52) = **4.2 KB** | **~9.2 KB** | 0 |
| **Actor spread through the map** (b30 +55, d20 +12, c10 +42; mostly dormant, out of view) | biped 4.35-4.36 KB (d20's flood and sentinels 5.56) + 1 weapon 1.03-1.06 = **~5.2 KB**; c10's every-encounter fight left 2.1 bipeds a living actor (bodies of the dead) = 9.8 KB | actor 1; headers 1.8-2.8; lights 1.8-4.1; props 0.5-0.7; pursuits 0-0.7; render states 0.1-1.1; cluster refs 0-2.6 each; looping sounds 0.4-0.6 = **~2.5 KB** | **~7.5 KB** (a dead one leaves ~4.4 until the garbage collector) | 0 |
| **Warthog** (driven, b30) | 3.26 KB (one 3,236 B vehicle) [E]; a 1,496 B vehicle-type object and a 752 B weapon came with the drive (+2.3 KB, not attributed) [U] | antenna 1 (700 B); lights 4 (124); recorded animations 2 (100); headers 3 (12); looping sounds 1 (52); cluster refs 7 + 7 (12) | **~4.8 KB** (7.1 with the two unattributed objects) | 0 |
| **Projectile** (b30's beach: every one 604 B asked) | **0.63 KB** | an effect (252 B) and its locations (60) where it hits or flies; a contrail (68) and its points (56 each) on a projectile that trails; a light (124) on one that glows [D: the tag decides which] | **~0.7 KB**, more with a long contrail | 0 |
| **Particle** | none | particle 1 (**112 B**); a system's particles take system particles (128) and each system 344 [E, poolpeaks sizes] | **112 B** | 0 |
| **Light** (dynamic) | none | lights 1 (**124 B**); a light cluster ref pair (2 x 12) per cluster it reaches: ~0.8 a light in b30's actors | **~148 B** | 0 |
| **Voice** | none | none: the sound start-up makes one mixer stream per engine channel (`IDirectSound_CreateSoundStream`, `psp_dsound.c`: a 64-byte-aligned block of the mixer's state + the game's side, ~3.4 KB each [U from the structs]); a playing voice takes one that is already made [D]. Its sound's samples sit once per sound tag in the 2,048 KB sound cache, shared by every voice playing it; a looping sound on an object takes an object looping sound (52 B) | **0** new; ~3.4 KB a channel if channels are added [U] | 0 [E: 5 -> 18 voices at most, same heap] |
| Object, any (for scale) | b30's opening: scenery 0.48-1.05 KB, machines 0.52-0.99, equipment 0.62, controls 0.51, light objects 0.53-0.58, sound scenery 0.48 | a header (12 B) | | 0 |

- **The objects pool is the actor's binding store:** 5.2 KB of the ~9.2 an actor in view takes, and the pool is full by design (1,000 of 1,024 KB at b30's route peak, ledger 4 §0.3; the garbage collector's limit keeps it there). The actor array has room (86 of 160 at its peak: 74 more); props (252 free at 312 B: ~68 actors in view), lights (533 free: ~160 actors) and cluster refs (~1,280 free: ~250) come next. Render states are full (512) but a miss reworks the lighting rather than failing.
- **What scales with what:** props and pursuits with how many units each actor perceives (3.7 and 4.7 an actor in a fight in view, 0.5-0.7 spread out); render states with drawing (1.7 an actor in view, 0.1 spread); lights with the actors' weapons (Covenant plasma weapons glow) [U].
- **d20's actors** (12 more, 7 more bipeds) aren't all bipeds: some are swarm or vehicle-borne [U]; its bipeds (flood, sentinels) are the largest, 5.6 KB.

### 3. How many the room would hold, by map

Ledger 4's heap room at the top (§0.2, shipped build, tock162), turned into entities if the game state's arrays and objects pool grew into it (an entity takes no heap of its own, §1): room / bytes in all [U on E]. The machine-wide stores beside it: user RAM outside the heap 70 KB (largest 28), and the ME's eDRAM 544 KB at 256-800 KB (superseded 2026-11-21: 280 KB at 520-800 KB; the ME's code copy holds 256-520, `memory_ledger.md` §0.4) (main-CPU game state there would pay the ME bus on every access: not priced).

| Map (ledger 4 run) | Heap room (KB) | Actors in view (9.2 KB) | Actors spread (7.5 KB) | Warthogs (4.8 KB) | Projectiles (696 B) | Particles (112 B) | Lights (148 B) |
|---|---:|---:|---:|---:|---:|---:|---:|
| a10 route | 344 | 37 | 45 | 71 | 506 | 3,145 | 2,380 |
| a30 route | 316 | 34 | 42 | 65 | 464 | 2,889 | 2,186 |
| b30 route | 308 | 33 | 40 | 64 | 453 | 2,816 | 2,131 |
| d20 route | 476 | 51 | 63 | 99 | 700 | 4,352 | 3,293 |
| a10 opening | 544 | 59 | 72 | 113 | 800 | 4,973 | 3,763 |
| a30 opening | 312 | 34 | 41 | 65 | 459 | 2,852 | 2,158 |
| a50 opening | 596 | 65 | 79 | 124 | 876 | 5,449 | 4,123 |
| b30 opening | 348 | 37 | 46 | 72 | 512 | 3,181 | 2,407 |
| b40 opening | 348 | 37 | 46 | 72 | 512 | 3,181 | 2,407 |
| c10 opening | 400 | 43 | 53 | 83 | 588 | 3,657 | 2,767 |
| **c20 opening** | **160** | **17** | **21** | **33** | 235 | 1,462 | 1,107 |
| c40 opening | 320 | 34 | 42 | 66 | 470 | 2,925 | 2,214 |
| d20 opening | 392 | 42 | 52 | 81 | 576 | 3,584 | 2,712 |
| d40 opening | 532 | 58 | 70 | 110 | 782 | 4,864 | 3,680 |

- **Memory is not the binding limit for actors; time is.** The tightest map (c20) holds ~17 more actors in view; ledger 2 puts the main CPU's room at ~1-2 more in view on b40's first fight (each ~1.9 ms main + ~1.1 ms ME a frame). At today's sizes, without growing anything, the objects pool binds first: its 24 KB at b30's peak is ~4 actors' bodies and weapons, after which the garbage collector deletes bodies sooner.
- **Particles, lights and projectiles** are cheap in memory (hundreds to thousands per map's room) and live in arrays with room today (particles 512 free, lights 533); their limit is time too (ledger 2: ~70 particles or ~5 projectiles a ms of the main CPU).
- **Custom content and co-op:** a remote co-op player (a biped, its weapons, its props in every actor) is an actor-in-view's ~9 KB; 4 players ~37 KB fit every map; 16 fit all but c20's opening.

### 4. Still open

1. The 1,496 B vehicle-type object and 752 B weapon that came with the drive: which tags (an objbytes line by definition would say).
2. The voice channel's bytes are read from the structs [U]; a `sizeof` print would make it [D], and the engine's channel count (its sound preferences) isn't read.
3. A per-map array table (ledger 4 printed the worst map's peak only): the array room by map, for maps where an array rather than the heap binds.
