# The Lights box

> **Draft v0 (2026-11-15 15:1x).** Drafted by the worker (or-job 43746134830, DeepSeek; the lead 11:43 asked for the page from the tables), checked by the audit. **Checked and right:** LSPLITCOARSE's piece factor 0.6 / floor 0.03 (`psp/src/psp_ge.c:596`); d20's light draws 63.5 a frame at 205.9 us each (the census run 43331666648's log); `lights_render_diffuse` 7.76 ms a frame in a d20 window (its profile.txt:197); the T1 / T2 rows (`light_t1_d20_flashlight`, `light_t2_parts_d20_flashlight`). **Corrected:** one citation (the LSPLITCOARSE pair's figures are its levers.tsv row, not measurements.tsv:12764). **Not checked line by line:** §1's engine walk and §3's lever table beyond the rows above. The worker's own weak points are §8; the 3.0 ms ME allocation is its proposal, not a ruling: the lead sets the budget. Paths written `sierra/...` and `run-results/...` are the worker's workspace names for this repo and the lab's results branch.


**Unfinished in places; marked.** Turns ran out in the middle of the last batch — I have the light-pass ground, the lever rows and both census runs read, but I did **not** get a look at `run-results/` for the d20/c20 *flashlight* runs (they are not in the workspace; see §6 Q6), and I did not read the engine's `lights_find_for_object_dynamic` / `lights_prepare_for_object_*` themselves (only their lever rows and the capacity model).

---

## 0. Findings (read this first)

