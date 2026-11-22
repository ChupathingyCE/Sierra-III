# The Lights box

> **Draft v0 (2026-11-15 15:1x).** Drafted by the worker (or-job 43746134830, DeepSeek; the lead 11:43 asked for the page from the tables), checked by the audit. **Checked and right:** LSPLITCOARSE's piece factor 0.6 / floor 0.03 (`psp/src/psp_ge.c:596`); d20's light draws 63.5 a frame at 205.9 us each (the census run 43331666648's log); `lights_render_diffuse` 7.76 ms a frame in a d20 window (its profile.txt:197); the T1 / T2 rows (`light_t1_d20_flashlight`, `light_t2_parts_d20_flashlight`). **Corrected:** one citation (the LSPLITCOARSE pair's figures are its levers.tsv row, not measurements.tsv:12764). **Not checked line by line:** §1's engine walk and §3's lever table beyond the rows above. The worker's own weak points are §8; the 3.0 ms ME allocation is its proposal, not a ruling: the lead sets the budget. Paths written `sierra/...` and `run-results/...` are the worker's workspace names for this repo and the lab's results branch.


**Unfinished in places; marked.** Turns ran out in the middle of the last batch — I have the light-pass ground, the lever rows and both census runs read, but I did **not** get a look at `run-results/` for the d20/c20 *flashlight* runs (they are not in the workspace; see §6 Q6), and I did not read the engine's `lights_find_for_object_dynamic` / `lights_prepare_for_object_*` themselves (only their lever rows and the capacity model).

---

## 0. Findings (read this first)

