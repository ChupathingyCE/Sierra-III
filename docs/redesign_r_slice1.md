# R, slice 1: the scaffold (a build spec)

The first slice of part R (`redesign_r.md` §3). It changes nothing the player sees: R's output is built and checked, never drawn. It's what the master builds first, and its proofs are emulator-only (the redesign rule). Written against tock162.

Labels as `redesign_r.md`.

## What it delivers

1. **The keep words:** `retained` (R's path; in slice 1 it builds R's output beside today's and draws today's) and `retainedcheck` (the compares below; implies `retained`). Both off by default.
2. **The instance list,** written by the main CPU beside today's records.
3. **The form table,** captured from today's path (§2), and its room.
4. **The touch,** written for the parts slice 2 will take (rigid, single pass, opaque), into scratch only.
5. **Two compares** that say R's output matches what today's path makes, and the counts that size slice 2.

## 1. The instance list

**Where it's written:** at the point the fixups call `psp_ge_model_begin` and `psp_ge_model_shape` (`psp_fixups.py` ~210-310; the engine's `rasterizer_model_begin`), with `retained` on, a new call `psp_r_instance(...)` beside them. It takes what those calls take:
- the node matrices (copied, as `defer_model_begin` copies them: up to DEFER_MAX_NODES × NODE_FLOATS);
- the base map scale; the ambient, distant lights and their count; the point lights (as `psp_ge_model_point_light` gets them);
- the shape (first person, centroid, radius, near and far for first person);
- the model and the geometry being drawn (its tag index and geometry index; the walk's level of detail and permutation choice), so the ME can find the part's forms;
- the effect and its intensity, change colours, glow, as `psp_ge_model_effect` and `_multipurpose` get them (slice 4 uses them; slice 1 stores them).

**Its buffer:** the frame's instance list, double-buffered with frames in flight as the record buffer is (`geinflight`), from the heap; its size logged each frame and its peak at the map's end. One record per model drawn.

**Its parts:** for each part the rasterizer then draws through `psp_model_draw`, the instance gets a part entry: the part's index in its geometry, and whether slice 2 could take it (rigid, a PCV block, single pass, opaque, not near the eye by the shape's test, not super high). Slice 1 still draws every part through today's path.

## 2. The form table

**What a form holds** (the master's word census, `redesign_r_slice1_words.md` on master/rslice1, rslice1c 42163086868 [E], b30's route, memos off): of 140.7 command words a part today, the form holds ~55 (vertex type and PRIM, the texture's format, sizes and filtering, the palette's format, culling, the cut-out test, the material's colours, each pass's blend and depth state); the touch writes ~51 (BASE ~14, VADDR/IADDR and the texture and palette addresses ~23, the node matrix ~12.5); the instance's lighting, ~25 a part today, is written once a model; the frame's fog is ~4. A part with a change colour has its material colours in the touch, not the form.

