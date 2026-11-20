# The draw list box

**v0 spec, 2026-11-11 (master).** The box that turns the frame's visible objects into the GE's per-instance words: which instances are drawn, with which form, matrices, bones and lights. It replaces R top-down as the plan (`redesign_r_topdown.md`: R as built is priced and is not this box; its pieces come in below as slices where they fit). The spine is the lab's `projects/sierra/research/opportunities/REBUILD.md` (§3.4 for this budget, §4 for this page's shape). Evidence labels as `CLAUDE.md`: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

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

- **In the two-core tick** (Sheepdog01 2026-11-13 11:02, decisions.md; the lab's `projects/sierra/research/opportunities/two_core_tick.md`): **the draw list isn't tick work.** It runs in the frame's drawing, after the tick's join, and reads the tick's final node matrices: serial to the tick, on both cores of its own. **Main CPU:** the walk and the records (RSO's kept walks, today 21.8 of ~38.5 still models a frame re-sent on b30 with B76's fix; v1's records). **ME:** the render job's replay into GE words, which is two_core_tick.md's "drawing's ME share" (~12 ms a frame at 30/30, 19-33 today [M]): every millisecond this box takes off the render job is ME time for its share of the tick. **Working set:** RSO's pool (24 KB of entries on the tock), a record per drawn instance, and the geometry the replay reads; the instruction-cache split by step applies here too (the walk's code on the main CPU, the emitter's on the ME).

## 4. Budget (REBUILD.md §3.4, research)

| Unit | Today (b30's flight, ms a frame) | v0 budget at 33.3 ms frames | Cut |
|---|---:|---:|---:|
| Main CPU: the drawing's build | 35.8 [M, b151] | **11** | ~3.3x |
| ME: the render job | 48 with R [M, whole b30 run] | **~12** | ~4x |
| GE: busy drawing | 59.9 [M] | 33.3 (the GE lists box's row) | ~1.8x |

Per instance: at ~150 drawn instances a frame [U], 11 ms is ~73 µs of the main CPU an instance and 12 ms ~80 µs of the ME, all work included. The box's standalone price is read against these.

**The ME's render job by stretch on the tock today** [M, 2026-11-12, tock162 9d00a903 (far level on, farvram, vmpack), `timerson mekinds`, Sheepdog01's pads to padend; the ME's own time a render job, one job a frame; the lab's sierra-hw 43312846529 (b30, PSP-1000, 2 runs) and 43314233107 (b40, PSP-3000, 1 run)]:

| Stretch | ME render, ms a frame | Main CPU waiting on it, ms a frame | Against v0's ~12 |
|---|---:|---:|---:|
| b30 cinematic | 18.2 | 0.1 | 1.5x |
| b30 flight | 33.0 | 0.2 | 2.8x |
| b30 landing | 32.8 | 0.1 | 2.7x |
| b30 beach | 20.1 | 0.2 | 1.7x |
| b30 bunker | 17.7 | 0.1 | 1.5x |
| b40 cliff | 18.8 | **7.4** | 1.6x |
| b40 snowfield | 17.5 | 0.0 | 1.5x |
| c20 first 120 s (300-3600) | 25.5 | 0.5 | 2.1x |
| c20 play (3600-13130) | 27.8 | 0.3 | 2.3x |
| a30 opening (300-3600) | 22.2 | 0.6 | 1.9x |
| a30 play (3600-9000) | 21.1 | 0.0 | 1.8x |
| d20 opening (300-3000) | 34.1 | 3.0 | 2.8x |
| d20 swamp (3000-6000) | 35.6 | 1.0 | 3.0x |
| d20 deeper (6000-9000) | 35.2 | 2.0 | 2.9x |

These replace §10's "~11-20 ms of the flight [superseded 2026-11-12: 33 ms measured, §4; U]": the flight and the landing are ~33 ms, about twice the rest, so the far ladder and kept words pay most there. On b40's cliff the main CPU waits 7.4 ms a frame for the render job: there the ME's row is on the frame's critical path. (Instrumented: `mekinds` and the timers inflate both units; read the stretches against each other. c20: the lab's 43314261608, PSP-3000, c20_play1 to padend; a30: 43318228389, PSP-3000, a30_play1 to 300 s. c20 holds 25-28 ms through its whole recording. **d20 is the heaviest measured: 34-36 ms a frame through its first 300 s** (the lab's 43331666648, PSP-3000, d20_play1; its lights and see-through parts read heavily, gereadskind there).)

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
- **Earlier tries on this ground:** `projects/sierra/tools/levercheck.py --ground` for the model walk, the ME's replay and the drawing's records lists them; the box page's next revision quotes that output.
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

