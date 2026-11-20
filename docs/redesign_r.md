# R, retained rendering: the design page (Phase 1)

Part R of `redesign_plan.md` (§3 R). Sheepdog01 gave it its go at Gate 1 (2026-11-05 19:28). It's built under the redesign rule (CLAUDE.md, "Redesign work"): a separate path switched whole under one keep word, its slices proven for correctness in the emulator, priced on the PSP at its milestone and its switch-over. This page is the design the slices follow; the master builds it. Revised 2026-11-05 after a second-opinion read against tock162's code (lab or-job 42145484821; each point re-read by the bottleneck session): forms carry no addresses (§2.3), the instance carries its node matrices and shape (§2.1), the level's saving is unverified (§2.6), and slice 1's word compare runs with the memos off (§3).

Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Code references are tock162 (master/tock162 70bc508 and later).

## 1. What R replaces

Today every model part is decided twice and re-emitted every frame:

1. **The engine's rasterizer walks the visible objects on the main CPU** and, per model, calls `_rasterizer_model_begin`, which the fixups hook to `psp_ge_model_begin` (node matrices, ambient, two distant lights) and `psp_ge_model_point_light` (up to two point lights) [D, `psp_fixups.py` ~210-260]. Per part it calls `psp_model_draw` → `psp_ge_draw_model` (vertex and index buffers, colour, base texture key, scales, alpha test), plus `psp_ge_model_multipurpose`, `_reflection` and `_detail` for the extra passes, and `psp_ge_queue_transparent` for see-through parts [D, `psp_fixups.py` ~2460-2530; `psp_ge.c` 668-680].
2. **The GE layer records each call** (`defer_take`, `psp_ge.c` 961: a kind, a length, the arguments copied) into the record buffer (128 KB by default, 224 with the keep set, DEFER_INFLIGHT_KB).
3. **The ME replays the records** (`defer_replay`, `psp_ge.c` 1488), calling the same GE-layer functions, which bind textures (`psp_tex_bind`), set lighting (`model_lighting`), convert or find geometry, and write the GE's words through pspgu into the frame list (`list_take`, `psp_ge.c` 3763).
4. **The GE reads the list from main RAM** beside the next frame's ticks.

On b30's flight that's 37.1 ms a frame of drawing on the main CPU, 67.9 ms of replay on the ME (models 37.8, 277 part draws a frame) and ~170-210 KB a frame of list [M, ledger 3; C4]. A part's words are 97-98% the same as the last frame's apart from its matrices [E, pdc1; pdcprobe M].

## 2. The new shape

### 2.1 The main CPU: an instance per object, not a call per part

The engine's walk keeps what decides *whether and how* an object is drawn: visibility, the level of detail, lighting (LIGHTFIRST's pass, on by default) and the object's own state. Where the rasterizer would start a model's parts, R writes one **instance** into the frame's instance list and skips the per-part loop:

