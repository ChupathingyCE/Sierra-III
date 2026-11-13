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

3. **The first slice: the origin held still (`ge_eye`)** (the lead, 2026-11-12 15:35: the box's prerequisite and every retained form's, R's included). `ge_eye` follows the eye every frame (`psp_ge.c` ~6399) and every model matrix, bone slot and point light is written relative to it, so an unchanged instance's words change whenever the camera moves, and no word can be kept. The slice holds the origin on an N-unit grid with the view carrying the remainder (as level_exact's shift already does); s0 (drawreuse1, the lab's 43309859657) counts the hops at N = 2/4/8. Proof: pictures in the emulator; play and seeds identical (drawing only).

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
4. **The arena:** main RAM, two generations, freed by the GE's finish of the last frame that CALLed a block. Sized from drawreuse1's peaks (b30 peak ~76k instance words a frame, of which the kept are the ~21% [E]; with rso as the gate, the kept words only).
5. **Proof:** `drawlistcheck` (each CALLed block's words against a fresh replay's, the origin held both sides); framecrc; play and seeds identical; strict. Then the box's standalone price over a capture (`PSP_CAPTURE=draw_list`) and its in-frame pair.

### 10.7 Against the budget (§4), honestly

- **The ME (budget ~12 ms):** v0 takes the kept instances' share of the models' replay. ~~The census's unchanged share of the parts' indices is 30-54% on the flight ... ~11-20 ms of the flight's ME [U]~~ **Revised by s0 (2026-11-12 16:2x):** the unchanged instances hold ~21% of b30's instance words (12-31% across b30, b40, c10, d20) [E], so of the flight's 37.8 ms of model replay (C4) [M] v0 takes **~5-12 ms [U on E and M]**, if time follows words. **The ME's row is carried by the changed instances:** posed units and moving objects, the ones B can't touch. Their cost is R's forms (bind, state, geometry and draw calls made once, ~29-31 ms of the flight's model replay at stake, R §2.4) and the posing box. v0 alone is not 12 ms, and B is the smaller half of it.
- **The main CPU (budget 11 ms):** v0 moves it little: RSO already skips scenery's walk, so v0 adds the other still types and drops the copies. The main CPU's 35.8 ms is mostly the posed units' per-part chain and the walk itself; that's **v1's**: one instance per object for the changed half (R §2.1), its node matrices handed over by the posing box rather than copied. v0's standalone price is read against the ME's row only, and this page says so in its result.
- **The GE (33.3 ms):** a CALLed block is read from main RAM as the list was; v0 neither adds nor removes GE work. The GE lists box's row.
