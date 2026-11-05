# Workload and capacity (ledger 2)

Every job a frame does, as a **fixed cost per frame plus a cost per entity**, with the unit it runs on now and the units it could run on, so capacity can be read off (`redesign_recon.md`, ledger 2). Sections are added job family by job family; lighting is the first.

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference. Counts from the emulator are exact for the play they come from; its times are shares, not milliseconds.

## Ledger 2: lighting (started 2026-11-04)

Sheepdog01, 2026-11-04 13:44: today's lighting cost, split into models and world, before the GE's hardware lights (`hw_capabilities.md` C2) are chased. Source: the light census (keep word `lightcensus`, build switch `PSP_LIGHTCENSUS=1`, branch `recon/light-census` on tock161), the lab's `findings/measurements.tsv` rows labelled LC1, and the earlier lighting levers (`tools/levercheck.py --ground lighting`).

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

The cache model's cycles in each lighting entry, inclusive of what it calls, as a share of all the run's cycles (lab sierra-batch 41904126863, tock161 68a2f134, `spliteach`, the replay on the main CPU; read with the lab's `psp/sierra/lc2read.py` and the build's link map). These are shares, not milliseconds.

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

**Relinks.** They are 3.4–3.8% of the tick on b30, d20 and the drive, and 1.9% on c20. On b30, 92–95 of the ~100 relinks a frame are weapons' attached lights, while under one weapon light a frame reaches the pass. So the relinks of lights never drawn are about 3.5% of b30's tick, if a relink costs the same whatever its carrier [U on E]. That's DAL's and LSAME's ground. `tools/levercheck.py DAL LSAME`:
- **DAL [stopped]**, held: dal 0f3b381 isn't picture-exact on a10 (81 systematic frames of 4,490). Revive when the missing a10 light is found and fixed, so pictures are exact, and a gain is shown.
- **LSAME [open]**, sized: ceiling 1.65 ms a frame on b30's flight, ~1.0 late cinematic, 0.6 d20 swamp [objl1].

The cache model's 3.8% of b30's tick sits in line with LSAME's 1.65 ms. This census adds who the relinks are for (weapons) and that the pass never sees them [E].

### 4. What it implies (a sizing, not a build)

