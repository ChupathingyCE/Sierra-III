# R, slice 1: the scaffold (a build spec)

The first slice of part R (`redesign_r.md` §3). It changes nothing the player sees: R's output is built and checked, never drawn. It's what the master builds first, and its proofs are emulator-only (the redesign rule). Written against tock162.

Labels as `redesign_r.md`.

## What it delivers

1. **The keep words:** `retained` (R's path; in slice 1 it builds R's output beside today's and draws today's) and `retainedcheck` (the compares below; implies `retained`). Both off by default.
2. **The instance list,** written by the main CPU beside today's records.
3. **The form table,** baked into the pack, and its loader.
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

**Baked** by a new step in the bake after modelconvert.py (`tools/formbake.py`, run where modelconvert runs: `bake/bake.py` ~627). Per model part that has a PCV block and draws opaque in one pass (slice 2's set), one form:
- the state words today's path writes for the part's base pass: blend, alpha test, culling, the texture's format, size and filtering, the CLUT's format (no addresses);
- the vertex type and the PRIM over the PCV block's records (no VADDR or IADDR);
- the rigid part's node index (from its first vertex, as `psp_ge.c` ~16884 reads it each draw).

Stored after the PCV region with a table indexed like the PCV table (the pack's header words: the next free ones after PCV's 11-15). A runtime that doesn't know them ignores them (as with PCV).

**Loaded** with the pack; its bytes logged (`R: forms N, KB K`), against C1's estimate (~200-300 bytes a form).

**Words the bake can't know** are left for the touch: addresses, and anything the immediate path decides per frame. The first build lists every state word the immediate path writes for a part (`psp_ge_draw_model`'s path) and classes each as baked, touched or per frame; the list goes in this page.

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

## 7. What slice 1 doesn't do

- draw anything from forms (slice 2);
- skinned parts, extra passes, the level, the light pass (slices 3, 4, 6, 7);
- move the frame list or the record buffer's room (slice 5 logs the store; the switch-over frees the room).