| Field | From |
|---|---|
| model, geometry (level of detail and permutation set) | the walk's choice, as `render_model` makes it |
| node matrices (a pointer into the frame's matrix snapshot) | the skinning parameters `psp_ge_model_begin` gets today |
| lighting: ambient, two distant lights, up to two point lights | `parameters->lighting`, as the fixups pass it |
| change colours, glow, self-illumination phase, the effect and its intensity | today's `psp_ge_model_multipurpose` / `_effect` arguments |
| the shape: first person, centroid, radius, near and far for first person | `psp_ge_model_shape`'s arguments (`psp_fixups.py` ~300), from which the near test, the size floor (`model_pixels`) and nearauto's nearest-object tally are made |
| flags: shadow, faded, sky | the walk (the sky is drawn through the model path, `ge_kind(KIND_SKY)`) |

**An instance is not small:** the engine's node matrices are a pointer that doesn't outlive the walk, so the instance copies them, as today's record does (`defer_model_begin`: up to DEFER_MAX_NODES 64 × NODE_FLOATS 13 floats, 3.3 KB; first-person arms 36 nodes). Its size is about today's model record. R's saving is in what follows it: no record per part and no per-part chain on the ME. The instance list is double-buffered with frames in flight, as the record buffer is now.

**An object with some parts R can't take** (see-through parts, oversized skinned buffers without a PCV block, super high, near the eye) keeps its model's begin, shape and effect records as today beside the instance, so its immediate parts draw as they do now.

### 2.2 Forms

A **form** is one part's GE words for one pass, made once a map. **They're captured, not baked** (2026-11-05 23:12, `redesign_r_slice1.md` §2): today's draw path runs the part once with the memos off into scratch, and the census's form words are kept (~55 of a part's 140.7 today [E]), with holes for the touch's. One implementation of the rules, exact by construction; a bake-side maker can come later as a load-time saver. modelconvert.py's PCV blocks (PRECONVERT, on by default) already put every opaque part's vertices in the GE's own form, with each skinned part's batches and bone slots (SKINW2's first-fit plan) [D, `tools/modelconvert.py`]. A form adds the rest:
- the state words: blend, alpha test, culling, the texture state except its address (format, size, filtering, the CLUT's format), since a texture's address exists only once it's bound (§2.3);
- the vertex type and the draw: for a rigid part one PRIM over its PCV records; for a skinned part, per batch, its PRIM, the batch's bone-slot words left to the ME (§2.4). No vertex or index address is in the form: the ME sets VADDR and IADDR before the CALL (§2.3);
- the rigid part's node (static: today the ME reads it from the part's first vertex each draw, `psp_ge.c` ~16884), kept in the form's table so the ME needn't read the vertices;
- per pass, a form of its own: the base pass; multipurpose (change colour, glow); reflection; detail; two-sided where the shader asks.

Forms are found by part, keyed by geometry, part, pass and shader permutation, and live with the geometry block's residency. C1 counted 667-1,679 forms a map at every level of detail; at C4's ~40 state words plus ~13 a draw call that's roughly 200-300 bytes a form [U on M]. **Super high is left to the immediate path** (it's drawn only for objects taller than the screen: the cinematics' close-ups), which takes the store to ~250-480 KB on most maps.

### 2.3 The store and residency

Forms live in a main-RAM arena (the GE can't read the ME's eDRAM). Its room comes from what R shrinks: the frame list (768 KB today) and the record buffer (128-224 KB), both down to tens of KB for the dynamic path [U on C1, C2].

**Forms hold no addresses.** Two things a form would point at move at run time, and both caches keep their bookkeeping in the act of using them:
- **geometry:** a part's PCV block lives in the geometry cache, read from the pack on demand. A block is protected from eviction only while it's locked recently: `protected_entry` keeps an entry whose `last_frame` is within RECENT_FRAMES (2) of now, and the lock is what sets `last_frame` (`psp_geo.c` 350-352, 530, 670, 816). There's no pin or refcount. A form path that stopped locking would let its blocks be evicted under it.
- **textures:** a texture's address is known only at bind (`resident_at[index]`, `psp_tex.c`), and the bind also marks it in use this frame (`frame_set_mark`, through `me_book.marks` on the ME, which can't load a missing texture and raises a want instead: `psp_tex.c` 281-311, ~2867-2908).

So per part per frame the ME does a **touch**, not a bind:
1. the geometry block's lock-equivalent: refresh `last_frame` and get its current address (or learn it's not resident);
2. each pass's texture: its current address and the frame mark (or a want, if it's missing);
3. writes VADDR, IADDR and the texture address words (a few words), then the CALL.

A part whose block or texture isn't resident takes the immediate path that frame, as a missing texture draws untextured today. The forms never change once built, so frames in flight need no protection beyond what the store's lifetime gives (a form is freed only between maps).

The GE's address offset was an alternative to patching; with no addresses in the forms it isn't needed. (The review found the port uses only BASE with CALL, `psp_ge.c` ~15122, and `sceGuOffset` is the screen origin.)

### 2.4 The ME: the frame list from instances

Per instance, the ME writes:
1. per rigid part, its node's matrix (from the instance's node copy and the form table's node index; relative to the GE's world origin, as today, `psp_ge.c` ~4868-4905);
2. for a skinned part's batch, its bone-slot matrices (up to 8 slots, 13 words a slot, ~104 words a batch: today's `bone_commands`, already memoised per skin and batch in `bone_memo`) from the instance's node copy by the batch's plan;
3. the lights and ambient, per instance (point lights are camera-relative, `position - ge_eye`, so they change every frame for a moving camera; the lighting memo is per job, `lit_forget_state`);
4. the material colours (change colour, glow, effect);
5. per part: the touch (§2.3), then a CALL to each pass's form, in the order the engine draws them.

