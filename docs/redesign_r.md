# R, retained rendering: the design page (Phase 1)

Part R of `redesign_plan.md` (§3 R). Sheepdog01 gave it its go at Gate 1 (2026-11-05 19:28). It's built under the redesign rule (CLAUDE.md, "Redesign work"): a separate path switched whole under one keep word, its slices proven for correctness in the emulator, priced on the PSP at its milestone and its switch-over. This page is the design the slices follow; the master builds it. It's a draft for review before building, as the plan was (a second-opinion read against the code).

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
| flags: first person, shadow, near the eye (§2.5), faded | the walk |

An instance is a few hundred bytes against a part's ~1 KB of words today. The instance list is double-buffered with frames in flight, as the record buffer is now.

### 2.2 The bake: forms

A **form** is one part's GE words for one pass, built once by the bake. modelconvert.py's PCV blocks (PRECONVERT, on by default) already put every opaque part's vertices in the GE's own form, with each skinned part's batches and bone slots (SKINW2's first-fit plan) [D, `tools/modelconvert.py`]. A form adds the rest:
- the state words: blend, alpha test, culling, the texture state (format, size, the address by a relocation, §2.3), the CLUT;
- the vertex type and the draw: for a rigid part one PRIM over its PCV records; for a skinned part, per batch, the batch's bone-slot words left for the instance (§2.4) and its PRIM;
- per pass, a form of its own: the base pass; multipurpose (change colour, glow); reflection; detail; two-sided where the shader asks.