3. **The first slice: the origin held still (`ge_eye`)** (the lead, 2026-11-12 15:35: the box's prerequisite and every retained form's, R's included). `ge_eye` follows the eye every frame (`psp_ge.c` ~6399) and every model matrix, bone slot and point light is written relative to it, so an unchanged instance's words change whenever the camera moves, and no word can be kept. The slice holds the origin on an N-unit grid with the view carrying the remainder (as level_exact's shift already does); s0 (drawreuse1, the lab's 43309859657) counts the hops at N = 2/4/8. Proof: pictures in the emulator; play and seeds identical (drawing only).

## 10. v0's design (2026-11-12, builder)

### 10.1 The ground, looked up first

`projects/sierra/tools/levercheck.py --ground model-walk` and `--ground me-replay` (the lab, 2026-11-12 15:1x), the tries that bear on this box:

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
- **Built and proven (2026-11-12 17:0x; Sierra-III builder/originheld, the lab's originheld1-3) [E]:** play and both seeds identical on b30, c20, d20, a30; the origin moves 1-4 times per 300 frames at N = 8; pictures differ by scattered speckle only (0-188 px a shot by more than 24 levels). First person keeps the eye-following origin (`use_projection` swaps the origin, the view and the model's point lights together): with the held origin its weapon took most of the speckle (originheld1), and before its point lights moved with the swap it was lit from the wrong place (originheld2).

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

**s0's first read (drawreuse1, 2026-11-12 15:5x; sierra-batch 43309859657, builder/drawreuse 73597ab on tock 4d07a4ae) [E, exact counts]:**

| Map (pad to game time 3000) | Instance words a frame | Reusable, origin left out | Origin held at 8 units | at 2 units | Origin hops per 100 frames (2 / 8 u) |
|---|---:|---:|---:|---:|---:|
| b30_play2 | 17,587 | 21.7% | 21.4% | 20.3% | 7.7 / 1.9 |
| b40_play1 | 8,747 | 11.5% | 11.4% | 11.0% | 4.7 / 1.5 |
| c10_play1 | 12,138 | 31.0% | 30.7% | 30.4% | 2.5 / 1.1 |
| d20_play1 | 10,841 | 13.7% | 13.5% | 13.0% | 5.5 / 1.6 |

- **The origin is not the obstacle:** held at 8 units it costs ~0.3 points of reuse; §10.3 stands, N = 8.
- ~~**Records that change are:** ... drawreuse2 names the record kind and field that change.~~ **What changes is real (drawreuse3, sierra-batch 43316117392, 2026-11-12 16:2x) [E, exact counts]:** by kind and field, a frame: b30 the node matrices 16.9 instances, the shape's centroid 10.9, point lights 3.2, distant lights 2.8, the far form's first tint 1.0, a reflection 0.7, the rest ~0.2-0.3; c10 nodes 15.8, point lights 4.4, centroid 3.8; d20 point lights 7.3 (their colour 6.8), nodes 6.9, centroid 6.4, multipurpose 2.0. No stray field: the instances that change are the posed and moving ones, about a third of them (b30 19.5 of 57), matching the census's two thirds unchanged (§5). **But they carry most of the words,** so by words B reuses only ~12-31%: the census's share of indices (30-75%) overstated B's share of the ME's work. The far form's tint (one word, ~1 instance a frame) is the only field worth a look on its own.
- **rso keeps nothing on the tock** (bugs.md 2026-11-12 15:5x): the far level's record isn't in rso's allowed kinds, so `rsoall` changed nothing here. v0's record takes the far form as a kind (§10.2 item 1).

**What s0 says for the order** (for the lead): B (v0 as designed) is worth ~5-12 ms of the flight's ME; R's forms for the changed instances are the larger ground on the same row. Building v0's B first is cheap (it rides RSO, and the origin costs little), but it shouldn't be read as the box's answer.

Nothing past s0 is built until s0's count is read: if the kept share of the ME's words is small on the heavy stretches, or the arena doesn't fit b30's heap, the design changes here first.

### 10.8 v0's B, as it will be built (2026-11-12 16:2x, builder; the lead 16:20: B small, as the skeleton the forms then fill)

1. **The far form in the key (main CPU).** RSO refuses every walk holding a `DEFER_DRAW_FAR` (bugs.md 15:5x). Taking it is more than a kind on its list: the far choice and the sprite's are made in `render_model` from `level_of_detail_pixels` (`psp_far_draw`), which RSO's key doesn't hold, so a kept far walk would be sent again after the object came near. The key takes the far answer (far or not, sprite or not, the tinted groups' mask), and `rso_send` locks the far block again (`psp_geo_lock`, as it locks each part's buffers) and drops the entry if it moved. Proof: rsocheck 0 differences, play and seeds identical. **Built and proven (2026-11-12 16:4x; Sierra-III builder/originheld c753104; the lab's rsofar1 43321001985) [E]:** play and seeds identical on b30, d20, a30; rso sends again 17.5, 22.3 and 6.3 walks a frame where it sent none; rsocheck's few differences (1 of 64,812 walks on b30, 22 of 23,592 on d20) are a far block that moved in the geometry cache, which `rso_send`'s relock catches.
2. **A kept walk is marked, not only copied (main CPU → ME).** `rso_send` writes one short record ahead of the copied ones: the entry's slot, its generation and the records' length. The copy stays in v0 (hand-over by reference is §10.2 item 2's later step); the ME needs the records only when its block is gone.
3. **The ME's block (ME).** On a marked walk whose block is valid (§10.3's origin unchanged, §10.4's answers and §10.5's touch the same), the ME writes the touch words and a CALL and skips the records. Otherwise it replays them into a block: pspgu's `gu_list->current` moved into an arena block for the walk, the memos forgotten at its start (§10.5), the replay run as today, a RET written, the block written back from the ME's cache, `current` restored and the CALL written. Inline data the replay takes (index copies) lands in the block with it. A block that would outgrow its room is abandoned and the walk drawn in the list as today.
   **What writing into a block meets in `psp_ge.c` today** [D, read 2026-11-12 16:5x], each handled while `current` is in a block:
   - **The list's bounds:** `list_take` refuses by `sceGuCheckList()` (current − start) against the frame's limit, and `show3_room` checks `current` against the part's end (geshow3) and may jump. While in a block both see the block: `gu_list->start` moves with `current` (restored after), and a block-local room check replaces `show3_room`'s part test; a block never jumps.
   - **The stall address:** every pspgu draw calls `sceGeListUpdateStallAddr` (wrapped, batched by STALL_BATCH), which would hand the GE an address inside the block. Updates are held while a block is written and the next one after the CALL covers it.
   - **The cached list (listcachedall, build 48/114):** words go through the cached view and are written back at stall updates (`list_flush_pending`, `cache 0x1B` per line). A block's lines are written back and dropped when the block is closed, before its CALL is written: the GE reads it from memory.
   - **The memos:** `lit_forget_state(&lit_me, 2)` and `psp_tex_me_memo_forget()` at the block's start and after its CALL (`me_memo_run_start` does the same at a job's start), so the block sets every state it uses and later draws don't trust state the block set.
   - **Inline data:** `sceGuGetMemory` regions (index copies, cut pieces) are taken from `current`, so they land in the block; their addresses are the block's and stay valid while the block lives.
   **What a block depends on, read in `psp_ge.c` for the build** [D, 2026-11-12 18:4x, builder], so the CALL is taken only where the replay would have written the same words:
   - **The camera's answers are thresholds on `model_pixels`, and `model_near`.** `psp_ge_model_shape` makes both from the shape record (the sphere to the camera). The draw path tests `model_pixels` against the size floor (`psp_ge_draw_model_body`), the passes left off small (`pass_small`), and the reflection's visor and env passes (`reflection_strength`'s `>= 24` and `env_pixels`). A block holds, for each of its shape records, the interval [lo, hi) of `model_pixels` inside which every test made while it was written answers the same: each test goes through one helper that narrows the interval. A reuse runs the shape's camera half alone (the sphere to the camera, `model_pixels`, nearauto's tally of what is near, which a CALL must still make) and needs each new `model_pixels` inside its interval. A test of `model_pixels` outside the helper would let a stale block through: under `PSP_DRAWLIST` the build greps the draw path for one and fails.
   - **Never in a block** (abandoned at the first sign; the walk is replayed into the list as today): `model_near` (the cut writes triangle lists made from the camera), first person, the sky, camouflage (its shimmer drifts with `taa_frame`), a texture missing or drawn as a stand-in (the ME's `want`), a conversion wanted (`convert_want`), planar fog, and any record kind but begin, shape, effect, point light, multipurpose, reflection, detail, draw model and draw far (RSO refuses most of the rest already).
   - **The view and projection stay out of a block.** A kept walk is never first person, but `model_first_person` may be left set by the walk before, and the shape would then switch the projection inside the block with the view as the camera is now. Before a block opens or is CALLed: `use_projection(0)` if first person is set, and `sceGumUpdateMatrix()`, so no matrix left dirty is written into it.
   - **After a CALL** the GE holds what the block's words set (the model matrix, bones, lights, the texture, blend, cull, alpha test), the same as after a replay, since the block's words are a replay's with the memos forgotten. What differs is the CPU's idea of it: the lighting and texture memos are forgotten again and gum's model matrix is marked dirty, so the next draw sets its own. The state a block enters with (depth range, fog, the near plane's projection) is the frame's at that point in the list, as for the replay; nothing in a block reads it on the CPU (planar fog, which does, is refused above).
   - **The touch: the owner of every address in the block's words**, noted while the block is written and checked before each CALL (any one differing replays the entry):
     - *Geometry:* the vertex and index addresses are the geometry cache's (a model's block or a baked level, `psp_geo_generation`; RSO's relock checks them on the main CPU) or a skin plan's: the converted slot, its generation and the plan's pointer (`converted[]`, `converted_generation[]`, `skinned[]`), since the main CPU's sweep (`converted_sweep`) frees plans.
     - *VRAM copies* (vrammodels): each `vm_redirect` noted as its source and target; the check reads the table without adding an entry.
     - *Textures:* every bind in the block (the base, the env cube, glow, detail, change and sheen masks) noted as its key and address. The check is `resident_at[index]` equal to the address, and it does again what a bind does besides words (`frame_set_mark`, `texlod_mark`, `bind_counts`), so a texture in use isn't evicted for not being bound.
     - *Inline data* (`list_take`: index copies, cut pieces) lands in the block and lives with it.
   **Built (2026-11-12 19:xx-22:xx, the builder's helper; Sierra-III builder/drawlistb acd5229, under `PSP_DRAWLIST`, `psp_ge.c` "the draw list's blocks" after rso's code) [D]:** where the page was silent, the simplest correct form, as chosen:
   - **The mark:** `DEFER_KEPT` (id, the records' words) ahead of the copied records; the id is `RSO_ID`, a number of its own stamped each time `psp_rso_capture_end` writes the entry, so the same id always means the same records. The replay's table is 1,024 slots taken by the id's low bits (a collision is a miss, then a new block).
   - **On a CALL the walk's other records are still carried out, its draws left out** (`dl_w.skip_draws`): they leave the model's state (nodes, lights, colours, the shape's camera half and nearauto's tally) as the replay would, and each shape's `model_pixels` is checked against its range as it comes. ~~skip the records~~: the draws are where the words are; the state records' own cost on a CALL is for the standalone price to say (a later step can skip them once nothing after the walk is shown to read their state).
   - **What is checked before a CALL:** `ge_eye` bit for bit (the origin held, settled to the world projection first), `model_size_floor` equal (it moves with the GE's load), each shape's `model_pixels` range, and the owners: each part's vertices by the geometry cache's generation, its conversion slot (key, generation, skin plan), each VRAM copy asked again (`vm_redirect`, which counts its reads as a draw does), and each texture's `resident_at`; on a CALL the binds' bookkeeping is done again (`frame_set_mark`, `bind_counts`, `texlod_mark`, `psp_tex_dl_touch`).
   - **Never in a block, as built:** `model_near`, first person, the sky, camouflage, `planar_far` set, a texture missing, drawn as a stand-in or through a bind block (`bindblocks`: a third call level), a conversion wanted, a far form's sprite (it faces the camera), more than 4 shapes, 64 owners or 32 textures. A block that meets one, or runs out of room, is given up: its words stay in the arena unused and the walk is replayed into the list (its records carried out twice that once).
   - **Room:** a block opens only with `DISPLAY_LIST_MARGIN` + 12 KB free in the half (list_full asks the margin past every draw); `gu_list->start` moves with `current` so `sceGuCheckList` goes on counting the frame's bytes (the list's limits stay the frame's), and `show3_room` tests the block's own end. `list_take`'s pending range is saved and restored round a block, and the block's lines and its record are written back and dropped (`cache 0x1B` a line, either CPU) before its CALL is written. Stall updates are held while a block is written.
   - **The arena:** 128 KB by default (`drawlistkbN`), the display list's last KB (768 KB becomes a 640 KB list and the arena, one allocation), in two halves written in turn. ~~192 KB from the heap~~ (2026-11-12, drawlistb2): on b30 that left the heap full (45 requests failed by game time 30; the reference's heap ends 48 KB free, 85 KB outside it), so the arena takes room the list had instead: the words it holds are words the list would have held, and the list's own slack is the only room on b30. What the smaller list costs on the heavy stretches (draws skipped with the list full) is part of the box's price. Only blocks in the half being written are CALLed, so a half is free once the GE has finished the last frame that read it; it is written again 3 replayed frames after it was left (the replay of frame N follows the show of N - 2, which waits for that list's finish: `show_picture`).
   - **`drawlistcheck`:** before a CALL the walk is written again into the room past the arena's newest (never switching halves, so the block checked stays callable) and compared word by word, an address inside either block compared as its offset; the CALL is the kept block's. **`drawlistblocksoff`**: the origin held without blocks, the reference side.
   - **psp_build.py** counts `model_pixels`' uses in `psp_ge.c` under `PSP_DRAWLIST` and stops on a change: a size test outside `MODEL_PIXELS_BELOW`/`_AT_LEAST` would let a stale block through.
4. **The arena:** main RAM, two generations, freed by the GE's finish of the last frame that CALLed a block. Sized from drawreuse1's peaks (b30 peak ~76k instance words a frame, of which the kept are the ~21% [E]; with rso as the gate, the kept words only).
   - **pspgu's CPU-side state goes with a block** (2026-11-12, drawlistb2/b4): pspgu builds some words from what its context remembers (`sceGuEnable(GU_FRAGMENT_2X)` sends the texture function it kept; the two map modes each send the other's half), so a block is CALLed only from the context it was written from (fragment 2x, texture function, both map modes, the switches `gu_states`: stale "pspgu" 0.2-0.4 a frame) and leaves the context it left (`dl_gu_write`); a block given up puts the entry context back before the walk is replayed. Its layout is pspsdk's `GuContext` (guInternal.h).
5. **Proof:** `drawlistcheck` (each CALLed block's words against a fresh replay's, the origin held both sides); framecrc; play and seeds identical; strict.
   **Proven in the emulator (2026-11-12 23:xx; Sierra-III builder/drawlistb acd5229; the lab's drawlistb4 43351789798) [E]:** play and both seeds identical on b30, d20, a30 with blocks alone, with `drawlistcheck` and under the strict interpreter; `drawlistcheck` 0 differing words in 272,166 blocks CALLed; strict 0 sites; pictures within the A/A control on b30 (12 of 3,927 frames against 15) and a30 (4 of 4,716), d20 21 of 4,481 scattered single frames (its control in drawlistb2 was 5: a d20 control in one batch is still to run) ~~a d20 control is still to run~~ **d20's control, read 2026-11-12 22:1x (the lab's drawlistb5, sierra-batch 43362232240; findings `drawlistb_d20_control`) [E]:** play and both seeds identical through 7,200 ticks with and without blocks; the A/A (two runs without blocks) differs at 4 frames (game time 920, 1191, 1813, 2567); blocks differ from both references at the same 9 more (490, 584, 616, 2253-2261, 3027), each in every tile: whole pictures, so **a real difference blocks make on d20**, not the emulator's noise (b30 and a30 stayed within their controls). ~~Shots of 2253-2261 on both sides: drawlistb6~~ **What differs (2026-11-12 22:0x, the lab's drawlistb6 43363155465, findings `drawlistb_d20_shots`) [E]:** at frames 1470-1475 (game time ~2,250) the side without blocks draws a cyan energy effect with smoke in the middle of the picture and lights the left wall from it; with blocks the effect isn't there (the room behind it shows) and the wall is darker: 26-51k of 130k pixels in those six shots, 21-192 in the six after. A kept block's words were checked equal (drawlistb4's d20-check), so what goes missing lies outside the block's words: what a kept walk skips with its draws, or state around the CALL. ~~The GE streams compared list by list: drawlistb7~~ **The GE streams (the lab's drawlistb7 43363484030, findings `drawlistb_d20_gestream`) [E]:** 1,180 of 1,589 lists identical (the rest differ in state commands around the CALLs, their draws equal); only lists 1499-1503 (frames ~1470-1474) differ in draws: with blocks ~150-240 fewer a list (824 against 1,062 in 1499), 5-10k fewer commands, the lists either side identical. So the difference is draws lost for five frames, not a block's words. **The cause (2026-11-12 22:2x; findings `drawlistb_d20_cause`) [E]: the list's room, not the blocks' logic.** The arena is the display list's last 128 KB, so with blocks the list is 640 KB, not 768; in the window holding d20's effect a frame wanted 412 KB of lights alone, and with 640 KB the list filled: lights 0.5, decals 0.5 and particles 3.3 draws skipped a frame, against 0 / 0 / 1.3 at 768 KB (the side without blocks skips particles there too: d20 is at the list's edge). drawlistb3's test of the 640 KB list was b30's flight, not a fight. The arena uses far less than it takes (a half's peak 38 KB of 64; ~2.5 KB of words CALLed a frame). **So the arena moves out of the list** (the lead's call on where: the texture RAM cache's carve, VRAM, or a smaller arena), and the PSP pair waits for it. **Moved (the lead 22:14, option (a); 5c7c253) and proven again (2026-11-12 23:3x; the lab's drawlistb9-b11; findings `drawlistb9_proof`, `drawlistb10_d20_control`, `drawlistb11_d20_readswait`) [E]:** the arena is 96 KB (two 48 KB halves, `drawlistkbN` 32-256) at the texture RAM block's front, placed each frame on the main CPU (`dl_arena_now`), the replay forgetting its blocks when the arena or its generation changes (`dl_arena_check`); the list is 768 KB again and nothing is skipped. Play and both seeds identical, `drawlistcheck` 0 words differing and strict 0 sites on b30, d20, a30. d20's pictures: drawlistb9's A/A of 2 was luck; with more controls (drawlistb10, and drawlistb11 with `readswait`, which leaves the noise as it was, so it isn't the reads' timing) d20's A/As run 8-19 frames, and blocks against the same room taken without them (`drawlistroomonly`, c4cdb31) 16-18: **no picture difference is the blocks'.** The room's own cost: the texture RAM cache 2,944 KB against 3,040, and on d20 41 frames draw a texture's stand-in against 39 (1,137 stand-ins against 1,086), the same in every run of a side. The list and the arena are one budget (`box_ge_lists.md` §4). ~~The in-frame pair follows.~~ **The in-frame pair (2026-11-13 01:35; the lab's sierra-hw 43375367364, PSP-1000, 0c66924, b30_play2 to 60 s, `- | drawlistblocksoff`, 2 a side; findings `drawlistb_inframe_b30_psp1000`) [M]: no gain.** The flight's frame, tick and drawing are within the runs' spread (frame 110.0 → 109.6 ms); the cinematic's drawing -1.1%; the ME's render job unchanged (38.3-38.6 ms a job either side over the whole runs). By path: a kept walk CALLed from its block costs the ME 35-41 µs, made 81-94 µs, against 53-60 µs replayed into the list without blocks: ~20 µs saved each over 18-35 kept walks a frame, ~0.4-0.7 ms of a ~38 ms job, inside its spread. The textures lost nothing measurable to the arena (298 loaded against 312, 80 evicted against 85, 0 untextured frames). So v0 over the still half doesn't move the ME's row, as s0's ~21% of words predicted (§10.7); the row is the changed half's, v1's (§11). A frame: b30 19.8 blocks CALLed (983 words), d20 7.0 (318), a30 14.7 (695); made 0.8-1.2; nothing refused or given up; a half's peak 39 KB of 64. The words are rso's offer (scenery by default): about a quarter of the ~3,700 words a frame s0 counted reusable on b30; `rsoall`'s types are the next step for the count. The 640 KB list alone draws b30_play2 as the 768 KB one (drawlistb3). Not yet: the standalone price over a capture, the PSP pair, and whether the state records' replay on a CALL can go. Then the box's standalone price over a capture (`PSP_CAPTURE=draw_list`) and its in-frame pair.

### 10.7 Against the budget (§4), honestly

- **The ME (budget ~12 ms):** v0 takes the kept instances' share of the models' replay. ~~The census's unchanged share of the parts' indices is 30-54% on the flight ... ~11-20 ms of the flight's ME [U]~~ **Revised by s0 (2026-11-12 16:2x):** the unchanged instances hold ~21% of b30's instance words (12-31% across b30, b40, c10, d20) [E], so of the flight's 37.8 ms of model replay (C4) [M] v0 takes **~5-12 ms [U on E and M]**, if time follows words. **The ME's row is carried by the changed instances:** posed units and moving objects, the ones B can't touch. Their cost is R's forms (bind, state, geometry and draw calls made once, ~29-31 ms of the flight's model replay at stake, R §2.4) and the posing box. v0 alone is not 12 ms, and B is the smaller half of it.
- **The main CPU (budget 11 ms):** v0 moves it little: RSO already skips scenery's walk, so v0 adds the other still types and drops the copies. The main CPU's 35.8 ms is mostly the posed units' per-part chain and the walk itself; that's **v1's**: one instance per object for the changed half (R §2.1), its node matrices handed over by the posing box rather than copied. v0's standalone price is read against the ME's row only, and this page says so in its result.
- **The GE (33.3 ms):** a CALLed block is read from main RAM as the list was; v0 neither adds nor removes GE work. The GE lists box's row.

## 11. v1: the changed half, in the box's own unit (2026-11-12 23:33, builder; the lead 22:11)

**Why v1:** v0 keeps what doesn't change, ~21% of b30's instance words (12-31% across maps) [E]; the ME's row (~12 ms against 33 on the flight [M]) and the main CPU's (11 against 35.8 [M]) are carried by the instances that change every frame: posed units and moving objects. v1 is R's §2.1 and §2.4 for them: one instance per object, not a call per part, and the ME's words from forms, written in `psp/src/box_draw_list.c` against the box's own data so it's priced alone (REBUILD §5), with v0's blocks kept for the unchanged half.

**The shape, from R's page (to be confirmed against its code before any build):**
- **Main CPU:** where the rasterizer starts a model's parts, one instance record (model, geometry, lighting, colours, shape, flags) and the per-part loop skipped. The node matrices are **handed over by the posing box** (its output block, `box_posing.md` §1b's contract), not copied as R's 3.3 KB a model; until posing switches over, a pointer into the frame's matrix snapshot.
- **ME:** per instance its lights and colours once, per rigid part its node matrix, per skinned batch its bone slots, then a touch (geometry and texture addresses, the frame marks) and a CALL per pass's form. R's measured table (§2.4 of `redesign_r.md`, b30's flight) puts the model replay at 39.2 ms today and ~8-10 under that shape [M today, U under R].
- **What R as built missed, and v1 must not:** R emitted every part every frame through the old layer's chain, so its render job fell only to 48 ms (whole b30 run [M]); MEXEC r2 didn't move it. v1's ME side is a small emitter per part kind with no record to decode and no per-part bind, lighting or geometry chain, and its instruction footprint is sized against the ME's 16 KB I-cache before it's written (the code-miss share was 29-32% of R's job [M]).

**Before code (design steps, in order):**
1. Read R's code as it stands and decide what's taken whole into the unit and what is the old layer's. **Where it is (2026-11-12 23:43) [D]:** R's milestone isn't on the rebase (only `psp_rloop.c`'s kept loop came across; `docs/mexec.md`): its code is on Sierra-III `master/rtop` (44c57f44, pre-rebase): `psp/src/psp_r.c` (3,327 lines: the instance list, the form capture and its 256 KB ring, the touch, the checks) and `psp_r.h`, with its hooks in `psp_ge.c` and `psp_fixups.py`, and its milestone price on that branch's `docs/redesign_r_topdown.md`: frame -0.6 to -2.4% on b30 (PSP-1000), -1.2 to +2.7% on c20 (PSP-3000), the ME's render job unchanged (48.4 → 48.1 ms on b30, 38.4 → 40.7 on c20), with 29-46% more texture reads from the smaller texture cache its heap room came from [M]. So v1 takes R's capture and touch, which hold, and not its emitter's place in the old replay, which is what kept the ME's job where it was; and the texture cache's room is priced on v1's side as on v0's.
2. The instance record and the hand-over with posing's output (the two pages' contracts in one line each).
3. The emitter's per-kind functions and their code size; the forms' store (R's 256 KB kept by use, 96.5-98.9% hits [E]) against the texture cache, since the list, v0's arena and the forms are one room (`box_ge_lists.md` §4).
4. The capture (`PSP_CAPTURE=draw_list`) that prices it alone over b30's flight, and its standalone budget per instance (~73 µs main CPU, ~80 µs ME, §4).

v0's in-frame pair (b30_play2's flight, the 3000) gives the baseline v1 is priced against.

### 11.1 What's already priced on v1's ground, and what that leaves (2026-11-13 02:42, builder; the lead 02:13: the v1 page so v1's build can start today)

v0's pair is in (§10.8: no gain, ~20 µs a kept walk), so the ME's row and the main CPU's are the changed half's. R and R top-down already priced most of the obvious forms of it on b30, and v1 is shaped by what they found rather than by §2's first sketch (Sierra-III `master/rtop`, `docs/redesign_r_topdown.md` §1-§12; `docs/redesign_r.md`):

| Tried on this ground | Result | So for v1 |
|---|---|---|
| **R as built:** forms captured, a part drawn from them by a touch and CALLs, inside the old replay | milestone b30 frame -0.6 to -2.4%, c20 -1.2 to +2.7%; the ME's job unchanged (48.4 → 48.1 ms) [M] | forms alone don't move the ME: what R drew got ~25-30% cheaper, but it left the dearest parts to the body |
| **T1, the part slot, the object's bones once, its lighting once** (lookups and pspgu calls cut) | no gain on the PSP-3000; the ME's model part draws +1 ms [M] | a part's ME time isn't its lookups or pspgu's calls: per-part bookkeeping is not v1's ground |
| **The rigid near runs restored** | none of the ~8 ms [M] | rigid near triangles were cheap |
| **SLOTMASK, CORNERSETTLE** (tighter bounds in the corner loop) | reach ~2% and ~4-5% [E]; stopped | the run's sphere bounds are spent |
| **MEXEC** (the ME's hot code from eDRAM) | -9% of the ME's job on b30's flight [M]; in | already in the tock |
| **v0 B** (kept walks CALLed) | ~0.4-0.7 ms of a ~38 ms job [M] | the still half is small |

**Where the ME's model time is on the flight** [M, rtop §7, §11]: the parts near the eye (the Pelican the player rides and its riders) are 9-19 ms a frame of the models' 22-52 ms, nearly all of them bending (26,603 of 26,606). Their first geometry pass is ~2/3 of it, and the corner loop is 64-70% of that pass (~44k cycles a part on the ride, ~5.6 ms a frame), at ~700-1,500 cycles a corner: several times a corner's arithmetic, so per-triangle overhead or dependence, not misses (corner_verdict holds 0.9% of the data misses) [U on M+E]. After the landing the near parts fall to 1-6 ms, and the beach's 20 ms (§4) is spread over the other kinds.

**So v1, in its own unit (`psp/src/box_draw_list.c`), is two pieces, each priced alone first:**
1. ~~**The near bending parts' classification kept** [ME]~~ **stopped (the lead, 2026-11-13 03:20; 03:48) on its count:** <1% of the near loop's corners bitwise, 6.7% within 1e-3 (v1count1, below): the eye moves against the Pelican every frame. Revive if a test catches the loop's 45.8% repeated outcomes without matrix equality (the eye's motion is what breaks this one). **PN builds first** as v1's first piece of `box_draw_list.c`. a near part's corner verdicts kept in the model's own space while the eye's place in that model holds still (a rider seated in the Pelican moves with it), worked out again only for the nodes whose matrices changed relative to the eye. Its ground is the ~5.6 ms loop on the ride; its count first [E]: frames and parts where the eye holds still in the model to within the verdict's tolerance, and corners whose bones moved. It needs posing's output by node (`box_posing.md` §1b) to know which bones moved. **Its ground's earlier tries** (`projects/sierra/tools/levercheck.py near`, `corner`): **PN** [open, proposed recon134] puts the Pelican cabin and its riders on a near layer of their own, as the first-person weapon (b30's cutscene and ride: 12.3 ms a frame cutting near-plane triangles [M, PSP-3000]; the load inside the Pelican is the Pelican itself, 5,742 rigid + 5,774 bending triangles a frame [E]); **NP** [not built] prefetched the loop's misses, small by the cache model and by its note "the time is the per-triangle and per-corner work itself, so the lever is looking at fewer triangles corner by corner"; **NEARZ** and the bounds (SLOTMASK, CORNERSETTLE) [stopped]. So piece 1's count compares its reach with PN's: a near layer takes the riders' cabin out of the near cut wholesale (a same-look change, side-by-sides), where a kept classification takes the frames the eye holds still. Whichever reaches more of the ~5.6 ms is built first.

   **Counted (2026-11-13 03:14, the builder's helper; recon instrument `v1count`, branch builder/v1count) [E]:** b30_play2 to 120 s in the emulator, game time 300-2,000 (851 frames; the cinematic to the landing), each near bending batch's first pass compared with the same batch's last frame (keyed by skin, batch and the k-th first pass of that skin in the frame), weighed by the corners the loop worked out and the triangles it visited. All near bending first passes: **42.0 batches, 1,279 corners, 4,017 triangles a frame** (the flight's window, game time 600-1,200: 82.5, 2,440, 7,883; the cinematic's, 300-600: 35.4, 1,148, 3,411).
   - **Piece 1's reach is nearly none:** the slots' camera-space matrices and the near limit equal last frame's **bitwise for 0.1% of the corners** (0.3% of batches, 0.1% of triangles), **within 1e-4 for 0.7%**, within 1e-3 for 6.7% (9.8% on the flight's window, 0 in the cinematic's). The Pelican itself, one instance with no reordering, is 0 bitwise and 1.0% within 1e-4: the eye moves against the Pelican every frame (the seat's animation, the cinematic camera, the pad's look), so "the eye holds still in the model" doesn't happen on the ride. The loop's outcome (the triangles kept, the counts dropped and cut) is the same as last frame's for 45.8% of the corners (74.9% of triangles): an upper bound for a classification kept by any test, not one matrices can reach.
   - **PN's reach is most of it:** the Pelican the player rides takes **50.7% of the corners** (13.4% of batches, 27.2% of triangles) and its riders and what they hold 31.2% (57.2%, 42.0%): **81.8% of the corners, 69.1% of the triangles, 70.6% of the batches** together (the flight's window 78.8% of corners, the cinematic's 94.1%). After the landing the near bending work falls to ~2-13 batches and 28-508 corners a frame, none on a vehicle.
   - **First person** never reaches the loop (`near` is `model_near && !model_first_person`, [D]); beside it, the first-person bending parts are 45.0 batches and 3,175 triangles a frame of the 151 batches and 14,300 triangles every bending part's first pass draws.
   - **d20_play1 to 120 s** (the swamp): near bending first passes 6.2 batches, 110 corners, 265 triangles a frame over game time 300-2,000 (at most 224 corners in a window), none on a vehicle: the near loop is b30's ride's, not the swamp's.
   - **Caveats:** the k-th order isn't wholly stable: 4.1% of the stretch's lookups found another object at the key (1,453 of ~35,700, nearly all in the ride's windows), 5.8% found no key (2,074); the tables never filled. The emulator draws a frame each 2 ticks here, the PSP each ~3.3 on the flight (110 ms), so between the PSP's frames the eye moves further and piece 1's reach there is lower still [U]. Counts only, no time: the corners' share stands in for the loop's time (corner_verdict's cost is per corner, rtop §11). Play and both seeds identical with the word on and off (statecmp, 3,600 ticks, b30 and d20). Lab sierra-batch 43398593811 (v1count1).
   **PN built and proven in the emulator (2026-11-13 04:18, the builder's helper; branch builder/drawlistpn a50872e; lab sierra-batch 43407310639, drawlistpn1) [E, D]: ~~a compromise as built~~ an exact speed-up in intent with its fault named, not a compromise (the builder 05:0x and the lead 04:27, superseding the helper's word), off by default.**
   - **What it is** (`psp/src/box_draw_list.c`, under `PSP_DRAWLIST`, keep word `drawlistpn`): the vehicle local player 0 sits in, and every object whose parents reach it (riders and what they hold; found on the main CPU as v1count's instrument found them, `psp_riding_relation` in players.c), drawn with the world's projection but a near plane of its own, `(distance to the model's sphere + its radius) / reach`, where reach is the GE's guard band (1,920 pixels from the screen's centre) as a slope. Past that plane no corner, and no point the GE makes where it clips a triangle, can leave the GE's range, so the GE clips the model itself and the CPU's near corner loop is skipped (`model_near` 0, as for first person). The depth slice is the part of the world's own depth mapping past that plane (`sceGuDepthRange(D_world(plane), 0)`), so the layer and the world test depth against each other as before: nothing pokes through and nothing is wrongly covered (none seen in the shots). It rides the existing model-shape record (first-person word 2); no new record kind. Riding models are never kept in v0's blocks. A build without `PSP_DRAWLIST` is byte-identical to 0472b9e's (local `cmp`).
   - **Play and both seeds identical** with the word on and off: statecmp over 6,000 ticks (b30_play2 to 200 s) and 3,000 (the shot runs); the A/A shot run identical too.
   - **The near corner loop's work, a frame** (probes, 300-frame windows): the cinematic's window (game time ~0-600) **1,147 → 67 corners (-94%)**, 24 → 2 batches, 3,411 → 298 triangles; the flight's and the landing's (~600-1,200) **2,439 → 516 corners (-79%)**, 63 → 26 batches, 7,883 → 2,893 triangles; after the landing the same as without (no riding models). Riding models 8.8 and 7.5 a frame, 3.2 and 5.1 of them near the eye. Bending triangles cut at the near plane by frame ~900: 80,600 → 13,126; triangles the CPU left out by frame 300: 1,001,350 → 82,075, so **~3,000 more triangles a frame reach the GE** (the cabin behind and beside the eye, which the GE now transforms and clips itself). On the flight the GE is ~89% vertex-side [M, `hardware_facts.md`]: the GE's side of the trade would need a PSP pair before any gain is claimed.
   - **The planes are far out:** mean 1.39 units in the cinematic's window (at most 6.0), 0.32 on the flight (at most 1.02), against the world's 0.0625. The Pelican's sphere holds the eye, so its plane is set by the whole sphere's reach, not by the cabin near the eye.
   - **What the shots show** (lab results `sierra-batch/43407310639/b30-ref-shots/` against `b30-pn-shots/`, the same frame numbers; `b30-ref2-shots/` the A/A: 7 of 1,501 frames differ; with the word 477 of 1,501, all in the ride): **the cabin's near parts are cut away and the world shows through.** shot001 (frame 181, game time 364): the door frame's strut on the right is gone; shot002 (frame 240, 482): the floor at the bottom edge and the roof's edge top right; shot003 (301, 604): the marine nearest the eye, left, loses his near half; shot005 (421, 844): the strut again; shot006 (481, 964, landed): the walls top left and bottom left. Riders past the plane are unchanged. From shot008 (frame 602) on, the pictures match within the A/A's noise.
   - **So PN as built detracts** ~~: a compromise (the overclocking bucket, CLAUDE.md)~~ (an exact speed-up in intent whose bound is too loose, not a compromise: the lead 04:27), off by default and not planned on. No plane the GE can clip at keeps the cabin and holds the guard band while the sphere holds the eye: a wall beside the eye reaches ~1 unit sideways at the plane, which the guard (a slope of ~7) allows only from ~0.15 units out, and finding the triangles that cross there is what the loop does. **Revive if** something tighter than the model's sphere bounds the geometry that crosses the plane, cheaply (per-part or per-run spheres in the eye's space, with a cap under which the plane can't be seen), or the GE's guard is kept another way (cut pieces kept in the model's space while the eye's place in the cabin holds: piece 1's form, which its count rules out at ~1e-3).

   **PN's tighter bound: design (2026-11-13 04:45, the builder's helper; the lead 04:27: "one honest try at the same look before it's parked", an exact speed-up in intent) [D]:**
   - **What a bound has to show.** The GE clips a triangle at the projection's near plane itself (`GU_CLIP_PLANES`, `hw_capabilities.md`), but every corner past the plane and every point it makes on the plane must land within its reach, 1,920 pixels of the screen's centre: a slope S = 1,920 × a pixel's width at depth 1 (~7, as above) (`ge_audit.md`: why the CPU cuts near triangles). PN as built took the reach from the whole model's sphere and moved the plane out to fit it. The slice keeps the world's plane, and with it the world's projection and depth slice (no new projection, no new record word), and asks the bound of a smaller piece instead: what passes goes to the GE whole and the GE clips it; what doesn't takes the loop as now. So the picture is the loop's, up to how the GE's clipped pieces round against the CPU's (as `guardwide` notes), which the shots judge.
   - **The test, per run** (the loop's runs of 16 triangles, `near_cluster_verdict`). A run the loop now works corner by corner (its sphere neither wholly in nor wholly out) is clear at plane p when, on each side axis a, either every slot's image of its sphere lies inside the GE's reach cone, `|c_a| + r·√(1+S²) ≤ S·c_z`, or every image reaches sideways no more than the reach at the plane, `|c_a| + r ≤ S·p`. All images one way or all the other, per axis: each form is a convex region the blended corners stay inside, their union isn't. A run that fails needs the plane max over the axes of `(|c_a| + r)/S` (the slab form), unless its images are all inside the cone. With p = 0.0625, a run that crosses the plane is clear only within ~0.44 units (S × p) of the eye's axis on both side axes: the cabin's walls, floor and roof beside the eye can't be, by the bound's own arithmetic, whatever its size; what can clear is the parts in front of the eye and the runs that are mixed only on the loop's guard planes (6 screens, 1,440 by 816 pixels) while inside the GE's 1,920.
   - **Per part:** the same test on the part's own sphere (`batch_sphere`'s), the part drawn whole if it clears. A part's plane is its runs' most.
   - **Its cost:** none precomputed anew. The runs' spheres are kept per part in its own space already (`near_clusters_make`), and the verdict already moves each by every slot: the test adds a few compares a slot an axis on centres it has, against a run's ~48 corners worked out. Plain floats: the ME runs it as it runs the verdict.
   - **What the GE gets more:** a clear run's triangles the loop would have dropped (wholly behind, or past a guard plane) and the ones it would have cut (sent as pieces) go to the GE whole.
   - **The count first** (recon `pnboundcount`, `PSP_DRAWLIST` + `PSP_RECON`, off by default; a build without `PSP_DRAWLIST` is byte-identical to cbf158c's, local `cmp`): every run the loop works corner by corner, tested as above at the world's plane, the corners it holds counted as clear or not and binned by the plane it would need; the same for the part's sphere; the riding models (marked by the shape word, drawn as the world) apart from the rest.

   **Counted (2026-11-13 04:53, the builder's helper; recon `pnboundcount`, branch builder/drawlistpn 4ab725d and dde4741; the lab's sierra-batch 43411031274 pnbound1 and 43412138447 pnbound2) [E, counts]:** b30_play2, 300-frame windows; the cinematic's (game time ~0-600) and the flight's and landing's (~600-1,200), the windows drawlistpn1 measured (the loop's corners match: 1,147 and 2,439 a frame).
   - **The bound per run clears 15-16% of the loop's corners at the world's plane:** 171.6 of 1,147.6 a frame in the cinematic's window (15.0%), 398.8 of 2,439.8 on the flight's (16.3%); the riding models' alone 153.5 of 1,079.9 (14.2%) and 283.4 of 1,923.6 (14.7%). **So 84-86% are still cut.** The part's own sphere clears less: 90 and 278 corners (7.9%, 11.4%), 2.9 and 8.7 parts a frame of the loop's 24.5 and 63.1.
   - **The plane the cabin needs** (the riding corners, by the plane their run would need): the world's 14-15%, to 0.125 4%, to 0.25 20-22%, to 0.5 5-6%, **to 1 unit 53-54%**, past 1 2% (cinematic) and 0% (flight). By the part (its runs' most): 7-9% at the world's, ~60% at 0.5-1 unit. Half the cabin's corners lie in runs that need a plane 8-16 times the world's.
   - **The GE's extra triangles** (a clear run's triangles the loop would have dropped, now the GE's): 12.1 a frame in the cinematic's window, 50.1 on the flight's (the riding models' 9.5 and 7.5), with 9.8 and 23.3 triangles a frame sent whole in place of the CPU's pieces: small next to PN as built's ~3,000.
   - **The ceiling of any bound per run** (each triangle asked exactly from the loop's own corners: does its part past the plane, with the points where its edges cross the plane, lie within the GE's reach): **81-85% of the loop's triangles** could be clipped by the GE itself (764 of 939, 1,673 of 1,977 a frame), but they share runs with ones that can't: the runs whose every triangle could hold **52.7% of the corners** in the cinematic's window and **56.9% on the flight's** (riding 52.7%, 59.8%). That is what a perfect bound per run of 16 would clear, and only the loop's own work knows it: a run's sphere reaches 15-16 of those ~53-60 points.
   - After the landing (no riding models; the loop 28-950 corners a frame) the bound clears 19-55% a window, 38.9% of the corners over game time ~1,800-6,000; d20_play1 to 120 s, 0-224 a frame, 57% (pnbound1). Play and both seeds identical with the word on and off (statecmp: b30 6,000 ticks and 1,800, d20 3,600).
   - **So, against the lead's bar (~half the corners at the world's plane): the bound clears ~15%, and the exact ceiling of any bound per run is ~53-60%. Under it: by the lead's order PN parks** (the slice wasn't built). **Parked 2026-11-13 04:55 (the builder, the lead 04:27's rule):** levers.tsv PN and its revive_if in grounds.tsv as below; v1's second piece next. **Revive if** a bound per run (or finer) costing less than the corners it spares clears ~half the near loop's corners at the world's plane: a run's sphere clears 15-16% on b30's ride and the exact ceiling per run of 16 triangles is 53-60% [E], so it would need nearly the exact answer cheaply; or the GE's reach is kept another way.

2. **One record an object for the moving half** [main CPU]: T3's instance (model, LOD, permutations, lighting, colours, flags; node matrices by reference to posing's block, not copied), the engine's per-part walk skipped for an object whose model, LOD and permutations are unchanged, the parts' touches carried while their caches still hold them (T3a). Its ground is the main CPU's 35.8 ms (§4), of which RSO already re-sends the still objects (~~71 of 93 a frame on b30~~ none on today's tock, which refuses the far level's records: v1p2count1, 2026-11-13); its count first [E]: the moving objects' per-part calls a frame, and the share of the walk they take.

   **Piece 2, its count first (2026-11-13 05:05, the builder; the lead 04:27: PN parked, piece 2 next) [D]:** piece 2's ground on the main CPU is already partly taken: **RLOOP** (R slice 5's kept loop, on by default, `psp_rloop.c`) skips `render_model_parts`' walk for a model whose key (model, permutations, level, forced shader, flags) it has noted, and makes the noted rasterizer calls in its place; RSO sends a still object's records whole. So what piece 2 adds is **one record an object in place of the per-part rasterizer calls** that RLOOP still makes, and the walk where RLOOP misses. The count (Sierra-III builder/v1p2count 0244897, recon keep word `v1p2count`, FIXES_V1P2 applied last; the lab's v1p2count1 43414157134): each model the main CPU starts a frame by what it did (RSO's records, RLOOP's calls, the walk, the far level), its rasterizer calls, and whether its key is its last draw's; with render_model, render_model_parts and rasterizer_model_draw each under the cache models alone, for the walk's and the per-part chain's instructions a frame. b30_play2 to 240 s and b40_play1 to 200 s.

   **Piece 2's count read (2026-11-13 06:55, the builder; the lab's v1p2count1 43414157134, findings `v1p2_*`) [E counts, D]:** play and both seeds identical with the count on and off (b30 7,200 ticks, b40 6,000). **On today's tock neither shortcut runs:** RLOOP is off without R (`rl_start`: only on its keep word), and **RSO sends nothing on b30** (offered ~48-84 models a frame, sent 0.0): nearly every offer holds the far level's record kind (14), which RSO refuses (§10's item 1). So every non-far model is walked. **What the walk carries:** b30 to 240 s, 5.8-23.2 walked models a frame and 23-124 rasterizer calls a frame, 85-90% of the calls from models whose model, level, permutations, forced shader and flags are their last draw's; the far level 7-99 models a frame at 0-3.4 calls; b40 to 200 s, 3-8.4 walked models a frame, 10-38 calls, 60-96% unchanged. **Its share of the main CPU's drawing (instructions, the cache model's whole calls):** `render_model` 14.6% of the drawing phase's instructions on b30 (68 calls a frame, ~6,800 instructions a call) and 7.9% on b40; `render_model_parts` (the walk with its calls) 7.3% and 5.2%; `rasterizer_model_draw` 3.7% and 2.8%. The emulator's drawing phase also holds ME work run on the main CPU there (the mixer, its decode, mevis: ~24% of b30's phase), so of the main CPU's own drawing the walk is ~9-10% [U]. **So piece 2's ceiling is small:** one record an object for the unchanged models takes at most ~85-90% of the walk, ~6-9% of the main CPU's drawing instructions [E], against a main CPU at 35.8 ms and a budget of 11 [M]. Counts, not times: the PSP says where the drawing's time goes. The larger and nearer ground on the same unit is RSO's own: §10's item 1 (the far form in RSO's key) would let RSO send again the ~48-84 still models a frame it's refusing.

   **The beach's ME by kind (2026-11-13 10:22; the master's sierra-hw 43449814663 on the PSP-1000, findings `beach_me_by_kind`) [M]:** the ME's render job 18.9 ms a job on the beach (32.9 on the flight); by kind a frame, all 25.8 ms: **model parts 4.9**, level draws 4.2, decals 3.1, screen quads 1.6, far level 1.5, lights 0.6. So on the beach, v1's ground on the ME (the model parts, where PN's near loop sits) is ~19% of the ME's frame, not the flight's ~5.6 ms near loop alone; with PN parked, v1's next ME piece is sized against these 4.9 ms.

**Not v1:** another form of the per-part emitter or its lookups (T1 priced them at nothing), tighter run spheres (spent), the rigid near path (cheap). **Open before design:** the beach's and the fights' 20 ms of ME (§4) by kind, on the PSP, since the flight's near parts are the worst case, not the sizing case (REBUILD.md: the fights size the boxes).

## 12. The ME's frame by kind, read again: the level's passes and the decals (2026-11-13 22:1x, audit; Sheepdog01's go 22:10)

Read from the log of the lab's sierra-hw 43449814663 (PSP-1000, tock 29f37ee, b30_play2 to 210 s, `timerson mekinds`; the run behind §11.1's "beach's ME by kind") and from `psp_ge.c` at the tock. No new run. Instrumented: read kinds against each other.