The per-frame state today's replay leaves for later draws has an owner under R: the near-object tally (nearauto) and the size floor from the instance's shape; the frame marks from the touch; the lighting and texture memos per job as now; `ge_kind`'s reserves (model, sky) by the instance's flags.

That's ~10-20 words a rigid part and ~100+ a skinned batch (its bone slots) instead of ~140-200 a part today [U on C4], no per-part bind, lighting or geometry chain (the replay's instruction-cache problem, `draw_rebuild.md`), and no record to decode. The ME's jobs keep their place in the frame: the frame list is handed to the GE in chunks as now (the stall address).

**What stays on the ME, by step** (b30's flight, PSP-3000 meparts4 42135685691, the ME's cycle counter; row c4_model_steps_b30_flight) [M for today, U for R]:

| Step | Today, ms a frame | Under R |
|---|---:|---|
| bind + state + lighting | 7.7 | forms; per-instance lights only where they change (~1) |
| geometry, to the batch | 8.1 | forms (gone) |
| draw calls | 5.2 | CALLs (~1-2) |
| bone matrices | 5.1 | stays (~5): the ME still writes each batch's bone slots |
| near tests and lists | 4.0 | one test an instance (~1) |
| two-sided, change and glow passes | 2.5 | forms (gone) |
| near-plane filter | 2.0 | the immediate path's near instances only (small) |
| convert | 1.6 | gone (PCV blocks) |
| geometry's rest, node matrix | 3.1 | mostly gone |
| **together** | **39.2** | **~8-10** |