Forms are found by part, as PCV blocks are (a pad word in the map's part, or a table beside the PCV table). C1 counted 667-1,679 forms a map at every level of detail; at C4's ~40 state words plus ~13 a draw call that's roughly 200-300 bytes a form [U on M]. **Super high is left to the immediate path** (it's drawn only for objects taller than the screen: the cinematics' close-ups), which takes the store to ~250-480 KB on most maps.

### 2.3 The store and residency

Forms live in a main-RAM arena (the GE can't read the ME's eDRAM). Its room comes from what R shrinks: the frame list (768 KB today) and the record buffer (128-224 KB), both down to tens of KB for the dynamic path [U on C1, C2].

Two things a form points at move at run time:
- **geometry:** a part's PCV block lives in the geometry cache, read from the pack on demand (`psp_geo_lock`, `psp_geo.c`);
- **textures:** the texture cache places and evicts them (`psp_tex_bind`, the VRAM copies, TXPT's first-sight load).

Each form carries its relocations, listed at bake (offsets of the words holding a vertex, index, texture or CLUT address). **Patch on change:** when the geometry cache loads a block or the texture cache moves a texture, the forms that point at it are patched, and a form whose target isn't resident is marked not ready; its part takes the immediate path that frame (and the texture's want is raised as today). Patches happen on loads and evictions, not every frame.

An alternative to patching vertex and index addresses [U, to verify]: the GE's address offset (the OFFSET register, which JUMP, CALL, VADDR and IADDR can be relative to) set per CALL, so a form's addresses are block-relative. It isn't used anywhere in the port today; slice 1 checks it in the emulator and on one PSP run before relying on it.

### 2.4 The ME: the frame list from instances

Per instance, the ME writes:
1. the world matrix (relative to the GE's world origin, as today, `psp_ge.c` ~4868-4905);
2. for a skinned part's batch, its bone-slot matrices (up to 8, BONE words) from the instance's node matrices by the batch's plan;
3. the lights and ambient, only where they differ from what the GE holds (MEMEMO's memo does this today);
4. the material colours (change colour, glow, effect);
5. a CALL to each visible part's forms, pass by pass, in the order the engine draws them.

That's ~30-60 words a part instead of ~140-200 today [U on C4], no per-part bind, lighting or geometry chain (the replay's instruction-cache problem, `draw_rebuild.md`), and no record to decode. The ME's jobs keep their place in the frame: the frame list is handed to the GE in chunks as now (the stall address).

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

The level's draws keep their visibility but stop copying indices. The bake orders each material's surfaces by cluster, so each (cluster, material) is a contiguous range of the BSP's own surface indices, already in main RAM. A level draw becomes a PRIM over a range, with the material's state as a form. The kept index copies go (28 KB a frame on b30's flight, C2). Its passes (lightmap, detail, glow) follow the same pattern.

**Planar fog** writes a per-vertex colour copy today (45 KB a frame on b30's flight, 36 on b40's snowfield, C2). The GE's own fog is distance fog, not planar; whether it reads the same is a look question for a side-by-side (slice 9).

### 2.7 The light pass

On d20 with the flashlight on, the light pass is 57-67% of the replay [M, C4]. Its pieces under R:
- **LMC:** a static light's reach and lit-triangle list kept across frames while the light doesn't move (d20's machine lights, ~85% of the swamp's light-frames static [E]);
- **LM:** one draw with up to four GE lights in place of one draw per light;
- **the flashlight:** either the GE's own spotlight in the surface's pass (cheapest, per vertex, no gel pattern) or the cone times the gel projected as the pass's texture by the texture matrix (the pattern per pixel, the ME's per-vertex reach and the 191 KB of lit copies gone; lever PROJLIGHT). Sheepdog01 (18:15): worth considering if it improves performance for a similar benefit; the gel's pattern is what's missing today. Settled by a side-by-side against the PC reference.

### 2.8 Fewer vertices (for the target)

SSPR: each lower level of detail simplified at bake to what's visible at the distance it's drawn. The GE is ~89% vertex-bound on b30's flight (C6), so this serves the 30 fps target. It rides on R's bake, last.

## 3. The slices

Each slice is proven in the emulator for correctness only (§4). A slice slower on its own isn't a rejection. The keep word is `retained` (off by default until the switch-over); the old path is the A/B side.

| # | Slice | Proven by |
|---|---|---|
| 1 | The scaffold: the keep word, the instance list written beside today's records (both paths run, R's output unused), the forms baked and their table, the GE offset checked | the instance list matches the records it shadows (a check pass); the bake's forms rebuild today's words for a part, compared word by word on b30, c10, c20, d20 |
| 2 | Rigid, single-pass, opaque parts from forms; everything else immediate | pictures against the old path (framecrc within the sides' spread) on the four maps |
| 3 | Skinned parts (bone slots from the PCV plans) | as 2 |
| 4 | The extra passes (multipurpose, change colour, glow, reflection, detail, two-sided) | as 2, and the cinematics' close-ups |
| 5 | Residency: patch on change, not-ready parts to the immediate path, the store's room logged | as 2 over a whole route (b30's), with the geometry and texture caches churning; no part drawn from a stale address (a check) |
| — | **Milestone:** b30's models drawn from forms, the record and replay bypassed for them | one PSP pair, `- \| retained`, b30's flight (the plan's milestone) |
| 6 | The level's ranges (the bake's cluster order) | as 2 |
| 7 | The light pass: LMC, then LM, then the flashlight's form | as 2; the flashlight by a side-by-side against the PC reference |
| 8 | Planar fog, if the GE's fog reads the same | a side-by-side |
| — | **Switch-over:** every kind R takes drawn by R; the old record and replay kept only for the dynamic kinds | one PSP pair on the heavy stretches; then the old model path goes |
| 9 | SSPR (for the target) | pictures at each level of detail's distance |

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
- **Residency churn:** if the geometry cache evicts often on a heavy stretch, patches and not-ready parts could eat the gain. Slice 5 logs it; the store could pin the blocks its forms use if needed.
- **The order of draws:** the engine's part order (and the see-through queue's sort) is kept, since the frame list emits instances in walk order and passes in the engine's order.
- **RSO** (records kept for still objects, on by default) becomes unnecessary under R; it stays on the immediate path until the switch-over.
- **The GE offset** (§2.3) is unverified on this hardware.
- **Who reviews:** this page goes through a second-opinion read against the code before slice 1, as the plan did.