**The beach's 25.8 ms a frame, every kind over 0.5 ms** [M; the three windows §11.1 averaged, ending at game time ~2,950, ~3,500 and ~3,800]:

| Kind | ms a frame | Records a frame, µs each |
|---|---:|---|
| **Audio and ring served between records** (`REPLAY_SERVED`: not drawing) | **6.2** (8.3 / 8.0 / 2.4) | - |
| Model part draws | 4.9 | 29-52, 108-153 |
| Level draws | 4.2 | 7.5-9.9, 440-571 |
| Decals | 3.1 (3.3 / 4.5 / 1.6) | 205-286, 7-16 |
| Screen quads | 1.6 | 19-20, 57-102 |
| Far level | 1.6 | 7-50, 29-50 |
| Light draws | 0.8 | 0.2-13, 119-185 |
| See-through queued | 0.8 | 13-23, 38-53 |
| See-through level queued | 0.6 | 1.8-2.0, ~300 |

§11.1's line named six kinds (15.9 of 25.8 ms) and left the largest, the served time, in "the smaller kinds". The render job's own 18.9 ms is the 25.8 less it. The ME over the same windows: 66-81% busy; audio 5.0 ms a 23 ms block (21.5% of the ME: decode 1.5, mix 3.4; 34 voices on average), the AI job 14.8 ms a job (16.6%), the ring 3%. **At 30 fps the audio's share is ~7.2 ms a frame on the beach, not REBUILD §3.4's ~4** [M x arithmetic]; the lead's brief `audio_me.md` (H1-H3) is its ground and has no branch yet.

