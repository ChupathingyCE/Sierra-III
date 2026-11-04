# Workload and capacity (ledger 2)

Every job a frame does, as a **fixed cost per frame plus a cost per entity**, with the unit it runs on now and the units it could run on, so capacity can be read off (`redesign_recon.md`, ledger 2). Sections are added job family by job family; lighting is the first.

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference. Counts from the emulator are exact for the play they come from; its times are shares, not milliseconds.

## Ledger 2: lighting (started 2026-11-04)

Sheepdog01, 2026-11-04 13:44: today's lighting cost, split into models and world, before the GE's hardware lights (`hw_capabilities.md` C2) are chased. Source: the light census (keep word `lightcensus`, build switch `PSP_LIGHTCENSUS=1`, branch `recon/light-census` on tock161), the lab's `findings/measurements.tsv` rows labelled LC1, and the earlier lighting levers (`tools/levercheck.py --ground lighting`).

### 1. The jobs

| Job | What it does | Unit now | Driven by | Known cost |
|---|---|---|---|---|
| Attached lights' relink | `object_connect_lights`: each object's attached lights reconnected to the map's clusters, once a frame (LD1) | main CPU, frame end | objects carrying lights | 0.9–4.5 ms a frame [M, objlight on w140] |
| The lighting lookup | `object_get_cached_render_lighting` and its refresh, per drawn object | main CPU, record lane | drawn objects | with the two preparations below |
| Static preparation | `lights_prepare_for_object_static`: lightmap samples, each a BSP ray down plus a lightmap read; 1 for a moving object, 5 for a still one | main CPU | refreshed objects | 0.1–1.1 ms a frame [M, objlight] |
| Dynamic preparation | `lights_prepare_for_object_dynamic`: the clusters' lights within reach, the two brightest kept | main CPU | drawn objects, each scene | 0.3–2.4 ms a frame [M, objlight] |
| Models' GE lighting | ambient + 2 distant + up to 2 point lights a vertex (`psp_ge.c`, `model_lighting`); a point light's reach and a spotlight's cone are worked out once for the model, at its sphere's nearest point [D] | GE | vertices drawn x lights on | unmeasured |
| Light selection | `lights_preprocess_scene`: the lights in view, their colours, lens flares | main CPU | lights in view | — |
| The world pass, selection | `lights_render_diffuse`: per dynamic light its clusters and the level triangles it reaches | main CPU, record lane | lit lights x their surfaces | — |
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

The pass's own work per triangle (cone and reach cuts, splits, pieces, bytes) by light: lab sierra-batch 41906956252 (to read; the ME-side counters of 41904975882 were lost, so the census runs again with the replay on the main CPU). d20's GE line meanwhile: 25 light draws a frame carried out, 90 triangles split, 419 pieces [E].

**What the counts say:**
- **Two point lights a model are enough.** Candidates within reach exceed two by 0–2.5 a frame on every stretch (b30 beach 15.0 against 12.5 kept, d20 16.1 against 13.9).
- **Most relinks are of lights never drawn.** On b30 weapons' attached lights are 92–95 of ~100 relinks a frame, while under one weapon light a frame reaches the pass. That is DAL's ground (dark attached lights; held for an a10 picture mismatch) and LSAME's.
- **The models' CPU chain is driven by scenery and bipeds**, not vehicles: b30's beach takes 92 lightmap samples (BSP rays) and 145 dynamic preparations a frame, 97 of each for scenery alone.
- **The world pass is d20's and the flashlight's.** On b30 outside the flashlight the pass gets under 300 triangles a frame; d20's machines and the flashlight hand it ~5,900.

### 3. The price [E]

The cache model's share of each lighting entry, inclusive of what it calls, with the replay on the main CPU: lab sierra-batch 41904126863 (to read: `CACHESPLIT <entry>` lines against the phase's whole). Entries: the two preparations, `lights_preprocess_scene`, `lights_render_diffuse`, `psp_ge_light_draw_body`, `light_reconnect_to_map`, `lights_illumination_at_point`.

### 4. What it implies (a sizing, not a build)

**The GE's lights for models' dynamic lighting [D, E].** They already carry it: the four slots are full (2 distant + 2 point) and the census finds almost never more than two point lights in reach. What's left to gain is look, not capacity: `sceGuLightSpot` and the GE's attenuation would vary a light across a model per vertex (the flashlight's cone on a Marine, a Warthog lit at one end) where today it's one value a model. The GE's attenuation is `1/(k0 + k1 d + k2 d²)` and its cone `cos^n`, not Halo's `(1 - d²/r²)²` and linear cone (recon_134: the fitted form is off by up to 38/255 and leaves 37/255 at the radius), so it would be a look change for Sheepdog01's A/B. Its cost is the GE's per-vertex time for a spot or attenuated point light against today's unattenuated point light, on the point-lit indices: 1,700–11,000 a frame on these stretches [E], up to 8,555 under the flashlight on b30. That per-vertex time is the piece the emulator can't price.

**A projected-texture pass for the world [D, U].** Today's pass already is a GE redraw of the lit triangles; what sits on the ME is the per-vertex light (reach, cone, facing, gel) and, for the flashlight, the split that makes the falloff and cone round on big triangles (FLC). A projected pass would move the cone and the falloff into textures the GE reads through the texture matrix (the old `lightconetex` path did the cone), leaving the ME a cull and an index copy; but the GE has one texture a pass, so keeping the surface's own texture (B7, a restoration) takes a second pass into the destination alpha, and the facing term needs normals in the level's vertices (they're kept apart today, 4 bytes a vertex). So the trade is the ME's per-triangle work (to be priced from 41906956252 and 41904126863) against at least one more GE pass over the lit pixels: worth it where the ME binds (d20's swamp, the flashlight), not where the GE does.

**Vehicles.** Their lighting is GE work: 8,789 indices a vehicle a frame times 2–4 lights. A cheaper vehicle light (fewer lights on its far parts, or the distant lights only) is a GE question for a PSP pair, not a CPU lever.

### 5. Still open

- Read 41906956252 (the pass's work per light) and 41904126863 (the shares); fill §2's pass table and §3.
- The one PSP pair (recon's to queue): d20 swamp, flashlight on, `keepsets="- | nopointlights"`, repeat 2: the models' two point lights off (the GE's per-vertex point lighting and the dynamic preparation's walk), `gebusy2 hwprofile` for the GE's and the CPU's halves. It prices the per-vertex cost a GE spotlight would add to. Only if §3 can't settle it first.
