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
  - **Counted (2026-11-12, builder; the lab's drawchange1, sierra-batch 43230535738, Sierra-III builder/drawcensus 4a1b530; rows `drawlist_unchanged_*`) [E, exact counts]:** an object instance counts as unchanged when its node matrices (bitwise), its lights and its form (model, level of detail, permutations, change colours, functions, flags) equal its own last frame's.

    | Stretch | Instances a frame | Unchanged | Their share of the parts' GE indices |
    |---|---:|---:|---:|
    | b30_play2, the flight | 85-119 | 53-62% | 30-54% (30% at the peak, 80k indices) |
    | b30_play2, the beach | 51-63 | 66-75% | 51-61% |
    | b40_play1, the fights | 54-70 | 62-75% | 35-50% |
    | b40_play1, the snowfield | 18-77 | 87% | 68-74% |

    - By kind: scenery 75-90% unchanged everywhere, equipment ~90-97%; posed units are the changed half (bipeds 0-22% in a fight, 54-88% in lulls; weapons follow their holders); of the changed, the node matrices moved in 74-100%, the lights in 5-32%, the form in 14-52%.
    - **So B stands, for the static half:** about a third (the flight's peak) to three quarters (the snowfield) of the ME's emitted words would be drawn again unchanged. The fights' worst windows are carried by the posed units, which B doesn't touch: those are the posing box's and the far tiers' ground (D2).
    - Not counted here: the level (not an object instance), first person and the sky; the PSP's pacing (more ticks a frame, so fewer unchanged posed units and as many unchanged statics).
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
- 2026-11-12: the change census counted (§5, B): B stands for the static half.
- 2026-11-12 15:2x: v0's design (§10, builder): the ground looked up, what's already built on it, the sticky origin found as the prerequisite, the slices and the count that sizes them.

## 9. Next

1. ~~**The change census [E, exact counts]:** an emulator batch over b30_play2 and c20_play1 that counts, each frame, the drawn instances whose key, node matrices (bitwise), light set and passes equal the last frame's, by kind. Its answer sizes B; nothing is built before it.~~ Done 2026-11-12 (§5).
2. ~~**The box's v0 cut from that count:** records with stamps, reused unchanged; captured with `PSP_CAPTURE=draw_list` and priced alone on the PSP (`bench-hw`, the harness's three modes).~~ Designed in §10; its first step is §10.6's count (s0).

## 10. v0's design (2026-11-12, builder)

### 10.1 The ground, looked up first

`tools/levercheck.py --ground model-walk` and `--ground me-replay` (the lab, 2026-11-12 15:1x), the tries that bear on this box:

- **RSO [in, on by default]:** a still object whose `render_model` inputs equal, byte for byte, the ones its kept records were made from has those records sent again and its walk skipped (`FIXES_RSO`, `psp_ge.c` `psp_rso_find`/`rso_*`). Sent again a frame: b30 71 of 93 offered, a30 65 of 134, c10 28 of 40 (rso4b, lab 39762411453); rsocheck 0 differ. **Scenery only by default** (`rsoall` offers every type); no point lights, effects, first person, shadow or sky in its key; off under `sspr`. **This is already B on the main CPU's side, for scenery.** The ME still replays every sent record in full.
- **PDCCALL [stopped, revive_if under R]:** a part whose record matched last frame's was CALLed from a retained block. c10 pair frame +1.2-2.3% [M, PSP-1000 41216890230], the ME's job unchanged (~40.7 ms); its ceiling was the copies' word time (3.5-4.3 ms of ~40); its store didn't fit b30's heap. It kept the key, the hash, the replay and the built parts (`redesign_plan.md` R).
- **PDC [stopped]:** prebuilt words copied into the old list through the old layers, bit for bit, from eDRAM; pairs even to +1.6 ms.
- **DRAWPART3 [stopped]:** the walk in two stages over runs of models; +0.2 to +1.2 ms drawing [M, PSP-3000].
- **R, retainedloop and retainedshader [in]:** the per-part loop's calls kept per model key and a bitmap's key once a map: b30 frame −2.9 to −4.8% [M, PSP-3000 42494203180] (the cost map's row "Doing less work").
- **The cost map's line** (`cpu_cost_map.md` §4, "Doing less work (reuse, skipping)": ceiling "lever by lever"; drawing's reuse taken so far is RSO, LIGHTREUSE and R slice 5 step 3). v0 attacks that line on the ME's side, where nothing is reused yet: the replay (`frame_timeline.md`: the ME is the frame's bound on d20's flashlight and close behind on b30's flight).