So R takes ~29-31 ms of the models' ME time on the flight, which carries most of the plan's ~-31 to -35 (the level's ~5 makes the rest).

### 2.5 What stays immediate

The record and replay stay for the dynamic kinds (C2's floor, ~33-73 KB a frame): the see-through queue, particles, decals, contrails, the HUD, screen quads, shadows. Also on the immediate path:
- **parts near the eye:** the GE drops a whole triangle with a corner behind the camera or past its 2,048-pixel guard band (`psp_ge.c` header), so an instance whose sphere crosses the near plane or the guard band draws through today's path, which cuts them (1-6 KB a frame, C2);
- **super high** geometries (§2.2);
- **any part whose form isn't ready** (§2.3), or that has none.

### 2.6 The level

**Unverified, and probably smaller than the plan's ~5 ms.** The review found the level's draw isn't one PRIM over a range: its index list is rebuilt every frame from the visible surfaces (`structure_render_build_dynamic_triangles`), stencil pieces are substituted (`psp_ge.c` ~10603), and when the slant buckets split a material it's drawn once a bucket with its own texture slope (`sceGuTexSlope(slope * angle_factor[...])`, `psp_ge.c` 10294). So the level's form is per (cluster, material, bucket) at best, and its per-frame index build stays unless the cluster order makes the visible set a union of ranges. Slice 6 reads `structure_render.c` and sizes it before any build; until then the plan counts R's ME saving on the flight from the models alone (~29-31 ms, §2.4's table), ~2-4 short of the floor's 33.

**Planar fog** writes a per-vertex colour copy today (45 KB a frame on b30's flight, 36 on b40's snowfield, C2). The GE's own fog is distance fog, not planar; whether it reads the same is a look question for a side-by-side (slice 9).

### 2.7 The light pass

On d20 with the flashlight on, the light pass is 57-67% of the replay [M, C4]. Its pieces under R:
- **LMC:** a static light's reach and lit-triangle list kept across frames while the light doesn't move (d20's machine lights, ~85% of the swamp's light-frames static [E]);
- **LM:** one draw with up to four GE lights in place of one draw per light;
- **the flashlight:** either the GE's own spotlight in the surface's pass (cheapest, per vertex, no gel pattern) or the cone times the gel projected as the pass's texture by the texture matrix (the pattern per pixel, the ME's per-vertex reach and the 191 KB of lit copies gone; lever PROJLIGHT). Sheepdog01 (18:15): worth considering if it improves performance for a similar benefit; the gel's pattern is what's missing today. Settled by a side-by-side against the PC reference.
  - **What each form gives up** (2026-11-10, research; the lab's `projects/sierra/research/opportunities/light_pass.md`): the frame buffer is 5650, so there's no destination alpha. The projected form takes the surface's texture only as its average colour (B7 given up, the gel's pattern gained); keeping both needs a light buffer of its own (~278 KB of VRAM). Before ordering this slice: the pass's zero point and B7's share on d20's heavy view (T1 there), and LMC's and LM's ground on today's build (c4 reads d20 swamp's light draws at 1.3 ms a frame before the flashlight, against LM's 15.1 from October). If B7's share is most of the pass, the flashlight's form goes first.

### 2.8 Fewer vertices (for the target)

SSPR: each lower level of detail simplified at bake to what's visible at the distance it's drawn. The GE is ~89% vertex-bound on b30's flight (C6), so this serves the 30 fps target. It rides on R's bake, last.

## 3. The slices

Each slice is proven in the emulator for correctness only (§4). A slice slower on its own isn't a rejection. The keep word is `retained` (off by default until the switch-over); the old path is the A/B side.

| # | Slice | Proven by |
|---|---|---|
| 1 | The scaffold: the keep word, the instance list written beside today's records (both paths run, R's output unused), the forms captured and their table | the instance list matches the records it shadows (a check pass); a form plus its touch words rebuild today's words for a part **with the lighting and texture memos off** (today's stream depends on what was drawn before), compared word by word against the immediate path on b30, c10, c20, d20 |
| 2 | **Rigid and skinned parts' base pass** from forms (the bone slots from the PCV plans, ~104 words a batch from the instance's node copy); extra passes and everything else immediate | pictures against the old path (framecrc within the sides' spread) on the four maps |
| 3 | **The reflection pass,** then two-sided, change colour and glow, detail | as 2, and the cinematics' close-ups |
| 4 | The sky (through the model path, its own depth range) and whatever slice 1's counts still leave immediate | as 2 |
| 5 | Residency under the touch: not-resident parts to the immediate path, the frame marks and wants as the bind gives them, the store's room logged | as 2 over a whole route (b30's), with the geometry and texture caches churning; no part drawn from a stale address and no in-use texture evicted (checks) |
| — | **Milestone:** b30's models drawn from forms, the record and replay bypassed for them | one PSP pair, `- \| retained`, b30's flight (the plan's milestone) |
| 6 | The level's ranges (the bake's cluster order) | as 2 |
| 7 | The light pass: LMC, then LM, then the flashlight's form | as 2; the flashlight by a side-by-side against the PC reference |
| 8 | Planar fog, if the GE's fog reads the same | a side-by-side |
| — | **Switch-over:** every kind R takes drawn by R; the old record and replay kept only for the dynamic kinds | one PSP pair on the heavy stretches; then the old model path goes |
| 9 | SSPR (for the target) | pictures at each level of detail's distance |
| 10 | **MEXEC: the ME's hot replay code run from eDRAM** (the lab's `me_unnamed.md`, lever 5; design `docs/mexec.md` on master/mexec2), then the hot/cold split with TGME's layout on what's left, then parts in runs by path | play and seeds identical (code placement only); priced as a PSP pair at R's milestone. **First price (2026-11-10, PSP-1000, b30's flight): the ME's GE job −3.8 to −5.6 ms a frame (−9%), exact** (the lab's `mexec_r1_b30_flight`). **List r2 (+ the light record) on d20's flashlight, PSP-1000: the ME's job −0.8 to −1.7 ms (−2 to −5%), light draws −1.5%, exact** (`mexec_r2_d20_flashlight`): the light pass isn't code-miss bound |

**Why slice 10 (2026-11-10, the master; the lab's findings `me_unnamed_test1_steps`, `_outside`, `me_imiss_spread_r`, `mexec_code_fetch_psp1000`, `_loaded`):**
- On R's milestone build the ME's code misses are **29-32% of its job on b30's flight** (14-21 ms a frame at ~75 cycles a miss) [M]: spread over every step of the part chain, not only the gaps between the timed steps.
- They fall over **~100 KB of code**, 75% of them in 104 KB [E, shares], so no split fits the ME's 16 KB I-cache.
- **The ME fetches a code line from eDRAM in 25 cycles against main RAM's 54 alone and 70 under bus load; eDRAM doesn't slow under load** [M, PSP-1000].
- So moving the ~104 KB into eDRAM (544 KB free) should save about half the code-miss time, **~7-10 ms a frame of the ME on the flight** [U on M and E]. That also frees the ME's room the two-core tick needs (the lab's `tick_core.md`).