**Made by capture, not by the bake** (bottleneck and the master, 2026-11-05 23:12). A form is today's own words for the part: the part is run once through today's draw path with the memos off into a scratch list, and the census's form words are kept, with holes for the touch's. One implementation of the rules, exact by construction; compare 2 then checks the touch and the holes. The bake's arithmetic (`formbake.py`) can come later, as a load-time saver, if capture costs too much. Its conditions:
- **The key holds everything a form word depends on:** the geometry, the part, the pass, and the shader's permutation, plus anything compare 2 sees vary between draws of one part (a change colour, a fade, active camouflage, the flashlight's pass). Capture under a passing state (a fading object, a damage flash) must never become every instance's form: with `retainedcheck` each draw is captured again and compared with the kept form, counted by command.
- **No side effects from capture:** run into scratch, with no texture want, geometry conversion or memo write it wouldn't have made anyway; anything it does make is logged.
- **When and how long: a store kept by use** (the master's working set, rslice1h 42177309996 [E], 2026-11-06 01:22). Blocks stay resident nearly the whole map (b30 2,036 of 2,037 forms' blocks), so tying forms to residency would keep every form drawn since the map began (b30 713 KB, c10 626 KB). Forms drawn in the last second take 59-120 KB on b30 and 52-129 KB on c10; in the last ten seconds, 142-335 KB and 74-507 KB. So the store keeps forms by use, least recently drawn out first, at **256 KB** to start: twice the worst second, about half the worst ten seconds. A form that isn't there is captured at its draw: a miss costs today's draw of that part plus a copy, so the worst case is today's path, never worse than it. A form also goes when its geometry block or texture leaves. Slice 1 logs hits and misses a frame, capture's ms a frame and its worst frame on b30's flight; slice 5 sets the store's size from the miss rate. **First read** (rslice1j 42180663021 [E], the master 01:52): at 256 KB, b30 finds 98.9% of its forms (224.5 a frame, 2.6 captured) and c10 96.5% (154.9, 5.6); the capture's copy costs 35 µs a frame on b30 and 75 on c10, worst frame 1.1 and 1.5 ms [E, the emulator's clock]. 256 KB holds.
- **Room:** the store's 256 KB plus one bind list a resident texture is part B's to place (`redesign_b.md` §2; c20's heap is the tight place); slice 1 logs the bytes and peak.
- **The texture's bind isn't the form's** (rslice1d 42166510443 [E], the master 23:45; rslice1e confirms the commands): a later draw of the same key differs from its form in 30-95% of compares by map, in TBW, so a texture's levels, widths and addresses follow where it's resident (VRAM or RAM, which levels). The likely shape: a **per-texture bind list** the texture cache writes when the texture lands or moves, which the form CALLs through an address the touch writes. The texture cache owns its words as the geometry cache owns its block. Its conditions: it lives while any frame in flight can CALL it (`geinflight`), a missing texture's touch CALLs the stand-in's list, and the GE's CALL nesting holds (frame list → form → bind list, a skinned batch's sub-list a third) [U: check the depth]. The forms' room is re-measured once the bind leaves them (256 KB filled on every map with it in).
- **The part's whole state, not the path's changes** (rslice1f 42172051868 [E], the master 00:40): with the bind out, c10's draws match their forms in all but 0.7% (from 95%), b30's in all but 13.6%, parting at AMC and ATE, then WMS, JUMP and VTYPE. Today's path writes the material colour and the cut-out test only when they change from the GE's current state, so a form captured after a part that set them leaves them out. Capture runs with the path's own state skips off, so every form carries its part's whole state.
- **The forms' room by key is larger than C1's estimate:** b30 kept 1,906 forms in 768 KB with 1,062 more past it, ~400 bytes a form, ~1.2 MB by key. That's more than the frame list (768 KB) and record buffer (128-224 KB) R frees, so the store is kept by use and sized to the working set (above; ~350 B a form once the bind left).
- **At the switch-over,** today's per-frame walk goes; the state decisions it captures from stay as R's form maker, factored into a function of (part, pass, permutation) that writes into a buffer, so R depends on the rules, not on the old walk.

## 3. The touch (scratch only in slice 1)

For each part entry slice 2 could take, the ME (with `retained`) writes into a scratch buffer, not the frame list:
1. the geometry block's lock-equivalent: a function in `psp_geo.c` beside `psp_vertex_lock` that refreshes the entry's `last_frame` and returns its address (or says it isn't resident), without converting or copying;
2. the texture's current address and its frame mark (a function in `psp_tex.c` beside `psp_tex_bind` that does the residency lookup and `frame_set_mark`, raises a want when missing, and writes no state words);
3. the node's matrix for a rigid part, from the instance's node copy and the form's node index;
4. VADDR, IADDR, the texture address words, then a CALL to the form.

## 4. The compares (`retainedcheck`)

**Compare 1, the instance:** for each instance, its fields against the records today's path wrote for the same model (`DEFER_MODEL_BEGIN`, `_SHAPE`, `_EFFECT`, the point lights). Any field that differs is counted by field and logged (`R check: instance N differ (field ...)`).

**Compare 2, the part's words:** for each part slice 2 could take, the touch's words plus its form's words, expanded as the GE would read them, against the words today's path wrote for the same part. Today's stream depends on what was drawn before (the lighting and texture memos), so the compare runs **with the memos off**: the run's keep set leaves out `statememo`, `memolight`, `memotex`, `mememo` and `mememorun`, and turns off any of them that the default table carries. A difference is counted by the GE command it's in and logged (`R check: part words N differ (cmd 0x..)`), with the first few dumped.

## 5. The counts slice 1 logs (each heartbeat and at the map's end)

- instances a frame; parts a frame; parts slice 2 could take, and why the rest can't (PCV missing, see-through, extra pass, near the eye, super high, skinned);
- the instance list's bytes a frame and its peak;
- the forms' count and bytes; the touches a frame; touches that found a block or texture not resident;
- the two compares' differences.

## 6. The proof (emulator, the lab's sierra-batch)

On b30's route (to game time ~2,700), c10, c20 and d20, strict, with `retained retainedcheck` and the memos off:
- **play and both seeds identical** to the same build without `retained` (R draws nothing, so the simulation can't change);
- **compare 1: 0 differences;**
- **compare 2: 0 differences** for the parts slice 2 takes;
- the strict interpreter clean;
- the counts in a findings row, and the share of parts slice 2 takes on each map in `redesign_r.md`.

Slice 1 is done when those hold. It gets no PSP run: it changes no time the player sees.

## 6b. Still objects (RSO)

RSO (on by default) sends a still object's records again without the model calls, so the instance list never sees those objects. Slice 1's proof runs with `rsooff`. **The rule from slice 2 on:** with `retained` on, an object R takes is drawn by R every frame, and RSO stands down for it (an instance plus forms is what RSO's re-sent records approximate, at less cost). RSO keeps the objects R can't take whole (those with see-through or other immediate parts). Its ~0.8-1.1 ms of record lane [E] moves into R's own gain; the milestone pair runs R against today's default (RSO on), so R pays for it there.

## 7. What slice 1 doesn't do

- draw anything from forms (slice 2);
- skinned parts, extra passes, the level, the light pass (slices 3, 4, 6, 7);
- move the frame list or the record buffer's room (slice 5 logs the store; the switch-over frees the room).