**What's different this time:** PDCCALL decided per part on the ME, by a hash of words it had already built, and CALLed blocks whose matrices it still rewrote. v0 decides per instance on the main CPU, by RSO's key, which the walk already pays for; the ME neither decodes, replays nor hashes a kept instance's records; and the words it CALLs include the matrices, which needs §10.3. The store is sized by a count (§10.6) before anything is built.

### 10.2 The shape

1. **The record is RSO's entry, widened.** The main CPU keys an instance as RSO does and, where the key holds, skips the walk. v0 offers every type RSO's key can hold (`rsoall`'s set) and keeps the per-type split as a count, so the census's equipment (~90-97% unchanged) and still weapons and props come in beside scenery.
2. **Handed over by reference, not copied.** Today a kept entry's records are copied into the record buffer (`memcpy` in `rso_send`, counted as `rso_sent_words`). v0 hands the ME a short record (the entry's offset and its generation) and the ME reads the records in RSO's pool. RSO's pool then frees an entry only when every frame that handed it over has finished on the ME (the frame stamp the ME's job ends with), so compaction waits on the fence (posing's lesson: hand-overs pass ranges, not copies).
3. **The ME keeps the words it wrote.** Replaying an entry, the ME writes that instance's words (bind, state, lights, matrices and bone slots, draws) into a block in a main-RAM arena, writes it back from its cache, and puts a CALL to it in the frame list. Handed the same entry and generation next frame, with §10.3-10.5 unchanged, it writes only the CALL.
4. **Instances outside the key** (posed units, anything moved, first person, point-lit objects until §10.6 says the lights can join the key) replay as today. They are the changed half the census counted; they are the posing box's and R's instance-per-object ground (§10.7).

### 10.3 The origin, the prerequisite