**The GE's lights for models' dynamic lighting [D, E].** They already carry it: the four slots are full (2 distant + 2 point), and the census almost never finds more than two point lights in reach. What's left to gain is look, not capacity. `sceGuLightSpot` and the GE's attenuation would vary a light across a model per vertex (the flashlight's cone on a Marine, a Warthog lit at one end), where today it's one value a model. But the GE's attenuation is `1/(k0 + k1 d + k2 d²)` and its cone `cos^n`, not Halo's `(1 - d²/r²)²` and linear cone (recon_134: the fitted form is off by up to 38/255 and leaves 37/255 at the radius), so it would be a look change for Sheepdog01's A/B.

Its cost is the GE's per-vertex time for a spot or attenuated point light, against today's unattenuated point light, on the point-lit indices: 1,700–11,000 a frame on these stretches [E], 8,555 under b30's flashlight. The PSP pair in §5 measured today's point lights at no measurable GE time [M], so the per-vertex price of a hardware light on models is small. The open cost is the look.

**The GE's lights for the world: LM [E].** One draw per surface with up to four GE lights, in place of one draw per light, is lever LM (sized, not started). The census gives its count. d20's machine lights hand over 63 (swamp) to 163 (hands off) draws a frame for 6.3–10 lights, ~10–16 draws a light. On the PSP's hands-off view ~110 are drawn a frame [M, sierra-hw 41888761035]. The GE's falloff can't match Halo's (above), so LM's merged draw keeps the per-vertex reach on the ME and sums the lights into one colour: a draw-count lever, not a hardware-light one.

**A projected-texture pass for the world [D, U].** Today's pass already is a GE redraw of the lit triangles. What sits on the ME is the per-vertex light (reach, cone, facing, gel) and, for the flashlight, the split that rounds the falloff and cone on big triangles (FLC). A projected pass would move the cone and the falloff into textures the GE reads through the texture matrix (the old `lightconetex` path did the cone), leaving the ME a cull and an index copy. Its price has three parts:
- **What it removes from the ME.** The census says what the loop does: 69% of the triangles it looks at are cut for reach and 31% lit (d20's machines, both views). The flashlight's are cut by cone (28%) and reach (36%), and 54 a frame are split into 143 pieces. On d20's hands-off view that's ~8,400 triangles a frame on the PSP [U on E]. A projected pass keeps the cull (or the GE draws all ~8,400), so it removes the per-corner falloff, facing and colour on the ~31% lit, plus the split. LMC (static lights' reach kept across frames: d20 swamp ~85% of light-frames are static [E]) removes the same per-corner work without a second GE pass, and is ranked first.
- **What it adds on the GE.** The GE has one texture a pass, so keeping the surface's own texture (B7, a restoration) takes a second pass over the lit pixels into the destination alpha. The facing term needs normals in the level's vertices (kept apart today, 4 bytes a vertex).
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

### 2. Per entity: the table

| Entity | Main CPU, a tick | Main CPU, drawing a frame | ME | Fixed part | Source |
|---|---|---|---|---|---|
| **Actor in play** (thinking, its biped and weapon updated and posed, collision, perception refreshes, props, lights) | **1.32–1.43 ms** [M] | **0.55 ms** when on screen [M] | **thinking 0.29–0.32 ms a tick; replay 0.78 ms a frame when on screen** [M] | AI ~3.3 emu ms a tick with no actor [E] | Off screen: PSP-1000 pair 41941016273, tick 17.55 → 27.61 ms for 7 actors (round 1 divided by 14.1 actor updates, 2 a biped a tick: 0.71 ms was per update). In view: PSP-3000 pair 41947176349, tick 18.8 → 26.7 ms, drawing 22.0 → 25.3 ms a frame, the ME's replay 24.3 → 29.0 ms a frame, its AI job 0.04 → 1.98 ms a tick, for 6 actors. `aicad3` (half-rate thinking) on, as in the default |
| ↳ of it by job | AI's main-CPU side ~35%, biped/unit ~28%, posing ~20%, objects ~14%, lights ~4% [U on M × E] | | | | cap2b c10's job shares on the same pair |
| **Asleep actor** (placed away from the player) | ~0 [E] | 0 | 0 | | a50, b40: counts unchanged |
| **Biped update** alone (`objcost`, per update) | 208–1,437 emu µs, median ~450 → ~1.3 ms [U on E] | | | | cap2b; the factor 2.9 below. d20's Flood 1,437 |
| **Vehicle, driven or flying** | driven Warthog 6.0–9.2 ms instrumented, **~3.2–4.8 clean**; a Pelican flying 4.7–9.3 instrumented (~2.5–4.9 clean), hovering 1.7–3.4 (~0.9–1.8) [M instrumented, /1.9 U]; 0.013–0.020 asleep [M, w139] | Warthog 2.3–2.6 ms a frame instrumented; 0.5–2.6 ms a vehicle [M, w139 `objcost`] | | | Today: PSP-1000 sierra-hw 41947183527 (`objcost`, timers on). w139's 3.4–3.9 ms driven (fixsim-hw1 39395866364) is within today's range: UVB and the tick passes haven't moved the driven Warthog measurably. Emulator 765–3,161 emu µs an update (cap2b) |
| **Projectile** | ~0.4 ms an update instrumented, **~0.2 clean** [M instrumented, /1.9 U] | ~0 | | | 41947183527, b30's beach: 1.0 ms a tick for 2.5 updates; the emulator's 0.13–0.32 (cap2b) agrees |
| **Weapon** (lying or held) | 3–38 emu µs → ≤ 0.1 ms [U on E] | | | | cap2b |
| **Particle** (alive) | **14.6 µs a frame** (28.6 an update) + 158 µs fixed [M, upper bound: point physics on the main CPU] | | on the ME by default (point physics) | 158 µs | 41938919389, 11 windows, 4–92 alive |
| **Sound voice** | | | **0.28 ms a frame** (192 µs a 23.2 ms block) + 0.19 fixed [M, mixed on the main CPU; the ME's own split agrees: 57 cycles a voice-frame mixing, decoding beside it] | 129 µs a block | 41938919389, 81 blocks, 1–13 voices; `audio_split_b30_flight` |
| **Light** | ledger 2's lighting section above | | | | |

**The exchange rate.** The emulator's tick grows 3.5 JIT ms for c10's 14.1 actors where the PSP-1000's grows 10.06 ms: **×2.9** reads the emulator's per-entity slopes as PSP-1000 ms on today's build [U]. Each [U on E] figure above uses it.

**What's fixed, what scales.** With no actor awake, c10's tick is still 17.5 ms on the PSP-1000 and its drawing 28 ms: 237 scenery updates a tick (~32 emu µs each, ~7.5 emu ms a tick) and 221 node-matrix calls are the map's own load, not its fighters'. Each map's fixed load is its scenery, its BSP and its effects; the census rows give it per map.

### 3. First answers to the four questions

All against 33.3 ms on each unit, at one tick a frame. **An actor in view costs ~1.9 ms of the main CPU (1.32 its tick, 0.55 its drawing) and ~1.1 ms of the ME (0.32 thinking, 0.78 its share of the replay); off screen ~1.4 and ~0.3.** The main CPU binds first, but not by much: on c10's view the ME's replay alone is 24–29 ms a frame, so a heavy map's ME is the next wall (B60 is it reached).

| Question | First answer | Weakest assumption |
|---|---|---|
| **Firefight waves** | On an arena whose own load is a light map's (tick ~3.6 ms, world drawing ~15 ms [U]), ~14 ms of the main CPU is left: **~7 actors in view, ~8–9 with half off screen**, before any vehicle (a driven Warthog ≈ 2–3 actors). The ME carries them if the arena's own replay is under ~20 ms (7 × 1.1 ms + audio). On c10's own ground none fit at 30/30. | The arena's world drawing and replay (no light map's split is measured on today's build) |
| **Co-op 4 / 16** | A remote player is a biped updated, posed and drawn without thinking: ~0.86 ms a tick (65% of 1.32) + 0.55 drawing ≈ **1.4 ms of the main CPU and ~0.8 of the ME** (its replay). 4 players add ~5.6 ms (17%) and ~3 ms of the ME; 16 add ~22 ms (67%) and ~12.5 ms of the ME. The heavy maps are already over 33 ms alone (b30's tick 35.5 ms, tock160 ladder), so co-op 4 waits on the campaign's own cuts; 16 needs remote players far cheaper than an AI biped on both units. | The network's own cost a player (unmeasured); the remote biped priced as the AI biped's non-thinking share (65%, emulator shares) |
| **A 128-player client** | 127 remote bipeds at ~0.86 ms a tick each is ~109 ms a tick: **not at today's per-biped cost**. It fits only if far players cost tens of µs (position only: no posing, physics or collision) and drawn players are capped near ~10 (~14 ms of the main CPU, ~8 ms of the ME's replay). The memory side is unchecked: today's actor pool is 128 (R4) and the multiplayer build's 8. | That a far remote player can skip posing and physics and still be correct for the server (`multiplayer_recon.md`: the client's floats are corrected) |
| **Custom content** | Room is 33.3 ms less the map's own load: today only the light maps (b40, c40, d40: 27–30 fps on tock160's ladder) leave any. 1 ms of the main CPU buys **~0.7 actors off screen or ~0.5 in view, ~70 particles, ~5 projectiles, or a quarter of a driven Warthog**; 1 ms of the ME ~3.5 voices or ~1.3 actors' share of the replay. | Clean vehicle and projectile figures are instrumented ones divided by 1.9 |

### 4. Still open

- ~~**The drawn actor on today's build.**~~ Answered (round 2): 0.55 ms of main-CPU drawing and 0.78 ms of the ME's replay a frame [M].
- ~~**The ME's AI cost per actor.**~~ Answered: 0.29–0.32 ms a tick [M], from the `ME: running` lines (no new instrument needed).
- ~~**Vehicles, projectiles on today's build**~~ Answered, instrumented [M]: the driven Warthog unchanged since w139 (~3.2–4.8 ms clean), projectiles ~0.2 ms an update. A clean pair would tighten the Warthog (drivetest needs `PSP_DEBUG_TOOLS=1`, so its clean side isn't the shipped build).
- ~~**The ME fault under `timerson`**~~ B59, fixed for recon builds (§1).
- **B60: placing an encounter beside the player on c10 stopped the ME** (the audio job past 40 ms, on the clean build). Which job held the ME isn't known; the replay at 29–35 ms a frame there is the likely one [U]. A fight that brings this many actors into view at once on a heavy map could do it in play.
- The fixed loads per map on the PSP: one budget run a map gives them (the ladder's lines hold tick and drawing; the job split is the emulator's).