### 12.1 Level draws: the passes are written again every frame

- **What a level draw does on the ME each frame** [D, `psp_ge_draw_level_body`]: scans every index for the lowest and highest vertex (the same answer each frame for a level stream); tests the stream's box (8 corners); unless the box is wholly inside the view, classifies each triangle from per-vertex codes and cuts the ones across a plane; copies the kept indices into the list (`geshow2`) or writes the engine's back; buckets the kept triangles by slant (`angle_sort`, 7 buckets); finds the nearest of ~96 vertices for the UV centre; then writes the passes (base, lightmap, detail, glow) once a bucket.
- **Where its time is** [M, earlier runs]: on c10 (PSP-1000, 2026-10-29, the lab's row "level parts on the ME, by step") passes (binds, lists) 4.0-4.3 ms a frame for 15 draws, classify corners 1.8, stream + index range 0.3: the passes are ~70%. On b30's flight (PSP-3000, 2026-11-05, `c4_model_parts_b30_flight`) the level's passes are 16,890-20,179 list words a frame for 13-15 draws, **~1,300 words a draw**, against ~40 for a model part's bind, state and lighting.
- **How much is there** [M, this run]: level draws are 3.3-8.1 ms of the ME a frame in every window (cinematic 8.1, flight 6.1, landing 5.4-5.5, beach 3.3-5.7, bunker 1.2-4.6), 7-15 draws at 340-590 µs each. On the beach 8 of 9 draws a frame are classified, 1 is wholly inside.
- **Not on any page's budget:** §5 says B's census left the level out; the far level box takes the far clusters (29-50 µs a record here) and nothing takes the near level's.
- **The lead:** a level draw's words depend on the camera only through (a) which triangles are kept or cut and (b) each triangle's bucket; the rest (binds, pass state, the indices themselves) is the same every frame with the origin held (§10.3). A draw whose kept set and buckets equal its last frame's could CALL a kept block, as v0's walks do (§10.8), and a draw wholly inside the view needs only (b).
- **The count that sizes it** [E, exact counts; the builder's, one batch]: per level draw a frame, on b30_play2 (flight and beach) and c20_play1: (1) wholly inside or classified; (2) its kept triangle set equal to its last frame's (the kept indices, bitwise) and its cut count zero both frames; (3) its bucket assignment equal to its last frame's; (4) (2) and (3) together, weighed by the passes' list words; (5) for the draws that fail (3) alone, how many triangles changed bucket (if few, buckets held with a margin, as the origin's cell is, would keep them: a picture change of the mip step's place, judged by shots). If (4) is small because the buckets move every frame, the form isn't a kept block but coarser or held buckets, and (5) says which.
- **Unknown from reading:** whether the passes' ~270-290 µs a draw is the words written or what writing them evicts; a PSP split says, not the count.

### 12.2 Decals: most calls put nothing on the screen

- **Today** [D]: the engine draws decals a visible cluster at a time (`_rasterizer_decals_draw`, the hook in `FIXES_23`), with no test per decal. For each, the main CPU takes a record and copies its corners (16 words a quad, `defer_decal`, since geinflight); the ME walks every quad (the small test, the sphere against the view) and only then binds and draws what's left (`psp_ge_decal_body`).
- **Counted on the PSP** [M, this run's `decal calls with nothing on the screen`]: on the beach's four windows 205-286 decal calls a frame, **153-224 of them with nothing on the screen**; the decal kind is 1.6-4.5 ms of the ME a frame there (7-16 µs a call).
- **The form:** the test made on the main CPU before the record is taken, from what the decal already holds: `decal_datum.position` [D, the engine's `decals.c`] and a radius that bounds its quads (the definition's largest radius, or one kept beside the decal when it's made; the datum has no spare float, so a side array by decal index). One sphere against the view's planes; a decal that fails takes no record, no copy and no ME time. Conservative radius: the same picture; drawing only.
- **Its ground, looked up** (`projects/sierra/tools/levercheck.py decal`): **DC** [stopped] made the call test on the ME from the corners ("walks every corner twice and its 3x sphere rarely skips many-quad calls"; instructions +7% on b40, +58% on c40 [E]); its revive_if is "a call test cheaper than the quad loop it skips". This is that: no corner is read. DB and RR3 (batching) and SMALLME are other grounds. The cost map's line: "Doing less work (reuse, skipping)".
- **Ceiling** [U on M]: ~~the empty calls at the kind's mean cost are ~2-3.5 ms of the ME a frame on the beach's windows~~ **[superseded 2026-11-14 by the pair, §12.7: the ME's decal kind didn't move with three quarters of the calls gone; an empty call costs the ME next to nothing, the kind's time is the drawn decals']**; an empty call skips the bind and the draw, so it costs less than the mean and the true figure is lower. The main CPU's side is the record and its copy (65 KB a frame on b30's beach by an older count [E, REC's row]). Priced by one PSP pair once built; the empty share on other maps is an emulator count.
- **Tag:** `[lever]` on the old path's effects ground unless the lead places it in the effects box.

### 12.3 The same read on the other maps: the level is the largest kind on three of five, and the decals are half the ME's frame on b40's drive (2026-11-13 22:5x, audit)

From the draw list census's own runs (§4: the lab's sierra-hw 43331666648 d20, 43314261608 c20, 43314233107 b40, 43318228389 a30; PSP-3000, tock 9d00a903 / f50359fa, Sheepdog01's pads, `timerson mekinds`), every 300-frame window past game time 300, the ME's ms a frame by kind [M, instrumented]:

| Kind | b30 beach (PSP-1000) | d20 (18 windows) | c20 (29) | b40 (35) | a30 (18) |
|---|---:|---:|---:|---:|---:|
| All | 25.8 | 33.5 (most 54.6) | 27.2 (62.2) | 20.7 (40.5) | 20.7 (36.1) |
| **Level draws** | 4.2 | 5.0 (most 8.5) | **6.8** (16.9) | **6.4** (15.0) | **5.3** (10.2) |
| Audio and ring served | 6.2 | 6.4 (11.9) | 4.2 (10.9) | 2.5 (5.3) | 3.0 (6.5) |
| Model part draws | 4.9 | 5.8 (10.9) | 3.5 (7.4) | 2.9 (6.3) | 4.8 (16.1) |
| Light draws | 0.8 | 6.2 (15.4) | 4.2 (16.5) | 1.4 (4.9) | 0.2 |
| **Decals** | 3.1 | 4.1 (8.5) | 2.0 (6.0) | 2.1 (**19.2**) | 1.0 (2.5) |
| Screen quads | 1.6 | 2.1 | 1.5 | 1.3 | 1.4 |
| See-through queued + level's | 1.4 | 1.1 | 2.8 | 1.4 | 1.4 |
| Far level | 1.6 | 0.9 | 0.5 | 0.8 | 1.3 |

- **Level draws are the largest drawing kind on c20, b40 and a30 and are 4-7 ms a frame on every map** (9-29 draws a frame at 170-590 µs each; c20's first window 68.7 draws, 16.9 ms). §12.1's count is sized by all five maps, not b30's.
- **b40's drive is the decals'** (b40_play1, the windows ending game time ~11,700 / 12,290 / 12,850, the driven warthog, Sheepdog01's worst stretch): **8.1 / 19.2 / 9.2 ms of the ME a frame in decals, 1,077 / 1,308 / 779 calls a frame, 992 / 965 / 633 of them with nothing on the screen**; in the middle window the decals are 19.2 of the ME's 40.5 ms. The slip effects' decals (the lab's `matfxdecal_split_b40_psp3000`: up to 3.7 made a tick) live 30 s, and each is carried out every frame its cluster is visible. ~~§12.2's test reaches most of it: **up to ~6 / ~14 / ~7 ms of the ME a frame there** [U on M: the empty share at the kind's mean cost]~~ **[superseded 2026-11-14, §12.7: the ME's side is ~0; the gain is the main CPU's]**, and the main CPU's records and copies for ~630-990 decals a frame with it.
- **Empty decal calls elsewhere** (a frame, by window): d20 5-159, c20 0-182, a30 1-207: the same pattern at b30's scale on every map.
- **A worst single record of 8.5-12.1 ms in the screen quads on every map** (b30 7.4-11.0, d20 12.1, c20 8.5, b40 10.1, a30 8.8), against a mean of 40-100 µs; and a level draw of 10.0 ms on b40, 6.4 on a30. One record a window at least: how often, and what it waits on, isn't in these lines (the 30 fps floor's side; open).

### 12.4 The 8.5-12 ms screen quad is the see-through flush (2026-11-13 22:3x, audit; the lead's 22:26 ask) [D]

`screen_quad` calls `transparent_flush()` before it draws ("the 3D world's see-through parts go before the HUD"), so the frame's first HUD quad carries out every queued see-through draw, sorted, inside its own record. mekinds times it under "screen quads" (and the main CPU's log line says so of its own figure: "HUD quads ... us a frame (with the see-through flush inside)"). So:
- the worst single screen-quad record on every map (b30 7.4-11.0 ms, d20 12.1, c20 8.5, b40 10.1, a30 8.8 [M, §12.3's runs]) is one frame's see-through drawing, not a HUD quad;
- the kind's mean (1.3-2.1 ms a frame for 17-20 quads) is the HUD's quads at ~40 µs each (b30's quiet bunker window: 19.9 quads, 38.8 µs, 0.77 ms) plus the flush, **~0.5-1.3 ms a frame on average** [U: the kind less 0.77];
- the ME's see-through drawing is therefore three kinds, not two: "see-through queued" and "see-through level queued" (the queueing, 1.1-2.8 ms a frame together) and the flush hidden here.
A flush of 8-12 ms in one frame is a third of a 33.3 ms frame on the ME: the 30 fps floor's side. What it holds on those frames (how many draws, which shaders) isn't logged per frame; mekinds would name it with the flush timed as a kind of its own (`REPLAY_KINDS` + 1, marked around `transparent_flush` when it has work): a small instrument change for the builder, then the next census run answers it.

### 12.5 The decals' main-CPU side: 29 ms a frame on b40's drive (2026-11-13 22:4x, audit) [M]

The same census runs' step timers (`profile.txt`, `timerson`; a step's time runs until the next step's start), the main CPU's `rasterizer_decals_draw`:

| Stretch | Frame | `rasterizer_decals_draw`, ms a frame | The ME's decal kind (§12.3) |
|---|---|---:|---:|
| b30's beach (PSP-1000, 43449814663; two windows, 20 s) | 66.7 ms, 15.0 fps | 1.4 | 1.6-4.5 |
| **b40's drive** (PSP-3000, 43314233107; seven windows, 71 s, game time ~10,900-11,780) | **160 ms, 6.3 fps, 12.5 ticks/s** | **29.4** (18.0-48.3 by window) | 8.1-19.2 |
| b40 before and after the drive (the same run) | | 0.1-0.7 | ~1-2 |

- On Sheepdog01's worst stretch the decals' drawing is **18% of the main CPU's frame** (second to `objects_update`'s 73.6 ms) and up to half the ME's, together: ~1,100-1,300 calls a frame at ~25 µs each on the main CPU (the vertex buffer's lock, the block's address, the key, the record and its copy) and 8-15 µs each on the ME, **for decals of which 80-90% put nothing on the screen** (§12.3).
- So §12.2's test, made in the engine's loop before any of that, is sized by this stretch: **up to ~20-25 ms a frame of the main CPU** [U on M: the empty share] ~~and ~6-14 of the ME on the drive; on the beach ~1 and ~2-3.5~~ **[2026-11-14, §12.7: measured -21.2 ms of the main CPU's drawing on the drive, the ME's kind unchanged]**; on the beach ~1 of the main CPU.
- It is not the decal ring's ground (the ring cut a decal's *making*, the room in the vertex cache); this is every live decal *drawn* each frame. DFADE's 30 s life bounds how many are live.
- A second form for the same stretch, if the test alone leaves the visible ones dear: the slip decals of one drive lie along one track; nothing here sizes that.

### 12.6 The decals' pattern, looked for elsewhere: particles, shadows, b40's snow (2026-11-13 23:0x, audit) [M figures from §12.3's runs; D where marked]

The pattern: the engine offers a kind a visible cluster at a time, the main CPU prepares every item, and what isn't on the screen is found out last. Where the same runs' counters put "made" far from "drawn":

- **Particles** [D, the engine's `render_particles`]: a particle is taken when its *cluster* is visible (`render_location_visible(&particle->location)`); each one taken is placed, keyed for the sort and built into its sprite on the main CPU; nothing tests the particle against the view. `render_particles` is 1.8 ms a frame on b30's beach and 2-4 through most of b40, but **10.8 / 12.2 / 20.3 ms a frame in the three windows where b40's drive starts** (game time ~10,500-10,880, PSP-3000 [M]), while the ME carries out 17-35 sprite records (0.6-1.0 ms) and the list takes 37-66 KB of sprites a frame. How many of those particles are off the screen isn't counted anywhere. **The count** [E, the builder's, with the decals' census]: per frame, particles taken by `render_particles`, and of them the ones whose sprite's sphere is wholly outside the view; b40_play1's drive and b30's beach. If most are outside, the same test before `render_particle_place` is the lever; if few, the 10-20 ms is real work and is the effects box's (how many are made, Sheepdog01's far-and-tiny ruling).
- **Shadows** [M, counters since the start of b40's run]: 1,368,582 receiving triangles left out on the ME past the blob's rim or faded to nothing (`shadowrim`), against 810,765 receiving corners written (~270k triangles): about five of six triangles the main CPU gathers for a shadow are dropped after the record. In ms it's smaller: `render_object_shadow_end` 0.9 a frame on the beach and the drive, 4.0-4.4 at most; the ME's shadow draws 0.2-0.9, 3.1 at most. Its ground: SM, SR (the lab's levers.tsv). Noted, not sized further.
- **b40's snow** [M]: `weather_particle_systems_render` is **4.2 ms of the main CPU a frame in every outdoor window of b40_play1** (38 windows, 3.4-5.2), 13-16 in three; per frame, so 4.2 of the 11 ms drawing budget at 30 fps. Its ground is WXME (c10's weather on the ME, sized -2.8 to -4.3 ms there, paused) and WXDEAD (in); b40 has no row.

### 12.7 What the builder's runs said (2026-11-14 02:1x, audit; the lab's rows `decaltest_pair_b40_psp3000`, `decaltest1`-`8`, `levelkept1_four_maps`, `levelkept3_patch_size`)

**The decal test, priced** [M, PSP-3000, sierra-hw 43569081426; Sierra-III builder/decaltest 409ff42, keepsets `decaltestold | -`, repeat 2, instrumented]: on b40's drive (game time 11,000-12,850) **the frame 133.4 -> 113.0 ms (-20.4 ms, -15.3%), the main CPU's drawing 60.7 -> 39.6 ms (-21.2, -34.8%)**, the tick +0.4 ms; elsewhere on b40 within the noise. Calls a frame 1,080 -> 261.
- §12.5's main-CPU ceiling held (~20-25 against -21.2).
- **My ME ceiling was wrong** (§12.2, §12.3, §12.5, marked where they stand): the ME's decal kind is 11.1 ms a frame before and 11.3 after. I priced an empty call at the kind's mean; the mean is carried by the ~260 decals that are drawn (~43 µs each on the drive), and a call that draws nothing costs the ME almost nothing. So **the ME's 8-19 ms of decals on the drive is still there**, and it is the visible slip decals': the "second form" §12.5 ends on (one track's decals drawn as one batch, or fewer made: the effects box's question) is the ME's lever, not the test.
- In the emulator the test is exact against the body's own (0 of 817,000 culled decals had a quad the body keeps, decaltest4); the pictures' differences were the emulator's run-to-run spread without geinflight (decaltest6-8).

**The level's kept blocks, counted** [E, exact counts; `levelkept1`, sierra-batch 43563877604: b30_leg1, d20_play1, b40_play1, a30_play1]: §12.1's (4) and (5):

| Map | The level's list, KB a frame | Kept set and buckets both as last frame's (a block reused as it is) | Kept set the same, buckets moved | Together |
|---|---:|---:|---:|---:|
| b30 | 58.6 | 15% (windows 2-69%) | 31% | 46% |
| d20 | 48.0 | 20% (5-48%) | 42% | 62% |
| b40 | 74.5 | 17% (6-62%) | 32% | 49% |
| a30 | 33.5 | 33% (21-49%) | 56% | 89% |

- **Verbatim reuse is small (15-33%)**: the buckets move nearly every frame, by 1.4-5.7 triangles a draw. **The kept set repeats for 46-89%.** So the form isn't v0's block CALLed as it stands; it's a block whose bucket-sorted index copy and bucket runs are patched (levelkept3: that copy is 9-18% of a moved draw's bytes; ~10-20% rewritten, the rest reused), or buckets held with a margin so they stop moving (a look change at the mip step's place: shots).
- **What the count can't say** (§12.1's last line stands): the per-triangle bucket work stays in either form (to know they moved), and whether the passes' ~270-290 µs a draw is the words written is still a PSP split. A patched block saves the words; if the time is the bucketing and classifying, it saves little. **That split comes before the build**: the level's parts timed on the ME on one census run (the timer exists: "GE: parts ... level: passes (binds, lists)", last read on c10 on 2026-10-29, zero in today's runs).
- A draw seen last frame is ~98% of draws, keyed by stream and base texture (the engine rewrites the index list each frame, so its address can't be the key).
- c20 isn't counted: the recon build runs out of heap there (levelkept2, levelkept4).
- **The drive's visible decals and DB** (2026-11-14 02:3x, from the whole read of the levers' statuses): decal batching was rejected as DB on d20 and b30 ("an exact merge almost never applies: the same-texture pairs differ in mip slope, 62% of pairs on d20; one slope a batch would be a look change for ~1 ms at most"), and RR3 (Vita's form) sits proposed. **That price was d20's and b30's.** On b40's drive the ME spends ~11 ms a frame on ~260 drawn decals, nearly all one effect's (the slip track: one texture, one state), where a batch a track at one slope is the far-and-tiny ruling's kind of approximation, not DB's ~1 ms. Every price carries its scenario: DB's stop doesn't cover the drive; `decalmerge` (DM, built) run on b40_play1's drive gives the joinable share there in one emulator batch.

### 12.8 Held buckets without the per-triangle test: a slack per draw (2026-11-14 06:31, audit; a read, nothing run)

For §12.1a's look question (buckets held unless the slant moves a triangle past a margin; the builder's page on `builder/drawlist-level`). The margin alone still needs every triangle's slant each frame to know whether one crossed it. A bound per draw removes that, by the box's second means (work done on change):

- **What a bucket depends on** [D, `angle_bucket_one`]: only the eye's position and the triangle. `1/cos^2` of the angle between the triangle's normal and the line from the eye to its centroid, against edges at half-octave steps; and the far flag from the nearest corner's distance against `DETAIL_FAR`. Nothing of the view's direction.
- **The bound** [D, geometry]: an eye that moves by `m` turns the line to a centroid at distance `|d|` by at most `asin(m / |d|)`, and changes a corner's distance by at most `m`. So when a draw's buckets are made, each triangle has a slack: `|d|` times the sine of its angle to the nearer held edge (the bucket's edge widened by the margin), and for the far flag `| |corner| - DETAIL_FAR |`. **The draw's slack is the smallest over its triangles**: one float kept with the block, with the eye's position when it was made.
- **Per frame, per draw:** `|eye - eye_made| < slack` -> no triangle can have crossed a held edge: the buckets hold by construction, with no per-triangle work. Otherwise re-bucket (all, or §12.1a's patch) and take a new slack. The bound is conservative: it only ever re-buckets early.
- **What sets how often it fires** [U]: near triangles (small `|d|`: the ground under the player, ~0.6 units below the eye) have small slacks, so the draws around the player re-bucket most frames while moving; far draws hold for many frames. The share by bytes is the count to take: over levelkept's windows, for margins of a quarter, a half and a whole bucket, the share of "kept set same, buckets moved" bytes whose draw's slack exceeds the eye's movement since it was made. That is exact in the emulator and sizes the idea before any look is judged.
- **The same shape for the kept set** [U]: a draw whose box was wholly inside the view volume by some margin stays wholly inside while the camera's turn and move stay under a slack kept with it, so the box test and the per-triangle classification are skipped as well. With both slacks a held draw costs two compares and a jump to its block.
- **Where it matters most** [E, `levelkept4_c20`, landed after §12.1a]: on c20 the kept set repeats for 98% of the level's bytes and 61% have moved buckets: the largest share of the five maps, on the map whose ME render job is furthest over (40-50 ms a job, level draws 6-9 of it; the lab's `ai_census_c20_leg1.md`).
- **The look** is §12.1a's question unchanged (a mip step's place up to the margin off, for the frames a bucket is held): drawing only, shots.


## 14. The HUD (the lead 2026-11-15 12:00: the HUD is the draw list box's; draft v0, 15:1x)

> Drafted by the worker (or-job 43747083755, DeepSeek), checked by the audit. **Checked and right:** the blips step is one call a frame at 1,761.7 us on c20's window (the census run 43314261608's profile.txt:1226); `MAXIMUM_MOTION_SENSOR_BLIPS` 16 and `MOTION_SENSOR_HISTORY_COUNT` 10 (the engine's `source/interface/motion_sensor.c:136,138`), so 160 slots walked a frame; the update's once-a-tick guard (`motion_sensor.c:828`). **A caution for any bare figure:** under `hwprofile` the instrument's own on-screen text (`pspDebugScreenPutChar`, 1.1-1.7% of the drawing on the bare sampler runs, the lab's `drawing_me_ledger.md` §3c) lands in the HUD's kind; the step tables here time the HUD's own steps and don't include it. Paths written `sierra/...` and `run-results/...` are the worker's workspace names. The ceiling (a kept sensor target, at most the blips step's own 1.2-1.6 ms a frame mean) is [U] until its counts (§14's last part) are taken.

**What I read, and the reading I took.** The section is drafted against the two step tables that are actually in this workspace — `run-results/sierra-hw/43314261608/profile.txt` (c20, Sierra-III `9d00a903`) and `run-results/sierra-hw/43331666648/profile.txt` (d20, `f50359fa`) — plus the engine as the port compiles it (`porttree/`). **b40's (`43314233107`) and a30's (`43318228389`) step tables are not in this workspace**, so their HUD figures stay as the ledger has them (`projects/sierra/research/opportunities/drawing_me_ledger.md:126`) and are marked so. Where the draft needed a mean I took the unweighted mean of the per-window figures over the play windows in which the step appears (c20: windows 3–48, 46 windows; d20: windows 5–37, 33), said so, and gave the window it came from; nothing is extrapolated. `[D]` = read in code or data, `[U]` = my inference, `[M]` = the lab's instrumented measurement.

### 14.0 Table of findings

| # | Finding | Label |
|---|---|---|
| 1 | The HUD's drawing is **1.3–2.5 ms a frame** (ledger §3b) and of it the motion sensor's blips are the single largest step on every map: **mean 1.62 ms a frame on c20** (max 2.57), **1.16 on d20** (max 6.35), 0.5 b40 / 0.4 a30 per the ledger. The ledger's c20 figure of 1.5 is slightly low against the step table's own numbers | [M, tables; D for the labels] |
| 2 | The blips step runs **exactly once a frame** and its `us/call` *is* its per-frame cost (c20 w20: 1.76 ms/frame, 1761.7 µs/call) | [D, arithmetic on M] |
| 3 | Each frame the blips step walks **10 history snapshots × 16 blip slots = 160 slots**, and issues **one GE draw call per live slot per snapshot** (up to 160) into a 64×64 target of its own, plus a black clear quad, a sweep quad, a mask quad and one composite sprite | [D] |
| 4 | The **blip data changes on a tick, and only a fresh scan 1 tick in 15** (`MOTION_SENSOR_UPDATE_PERIOD = 15`); the other 14 ticks copy the previous datum. Only the sweep's scale changes every frame | [D] |
| 5 | So "work on change" applies to the blips' target but not to the sweep: a kept blips target would save **at most the step's own figure** (1.62 ms c20, 1.16 d20) less the unsized per-frame `update_motion_sensor` walk inside it | [U on M] |
| 6 | All earlier HUD levers are priced under 1 ms and stopped for being under a pair's spread; **no lever touches the blips' per-frame walk**, and the VFPU trig lever that exists covers `hud_draw.c`, not `motion_sensor.c` | [D] |
| 7 | The counts owed are the blips' update/render split, the units walked a frame, the b40/a30 step tables, and a picture test of a kept target | — |

### 14.1 Each HUD step's cost by map, from the step tables

The steps are Sierra-III's own labels — `psp_fixups.py` puts `"hud: unit motion sensor blips", "before"` on the `motion_sensor_draw_screen(` call (`sierra/psp_fixups.py:2232`), which the generated source shows as `PSP_STEP("hud: unit motion sensor blips");` on `motion_sensor_draw_screen(` (`porttree/source/interface/hud_unit.c:1217-1221`). `stepsplit.py` groups `^hud:` into "D HUD and screen" (`projects/sierra/scripts/stepsplit.py:26`); note `^hud_update` is grabbed earlier, by "T tick: other" (`projects/sierra/scripts/stepsplit.py:16`), so `hud_update` is *not* in the HUD's drawing figure, while `hud_messaging_update` is.

**c20 (PSP-3000, `43314261608`, `profile.txt`), ms a frame, unweighted mean of windows 3–48 and the worst window; calls a frame; µs a call (median window ≈ mean ÷ 1):**

| Step | mean | worst (window) | calls/frame | µs/call, range |
|---|---:|---|---:|---|
| `hud: unit motion sensor blips` | **1.62** | 2.57 (w26), 0.06 (w47) | 1.0 | 116–2566 |
| `hud: unit shield + health` | ~0.20 | 0.29 (w34) | 1.0 | 163–290 |
| `hud: grenades` | ~0.19 | 0.26 (w39) | 1.0 | 158–258 |
| `hud: weapon numbers` | ~0.15 | 0.18 (w31) | 3.0 | 46–60 |
| `hud: weapon statics` | ~0.14 | 0.20 (w18) | 3.0 | 40–67 |
| `hud: unit motion sensor panel` | ~0.08 | 0.24 (w34) | 1.0 | 75–106 |
| `hud: crosshairs` | ~0.10 | 0.18 (w20) | 1.0 | 74–215 |
| `hud: weapon meters` | ~0.07 | 0.09 | 3.0 | 21–29 |
| `hud: unit auxiliary overlays` | ~0.12 | 0.17 (w39) | 1.0 | 106–170 |
| `hud_messaging_update` (grouped with the HUD) | 0.08–0.38 | 0.38 (w22) | 1.0 | 33–384 |

**d20 (`43331666648`, `profile.txt`), windows 5–37:**

| Step | mean | worst (window) | calls/frame |
|---|---:|---|---:|
| `hud: unit motion sensor blips` | **1.16** | 6.35 (w37), 2.87 (w34) | 1.0 |
| `hud: unit shield + health` | ~0.19 | 0.29 (w34) | 1.0 |
| `hud: grenades` | ~0.18 | 0.27 (w34) | 1.0 |
| `hud: weapon numbers` | ~0.17 | 3.12 (w35), 0.71 (w31) | 3.0 |
| `hud: weapon statics` | ~0.15 | 0.26 (w34) | 3.0 |
| `hud: unit motion sensor panel` | ~0.09 | 0.12 (w34) | 1.0 |
| `hud: crosshairs` | ~0.10 | 0.19 (w19) | 1.0 |

**b40 and a30** (from the ledger, the tables not in this workspace): HUD and screen 1.6 / 1.3 ms a frame, of which the blips 0.5 / 0.4 (`projects/sierra/research/opportunities/drawing_me_ledger.md:126`: "| **HUD and screen** (`hud: unit motion sensor blips` alone 0.9 / **1.5** / 0.5 / 0.4) | 2.3 | 2.5 | 1.6 | 1.3 |"). **[M via the ledger; not re-read here]**

**Two corrections to the ledger's row, from the tables themselves [D, arithmetic on M]:**
- c20's blips mean is **1.62**, not 1.5, and its worst window is **2.57 ms**, not 1.5. The ledger's figure is the low end of the play windows, not the mean.
- "Whatever the scene" holds for the total but not for the blips on d20: the step is **0.18–0.92 ms in the quiet windows** (w5–w18) and **0.7–6.35 ms** once the swamp's units are about (w19–w37) — so the blips are scene-dependent on d20, and they track the number of blips drawn (§2).

Witness lines: `run-results/sierra-hw/43314261608/profile.txt:1226` `hud: unit motion sensor blips                     1.76     2.3      12.8    1761.7`; `run-results/sierra-hw/43314261608/profile.txt:1607` `hud: unit motion sensor blips                     2.57     2.7      10.4    2566.2`; `run-results/sierra-hw/43331666648/profile.txt:2311` `hud: unit motion sensor blips                     6.35     4.9       8.1    6041.5`.

### 14.2 What the motion sensor's blips step does each frame

`hud_render_unit_interface` computes the panel's anchor, then calls the sensor (`porttree/source/interface/hud_unit.c:1217-1221`):

```
PSP_STEP("hud: unit motion sensor blips");
motion_sensor_draw_screen(
    local_player_index,
    local_player_count() > 1,
    &corner);
```

and `motion_sensor_draw_screen` does **two things a frame** — an update and a render (`porttree/source/interface/motion_sensor.c:792-796`):

```
	if (local_player_index != NONE)
	{
		update_motion_sensor(local_player_index);
		render_motion_sensor(local_player_index, in_multiplayer, pt);
	}
```

**(a) `update_motion_sensor` — per frame, over the 16 slots** (`motion_sensor.c:548-591`). For each of `MAXIMUM_MOTION_SENSOR_BLIPS` (16, `motion_sensor.c:136`) it takes the object index, `unit_try_and_get`, calls `should_draw_object` (which itself re-tests `unit_try_and_get`, `game_engine_draw_object_in_motion_sensor`, the trigger and grenade-throw flags, then `object_get_velocities` and a squared-magnitude test against `motion_sensor_velocity_sensitivity` — `motion_sensor.c:1033-1070`), then `object_get_bounding_sphere` and a distance-squared test (`motion_sensor.c:556-584`), and writes the blip's packed position. It also fetches the custom (goal) blips (`motion_sensor.c:603-608`).

**(b) `render_motion_sensor` — per frame, 10 history snapshots × 16 slots** (`motion_sensor.c:675-712`):

```
	for (history_index = 0;
		history_index < MOTION_SENSOR_HISTORY_COUNT;
		history_index++)
	{
		...
		for (blip_index = 0;
			blip_index < MAXIMUM_MOTION_SENSOR_BLIPS;
			blip_index++)
		{
			struct motion_sensor_blip *blip = &sensor->blips[blip_index];

			if (blip->type != _blip_type_none)
```

with `MOTION_SENSOR_HISTORY_COUNT = 10` (`motion_sensor.c:138`). **So 160 slots are walked a frame and every live one is drawn again** — the whole history is re-rendered, each snapshot with its own fade and radius (`motion_sensor.c:685-689`, `power(1.0f - weight, 3.5f) * 7.0 + 1.0`), and the custom blips on top (`motion_sensor.c:714-738`).

**(c) What each drawn blip costs** — `render_blip` (`motion_sensor.c:1095-1145`): a `rotate_vector2d` with `sine(-sensor->yaw)` and `cosine(-sensor->yaw)`, a squared magnitude, a `square_root`, a `pow(distance / range, 0.7)`, two `scale_vector2d`s, then

```
			rasterizer_hud_motion_sensor_blip_draw(
				&blip_position,
				fade,
				blip_draw_size,
				blip_color,
				(boolean)(blip_type == _blip_type_custom));
```

`real_math.h`'s `sine`/`cosine` are `sin`/`cos` (`porttree/source/math/real_math.h:955-965`) and `power` is `pow` (`real_math.h:1040-1045`); the PSP narrows them to the float library (`double halo_sin(double x) { return sinf((float)x); }`, `sierra/psp/src/psp_libc.c:298`; `halo_pow … powf`, `psp_libc.c:309`, wired at `sierra/psp_fixups.py:4988, 4995`). The VFPU trig lever deliberately does not cover this file (§3).

**(d) What the GE gets** — each blip is its own quad and its own draw call: `psp_ge_blip` writes the record and calls `blip_draw` (`sierra/psp/src/psp_ge.c:25378-25388`), which binds the blip texture and calls `blip_quad` (`psp_ge.c:25365-25375`), which ends in **`sceGuDrawArray(GU_TRIANGLE_FAN, … , 4, NULL, out)`** (`psp_ge.c:25300`) — one per blip per snapshot. On top of that, per frame: `psp_ge_blips_begin` flushes the world's see-through parts and *draws the target black with another quad* (`psp_ge.c:25349-25363`; `blip_target_open` at `psp_ge.c:25327-25347`, "black, drawn rather than cleared"), and `psp_ge_blips_end` draws the sweep and the mask quads and then composites the 64×64 target to the screen as one sprite of radius 42 game pixels (`psp_ge.c:25507-25554`).

**Why it costs up to 1.5–2.5 ms on the main CPU** [U on M]: the per-frame cost scales with the number of blips drawn, not with the scene. c20's window 20: `hud: unit motion sensor blips 1.76 ms/frame` against `blips 674 (104.5 a frame, 6.4 us each)` (`run-results/sierra-hc…` — precisely `run-results/sierra-hw/43314261608/log.txt:2161`), i.e. **≈17 µs of main-CPU time a blip** (record, `GE_PRETOUCH_HUD`, the GE's 2D state and one `sceGuDrawArray`), while the replay's own share is 6.4 µs a blip. The step is 1.76 of 104.5 × 17 ≈ consistent, and the worst window (d20 w37, 6.35 ms against `blips 561 (82.0 a frame)` at `run-results/sierra-hw/43331666648/log.txt:6943`) is **≈77 µs a blip** — the step is the *same* work in a starved frame, not more work. What exactly makes the per-blip cost 17 µs here and 77 µs there (list pressure, texture binds, cache) is **not measured** — see §5.

Two structural notes the box cares about [D]:
- the sensor draws into **a tiny target of its own** and composites it — so its GE block is already separable from the rest of the HUD (`psp_ge.c:25206-25217`);
- the sensor's target is where `blipdepth` put it: **in the depth buffer's first 16 KB**, not at `0x0CC000` (`psp_ge.c:25232`, `BLIP_TARGET_DEPTH (2 * FRAME_BYTES)`), live in these runs (`run-results/sierra-hw/43314261608/log.txt:171-232`, `GE: blipdepth: the motion sensor's target in the depth buffer at 088000`).

### 14.3 Every earlier lever on the HUD, its price, and why it stopped

| Lever | What it was | Price | Why it stopped |
|---|---|---|---|
| **HUDX** (`projects/sierra/findings/levers.tsv:163`) | HUD and menu widgets cached / a static-part display list | "In play the hud: steps sum 3.9 ms a frame on b30 [M], but the first HUD draw pays `list_hud_phase` (transparent_flush + taa_resolve), so **the HUD's own work is ~0.9 ms a frame** [E]"; "A static-part display list would save a fraction of that" | "sized: smaller than thought; not built"; "M6: dropped from every map: too small to move any map's limit" |
| **SMALLME** (`levers.tsv:399`) | decals, HUD, screen quads as ME jobs | "the HUD **0.64 ms** of b30's 140.8 ms frame (0.45%; … motion sensor 0.24, shield and health 0.11, grenades 0.10, weapon numbers 0.09, statics 0.08)"; "removes at most ~1.2 ms a frame … on b30's flight" | "parked (research, master 2026-10-29 15:40Z) … under a PSP pair's spread"; revive_if: "the HUD's and decals' main-CPU time rising above a pair's spread" (`grounds.tsv:399`) |
| **VRB1** (`levers.tsv:249`) | HUD pieces into the framebuffer stride-tail strips | "removes ~0 (the HUD costs 10.8 KB a frame of main-RAM reads in all on b30 = ~0.1 ms of tick…)" | "rejected (census: 5 KB at most)"; revive_if: "HUD pieces small enough for a 32-px strip" (`grounds.tsv:249`) |
| **VRB2 / `blipdepth`** (`levers.tsv:250`) | the sensor's 64×64 target in the depth buffer, freeing 16 KB | "removes ~8-30 KB a frame of GE main-RAM reads … = **0.07-0.29 ms** of tick at 9.6 us a KB [U], under the ~1% gate alone"; exact, merged | "ship with VRSP, not on its own" — **it is on in the census runs** |
| **VRB3** (`levers.tsv:251`) | depth buffer reused past the sensor target | "removes ~0.1 ms at best: what draws after the 3D pass reads little from main RAM on b30 (HUD 10.8 KB a frame…)" | "idea (not built)" |
| **FM5 / HUDPIN** (`levers.tsv:199`, `:353`) | HUD and sensor textures pinned under a full cache | a correctness fix, not speed: unpinned draws 88→0, missing 110→23; pins hold 315 KB on b30 / 129 KB on a10 | merged (b144polish), on by default |
| **GR / `gerace`** (`levers.tsv:118`) | draws whose inputs change after hand-over | instrument only: "only the HUD text picture changes after hand-over (d20 3 frames, a10 5); no port race to fix" | closed as a race question |
| **VFPUTRIG** (`levers.tsv:536`; `psp_fixups.py:51725-51743`) | sine/cosine on the VFPU for drawing-only callers, "the HUD's rotated bitmaps and overlays" | b30 PSP pair: drawing −3%, frame −1.3% | shipped on; **but its HUD fix is `hud_draw.c` only — `motion_sensor.c` is not in `FIXES_VFPUTRIG`**, so the blips' `sine`/`cosine`/`pow` still take the float library [D] |
| **RENDERTICK** (`levers.tsv:344`) | "the HUD three" run once a frame instead of every tick | "hud_update 11.7 us a b30 tick (10.5 c20)"; ≤ ~0.2% of the tick | "counted, not built … under the bar" |
| **RB1** (`levers.tsv:85`) | weapon interface state zeroed (stack garbage) | restoration, "play unchanged" | shipped b126; not a speed lever |

`levercheck.py hud|blips|motion sensor` (the ledger says it is owed at `drawing_me_ledger.md:150`) now comes back with: **no lever on the blips' per-frame walk exists**, only HUDX's "static-part display list" glance, SMALLME's move to the ME, and VRB2's memory placement. The word `blipdepth` is VRB2, a memory lever (`sierra/docs/review_vrbroom.md`, `levers.tsv:250`), not a time lever on the drawing.

### 14.4 What changes on a tick or an event, and what changes every frame

The distinction the box needs is whether a GE block can be kept and replayed while its inputs are unchanged — the mechanism the port already has for models (`psp_ge.c:14236-14247`: "An object whose inputs changed RSO_CHANGES times running without a send … rests for RSO_REST frames"; `rso` is on by default in the campaign, `psp_ge.c:14912`).

| Part | Changes | Evidence | Ceiling if kept [U on M] |
|---|---|---|---|
| **the sensor's blip set** | **on a tick, and a fresh scan only 1 tick in 15** | `motion_sensor_tick` guards the update with `if (motion_sensor_globals->last_update_time != game_time_get())` (`motion_sensor.c:828`); `motion_sensor_update` re-scans only when `(current_time % MOTION_SENSOR_UPDATE_PERIOD)` is zero, and **otherwise copies the previous datum** (`motion_sensor.c:312-331`), `MOTION_SENSOR_UPDATE_PERIOD = 15` (`motion_sensor.c:139`) | up to the blips step: **1.62 ms c20 / 1.16 d20** mean, less the unsized per-frame `update_motion_sensor` walk inside it |
| **the sensor's sweep** | **every frame** (a continuous function of game time, interpolated between ticks) | `motion_sensor.c:741-747`: `render_interpolation_drawing() ? motion_sensor_sweep_theta(render_interpolation_game_time_sec(game_time_get())) : sweep_theta`; the function itself `motion_sensor.c:802-817` | a handful of µs: 2 quads (`psp_ge.c:25412-25433`), not worth keeping |
| **the sensor's 10 history snapshots** | **the index advances a tick; 14 of 15 ticks carry identical data** | `motion_sensor.c:307-310` (index), `:325-328` (the copy) | the 160-slot walk's cost — this is what "keep while unchanged" would really buy |
| **the sensor's panel** (background + foreground) | constant (fixed placement, `NONE` flash time) | `hud_unit.c:1190-1207` | ~0.08–0.24 ms a frame (its own step) |
| **weapon statics / meters / numbers** | on events: state flags and number values from `weapon_build_weapon_interface_state` | `hud_weapon.c:1745-1811`; the values `number_values[state_index]`, `state_flags[state_index]` | 0.07–0.20 ms each |
| **crosshairs** | on an event (weapon/zoom/firing state) | `hud_weapon.c:2111-2116` | ~0.10 ms |
| **shield + health, grenades** | on damage / on a grenade count change | `hud_unit.c:945-1155`, `hud_weapon.c:2144-2147` | ~0.19-0.20 ms each |
| **`hud_messaging_update`** | on an event (a message) | `hud.c:1620-1622` | 0.08–0.38 ms |
| the **first HUD draw** | once a frame by construction | `psp_ge.c:3076-3085`: `list_hud_phase` runs `transparent_flush(); taa_resolve();` once, on whichever HUD call comes first — the note already in the box at `sierra/docs/box_draw_list.md:349` | not HUD work: it is the see-through flush, already §12.4's |

**The ceiling, stated carefully.** A kept sensor target is the one HUD block whose inputs mostly do not change; its ceiling is the blips step's own measurement, **1.62 ms a frame on c20 and 1.16 on d20** (max 2.57 / 6.35), and that is an upper bound only: (i) the per-frame `update_motion_sensor` inside the step would have to move to the tick to be saved at all, and it is **not sized**; (ii) the sweep must still be drawn every frame, so the target cannot be left untouched as it is today, where `blip_target_open` paints it black first (`psp_ge.c:25342`) — a kept target needs either a second small target or a composite of the kept blips plus a fresh sweep; (iii) the whole rest of the HUD is at most **~0.6-0.9 ms a frame** (the small steps in §1, matching HUDX's "the HUD's own work is ~0.9 ms a frame", `levers.tsv:163`). **No ceiling here is a promise of a 1.6 ms cut.**

### 14.5 The counts owed

1. **The blips step's split between `update_motion_sensor` and `render_motion_sensor`** (`motion_sensor.c:794-795`). There is no `PSP_STEP` inside `motion_sensor.c` at all — a one-line instrument, then one census window answers it. Without it, "keep the block" cannot be sized: it may be that half the step is the update, which no kept block removes.
2. **The units walked a frame.** `update_motion_sensor` walks only the 16 recorded slots, but the tick's scan walks every unit (`motion_sensor.c:389` `object_iterator_new(&iter, _object_mask_unit, TRUE);`); how many units a d20/c20 frame's slots point at, and how many of the 160 history slots are live, are not counted anywhere. `mekinds`' `blips N (M a frame)` is the drawn count only (`psp_ge.c:1591`, `[DEFER_BLIP] = "blips"`).
3. **Why a blip costs 17 µs in one window and 77 in another** (§2). The candidates are the per-blip `GE_PRETOUCH_HUD` (`psp_ge.c:25380`, which can `inflight_sync()` — `pretouch` at `psp_ge.c:1054-1061`), the texture bind in `blip_texture` (`psp_ge.c:25304-25317`), and list pressure. Not measured; do not assume.
4. **b40's and a30's step tables for `hud: ...`** — this workspace has c20 and d20 only. The ledger's 0.5 and 0.4 ms are unverified here.
5. **A picture test of a kept target** (the rso pattern's proof shape): a PSP pair, `statecrc` + `framecrc`, before any build.
6. **What `nohud` actually removes.** The keep word exists but only at `hud_render_weapon_interface`'s door (`psp_fixups.py:4902`, `PSP_KEEP_ONCE("nohud")` → `return`), so no existing A/B prices the whole HUD or the sensor; a `nohud`-style word in `hud_draw_screen` and a `noblips` word in `motion_sensor_draw_screen` would give the frame-level price of each, and are the cheapest way to check §1's figures against the frame (2 runs, one keep word each).

**Unfinished in this draft.** The b40/a30 columns of §1 and the weighted-by-frames means are owed (I used unweighted window means and said so); §2's µs-a-blip arithmetic is mine on the lab's two counters and should be re-derived by the audit; §4's ceiling is an upper bound, not a price.
