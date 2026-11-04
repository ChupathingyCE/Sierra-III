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

Its cost is the GE's per-vertex time for a spot or attenuated point light, against today's unattenuated point light, on the point-lit indices: 1,700–11,000 a frame on these stretches [E], 8,555 under b30's flashlight. That per-vertex time is the one piece the cache model can't price (it models the main CPU, not the GE). It needs the PSP pair in §5.

**The GE's lights for the world: LM [E].** One draw per surface with up to four GE lights, in place of one draw per light, is lever LM (sized, not started). The census gives its count. d20's machine lights hand over 63 (swamp) to 163 (hands off) draws a frame for 6.3–10 lights, ~10–16 draws a light. On the PSP's hands-off view ~110 are drawn a frame [M, sierra-hw 41888761035]. The GE's falloff can't match Halo's (above), so LM's merged draw keeps the per-vertex reach on the ME and sums the lights into one colour: a draw-count lever, not a hardware-light one.

**A projected-texture pass for the world [D, U].** Today's pass already is a GE redraw of the lit triangles. What sits on the ME is the per-vertex light (reach, cone, facing, gel) and, for the flashlight, the split that rounds the falloff and cone on big triangles (FLC). A projected pass would move the cone and the falloff into textures the GE reads through the texture matrix (the old `lightconetex` path did the cone), leaving the ME a cull and an index copy. Its price has three parts:
- **What it removes from the ME.** The census says what the loop does: 69% of the triangles it looks at are cut for reach and 31% lit (d20's machines, both views). The flashlight's are cut by cone (28%) and reach (36%), and 54 a frame are split into 143 pieces. On d20's hands-off view that's ~8,400 triangles a frame on the PSP [U on E]. A projected pass keeps the cull (or the GE draws all ~8,400), so it removes the per-corner falloff, facing and colour on the ~31% lit, plus the split. LMC (static lights' reach kept across frames: d20 swamp ~85% of light-frames are static [E]) removes the same per-corner work without a second GE pass, and is ranked first.
- **What it adds on the GE.** The GE has one texture a pass, so keeping the surface's own texture (B7, a restoration) takes a second pass over the lit pixels into the destination alpha. The facing term needs normals in the level's vertices (kept apart today, 4 bytes a vertex).
- **Where it could pay.** Only where the ME binds: d20's heavy view, where the main CPU waits 14–17 ms a frame for the ME's render job (`cpu_cost_map.md` §1). On b30 under the flashlight the loop sees ~57 triangles a frame and lights none, so there's nothing to win.

A projected pass is a compromise (a look change) unless the textures reproduce Halo's falloff to the 1/255. By the rule for compromises it is catalogued, not built.

**Vehicles.** Their lighting is GE work: 8,789 indices a vehicle a frame, times 2–4 lights. A cheaper vehicle light (fewer lights on its far parts, or the distant lights only) is a GE question for a PSP pair, not a CPU lever.

**The emulator's copy room.** Until the emulator's copy room matches the PSP's, its world-pass totals on d20 (and b30's flashlight) undercount, and every emulator drawing figure on d20 is short of the level surfaces the PSP draws. Per-draw and per-triangle ratios stand.

### 5. Still open

- **The GE's per-vertex price (a PSP pair; recon queues).** The cache model can't give it. d20 hands off (the PSP's heavy view, where point-lit models and the ME both bind), `keepsets="- | nopointlights"`, `repeat=2`: the models' two point lights off, which removes the GE's per-vertex point lighting and the dynamic preparation's walk. Keep words: `nearauto assettrack geo2 nomouth fixedticks fixedseed gebusy2 hwprofile exitat120`, maps `/bake/w157m/halo/maps`, on the tock's own commit and recipe. The GE's busy time per point-lit index, read from `gebusy2`, prices what a GE spotlight or attenuated light would add.
- **Is the copy shortage the emulator's, or tock161's?** A tock161 PSP log on d20 or b30 (`GE: level copies short of room`) settles it. The PSP log read here was tock159, LTO+PGO.
- The world pass's ME time on today's build: LM's 15.1 ms (d20 swamp) is from an earlier build.
