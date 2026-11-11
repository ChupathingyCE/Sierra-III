# The draw list box

**v0 spec, 2026-11-11 (master).** The box that turns the frame's visible objects into the GE's per-instance words: which instances are drawn, with which form, matrices, bones and lights. It replaces R top-down as the plan (`redesign_r_topdown.md`: R as built is priced and is not this box; its pieces come in below as slices where they fit). The spine is the lab's `research/opportunities/REBUILD.md` (§3.4 for this budget, §4 for this page's shape). Evidence labels as `CLAUDE.md`: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## 1. Contract

- **Reads:** the frame's visible instances (the visibility box's output: object, model, permutation, level of detail, the clusters it stands in); each instance's node matrices (the posing box's output, or the engine's `object_compute_node_matrices` until posing switches over); the lights touching it (the lighting box's output, today `object_lights`); the forms (each model part's GE words, baked: R's ring and the bake's pack).
- **Writes:** one record per drawn instance a frame: its form, its matrices and bones in the form the GE takes, its light words, its pass list; and the list of those records for the GE lists box to emit.
- **Who reads it, when:** the GE lists and forms box, in the frame's drawing, after visibility and before the frame's end. Nothing in the tick reads it: it is drawing only, so the multiplayer boundary doesn't reach it.

## 2. Data

- **An instance's record, carried between frames:** its key (object, model, permutation, LOD), its form, a stamp of what it was built from (its node matrices' change count, its lights' set, its passes), and the words it emitted. A record whose stamp is unchanged is drawn again from its words; nothing is recomputed.
- **The forms:** R's ring (128 KB on b30, `retainedringkb128`) and the bake's pack, as today.
- **Footprint goal:** records sized to the frame's drawn instances (~90-250 on the heavy sections [U]), owned by the box, one writer a line (the ME writes a record's words, the main CPU its key and stamp).

## 3. Placement

- **Today:** the main CPU walks the drawing and builds the records (35.8 ms a frame on b30's flight, b151 [M]); the ME's render job replays them into GE words (48 ms with R, the whole b30 run [M]).
- **v0:** the same split, with the work cut by B (§5). Whether the main CPU's walk moves to the ME, or the ME's emitter to the main CPU's VFPU, is decided by measurement on the PSP once the box is priced alone.

## 4. Budget (REBUILD.md §3.4, research)

| Unit | Today (b30's flight, ms a frame) | v0 budget at 33.3 ms frames | Cut |
|---|---:|---:|---:|
| Main CPU: the drawing's build | 35.8 [M, b151] | **11** | ~3.3x |
| ME: the render job | 48 with R [M, whole b30 run] | **~12** | ~4x |
| GE: busy drawing | 59.9 [M] | 33.3 (the GE lists box's row) | ~1.8x |

Per instance: at ~150 drawn instances a frame [U], 11 ms is ~73 µs of the main CPU an instance and 12 ms ~80 µs of the ME, all work included. The box's standalone price is read against these.

## 5. Means

- **B, work on change: the premise and the largest lever.** An instance whose form, matrices, bones and lights are unchanged since the last frame costs no ME work and no new record: its last words are drawn again. R as built emits every instance every frame, which is the plainest reason its render job didn't fall (`redesign_r_topdown.md`, the milestone priced). **Its worth is unknown until counted:** the first slice (§9) counts, per frame on Sheepdog01's pad benches, the instances unchanged between frames, by kind (scenery, still props, still weapons, posed units and vehicles, the far tiers).
- **A, data owned and packed:** the record and its form laid out for the ME's walk, so a frame's records stream through the ME's data cache. R's ring is the start.
- **C, both cores on separate data:** the main CPU builds the frame's keys and stamps; the ME emits only the changed records' words. Neither writes the other's lines.
- **The ME's code:** ~47k instruction misses a frame, ~93% capacity misses [U on E x M] (nearme1). MEXEC r2 (the ME's hot code in eDRAM) is in R's milestone and didn't move the job; the emitter as one small function per part kind (R §2) is the box's form.
- **The near parts' corner loop:** ~5.6 ms of the ME a frame on the ride [M]; the run's sphere bounds are spent (SLOTMASK ~2%, CORNERSETTLE ~4-5% [E]). What's left there: fewer corners per run at the bake (R §10 item 3). Under B, the loop runs only for instances whose bones changed.

## 6. What it replaces

- **Entry points [D]:** the engine's `render_model` and the PSP's ME render job (`psp_ge.c`'s model part draws), cut over whole under the box's build switch.
- **R's pieces, as slices where they fit:** the forms and their ring (R's slices 1-4), the bypass (`retainedbypass`), the memo on the ME (`retainedmemo`, the start of B's stamp), T1/T2a/T3a (the near parts' first pass) and MEXEC r2. Each is kept where it serves the box's budget and dropped where it doesn't.
- **Earlier tries on this ground:** `tools/levercheck.py --ground` for the model walk, the ME's replay and the drawing's records lists them; the box page's next revision quotes that output.
- **Upstream manifest (REBUILD.md §5b):** `render_model`, `rasterizer_model_draw` and the model part functions they call, at the pin; filed as `psp/boxes/draw_list.upstream` with the first code.

## 7. Proof

Drawing only: play and both seeds identical with the box on and off (statecrc), and pictures (`framecrc`, shots) the same within the far ladder's rules. In the frame: two builds, the old path only and the box only, on the same pad bench.

## 8. States

- 2026-11-11: spec v0 (this page). Budget from REBUILD.md §3.4.

## 9. Next

1. **The change census [E, exact counts]:** an emulator batch over b30_play2 and c20_play1 that counts, each frame, the drawn instances whose key, node matrices (bitwise), light set and passes equal the last frame's, by kind. Its answer sizes B; nothing is built before it.
2. **The box's v0 cut from that count:** records with stamps, reused unchanged; captured with `PSP_CAPTURE=draw_list` and priced alone on the PSP (`bench-hw`, the harness's three modes).