**Why slices 2-4 were reordered** (2026-11-05, slice 1's counts; the master's rslice1a v3 42153611114, master/rslice1 f81a02d9, d20): of 40 opaque parts a frame, the first draft's slice 2 (rigid, single pass, opaque, not near) took **1.1**; the rest were skinned 17.0, with a reflection pass 15.4, two-sided 4.2, glow 1.3, no baked buffer 0.9. So the skinned parts join the rigid ones in slice 2 and the reflection pass leads slice 3: the milestone measures R on most of the parts, not 3% of them. **Confirmed on all four maps** (rslice1b v2 42160136450, row r_slice1_parts [E]): opaque parts a frame, the rigid set's take, then the main others:

| Map | Parts | Rigid set | Skinned | Reflection | Glow | Two-sided | No model begun |
|---|---:|---:|---:|---:|---:|---:|---:|
| b30 | 246.4 | 42.8 (17%) | 127.5 | 19.1 | 16.7 | 16.0 | 11.5 |
| c10 | 150.0 | 3.5 (2%) | 96.9 | 25.2 | 22.8 | | |
| c20 | 27.8 | 1.0 | 10.1 | | | | |
| d20 | 33.7 | 0.7 | 10.9 | 8.6 | | | |

Skinned is half or more of every map's parts, so slice 2 (rigid and skinned) takes ~69% of b30's and ~67% of c10's parts, and slice 3's reflection pass the next 8-17%. Glow sits level with two-sided on b30 and above it on c10, so slice 3 takes them together. b30's 11.5 parts with no model begun (first person or the sky, unread) are slice 4's to place. Compare 1 held on all four: 0 of 265,383 instances differ. b30 filled the 64 KB instance list; it's 128 KB now and rslice1c reruns b30, with the full state-word census for slice 1's §2. Slice 1's proof: play and both seeds identical at every tick on b30's route, c10, c20 and d20; strict clean; compare 1: 0 of 15,807 instances differ; the instance list 12.2 KB a frame, peak 42.6 KB (d20).

## 4. The proofs

- **The simulation is untouched:** R reads the world, it doesn't change it, so play and both seeds stay identical with `retained` on and off (statecrc), on b30, c10, c20 and d20. That's also the multiplayer boundary's proof.
- **The pictures:** framecrc against the old path within the sides' own spread; where a slice changes how a pass is made (the flashlight, fog), a side-by-side against ChupathingyCE's PC reference frames (the gpu runner).
- **The strict interpreter** clean on the four maps (range, unaligned, FPU).
- **The counts each slice logs:** forms drawn and parts left immediate (and why), the store's bytes and its patches a second, the instance list's bytes, the frame list's bytes.

## 5. What the milestone must show

On b30's flight, bare, against the old path [targets from the plan, §1 and §3 R]:
- the ME's replay down ~31-35 ms a frame (the floor's need is 33);
- the main CPU's drawing well down from 37 ms a frame (the plan assumes ~15-20 for drawing, other and frame end together);
- the frame list down from ~170-210 KB a frame;
- the tick no slower (the GE reads the forms' words from main RAM as it read the list's).

If the ME falls short of its share, Sheepdog01 decides before slice 6 (the plan's road, Phase 2).

## 6. Risks and open questions

- **The walk's remaining cost:** how much of today's 37 ms of drawing is the per-part chain R removes, and how much is the walk it keeps (visibility, LOD, lighting). `draw_rebuild.md`'s census puts the per-part chain at the largest stage [E]; slice 1's shadow pass can time both sides.
- **Residency churn:** if the geometry cache evicts often on a heavy stretch, not-ready parts fall to the immediate path and eat the gain. Slice 5 logs it.
- **The marks' cap:** the ME buffers texture marks with a hard cap (`marks_dropped`); with every part touching its textures each frame the count is the same as today's binds, but slice 5 checks the cap holds.
- **The sky** goes through the model path (`KIND_SKY`, its own depth range, fog off): it's formable with its flags, or stays immediate; slice 4 decides.
- **PCV gaps:** oversized skinned buffers fall back to raw vertices (`psp_geo.c` ~1675) and see-through draws convert their own: both stay immediate.
- **The order of draws:** the engine's part order (and the see-through queue's sort) is kept, since the frame list emits instances in walk order and passes in the engine's order.
- **RSO** (records kept for still objects, on by default) becomes unnecessary under R; it stays on the immediate path until the switch-over.
- **Who reviews:** this page goes through a second-opinion read against the code before slice 1, as the plan did.