| # | Claim | Label | Where |
|---|---|---|---|
| 1 | The Lights box is **already a box in REBUILD §3.3 and has no page**; the page's scope is the level's dynamic lights **and** the flashlight, on both units | [D] | `research/opportunities/REBUILD.md:101`, `sierra/docs/box_draw_list.md:7` |
| 2 | **The ME has no separate "light" path.** A light draw is a recorded call (`DEFER_LIGHT_DRAW`) replayed by the ME's render job and timed inside it as kind **"light draws"** — the box's ME budget is inside the draw list's render job, not beside it | [D] | `sierra/psp/src/psp_ge.c:24103`, `:1824`, `:1590` |
| 3 | A light draw's cost is **the split** (59–65% of the light draws, T2) — not B7 (~20%, T1) — because build 103's per-corner falloff forces a split on big triangles | [M] | `research/opportunities/light_pass.md:162-163`, `:202-205` |
| 4 | **Best priced lever so far: LSPLITCOARSE at 0.60, in and on by default** — ME light draws −13% / −25 to −28% on d20's flashlight (−5.5 / −9 to −10.1 ms a frame) | [M] | `psp/sierra/findings/levers.tsv:193`; `sierra/psp/src/psp_ge.c:906` |
| 5 | **Upstream ships a dynamic-light limit the port does not have**: `port_dynamic_lights_choose`, `HALO_CONSOLE_LIGHT_LIMITS`, 8 diffuse / 2 specular. The PSP build does **not** define it, and its default is 0 (the Xbox's "every one") | [D] | `porttree/source/objects/object_lights.c:2355-2431`, `porttree/port/linux/src/port_config.c:211-217`, `sierra/psp_build.py:435-441` |
| 6 | The main CPU's light work is **selection**, not the pass: on d20 7.76/8.50 ms a frame in two windows (`lights_render_diffuse` 11.5 / 15.3% of the frame) | [M] | `run-results/sierra-hw/43331666648/profile.txt:197`, `:262` |
| 7 | The **flashlight splits on no PSP run in the workspace** (0 split triangles, 0 pieces in every 300-frame line of both census runs) — today's own numbers for the flashlight come only from the d20 flashlight runs of 2026-11-10/11 | [D, the runs] | `run-results/sierra-hw/43331666648/log.txt:1830`, `run-results/sierra-hw/43314261608/log.txt:2123` |
| 8 | **No `psp/boxes/lights.upstream` exists, and no box page of any kind yet** — §5b's manifest has to be written with the page | [D] | `sierra/psp/` listing (only `presets.txt`), no `*.upstream` found |
| 9 | Measured dynamic lights a frame: **d20 up to 10.4 mean / 15 the most** (one window), **c10 beach 1.5 mean**, c20 0.5–2.5 | [M] | `run-results/sierra-hw/43331666648/log.txt:1809`, `:3432`, `run-results/sierra-hw/43314261608/log.txt:4919` |
| 10 | `lights_preprocess_scene` (the box's main-CPU selection) sits **before** the ME's collect and before `lights_render_diffuse`; the ME's answer for visibility is collected at its start (`psp_mevisland_collect` is called just before it) | [D] | `porttree/source/render/render.c:522-525`, `porttree/source/objects/object_lights.c:1410` |

---

## 1. What the engine and the port do for a light, each frame

### 1a. Main CPU — the engine's two passes

**(i) `lights_preprocess_scene`** — `porttree/source/objects/object_lights.c:1353`:

```c
1366		lights_port_recover();
1368		plasma_lighting_update();
1410		lights_globals.scene_point_light_count = structure_visibility_find_objects(
1419		rasterizer_lights_begin();
```
Per light in the scene (`:1429-1677`): its intensity/colour from its object's function values (`:1459`), its radius (`:1548`), its marker placed as drawn (`:1593-1609`), the flashlight centred on the weapon (`first_person_weapon_center_flashlight`, `:1619`), `rasterizer_light_submit` (`:1647`), the closed-door filter (`:1671`). On d20 this step is **0.63–1.51 ms a frame** in the census windows [M, `run-results/sierra-hw/43331666648/profile.txt:15`, `:202`]. **On the PSP the Light effects are compiled out** (`#define HALO_CONSOLE_NO_LIGHT_EFFECTS 1`, `sierra/psp_limits.h:43`), so `plasma_lighting_update()`'s and `plasma_lighting_prepare()`'s bodies are the port's no-ops — [U, from the define; the no-op bodies themselves are `port/linux/game/plasma_lighting.c`, not on the port's compile path].

**(ii) `lights_render_diffuse`** — `porttree/source/objects/object_lights.c:2448`:
```c
2454		port_dynamic_lights_choose();
2468				&& port_dynamic_light_kept[light_index])
2480					cluster_count = light_build_cluster_array(
2502					structure_render_diffuse_light(
```
Per kept dynamic light: its clusters, its bounding sphere, then `structure_render_diffuse_light` (`porttree/source/structures/structure_render.c:1609`), which builds the surface list (`:1630` `structure_visibility_build_surfaces`), builds the dynamic triangle list (`:1644`), and runs `structure_render_pass` (`:1665`) with `rasterizer_environment_diffuse_light_draw` per surface/lightmap/material. `lights_render_specular` returns at once on the PSP (`SKIPPED_PASSES`, `sierra/psp_fixups.py:3713`).

**What the port replaced in (ii):** the diffused light's *begin* and *draw* are cut over to `psp_ge_light_begin` / `psp_environment_light` → `psp_ge_light_draw` (`sierra/psp_fixups.py:6254`, `:6281`). So on the PSP the engine's per-material walk survives, and the pass's own body is the port's.

### 1b. Main CPU — the record, and where the work really goes

With the campaign defaults (`gedefer` `geinflight` `gemeon`, `sierra/psp/src/psp_platform.c:1200-1202`), every light draw is **written down**: `psp_ge_light_draw` (`sierra/psp/src/psp_ge.c:24099`):
```c
24103	GE_DEFER(defer_words(DEFER_LIGHT_DRAW, 5, DEFER_P(vertices), ...));
```
`GE_DEFER` returns immediately while recording (`:642`, `:638`), so **on the main CPU a light draw costs the record's words, not the pass** — 5 words plus the pointers; the pass runs later, on the ME.

### 1c. The ME — `psp_ge_light_draw_body`, step by step

`psp_ge_light_draw_body` `sierra/psp/src/psp_ge.c:23459`:
1. **Set-up** — stream find (`:23484`), the bake's pieces (`:23496`), the texture bind (`:23507`), the cone's tangent and matrix (`:23519-23520`). T2: **3%** of the light draws.
2. **Per triangle:** a cone rejection first (`outside_cone`, `:23598`); then the split decision — any edge past the piece size (`light_edge_splits`, `:23636`), `split = light_piece_factor * nearest * tangent` (`:23103`); the reach test (`:23649`); `lviewcull`'s facing/view test (`:23651`); then `light_split` or `light_split_carry_root` (`:23680-23682`).
3. **Reach and gel per corner**, memoised across the draw's vertices (`light_memo`, `:23687-23712`, gel `:23779-23810`); the spot's behind-the-light clip (`:23811-23848`).
4. **Emit** into `scratch_vertices` (`light_emit_known`, `:22826`), cut pieces into `light_pieces` (`:22855`).
5. **After the loop** (`:23974-24083`): the kept triangles into up to 7 depth bands (`LIGHT_BANDS 6` + the exact ones, `:22873`), each **copied** into the list (`memcpy`, `:24026`) and drawn with its own depth nudge; then the cut pieces. `lightmerge` replaces the bands with one draw (`:23942-23973`).

T2's split of those steps on d20's flashlight [M, `psp/sierra/findings/measurements.tsv:12739`]: **cone cull + split 28.7 / 17.0 ms a frame (59 / 65%)**, reach+memo 5.9 / 2.0, lit corners+gel 2.3 / 0.8, clip+emit 2.8 / 1.1, set-up 1.6 / 0.9, **after the loop (bands, copies, draws) 7.7 / 4.2 (16%)**.

**What the ME's copy cannot tell it:** a light's draw is kind `DEFER_LIGHT_DRAW` and `psp_ge_light_begin`/`_gel` are kinds of their own (`:1590`, `:1596`); the mekinds line is the box's ME budget line, and on d20's census the light kind is **6.2 ms a frame mean, 15.4 the most** (`sierra/docs/box_draw_list.md:336`).

---

## 2. The costs already measured (each with its run, device, map and stretch)

### 2a. The ME, by kind, per census run (today's build)

| Map / run | Light draws, ms a frame (mean, most) | Device, build | Stretch |
|---|---:|---|---|
| b30 beach | 0.8 (0.2–13) | PSP-1000, `sierra-hw 43449814663` | beach, `box_draw_list.md:301` |
| d20 | **6.2 (15.4)** | PSP-3000, `43331666648`, tock `f50359fa` | `d20_play1`, 37 windows, `box_draw_list.md:336` |
| c20 | **4.2 (16.5)** | PSP-3000, `43314261608`, tock `9d00a903` | `c20_play1`, `box_draw_list.md:336` |
| b40 | 1.4 (4.9) | PSP-3000, `43314233107` | `box_draw_list.md:336` |
| a30 | 0.2 | PSP-3000, `43318228389` | `box_draw_list.md:336` |

The same two runs' own words, **per draw**: d20 `light draws 13072 (63.5 a frame, 205.9 us each, worst 1036)` (`run-results/sierra-hw/43331666648/log.txt:1866`); c20 `light draws 16495 (68.8 a frame, 239.7 us each, worst 4390)` (`run-results/sierra-hw/43314261608/log.txt:4977`). **The per-window spread is 5.2 a frame at 253.5 µs to 63.5 a frame at 205.9 µs** on d20 (`:930` vs `:1866`). [D, the logs; the summary table is [M]]

### 2b. The flashlight on d20 (the box's binding case)

| Figure | Value | Run / label |
|---|---|---|
| Light draws, two windows | 28.2 / 47.9 ms a frame; 108 / 94 draws at 260 / 511 µs | `c4_replay_by_kind_d20_flashlight`, PSP-3000 tock `88d3e33c` [M], `light_pass.md:19` |
| Same route before the flashlight | light draws **1.3 ms** | same row |
| T1: off / `lightconetex` / `nolightpass` | light draws 53.3/41.9, 41.2/32.9, 0.1–0.3 | `light_t1_d20_flashlight`, PSP-3000, `sierra-hw 42927485330` [M], `measurements.tsv:12738` |
| T2: the split's share | 59% / 65% | PSP-3000, `42929566682` [M], `measurements.tsv:12739` |
| LSPLITCOARSE 0.60 vs 0.30 | 36.8/25.9 against 42.3/36.0 ms | PSP-1000, `42950287966` [M], `levers.tsv LSPLITCOARSE row (the same run 42950287966; measurements.tsv:12764 is that run's statecrc row)` |
| LVIEWCULL | −1.9 / −2.6 to −3.7 ms (−4 to −9%) | PSP-1000, `42937159018` [M], `levers.tsv:191` |
| LMERGE (rejected) | +1.5 ms (+3.6 / +4.2%) | PSP-1000, `42945712235` [M], `levers.tsv:186` |
| LSPLITMEMO (rejected) | +3.5 / +6.3 ms | PSP-3000, `42933704266` [M], `levers.tsv:189` |
| LIGHTAFFINE (rejected) | +0.5 / +1.4% | PSP-3000, `42942214489` [M], `levers.tsv:192` |
| MEXECL | −0.6 / −0.1 ms (−1.5%) | PSP-1000, `mexec_r2_d20_flashlight` [M], `levers.tsv:185` |

**Which unit binds:** T1's note says the light pass "binds d20's flashlight frame (the ME's job is the frame's floor there)" [M, `measurements.tsv:12738`]. But `d20_flashlight_frame_tock` corrects T1's frame figure: the frame by the clock was **62–117 ms** and **ticks 22–24/s — the main CPU binds** [M, `light_pass.md:173`]. Both statements are in the record; the later one (11-11 02:2x) is the one that stands, and it is why LSPLITCPU is held (`levers.tsv:190`). **Do not read the box's d20 line without this.**

### 2c. The main CPU

| Step | d20 | c20 | Source |
|---|---:|---:|---|
| `lights_render_diffuse` | **2.4** (mean of windows, 6.0, 7.76 and 8.50 among them) | 1.1 | `drawing_me_ledger.md:127` [M]; windows `43331666648/profile.txt:197`, `:262`; `43314261608/profile.txt:462` |
| `lights_preprocess_scene` | 1.22 / 1.10 (2.0–2.1% of the frame) | 0.52–0.60 | `43331666648/profile.txt:202`, `:267`; `43314261608/profile.txt:604` |
| per light, per call (`us/call`) | 7,763 / 8,696 if one call a frame | ~850–1,690 | `43331666648/profile.txt:197`, `:262`; `43314261608/profile.txt:462` |
| Relinks (`light_reconnect_to_map`) | 3.36% of the tick | 1.92% | [E, cache model] `capacity_model.md:93-94, 102` |

**Caveat on `us/call`:** `lights_render_diffuse` is called once a frame (5 calls over 11.3 ticks a frame in window 1), so the 7.8–8.7 ms a frame figures are real ms, not per-call noise [D, `calls/s` column].

**What is NOT in the tables:**
- **The main CPU's cost of a light *record*** (the `GE_DEFER`/`defer_words` copy). No figure. **CAN'T TELL** — no census splits it; it was searched for in both `profile.txt`s and neither has a step for it.
- **The ME's light draws for b30 across the whole run** — only the beach's window (0.8 ms) is published; the d20 and c20 census windows above are *not* smartphone flashlight views.
- **`lights_render_diffuse` on d20 at its heaviest** — the mean of the ledger (2.4) and the window figures (7.76 / 8.50) disagree by 3×; I have not resolved which stretch each window is. [U] the ledger's mean is the whole run, the 8.50 window is a lit scene.

---

## 3. Every earlier lever on this ground (price, why it stopped, revive_if)

Levercheck's ground is `lighting` (`tools/levercheck.py:44`); rows below are `psp/sierra/findings/levers.tsv` and `grounds.tsv`.

| Lever | What | Price (device, run) | State, why it stopped | revive_if |
|---|---|---|---|---|
| **FLC / lightcarry** | cone split carries each corner's work down the halvings | d20 flashlight drawing 43.9 → 33.7 ms (−23.2%), frame −10.0% [M, PSP-3000 `41781771922`]; b30 bunker drawing −14% [`41788564764`] | **in, on by default** (tock158) | — |
| **LVIEWCULL** | a triangle the split would cut to nothing is skipped first | light draws −4 / −7 to −9% [M, PSP-1000 `42937159018`] | **in, on by default** (tock162 8d812b5) | — |
| **LSPLITCOARSE** (`lightpieceN`, `lightleastN`) | fewer, larger pieces (0.60) | light draws −13% / −25 to −28% (−5.5 / −9 to −10.1 ms) [M, PSP-3000 `42946137778`]; pieces −48/−71% [E `lightpiece1_shots`] | **in, on by default at 0.60**; **Sheepdog01's hand run can still veto** | Sheepdog01 vetoes → `lightpiece30` |
| **LIGHTRINGLOOK** | the ME serves the ring inside the light pass | lates 12/12 → 0/0 [M, PSP-3000 `41807114492`] | **in** (tock159) | — |
| **MEXECL** | the light record's functions in eDRAM | light draws −0.6 / −0.1 ms (−1.5%) [M, PSP-1000 `mexec_r2_d20_flashlight`] | built; **not bound by code misses**, so it bought ~1.5% | a light draw shown to miss in code; not met |
| **LSPLITMEMO** | the split's shared corners memoised | **+3.5 / +6.3 ms slower** [M, PSP-3000 `42933704266`] | **REJECTED** — the 47 KB table is 3× the ME's D-cache | the draw-wide table **in the ME's eDRAM** (27 cycles a line under load against 77), corners copied to the stack, priced after LVIEWCULL and LFPUSCHED (`grounds.tsv:189`) |
| **LFPUSCHED / LIGHTAFFINE** | the affine carry of a midpoint's values from its parents | **+0.5 / +1.4% — no gain** [M, PSP-3000 `42942214489`] | **REJECTED as built** | the halvings shown instruction by instruction to cost the corner arithmetic it replaces (`grounds.tsv:192`) |
| **LMERGE** | one draw a record, corners lifted on the ME | **+1.5 ms (+3.6 / +4.2%)** [M, PSP-1000 `42945712235`] | **REJECTED** as built | a lift the **GE** makes, or the list-direct half alone (`grounds.tsv:186`) |
| **LDIRECT** | corners written straight into the list (bands kept) | ceiling **0.3–0.7 ms of the ME** [U, from `light_bands_singleband_share`: single-band records hold 80% / 44% of d20's copy] | **parked 2026-11-11** — under 1% of d20's 62–117 ms frame, and the allocator must give back a block's tail | the ME binds a flashlight stretch where single-band records carry most of the copy **and** a ledger shows it over ~1 ms (`grounds.tsv:559`) |
| **LSPLITPX** | a piece stops splitting under N px | — | **STOPPED** — no kept piece under 4 px, 3–6% under 16 [E, `light_t3_counts`] | a level spotlight seen from afar (`grounds.tsv:184`) |
| **LSPLITCPU** | the split on the main CPU's VFPU | its room was b151's 18.9 ms of main-CPU sleep [M]; frame −10 to −13% at most [U] | **HELD 2026-11-11** — on the tock d20's tick is 35–43 ms, so the main CPU binds | the two-core tick or AITICKOLD brings the tick under real time and a ledger shows the main CPU waiting again (`grounds.tsv:190`) |
| **LMC** | a still light's lit list kept | d20 swamp 2×2: frame +2.8 to +7.0% (noise), drawing +7.9%; **the ME's job no shorter** [M, PSP-3000 `42409934446`] | **PARKED under R** | under R's light slice, priced by the ME's job time (`grounds.tsv:295`) |
| **LPLAN** | a light's surface plan kept | d20 drawing −2.0 ms, tick +0.9, **net 0** [M, PSP-3000 `40138280704`] | open, built off (`lightplan`) | (the flashlight moves every frame) |
| **LM** | lights merged per surface | 15.1 ms of d20 swamp's light draws on the October run; **c4 reads 1.3 ms today before the flashlight**; reconciled 19:5x: window 3 (the swamp's own point lights) 19.5 ms, 64% of triangles drawn twice | open, sized, not started | — |
| **LR** | runs of 16 triangles skipped by sphere | 21% of d20's triangles sit in runs wholly beyond | proposed, small | — |
| **LB** | a level draw's whole loop skipped | d20 19 draws a frame, drawing −0.1% | **rejected, worth ~0** | — |
| **LFL1** | one bucket pass in place of 7 band copies | the band loop's post time unmeasured | idea | — |
| **PROJLIGHT / LBRIGHT** | cone × gel × falloff projected per pixel | LBRIGHT's ceiling corrected: B7's ~20% **plus the copies**, not most of the pass | idea; **its original "destination alpha" claim is impossible** — the framebuffer is 5650 (`psp_ge.c:4804` per `light_pass.md:58`) | Sheepdog01 opens compromises (`grounds.tsv:481`) |
| **LGELTOP** | the gel per pixel over B7's pool | look; ME less (no per-corner gel), GE +1 draw | idea | Sheepdog01's side-by-side |
| **GELF / GELC** | a gel by its six faces / its own cube | GELF: hallway excess red 0.90 vs 0.15 avg (PC 16.69/4.19) [E]; GELC: −0.63 cube, +2.2–2.6% frame where alert lights are in view [M, `40585239312`] | GELC **in** (restoration); GELF superseded | — |
| **DAL** | a black attached light never linked | 5.2 / 5.0 / 29.6 / 9.4 relinks a tick removed | **HELD** — a10 not picture-exact (81 systematic frames of 4,490) | the missing a10 light found (game time 1,268 and 4,728–5,326), then a shown gain (`grounds.tsv:213`) |
| **LSAME** | a relink skipped on an unchanged light | b30 beach frame −2.2%, tick −2.3% [M, PSP-1000 `42934331131`] | **in, on by default** | — |
| **LF** | light fixtures sleep | no gain once LD1 is in | **dead** | fixtures still costly after LD1 (`grounds.tsv:238`) |
| **LIGHTCACHE** | the render-state cache's 256 entries | unknown | open, a miss counter first | — |
| **SHADOWWALK** | the shadow casters' level walk | sized, can't pay | not built | — |
| **`HALO_CONSOLE_LIGHT_LIMITS`** (upstream's, the *only* dynamic-light limit anywhere) | 8 biggest by radius/distance redraw the level, 2 add specular | upstream's own: 6 lights = 11.3 ms GPU specular + 3.7 diffuse **on the Switch** | **not built on the PSP; not a lever started outside this box** (`light_pass.md:339`) | — |

**What this list says:** four arithmetic and layout tries on the split (LSPLITMEMO, LIGHTAFFINE, LMERGE, MEXECL) bought ~0 or lost; the three that paid (FLC, LVIEWCULL, LSPLITCOARSE) all **removed work** from the split — which is exactly means B ("work on change or on need", `sierra/CLAUDE.md:59`).

---

## 4. A proposed budget per unit, against d20's and c20's figures

REBUILD §3.4's drawing budget is `main CPU 11 ms · ME ~12 ms · GE 33.3` (`REBUILD.md:109-116`; `box_draw_list.md:26-30`). The lights' share of it is not set anywhere. The proposal below is **mine** and every ceiling is [U].

### 4a. What the box must fit, from the tables

| Unit | d20 today (census mean → most) | c20 today | Allocation proposal |
|---|---:|---:|---:|
| **Main CPU** — `lights_preprocess_scene` + `lights_render_diffuse` | 2.4–8.5 (worst window 8.50) | 1.1–1.7 | **2.5 ms**, and **≤1.5 ms** once the light limit is in |
| **ME** — the light draws, inside the render job | 6.2 (15.4) | 4.2 (16.5) | **3.0 ms mean, 5.0 at the most** |
| **Per light** (ME, mean): 6.2 / ~8.6 lights [U, the counts below] | **~600–800 µs a light** | ~2.5 ms a light at 1.7 lights [U] | **≤300 µs a light** [U] |
| **Per light** (main CPU selection) | 7,763 / 8,696 µs a call [D, the profile] — one call a frame | 850–1,690 | **≤1,000 µs a light** [U] |
| **Per light draw** (ME) | 205.9 µs (63.5 draws) over the run; 511 worst-case mean on the flashlight | 239.7 µs (68.8 draws) | **≤150 µs a draw** [U] |
| **GE** | not in the box: the pass is a GE redraw of the level's own triangles; `lights_render_specular` returns at once | — | — |

**The count the per-light figures rest on.** The engine's `lights:` line is the one count that exists, and it counts **dynamic lights with a rasterizer light index** — `run-results/sierra-hw/43331666648/log.txt:1809`: `lights: 10.4 dynamic lights a frame (15 the most), 0.0 of them the first-person weapon's`; c20's heaviest: `2.5 dynamic lights a frame (5 the most), 1.4 of them the first-person weapon's` (`43314261608/log.txt:4919`). **The light *draws* are not that count** (63.5 draws for ~10 lights on d20). The draws-per-light ratio is not published for these runs — **CAN'T TELL**. The capacity model's old census gives it: **~10–16 draws a light** (`capacity_model.md:114`, d20 machines). At 10–16, d20's 6.2 ms over 10.4 lights is **~600–800 µs a light** [U on M].

### 4b. What each of the three means buys here

**Means A — data packed for the walk.**
- The light draw's data is the **level's own vertex array**, read through `stream->base[triangle[corner]].x` (`psp_ge.c:23597`) with a packed normal beside it (`:23621`) and the memo keyed by the vertex's low bits (`LIGHT_MEMO 256`, `:23694`). Already the level's packed stream — the box cannot repack what it does not own.
- **What is left for A:** the split's scratch (`scratch_vertices`, 480 triangles, `:21756-21757`) and the cut buffer (`MAXIMUM_CUT_CORNERS 384`, `:6932`); LDIRECT was the one attempt at removing the scratch→list copy and it was parked at **0.3–0.7 ms** [U].
- **Ceiling [U]: ~0.5–1 ms of the ME a frame on d20.** Modest, and it is already one lever's ground.

**Means B — work on change or on need.** This is where every paying lever of the last month landed:
- **On need (fewer of them made):** the dynamic-light limit — 8 biggest by radius over distance. Measured today: d20's most is **15**, c20's is **5**, so the limit would cut d20 by up to ~7 lights in its peak windows and c20 by none. It departs from the Xbox's behaviour at 0, so it is a **look call** under Sheepdog01's far-and-tiny rule (`light_pass.md:339`); the cost of building it is one integration (`port_dynamic_lights_choose` already exists upstream). **Ceiling [U]: the main CPU's selection is linear in `scene_point_light_count`, so half the lights is ~half of 8.5 = −4 ms on the main CPU at d20's peak**, and the ME's light draws by the draws those lights reached (ratios unpublished — **CAN'T TELL**).
- **On change:** LMC is this means and was parked only because it paid nothing before R. Its own numbers: served 59% of d20's light draws and 48% of a10's from kept results [E, `levers.tsv:295`] — but the PSP pair showed **the ME's job no shorter** (16.0–16.1 off, 16.5–19.1 on). **Revive_if stands: price it where the ME binds.**
- **LSPLITCOARSE is the same means reached from the other side** — fewer pieces, same look — and it is the only measured large win.
- **Ceiling [U]: 2–5 ms of the ME on d20's flashlight already delivered (FLC + LVIEWCULL + 0.60); the light limit's −4 ms of the main CPU unsized on the ME; LMC unpriced under R.**

**Means C — both cores on separate data.**
- The light pass is **already** on the ME; the main CPU's side is selection. The only split this means could make is LSPLITCPU, and it is held because the main CPU binds (ticks 22–24/s) [M].
- **Ceiling [U] today: ~0. The room appears only with the two-core tick.**

### 4c. Against the frame's own figures

`drawing_me_ledger.md:164` calls the lights **"the largest unowned ME row on d20 and c20"** and §5 item 3 asks for this page: *"d20 late's ME doesn't close on paper without it (~2.5–4 ms short after §1b)"* (`:190`). At the budget above (3.0 ms mean against a 6.2 mean) **the box has to find ~3.2 ms of the ME on d20 and ~1.2 on c20** — and ~2.5–4 ms is exactly d20 late's shortfall. So the box is the stretch's answer only if its means reach ~3 ms, which means B must hold.

---

## 5. What it replaces (the upstream manifest's line, for §5b)

Engine entry points the port already cuts over — this is `psp/boxes/lights.upstream`'s content, which does **not** exist yet [D]:
- `lights_render_diffuse` — `porttree/source/objects/object_lights.c:2448` (the selection loop survives; the draw is cut over)
- `structure_render_diffuse_light` — `porttree/source/structures/structure_render.c:1609` (survives; its draw callbacks go to the port)
- `_rasterizer_environment_diffuse_light_begin/_draw/_end` — `sierra/psp_fixups.py:6254`, `:6281`, `:6289`
- `rasterizer_light_submit` — `porttree/source/rasterizer/rasterizer_lights.c:1179` (survives)
- `lights_render_specular` — skipped whole (`psp_fixups.py:3713`)
- Gel: `#lightgel` cuts the begin's call (`psp_fixups.py:33181-33246`), the gel's frame is `#gelframe` (`:33259`)
- `lights_preprocess_scene` (`porttree/source/objects/object_lights.c:1353`) is **not** cut over; the port also has `psp_lights_defer/flush` around the relink (`FIXES_LD1`, `psp_fixups.py:30069-30131`) and `FIXES_LSAME` (`:29841`)
- **The port's own port-only code in this ground:** `sierra/psp/src/psp_ge.c`'s `psp_ge_light_begin/_gel/_draw/_draw_body`, the memo, the split (two forms), the bands, `lightmerge`, `LIGHT_RESERVE` 64 KB (`:3118`).

---

## 6. Proof (how its output is shown right)

- **Pictures, not words:** the box replaces a pass, so the test is pictures and play, never word identity (`sierra/CLAUDE.md:65`).
- Every shipped lever here has its own proof on this ground: FLC — 0 mismatches in 118,784 + 12,288 + 12,288 split triangles [E, `levers.tsv:465`]; LVIEWCULL — play and both seeds identical on d20, b30, c10 [E, `lv…`]; LSPLITCOARSE — play and seeds identical at 12,000 ticks, 14 of 48 shots identical, at most 6 pixels a shot differ by more than 16 [E, `light_pass.md:318-320`].
- **The pair must run the old build against the new one**, each its own build (`REBUILD.md:141`), on Sheepdog01's pad of the box's stretch (d20's flashlight **and** the d20 census stretch, plus c20's play).
- **The census is the outcome measure:** mekinds' light-draw kind, the `lights:` count and the profile's two steps already exist and need no new instrument [D]. **What does not exist and is needed:** a main-CPU step for the record (`GE_DEFER`), because the box's cheapest lever (the light limit) shows on the main CPU and today nothing times a light record.

---

## 7. The counts and splits that are missing, each as a question with its decision

1. **How many light draws a light makes, on d20 and c20, today?** — counts the light-limit lever and gives the per-light budget its true denominator. *Decision:* whether the limit is built at 8, at a size threshold, or not at all. (The one existing census is the October engine code: ~10–16 draws a light, `capacity_model.md:114`.)
2. **What does a light *record* cost the main CPU (`psp_ge_light_draw`'s `defer_words`)?** — no figure anywhere; searched both census `profile.txt`s. *Decision:* whether the box's main-CPU row is selection or records, and therefore whether the record belongs in this box or the draw list's.
3. **Does d20's flashlight have a 300-frame window on the census build with the split running?** Both runs in the workspace show **0 split triangles, 0 pieces** — so today's per-draw flashlight figures come from 2026-11-10/11 builds only. *Decision:* whether the box's next PSP pair can be a flashlight pair on today's tock, or must re-measure the split's share first.
4. **How much of d20's 6.2 ms mean is the *swamp's own point lights* (window 3) and how much the flashlight?** T1/T2 give both views on the 11-10 build; the census mean does not split them. *Decision:* the box's first slice — the light limit (point lights) or the split (flashlight).
5. **LMC's price on the ME-side of a stretch where the ME binds.** Its pair was on a build where the ME's job didn't shorten [M]. *Decision:* whether LMC is revived under R or stays parked; it is the box's only "on change" lever with a measured serve rate (59% of d20's light draws).
6. **Where are the d20/c20 *flashlight* census runs' results?** `run-results/` holds only `43331666648` (d20 play) and `43314261608` (c20 play); the flashlight runs named in `light_pass.md`/`measurements.tsv` (e.g. `42927485330`, `42929566682`, `42950287966`) are **not in the workspace**. **CAN'T TELL** whether their logs would add a per-window split. *Decision:* whether the box's d20 line is quoted from the 11-10/11-11 pair or must be re-run.
7. **Does the PSP's `MAXIMUM_RENDERED_LIGHTS` (128, `sierra/psp_capacity.h:154`) ever bind?** The engine's choose arrays are sized by it (`object_lights.c:2367-2368`) and the count reaches 15. *Decision:* none today — but the box's data section (§2) must state it, and it is 128 booleans at 2 arrays.
8. **The port's specular:** `lights_render_specular` returns at once, so **modes/specular on d20's shiny surfaces cost the box nothing** and are a look restoration, not a lever (`light_pass.md:310-315`). *Decision:* whether the box owns that restoration or the catalogue does.

---

## 8. Where a claim here is weak, said plainly

- **The 3.0 ms ME allocation is mine** [U]. It is drawn from the box's need to find ~3.2 ms on d20 (`drawing_me_ledger.md:190`) and from the four tries that bought ~0 — not from any priced form that reaches it.
- **The light limit's −4 ms on the main CPU** is arithmetic on 8.5 ms and a count of 15 [U on M]; the size-threshold form Sheepdog01 would accept is not sized.
- **"~600–800 µs a light"** depends on the draws-per-light ratio from a 2026-11-04 census of the *engine* build, not today's tock [U].
- **The T1 vs `d20_flashlight_frame_tock` conflict on which unit binds d20's flashlight** is recorded above and **not resolved**; the later reading (main CPU binds) is the one I used — if it is wrong, LSPLITCPU reopens and the box's ME ceiling rises by 10–13 ms of a 91 ms frame.