Every model matrix, bone slot and point light is written relative to the GE's world origin on the ME (`relative()`, `psp_ge.c` ~4940; point lights `position - ge_eye` ~11536), and the origin follows the eye: each frame `ge_eye` is the camera's position rounded to a value the GE keeps exactly (~6399, LK22's level_exact). So **an instance whose world state hasn't changed still writes different words every frame the camera moves**, which is nearly every frame of play. The census counted world-space state (§5); without this step its "unchanged" instances would not produce unchanged words.

v0 holds the origin still: `ge_eye` on a grid of N units (values the GE keeps exactly), moved only when the eye leaves its cell (with a margin, so the eye on a boundary doesn't hop it back and forth); the view carries the remainder, as `level_exact`'s shift already does (~6414). A hop re-emits every instance once.

- **Precision [D, U]:** the GE keeps a matrix word's top 24 bits (15 of mantissa). The origin at the eye was chosen because a hundred units out a matrix lost ~1 mm, several pixels beside the camera (the comment at ~6385). Within N units the loss scales with N: at N = 8 about a twelfth of that. First person is never kept and can keep the origin at the eye (its own matrices, as today). Proof: shots on the near views, Sheepdog01's look (§10.5).
- **N** is a keep-word number, sized by §10.6's count of hops a minute against words reused.

### 10.4 What the camera decides on the ME

The ME's replay makes some answers from the camera, and a kept block is CALLed only while they are the same: the near-plane and guard-band test (the immediate cut), the size floor (`model_pixels`), the far tiers' choice, a reflection's camera fade (RSO already keeps its fractions, `rso_fades_same`), and nearauto's tally of what is near (counted, not written into the words). Each is kept with the block and tested again in one pass per instance; a changed answer replays the entry. Words made per vertex from the camera (planar fog's colour) are never kept: their instance replays.

### 10.5 The touch, the arena and the proofs

- **The touch, as R §2.3:** each kept part's geometry block refreshed (`last_frame`, its address) and each texture marked in use (its address, or a want). RSO already does the geometry half on the main CPU (`rso_send` locks each part's buffers again and drops the entry if one moved) and pretouches the textures; v0 keeps that and adds the texture addresses the block's words hold. An address that moved (the geometry cache, a texture moved in VRAM, vrammodels' copies) replays the entry. A block's words carry no address the touch hasn't checked.
- **A block stands alone:** today's words depend on what was drawn before (the lighting and texture memos skip state the GE already holds; R slice 1 compared with the memos off for that reason). A block is written with the memos forgotten at its start, so it sets every state it uses, and they're forgotten again after its CALL, since the GE's state after it is the block's. The extra state words are paid once when a block is made, not each frame it's CALLed; s0 counts the words as today's replay writes them, so its reusable words are a slight undercount of a block's.
- **The arena:** main RAM (the GE can't read eDRAM), two generations: a block replaced in frame F is freed only when the GE has finished the last frame that CALLed it. Its room is what v0 takes away: RSO's copies in the record buffer and the frame list's words for kept instances (C2's index copies go when a part is CALLed). Its size comes from §10.6 before it's built.
- **The proofs:** drawing only, so play and both seeds identical, box on and off (statecrc); pictures by framecrc against the old path within the sides' spread, and shots of the near views for the origin's precision. The ME's code reaches no VFPU instruction or system call (it runs on the ME).
- **The switch:** `PSP_DRAWLIST=1` builds it in, off by default; under it the keep word `drawlist` turns it on, and `drawlistcheck` replays every kept entry beside its CALL and counts the words that differ (the exactness check, with the origin held still on both sides).

### 10.6 The slices

| # | Slice | Tag | Proven by |
|---|---|---|---|
| s0 | **The count** (`drawreuse`, Sierra-III builder/drawreuse 73597ab; the lab's drawreuse1): per frame, the ME's model-part words by instance, split kept (RSO's key, `rsoall`'s set) and replayed; for the kept, those whose block would survive at N = 2, 4 and 8 (hops a minute), the camera answers (§10.4) and the touch (§10.5); the arena's peak bytes, both generations; RSO's copied words. b30_play2, c20_play1, b40_play1, d20, c10 | [enables:draw list] | exact counts [E]; it sizes the arena against each map's heap and gives the ME words v0 removes, read into ms on C4's per-kind ME times [U on M] |
| s1 | The origin held still (§10.3) | [box:draw list] | play and seeds identical; framecrc; shots of the near views |
| s2 | The hand-over by reference (§10.2 item 2) | [box:draw list] | play and seeds identical; framecrc identical (the same words) |
| s3 | The kept blocks, the touch and the camera answers (§10.2 item 3, §10.4, §10.5) | [box:draw list] | `drawlistcheck` 0 words differ; framecrc; the strict interpreter |
| cap | `PSP_CAPTURE=draw_list`: a frame's handed-over records (entries, RSO's pool, the camera, residency) over b30_play2's flight | [enables:draw list] | the box alone on `bench-hw` over the capture, warm, emptied and crowded, the GE beside (REBUILD.md §5) |
| — | **In the frame:** two builds, `PSP_DRAWLIST=0` and `=1`, b30_play2 and c20_play1 | | one PSP pair each (the lead calls the cut-in) |

Nothing past s0 is built until s0's count is read: if the kept share of the ME's words is small on the heavy stretches, or the arena doesn't fit b30's heap, the design changes here first.

### 10.7 Against the budget (§4), honestly

- **The ME (budget ~12 ms):** v0 takes the kept instances' share of the models' replay. The census's unchanged share of the parts' indices is 30-54% on the flight and 51-61% on the beach (§5) and the models are 37.8 of the flight's 67.9 ms of replay (C4) [M]; if the words follow the indices, that's ~11-20 ms of the flight's ME [U], before s0 counts it. With MEXEC r2 and R's forms for the changed half, the row is reachable; v0 alone is not 12 ms.
- **The main CPU (budget 11 ms):** v0 moves it little: RSO already skips scenery's walk, so v0 adds the other still types and drops the copies. The main CPU's 35.8 ms is mostly the posed units' per-part chain and the walk itself; that's **v1's**: one instance per object for the changed half (R §2.1), its node matrices handed over by the posing box rather than copied. v0's standalone price is read against the ME's row only, and this page says so in its result.
- **The GE (33.3 ms):** a CALLed block is read from main RAM as the list was; v0 neither adds nor removes GE work. The GE lists box's row.