| # | Claim | Label | Where |
|---|---|---|---|
| 1 | The Lights box is **already a box in REBUILD §3.3 and has no page**; the page's scope is the level's dynamic lights **and** the flashlight, on both units | [D] | `projects/sierra/research/opportunities/REBUILD.md:101`, `sierra/docs/box_draw_list.md:7` |
| 2 | **The ME has no separate "light" path.** A light draw is a recorded call (`DEFER_LIGHT_DRAW`) replayed by the ME's render job and timed inside it as kind **"light draws"** — the box's ME budget is inside the draw list's render job, not beside it | [D] | `sierra/psp/src/psp_ge.c:24103`, `:1824`, `:1590` |
| 3 | A light draw's cost is **the split** (59–65% of the light draws, T2) — not B7 (~20%, T1) — because build 103's per-corner falloff forces a split on big triangles | [M] | `projects/sierra/research/opportunities/light_pass.md:162-163`, `:202-205` |
| 4 | **Best priced lever so far: LSPLITCOARSE at 0.60, in and on by default** — ME light draws −13% / −25 to −28% on d20's flashlight (−5.5 / −9 to −10.1 ms a frame) | [M] | `projects/sierra/findings/levers.tsv:193`; `sierra/psp/src/psp_ge.c:906` |
| 5 | **Upstream ships a dynamic-light limit the port does not have**: `port_dynamic_lights_choose`, `HALO_CONSOLE_LIGHT_LIMITS`, 8 diffuse / 2 specular. The PSP build does **not** define it, and its default is 0 (the Xbox's "every one") | [D] | `porttree/source/objects/object_lights.c:2355-2431`, `porttree/port/linux/src/port_config.c:211-217`, `sierra/psp_build.py:435-441` |
| 6 | The main CPU's light work is **selection**, not the pass: on d20 7.76/8.50 ms a frame in two windows (`lights_render_diffuse` 11.5 / 15.3% of the frame) | [M] | `run-results/sierra-hw/43331666648/profile.txt:197`, `:262` |
| 7 | The **flashlight splits on no PSP run in the workspace** (0 split triangles, 0 pieces in every 300-frame line of both census runs) — today's own numbers for the flashlight come only from the d20 flashlight runs of 2026-11-10/11 | [D, the runs] | `run-results/sierra-hw/43331666648/log.txt:1830`, `run-results/sierra-hw/43314261608/log.txt:2123` |
| 8 | **No `psp/boxes/lights.upstream` exists, and no box page of any kind yet** — §5b's manifest has to be written with the page | [D] | `sierra/psp/` listing (only `presets.txt`), no `*.upstream` found |
| 9 | Measured dynamic lights a frame: **d20 up to 10.4 mean / 15 the most** (one window), **c10 beach 1.5 mean**, c20 0.5–2.5 | [M] | `run-results/sierra-hw/43331666648/log.txt:1809`, `:3432`, `run-results/sierra-hw/43314261608/log.txt:4919` |
| 10 | `lights_preprocess_scene` (the box's main-CPU selection) sits **before** the ME's collect and before `lights_render_diffuse`; the ME's answer for visibility is collected at its start (`psp_mevisland_collect` is called just before it) | [D] | `porttree/source/render/render.c:522-525`, `porttree/source/objects/object_lights.c:1410` |
| 11 | **On today's tock the ME spends more than half of d20's render job on light draws: 31.4 ms a frame of 56.8**, 163 draws a frame at 193 us each, once the level opens up (1.4-2.7 ms in the opening). The largest single kind on the ME there; level draws are 13.8 ms | [M] | §10; the lab's row `diet5_mekinds_d20_3000` |

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

T2's split of those steps on d20's flashlight [M, `projects/sierra/findings/measurements.tsv:12739`]: **cone cull + split 28.7 / 17.0 ms a frame (59 / 65%)**, reach+memo 5.9 / 2.0, lit corners+gel 2.3 / 0.8, clip+emit 2.8 / 1.1, set-up 1.6 / 0.9, **after the loop (bands, copies, draws) 7.7 / 4.2 (16%)**.

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

### 2d. The pass's own instructions by step, d20's opening (counts, 2026-11-21; no code)

[E, exact instruction counts from the emulator's cache model: the lab's sierra-batch 44976939797 (findings `dietsplit2_light_pass_lines`).] codediet/batch4 d55149dc in a `-g` build, the drawing on the main CPU (`m6off`) so each instruction's source line is named, d20's opening with nobody at the controls to 120 s. **It says where the pass's instructions are, not what they cost:** on a PSP the pass runs on the Media Engine from its copy, and misses, not instructions, are most of its time there. The counts are each function's own: `sqrtf`, `memcpy`, the list's words and pspgu's calls are callees and not in them. Line numbers are `psp/src/psp_ge.c` at d55149dc. This stretch is not §1c's flashlight window: the cone, gel and whole-triangle steps are 0.4% of the pass here, so its lights are mostly the level's point lights [U].

The pass is **4,870.5 M instructions, 34.2% of the drawing's 14,231.6 M**, over 190,577 calls of `psp_ge_light_draw_body` (25,557 a call with its three callees).

| Step (§1c's order) | Lines | Instructions | Of the pass | A body call |
|---|---|---:|---:|---:|
| 1 set-up: stream, matrices, bind | 23646-23739 and inlined helpers | 51.9 M | 1.1% | 272 |
| 2 the triangle walk: index bounds test, the corners' source pointers | 23757-23783 | 440.0 M | 9.0% | 2,309 |
| 3 each corner's reach through the draw's memo | 23872-23888 | 1,072.8 M | 22.0% | 5,629 |
| 3 `light_reach` itself (16,098,504 calls, 84.5 a body call): distance and fade 51%, the packed normal 24%, facing 13%, 1 / distance 12% | 22786-22821, 12390-12401 | 820.4 M | 16.8% | 4,305 |
| 3 the lit test, and a lit triangle's corners into camera space | 23921-23964, 6877-6893 | 885.2 M | 18.2% | 4,645 |
| 3 gel, cone and whole-triangle tests | 23977-24036 | 17.9 M | 0.4% | 94 |
| 4 the emit: its call, `light_emit` (3,197,989 calls, 16.8 a body call; the five plane tests 76%) and `light_emit_known` | 24055-24086, 6905-6918, 23012-23052 | 802.5 M | 16.5% | 4,211 |
| 5 after the loop: the kept vertices' colour | 22824-22827 | 244.3 M | 5.0% | 1,282 |
| 5 after the loop: the distance bands (nearest corner, sort, copy, draws) | 24159-24217 and inlined helpers | 511.3 M | 10.5% | 2,683 |
| 5 texture state, the other draws, the tail | 24102-24136, 24231-24282 | 22.2 M | 0.5% | 117 |

Every source line of the four functions is in a row; the body's rows sum to 3,331.7 M of its 3,333.8 M (2.1 M, 0.06%, carry no line).

- **What does not depend on the camera** [D, read in the code]: which triangles a light reaches and how strongly. The walk, the reach and its memo, `light_reach` and the colours work from the light's position, radius and colour and the vertex's position and normal: **52.9% of the pass's instructions** (step 2, the two reach rows and the colour). For a light and a surface that did not move since the last frame they come out the same; in the default build nothing of them is kept from one frame to the next (the memo is cleared a draw; LMC, §4b, is the parked lever that kept a still light's lit list, and served 59% of d20's light draws from it).
- **What does:** the corners in camera space, the emit's plane tests and cuts, and the distance bands with their depth nudges: **45.2%** (the lit-corners row, the emit and the bands). These follow the eye.
- **Five reaches for every triangle emitted:** 16.1 M reach computations against 3.2 M emits. Most corners worked out belong to triangles the light does not reach.
- **Instruments on the every-triangle path in this build:** none counted. The probes (`probes_on`), the census and the ring look (`psp_on_me()` is false under `m6off`) are tests that fall through; on a PSP the ring look adds a clock read every 8 triangles on the ME.
- **Not answered here:** how many of the calls are the same light on the same surface as the frame before (the "on change" share), and the flashlight's window, where §1c's split says the cone cull and split lead. The banding is by source line from one run; the worker's read of it ran out of turns (or-job 44981882245), so the rows were summed and the lines read by the diet's session.

## 3. Every earlier lever on this ground (price, why it stopped, revive_if)

Levercheck's ground is `lighting` (`projects/sierra/tools/levercheck.py:44`); rows below are `projects/sierra/findings/levers.tsv` and `grounds.tsv`.

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

---

## 9. Slice 1, a draft: the reach on change (2026-11-21; design before code; nothing built)

**Owner from 2026-11-21 22:28 (Sheepdog01): the code diet's session, design and build; the box stays the lead's as spec.** This section is written before step 1's [M] (the light kind's ME time on today's tock, from the next d20 pair's `mekinds` side); its gate below decides whether it is built at all.

**What it is.** A level light that did not change since the last frame reaches the same triangles of the same level draw, as strongly. Today that is worked out again for every light draw of every frame: the triangle walk, each corner's reach through the draw's memo and `light_reach` are **47.8% of the pass's own instructions** on d20's opening, and five reaches are worked out for every triangle that is emitted (§2d [E]). Slice 1 keeps, for a light and a level draw, **the lit list**: the indices of the triangles the light reaches and each of their corners' reach, as the floats the walk produced. A frame that finds the list valid skips the walk and the reach, and goes on from the lit corners as it does today: into camera space, through the emit's plane tests and cuts, the colour and the distance bands, which follow the eye and the light's colour and are not kept.

**What was tried on this ground** (the lab's `levercheck.py --ground lighting`; rows LMC, LR, LM, LPLAN): this is **LMC** ("static lights' light-pass work kept across frames"). It was built and proven exact on 2026-11-07 (0 mismatched of 35,294 checked draws on d20, play and seeds identical; **59% of d20's light draws and 48% of a10's served from kept results** [E]) and parked: on its PSP pair (d20's swamp, PSP-3000, 2 x 2) the ME's render job was no shorter (16.0-16.1 ms off, 16.5-19.1 on) and the main CPU waited ~5 us a job on it. Its revive_if: "a stretch where the main CPU waits on the ME's render job". LR (runs of 16 triangles skipped by a sphere) reaches only the 21% of d20's triangles that sit in runs wholly beyond the light [E]; LPLAN (the main CPU's surface plan kept) priced at net 0 [M]; LM (lights merged per surface) changes what the GE draws and is a later slice. **What is different now:** (1) the stretch: LMC's window held about 1.3 ms of light draws a frame by a later reading (the lab's LM row, c4), so there was little to save and its bookkeeping could cost as much; (2) the frame's path: d20's unsteered opening had the main CPU waiting 14-17 ms a frame on the render job on tock159 [M, old], which the next pair's baseline side measures again; (3) the size: the lists are sized to what is lit (about 300 bytes a kept draw, ~32 KB in all, below), not LMC's 160 KB of per-vertex reach; (4) the code: LMC's branch is in neither repository today (its commit is not in the archive's 857 branches), so the slice is written from this page in `psp/src/box_lights.c`, which the box's rules ask for in any case.

**The gate (before any code).** The slice is built if step 1's [M] shows, on a stretch that can be benched, the ME's light kind at about 2 ms a frame or more **and** the main CPU waiting on the render job there. If the light kind is small on today's tock or the main CPU does not wait, this section is withdrawn and the first slice is chosen again from §3 (the light limit is a look call for Sheepdog01; LM is next by size).

**Kept, and what ends it.**
- **Key:** the light (its position, radius, cone and falloff, gel and definition, compared bit for bit with last frame's) and the level draw (the map's own vertex range and index range and its material: the map's data, not the address of a copy, which moves when the geometry cache gives a block up).
- **Value:** the lit triangles' three indices each (6 bytes) and their three reaches (12 bytes), in the walk's order, so the emit sees the same triangles in the same order and the display list comes out the same word for word. At d20's opening's 16.8 emitted triangles a light draw [E] that is about 300 bytes a kept draw, about 19 KB for the 63.5 light draws a frame of d20's census [M, older tock]; the store's count sets the real figure.
- **Ended by:** the light changing (the flashlight changes on every frame the player moves or turns: it is never kept, and is this box's other slice), the level changing (a map load), the store taking the block back, or `lightpiece` / `lightleast` changing. A spotlight's split path is left to the old code in slice 1: point lights only.
- **Colour:** worked out each frame from the kept reach (the tint and the light's colour may change while its place does not).

**The kept record, from the walk** [D, `psp_ge_light_draw_body` and the level streams in `psp_ge.c` at the tock; read 2026-11-22]:
- The walk works in the level copy's frame: the body finds the draw's stream by the map's vertex pointer (`level_stream_find(vertices)`) and takes the stream's origin off the light's position before the loop. So the reach floats belong to a light position **and a stream**: the key holds the light's values, the draw's given vertex and index pointers and count, and the stream's stamp (next line).
- A stream ends in one place, `level_stream_let_go` (the copies' budget, the carve given back, a map's end), on the main CPU, which cannot reach the ME's eDRAM to clear anything. So each stream gets **a serial number set when it is made**, and a kept list is valid only while its stream's serial is the one it was kept under. That is one more line in `psp_ge.c` (a field of `struct level_stream` and its set), listed with the hooks.
- `bake_substitute` may swap the draw's index list for the stencil bake's pieces before the loop: the kept triangles are those of the list that was walked, and the stream's serial covers the bake's state changing with it [U: whether the bake can change under a live stream is read before code].
- Slice 1's lights: no cone (`spot`, cosine cutoff between 0.05 and 0.999, takes the split path) and no gel (a gel light carries three reaches a corner); under the default light mode. The others take the old path untouched.
- A kept triangle is its three vertex indices (6 bytes, so the draw's index list is not read again) and its three reaches as floats (12 bytes).

**Units.** The pass runs on the ME, and so does all of this: the light reaches the ME as its begin record's own values (`psp_ge_light_begin`'s position, forward, colour, radius and the two cosines, then `psp_ge_light_gel`; `psp_ge.c`'s `light` is set from them as the record is replayed [D]), so the ME compares those values with a kept list's key itself. **The main CPU does nothing new and no record changes** (corrected 2026-11-21 23:5x: the first draft asked the main CPU for an "unchanged" word; it isn't needed, the key is the values). The kept lists are written and read by the ME only: no line is shared with the main CPU. The ME's code is `box_lights.c`'s, free of VFPU and system calls, in the ME's copy (its room from eDRAM 520-800 KB through `memory_ledger.md`, as the draw list's Tier 1).

**Where the kept lists live (the lead, 2026-11-21 23:4x): in the ME's eDRAM, in a table of the box's own.** The draw list's store (`box_draw_list.md` §12.1a) is for what the GE CALLs: display-list blocks, in main RAM, in slots freed by the GE's fence. A lit list is the ME's working data: the GE never reads it, and nothing the light pass sends the GE can be CALLed again, since it follows the eye. So it needs no fence, it does not wait on that store's interface, and the ME reads its own eDRAM at about a third of main RAM's cost a line under load (the figures in LSPLITMEMO's revive_if, the lab's `grounds.tsv`: 27 cycles a line against 77). **About 32 KB of the 280 free at 520-800 KB**, claimed through `memory_ledger.md` in the commit that builds it, placed with the copy's 264 KB room and the draw list's Tier 1 growth of the copy (2-3 KB) in view. The store's invalidation stamps are borrowed where they fit (the level stream's identity is the same question there); its storage is not.

**Hooks in `psp_ge.c`** (the only lines outside `box_lights.c`; under the build switch `PSP_BOXLIGHTS`, off by default): a level stream's serial number where the stream is made, and two in `psp_ge_light_draw_body`: (1) before the triangle walk: ask the box for this light's lit list of this draw and, if it has one, run the lit-triangle part of the loop over it; (2) after a walk that found none: hand the box the lit triangles and reaches it just worked out, to keep if the light's values are the ones it saw last frame. Nothing in the record path, `psp_ge_light_begin` or the main CPU's selection changes.

**Proof.** In the emulator: a check word that walks anyway and compares the kept list with the walk's (0 differ), play and both seeds identical, and every game list identical between the old-only and the new-only build. On the PSP: old-only against new-only on the same bench, d20's unsteered opening first; priced in the frame, with the ME's light kind (`mekinds`) and the main CPU's wait beside it.

**Budget (provisional until step 1).** §4a's 3.0 ms mean and 5.0 at the most of the ME stand as the proposal. If the kept lists serve the 59% of light draws LMC served, and those draws lose the 48% of their instructions that the walk and the reach are, the pass's instructions fall by about 28% on d20 [U on E: instructions, not time; misses are most of the ME's time and the kept list is read in place of the level's indices, vertices and normals, which is where a gain in misses would come from, unmeasured].

**Not in slice 1:** the flashlight, the split, the camera-side half (45% of the pass's instructions), LM's merge, the light limit, and anything the GE draws differently.

---

## 10. The first measurement on today's tock (2026-11-22)

**What ran [M].** d20 unsteered to 120 s on a PSP-3000, the tock's code at 610e9013 under its counted profile, two runs with `mekinds` (the ME's cycle counter around each record kind, a line every 300 frames) and two without. The lab's row `diet5_mekinds_d20_3000`.

| 300 frames | All kinds, ms a frame | Light draws | Level draws | Audio and ring | Model parts |
|---|---|---|---|---|---|
| 1 | 17.1 | 1.4 | 5.7 | 3.2 | 2.3 |
| 2 | 21.5 | 2.7 | 6.0 | 4.5 | 1.3 |
| 3 | 43.8 | 24.0 | 9.6 | 2.6 | 2.5 |
| 4 | 56.8 | 31.4 | 13.8 | 3.4 | 2.3 |
| 5 | 56.8 | 31.4 | 13.8 | 3.3 | 2.3 |

- In the last two windows a frame makes **163 light draws at 193 us each** and 58 level draws at 237 us. Light begin, gel faces and model point lights are under 0.1 ms together: the cost is the draws.
- The two runs agree within 0.2% in every window.
- Without the timer the render job is 55.3 ms there, so the timer adds about 1.5 ms.

**What it does not show.** Whether the main CPU waits on that job. In these runs it did not (5 us a job): the frame was 148-204 ms with 123-141 ms of it ticks, so the tick bound it. The runs carried `statecrc`, and how much of that tick is the state's hashing was not measured [U]. The only measured wait on d20 is tock159's 14-17 ms a frame (§2).

**What it changes here.**
- §4's budget was written against 6.2 ms a frame of light draws on d20 (the census mean). On this stretch of today's tock it is 31.4 ms: the budget's ME row needs this stretch beside the census one before a ceiling is set.
- §7's question 4 (the swamp's point lights against the flashlight): this run is unsteered, the flashlight off, so the 31.4 ms is the level's own lights.
- §9's slice 1 skips the walk and the reach for a light that has not changed, 47.8% of the light pass's instructions in the model [E]: up to about 15 ms of the 31.4 [U on M x E]. Its gate asked for 2 ms a frame.

## 11. Slice 1 as built: the kept reach (2026-11-22)

**The form** (the lead, 2026-11-22): the reach kept while a light is still, section 9's design. Trimming each draw to its cone was the other candidate; d20's swamp has no cone to trim to: the PSP's own count there is 0 of 96,130 light draws with a projected cone [M, the lab's sierra-hw 45010621417]. Its lights are point lights.

**Where it is.** `psp/src/box_lights.c` and `box_lights.h`, built in by `PSP_BOXLIGHTS=1` (off by default), turned on by the keep word `lightkeep` (off by default). The hooks in `psp_ge.c`, all under the build switch: a level copy's serial number where its vertices are allocated; in `psp_ge_light_draw_body`, the question before the triangle walk, the kept triangle's branch at the top of the loop, the lit triangle handed over after the reach test, the list's end after the loop; and the counts' line in the frame-end report.

**The key**, compared word for word: the light's position from the copy's origin, its direction, radius and two cosines (what `light_reach` reads); the copy's origin, its slot and its serial number (its vertices and normals); the draw's index list by its address, its triangle count and the geometry cache's generation at that address (`psp_geo_generation`, which changes with every load into a block; 0 for the map's own data).

**What is kept**: each lit triangle's three vertex indices and three reaches, in the walk's order (20 bytes). A list is kept at a key's second sighting: the first only records the key, so a light that moves, whose key never comes twice, keeps nothing. A walk cut short at the scratch's end keeps nothing.

**With a kept list** the loop runs over the lit triangles alone. Each corner's memo slot is set as the walk would set it, and the triangle joins the walk's own code where its corners go to the camera: the same functions on the same values from there on, so the display list comes out the same.

**Left to the walk** (each counted): a spotlight (its cone cull and split), a gel (three reaches a corner), a copy with stencil bake pieces (the bake swaps the index list a draw and reads it whole to do so), another light mode, and `lightmemocheck`. A point light with a cone fade but no spot path is kept: its reach has no term from the eye.

**The store**: 192 KB of the ME's eDRAM at 608-800 KB (`psp_me.h`'s `ME_EDRAM_LIGHTS`; `memory_ledger.md`; first built as 128 KB at 656-784 with 256 keys, resized in section 13), above the code copy's room, which the draw list's longer list takes to 532, and up to the sound tier at 800; `psp_me.c` refuses at build time a copy room that reaches it. Not section 9's 32 KB: the swamp's 163 light draws a frame at the opening's ~17 lit triangles a draw need about 55 KB of triangles and 12 KB of keys [U on M x E]. 512 keys and about 7,780 triangles; when the triangles run out, or 64 keys have been turned away because their probes held only kept lists, the store is cleared and fills again. Written and read only where the pass runs. A PSP's main CPU running the pass itself (the fallback) cannot reach the store and walks as before. The emulator uses the same range when it maps it, and the heap otherwise.

**The check**: `lightkeepcheck` asks for the kept list, walks anyway, and compares the walk's lit triangles with the kept ones bit for bit, in order and in number.

**The line** (every 300 frames): `lights box: light draws asked, served from a kept list, lists kept, first sightings, slots replaced, the store cleared, walks cut short, triangles in the store; not asked: spotlights, gels, a baked copy, another mode, out of the store's reach; lightkeepcheck: compared, differed`.

**Not yet**: its functions are not in the ME's code copy (they run from main RAM; the pass's body, which is in the copy, grows by the hooks), and nothing is measured. Proof and price follow section 9: the check and old against new in the emulator, then d20 unsteered on the PSP-1000 without `statecrc`, one build, `- | lightkeep | mekinds`, which also gives the main CPU's wait on the render job.

## 12. Slice 1's proof in the emulator (2026-11-22)

One batch, two builds of the same commit: without `PSP_BOXLIGHTS` (the old path alone) and with it (word off, `lightkeep`, `lightkeepcheck`), to 3,900 ticks hands off (the lab's row `lightkeep1_proof`) [E, exact counts].

| Map | Light draws asked | Served from a kept list | Kept lists compared with the walk | Differed | Not asked |
|---|---:|---:|---:|---:|---|
| d20 | 185,603 | 89,542 (48%) | 89,542 | 0 | a baked copy 4,234 |
| a10 | 8,014 | 3,337 (42%) | 3,337 | 0 | gels 28,305 |
| c20 | 5,514 | 87 (2%) | 87 | 0 | gels 23,850, a baked copy 5,031 |

- **Exact where it serves.** Every kept list the check compared was the walk's own, bit for bit. On d20 the play, both seeds and every game list are identical with the word on, with the check, and with the word off; the old-path build plays the same too (its lists differ from the new build's in their addresses only: the same word and draw counts in all 1,947).
- **a10, one list open.** Play and seeds are identical, the word on and the check drew the same lists as each other, and the word off parted from both at one list (6 draws of 344) and in the hashes after it, with the pictures the same. The check draws from the walk as the off side does, so the word is not the likely cause; ~~the off side is being run twice more~~ **[closed the same day, section 14: the emulator's twin of the store takes game heap on the sides with the word.]**
- **It serves half of d20's light draws, and that is the next thing to look at.** 95 light draws a game frame are asked and 46 are served. The rest are first sightings whose key is never seen again before its slot is taken (94,324 first sightings, 92,815 slots replaced, 1,732 lists kept). ~~A light whose place, direction, radius or cone changes every frame, or a copy whose serial does.~~ **[Corrected the same day, section 13: no key moves; the table was too small.]**
- **Gels and baked copies are outside this slice** and are most of a10's and c20's light draws.
- **The ME's code copy.** With the hooks, the pass's body grows and the copy holds 214 of the list's 223 functions (219 without the box): five more run from main RAM in this build, on both sides of its pair.

## 13. The store resized, slice 1's cost sheet, and what could be baked (2026-11-22)

**Why half was served [E, D].** The proof run's own windows say it. In d20's swamp, each window of 300 frames reads the same: 47,700 light draws asked (159 a frame, the PSP's 163), 24,750 served, 22,950 first sightings, 22,950 slots replaced, 0 lists newly kept. Nothing moves: the same 76 keys a frame arrive as first sightings every frame. The table held 256 keys with 4 probes, and a key that found its probes taken turned out the key at its own place, kept or not. At 159 keys a frame the table is 62% full, about half the keys find no room, and they turn each other out before any is seen twice. The earlier reading (a light or a copy that changes every frame) was an inference from the totals and was wrong.

**The change (`box_lights.c`, `psp_me.h`; no change in the pass's body).** 512 keys and 8 probes (a third full at the swamp's load); each slot holds its key's hash, compared first; a key that finds no empty slot takes one seen once and not again, and never a kept list; one that finds only kept lists is turned away and walks as before, and 64 of those clear the store (it is full of lists nobody asks for). The store grows from 128 KB to 192 KB (608-800 KB of the eDRAM): the swamp's 82 kept lists held 3,088 triangles, 38 a list, so 159 lists need about 6,000 triangles against the old room of 5,580 and the new one of about 7,780.

**Slice 1's cost sheet** (the lab's rule of 2026-11-22: saved and added per item from the code, the count from a PSP, built only past ~0.5 ms). The item is a light draw.

| | Per light draw | From |
|---|---|---|
| Its whole cost today | 193 us, about 64,000 cycles | [M] PSP-3000, d20's swamp, 163 a frame (`diet5_mekinds_d20_3000`) |
| Saved when served: the walk and the reach of every triangle of the draw | about 92 us, 30,700 cycles | 47.8% of the pass's instructions [E, `dietsplit2_light_pass_lines`] x [M]; taken as the same share of its time [U] |
| Added when asked: the key's 17 words built, their hash, the copy's generation looked up, up to 8 slots' hashes compared and one 68-byte compare, 2-4 of the store's lines | about 600-1,100 cycles, 2-3 us | [D] |
| Added when served: a kept triangle's 20 bytes read, 38 a list | about 12 lines, under 1,000 cycles, 3 us | [D], at the hardware page's miss cost |
| Added at a first and second sighting: the walk as before, and 20 bytes written a lit triangle at the second | under 1,000 cycles, twice a key | [D] |

Net in d20's swamp with every still light served: 163 x (92 - 6) us, about 14 ms a frame of the ME's 56.8 ms render job [U on M x E x D]; with half served, as first built, about 7 ms. Saved to added is about 15 to 1. In the lab's order of cuts this is the fourth kind (a skip checked per item), and it clears that kind's bar of ten to one. What it gives the frame depends on how long the main CPU waits on the render job, which the PSP job measures.

**What could be baked instead (the first kind: no check at run time) [E, D].**
- **d20's swamp lights are still.** The light plan's census (the lab's `light_plan_census`): about 10 light walks a frame on d20, every one with the sphere and clusters of a light of the frame before once the level has settled (3,000 of 3,000 a window), about 840 surfaces a walk. This proof agrees from the other side: the same keys, bit for bit, every frame of the swamp's windows, light and copy both.
- **What they light here is the level.** The pass in question is the level's light draws (159-163 a frame); model point lights, the gel faces and light begin are under 0.1 ms together on the PSP.
- **So the lit triangles of each still light on each level part, with their reach, can be made at bake time** and looked up by light and part: no key, no store, no first walk, and nothing lost when the camera moves. Slice 1 as built keys on the copy's origin and serial, so a copy remade around a moving camera is a new key; a baked list would name the map's own triangles.
- **Not bakeable, and not counted in these hands-off runs:** lights that move with something (weapon fire, plasma, grenades, explosions, a flashlight, a vehicle's lamps) and lights whose radius is animated. A light that only changes colour or brightness keeps its lit triangles. A count of the light draws by kind in real play is what would size the two shares.

## 14. Slice 1 in real play: rejected as built, and where the work stopped (2026-11-22)

**State:** slice 1 is rejected as built (the lab's lever `LIGHTKEEP`); its PSP ask was withdrawn before it ran. The box's work stopped the same day for the tick rewrite; slice 2 (the bake) has its counts below and no cost sheet yet.

**What it serves in play [E]** (the lab's row `lightkeep3_play`; the word on seven pad runs of real play): 0.2% of light draws on b30's leg, 0.4% on a50's, 2.7% on b40's, 2% on c40's, 11% on d20's play; none on c20's and a30's legs (gels, baked copies). Hands off on d20 it serves about half to all.

**Why [D, E]:** the key holds the draw's index list, and a light draw's index list is the triangles of the light's surfaces that are in view, made again each frame. It repeats only while the camera stands still. By section 13's sheet (about 92 us saved a served draw, about 3 us added a draw asked) that nets about nothing on the legs and under 1 ms a frame on d20 in play.

**What is still in play [E]** (`lightkinds_play`): the light itself (its place in the world, direction, radius and cone) is as seen before for 53-95% of light draws (b30 65%, c20 93%, a30 53%, a50 69%, b40 95%, c40 94%, d20's play 93%). A still light on a material's copy that is as it was when the light last lit it is at least 46% of b40's light draws and 58% of d20's in play; a copy remade under a still light is 0.0-0.1%. Those two are floors: the count's table of lights gave rows up. So what can be kept, at run time or at bake time, is the light's reach at the copy's vertices, not its triangle list: in the pass's instruction split that is `light_reach` and the memo's reach band, 39% of the pass, where slice 1 took 48% with the walk.

**What the pass costs in play [M]** (`light_pass_me_share_in_play`, from runs the lab held): on d20's play 5.7 ms a frame of the ME's 26.6 over the run (21%), 12-14 ms in its two heaviest windows; on b30's live fight 0.71 ms of 33.2. The 31 ms of section 10 was the swamp with nobody at the controls. A bake is a cut for d20 and maps like it, and its sheet's ceiling is the 5.7 ms.

**The proofs' two artefacts, both the emulator's [E]:**
- **a10's one list (section 12)** was not the word: two more runs with the word off drew exactly as the first (`lightkeep1b`). In the emulator the store is a twin taken from the game's heap at the first asked draw; on a10 that 128 KB left no room for one material's copy at one frame, which was drawn a frame later.
- **The resized store's proof (`lightkeep2_proof`) does not show what the new table serves.** Its kept lists are exact (0 of 59,516 on d20 and of 3,331 on a10 differed from the walk; play identical), but with 192 KB of heap taken on the sides with the word, d20's lists and pictures part from the off side's and the swamp reads 33% served. A PSP's store is eDRAM and takes no heap. A box whose store is eDRAM has to prove in the emulator with the store outside the game's heap.

**Not done:** the bake's cost sheet (time against the 5.7 ms; memory by vertices in reach per light per material, per map); a count of vertices in reach; a fresh `mekinds` run of d20's play on today's tock.
