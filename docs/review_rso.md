# Review: RSO, a still object's records sent again (branch `rso`, keep words `rso` `rsoall` `rsocheck` `rsokbN`)

Off by default. Base: w140 4a08989. Labels: [M] PSP, [E] emulator or cache model, [D] read in code or data, [U] inference.

## What it does

Under M6 #2 the engine's drawing walk writes the GE layer's calls down as records (`psp_ge.c`, `gedefer`), which the ME carries out a frame later. A drawn object's records are a model begin, a shape, an effect, and per part a multipurpose and a draw. With `rso`, a scenery object whose render_model inputs equal, byte for byte, the ones its kept records were made from sends those records again, and render_model's walk (the relative node matrices, the regions and parts, the shaders' keys, colours and change colours) is skipped. `rsoall` offers every object type on the same key (a measurement word: the first cut is scenery). `rsocheck` walks anyway and compares the walk's records with the kept ones word by word; its log line must say 0 differ.

Removal estimate (survey count, survc1 [E]): reusable model draws a frame b30 67-112, a30 102, c10 45; scenery most of them; the reusable share of the objects pass is b30 15-35%, a30 25-31%, c10 29-33%.

## The key, and why it makes the reuse exact

What a record holds, and where each word comes from [D: `psp_ge.c` `defer_model_*`, `psp_fixups.py` rasterizer.c's model begin and draw]:

| Record | Words | From |
|---|---|---|
| model begin | node pointer (the replay reads only whether it is set), node count, base map scale, light count, ambient, distant lights 0 and 1 (colour and direction), the relative node matrices | render_model: the object's node matrices x the model's default inverses (tag data); the lighting |
| shape | first-person flag, centroid, radius, first-person near and far | the flags; the object's sphere; the globals tag (fixed for the map) |
| effect | type, intensity | the model effect |
| multipurpose | multipurpose and glow keys, change colour, glow colour | the shader (tag data), the object's change colours; the glow from the time (below) |
| draw | vertex and index addresses, counts, strip, colour, base key, scales, alpha test | the geometry cache's lock of the part's buffers; the shader |

**Nothing camera-dependent is in a record**: to_camera, the near-plane test, the size floor, the projection, the fog and the see-through sort are all worked out as the records are carried out, from the frame's own window begin [D: `psp_ge_model_shape`, `psp_ge_model_point_light`, the replay]. The one camera input the walk uses is the level of detail (from `object_get_level_of_detail_pixels`), and the key holds the level it picks (`geometry_detail_level_index`, computed the same way), not the pixels.

**The key** (`struct psp_rso_key`, models.c's fixup): the model index, the level of detail, the forced shader permutation, the flags, the node count, the object's region permutations, its 4 change colours and 4 function values, the whole `render_lighting`, the centroid and radius, the whole `render_model_effect`, and the object's world node matrices (at most 16 nodes). Compared with memcmp: a hash collision can't make a false hit. Padding is zeroed first; padding copied inside the lighting and effect structs can only make a false miss.

**What the key leaves out, and why that's safe:**
- *Inputs that vary within a map but aren't in the key make the model not kept* (`psp_rso_volatile`, set during the walk): a self-illuminated shader with an animated glow (it reads the game time), any see-through part (its map animation reads the time, and it goes to the frame's sorted list), any part that falls to the Xbox path (dynamic buffers, geometry not yet read). And any record kind other than the five above (a point light, a detail map, a decal, a big model's drained run) stops the capture.
- *Made only where nothing else is read* (`psp_rso_key_make`): no point lights (their indices name the frame's light list), effect type none, flags at most no-planar-fog (no first person, shadow, sky or immediate), no debug view (`render_model_nodes`, markers, counts, no_geometry, debug_model_lod), no cortana hack.
- *Constants for a map*: shader and model tag data, the texture keys (`psp_tex_bitmap_key`, `psp_tex_has`, `psp_relief_key` are lookups with no side effects), keep words, the globals tag. Every entry is dropped at a new map or BSP (`psp_ge_forget_level`).
- *The walk's other effects are done again*: each texture the records bind is pretouched in record order (the same `multipurpose_pretouch` and model-base pretouch the walk calls), and each part's buffers are locked again in the geometry cache; if a lock gives an address other than the record's (the block was evicted and read back elsewhere), the walk runs instead. The rasterizer fixup's per-model globals (animation values, change colours, unique id) are read only within the same model's walk, after its own begin [D]; the Xbox layer's per-model state is set at begin and cleared at end, and d3d8_gl.c is the null renderer (FIXES_GEFEED) [D].
- *Contiguity*: a span is kept only if no reset of the record buffer (drain, full buffer, hand-over, sync: `defer_resets`) came between its first and last record, the records parse exactly to the end and their count matches. A frame whose buffer fills while sending a kept span drains as defer_take does: the same calls in the same order.

## Memory

One block of **free user RAM outside the heap** (`sceKernelAllocPartitionMemory`, as the display list and the see-through table), never the heap: 48 KB (`rsokbN`, 8-256), halved until 80 KB stays free there (the see-through table's rule). a10 has 4-12 KB free inside the heap but ~127 KB free outside it on the PSP-1000 [M], so a10 gets 32 KB or less; a30's heap is untouched. No room: off, logged. An entry is 28 bytes of header + the key (232 + 48 x nodes bytes) + 8 bytes a part + the records (about 144 + 52 x nodes + 92 x parts bytes): ~560-850 bytes for a one-node scenery model of 2-4 parts [U], so 48 KB holds ~60-85 objects; the log line gives entries and KB used, and "no room" counts the objects that didn't fit. Entries unused for two windows are dropped and the rest moved down at a frame's first object.

## Cost when off

One test a model draw (`psp_rso_object`, set to NONE), one a part (`psp_rso_capturing`), one a window (`rso_window`), and a counter at each buffer reset.

## Tests to run (proof)

framecrc and play, both seeds, identical with and without `rso` on b30, a30, c10 under `nomouth`; `rsocheck` 0 differ; heap lines on a30 and a10 unchanged; the cache model's objects pass on c10, a30, b30. Results: the lab's findings.

## Proof and gain, first batch [E]

The first batch (b30, a30, c10, a10; hands off, `fixedticks fixedseed nomouth statecrc framecrc`; cache models hands off to game time 120 s) on the first cut:
- **Play and both random seeds the same at all 6,000 ticks** on every map, `rso`, `rsocheck` and `rsoall rsocheck` against off. The whole state differs in `objects` and `object looping sounds`, the allocations that follow the emulator's speed; two runs with it off agree in all of it on b30 and a30, and differ in `objects` on c10.
- **But that proves nothing yet: no record was sent again.** Every log line shows 0 entries kept, 0 inputs changed and `rsocheck` 0 walks compared, while a30 kept 30-36 walks a window and c10 1.6. The stamp that ages the entries moved at each window begin, and a frame begins more than one (`rasterizer_window_begin` and the structure's lightmaps begin both start the GE's window), so the two-stamp limit dropped every entry before its object came round again. The stamp now moves at the frame's end.
- **Most offered models weren't kept at all**: b30 85-94 a window, all of them; a30 74-98 of 105-134; c10 38 of 40; a10 2-8 of 3-8. The reasons were counted together (a glow that follows the time, a see-through part, the Xbox path); they are now counted apart. A self-illuminated shader whose two glow colours are equal glows the same at any time, so it no longer bars the model.
- **Cost with nothing sent again**: the objects pass c10 3.26 -> 3.44, a30 7.20 -> 7.79 (`rsoall` 7.82), b30 7.87 -> 8.11 ms a frame in the cache model: keying and copying every kept walk.
- Memory: 48 KB outside the heap on every map, 81-85 KB left free there; heap untouched.

The second batch tests the fix on the same maps.

## Proof and gain, second batch [E]

The same runs on the fix (entries aged by frames, a constant glow kept, the reasons counted apart):
- **Records are sent again now**: a frame of the cache runs' second 600-frame window, b30 56 of 93 offered, a30 50 of 134 (`rsoall` 54 of 155), c10 29 of 40; a10 offers 3-8 and sends 0-2.
- **Play and both random seeds the same at all 6,000 ticks** on b30, a30, c10 and a10 (`rso`, `rsocheck`, a30 `rsoall rsocheck`), the whole state differing only where it follows the emulator's speed, as before. framecrc against off differs at b30 715, a30 1, c10 3, a10 1 frames of ~3,000, inside two runs with it off (b30 801, a30 3, c10 2): b30's frame hashes differ run to run, so they can't judge it there.
- **`rsocheck` doesn't say 0 differ yet**: b30 47,720 of 114,483 compared walks, a30 1,718 of 111,017, c10 9,986 of 17,301; the first difference is always in a model shape record, word 6. That record is the one `rasterizer_model_end` writes with no centroid: `defer_copy` writes only the "none" flag and leaves the three centroid words as the buffer had them, and the replay never reads them [D]. Every map averages 2.6-2.9 differing words a differing walk, at most the three [U]. The compare must skip those words (or `defer_copy` write zeros there) before it can prove the rest exact.
- **Why models aren't kept**, a frame of offered: no room (the 48 KB block full) b30 27, a30 42; a see-through part a30 25, b30 2; inputs changed since the last walk c10 10, a30 17, b30 8; a glow that follows the time only with `rsoall` (a30 4); the Xbox path ~0.
- **Gain is small**: the record lane in the cache model c10 10.70 -> 10.42 ms a frame (-2.6%; objects pass 3.27 -> 2.91), a30 11.75 -> 11.83 (+0.7%; `rsoall` -0.7%), b30 16.96 -> 16.73 (-1.4%). The walk's steps fall by 0.71-0.91 ms, but render_model's own step grows by 0.39-0.87 ms: every offered object's key is zeroed (about 1 KB with 16 nodes' room), filled and compared, and each object sent again copies its records (4-5 thousand words a frame). At the share of the PSP's record time that is c10 ~0.7, a30 at most ~0.2 and b30 ~0.45 ms a frame [E x M], under the 1% gate on a30 and b30 and only at it on c10.
- Memory: the heap's lines on a10 and a30 the same with it on (most in use, least free inside).

**Not merged.** What would bring it over the gate [U]: compare the key's fields in place against the inputs instead of building a zeroed copy, and only the bytes used (about half the per-object cost); room for the objects that don't fit, by smaller entries or by keeping the most-sent ones rather than the first; with both, roughly a30 9%, b30 7% and c10 5% of the record lane. A see-through part's map animation reads the time, so keeping those models means working the animation out as the records are carried out; the objects whose inputs change need a count of which key field moves first.

## v3: the check whole, the key in place, room for what is sent again

Base: rso 69520aa merged with w140 bc12443 (one conflict, psp_fixups.py: FIXES_RSO and FIXES_LFS both added; both kept, RSO listed before LFS).

- **Records zeroed where nothing is copied** (`defer_copy`, psp_ge.c). The model end's shape record (`psp_ge_model_shape(0, NULL, ...)`) and a begin with fewer than two distant lights left their words unwritten: whatever an earlier frame had left in the buffer. The replay reads them only behind the flag word [D], so this changes nothing drawn; it is done in the record maker rather than by teaching rsocheck to skip those words because then a record is the same word for word whenever its inputs are, so rsocheck can compare records whole and a kept record never carries a stale word to the ME. Cost: 3-6 stores where a pointer is NULL.
- **The key compared in place** (`psp_rso_key`, models.c's fixup). v2 zeroed a ~1 KB struct, filled it and memcmp'd it for every offered object, kept or not. Now `psp_rso_find` returns the object's kept key (if any), and models.c compares its inputs against it field by field, stopping at the first difference; only once a walk is kept (`psp_rso_capture_end` returns where) is the key written, straight into the entry. Same layout both ways (one function, `fill` true or false). Only the bytes the walk reads: the header words (model, level of detail, forced permutation, flags, node and distant light counts, region count), the region permutations the model has (`MIN(regions.count, region_permutation_count)`), the ambient colour, both distant lights, the reflection tint and shadow, the effect from its intensity on, centroid, radius, change colours, function values, node matrices. Left out: padding, the point light indices (read only below `point_light_count`, which must be 0 [D: the rasterizer.c fixup's loop]), the effect's type (must be none). Each length is fixed by words compared before it, so a compare never reads past the kept key.
- **Inputs that keep changing rest** (`rso_rest`). v2 kept an object again every frame its inputs changed (b30 8, a30 17, c10 10 a frame), filling the room with entries that were never sent and copying ~700 bytes each time. Now an object whose inputs changed twice running without a send gets a header-only entry and is walked, not kept, for 64 frames (`RSO_CHANGES`, `RSO_REST`); counted as "resting" on the log line. Only which objects are walked changes, never what a walk or a send writes.

Exactness argument unchanged: a hit still needs every byte the walk reads to be equal, and the records sent are the ones that walk wrote.

## Proof and gain, v3 [E]

v3's first batch sent nothing on any map: the key's length was counted with 12 words a node matrix, but `real_matrix4x3` is 13 (the scale first), so every kept key was a node count's words short, the fill ran into the entry's own buffer fields, and every compare stopped at the node matrices as "inputs changed", so every object rested (b30 78-89 of 86-94 offered a frame). Play and both seeds were still the same at all 6,000 ticks on every map. The length is now counted by the same field list as the fill and the compare (one macro list; with no key it only adds up), host-tested: length = fill = compare for 1-16 nodes and 0-5 regions, a moved matrix fails the compare, nothing written past the entry.

The second v3 batch, on the fix (same maps, words and stretches as before):
- **Records sent again**, a frame of the cache runs' second 600-frame window: **b30 68 of 93** offered (6,075 words; 75 entries in 42 of 48 KB; later windows 68-70 of 93-94), **a30 65 of 134** (5,265 words; 109 entries in 43-45 KB; 43 resting, 25 see-through), **c10 15 of 40** (1,919 words; 40 entries in 20 of 24 KB; 20 resting). Against v2: b30 56, a30 50, c10 29. c10 loses: its offered models' inputs change often enough that most of them rest (the hands-off run's only window: 2 sent, 35 resting).
- **`rsocheck` 0 differ on every map**, now with records compared whole: b30 138,840 walks, a30 145,242, c10 9,819, a10 1,001.
- **Play and both random seeds the same at all 6,000 ticks** on b30, a30, c10, a10 (`rso` and `rsocheck` against off); the whole state differs only in `objects` and `object looping sounds`, which follow the emulator's speed (a10's two runs with it off differ there too).
- **Pictures**: framecrc against off differs at b30 1,020, a30 1, c10 3, a10 10 frames of ~2,990, against two runs with it off in the same batch at 1,041 / 2 / 4 / 12: inside the control on every map.
- **Memory**: the heap's lines on a10 and a30 the same with it on and off; the block stays outside the heap.
- **Gain**, the record lane in the cache model (visibility to the HUD, game time 5-110 s, the same stretch both sides): **c10 10.80 -> 10.61 ms a frame (-1.8%**; objects pass 3.31 -> 3.09), **a30 11.86 -> 11.28 (-4.9%**; 7.31 -> 6.75), **b30 17.07 -> 16.47 (-3.5%**; 7.95 -> 7.43). The model draw step falls by 0.28-0.54 ms; render_model's own step, where the key is compared and the kept records copied, grows by 0.20 / 0.57 / 0.58 (v2 0.39 / 0.76 / 0.69, with less sent).
- **Priced** at the share of the PSP's record time [E x M]: c10 0.51-0.53 ms a frame (gate 0.65: under), a30 0.88-1.20 (gate 0.63-1.18: over), b30 1.12 at the whole map's record time (gate 1.17: at it; beach 0.85, flight 1.34).

Over the gate on a30 and at it on b30, so it goes to the PSP: a c10 and a30 pair from one build, off and `rso`, settles how long. c10's miss is the resting: an object whose inputs changed twice running rests for 64 frames, and on c10 that is most of them [U: which input moves is not yet counted].

## v4: the reflection record, camera-faded reflections, room made when full

Base: w140 13a0194 merged in. On w140 every walk wrote the new reflection record, which v3 refused as "other records", so v3 kept nothing there.

- **The reflection record kept** with the model's own records (v4a). On b30 that sent 66 of 93 offered a frame again; a30 and c10 still sent none, because most of their reflections are faded by the camera's distance, and v4a counted those as inputs that change.
- **A camera-faded reflection kept, its fade worked out again before a send** (`psp_rso_fade`, `rso_fades_same`). While a walk is kept, each fade's cutoff, falloff and fraction go into the entry (up to 8). Before a send, the fraction is worked out again at the model's distance along the view now, by the same function on the same globals, and every fraction must be equal bit for bit. Any difference means the object is walked, counted as "a reflection's fade". So a send carries a fade only when the walk would have written that same fade.
- **The key trimmed to what the records are made from.** The effect record writes intensity only for camouflage, its only reader. The key leaves out the effect, function values, shadow fields and the distant lights past the count.
- **Room made only when an entry doesn't fit** (`rso_make_room`, at most once a frame). It drops entries unused for 2 frames and ones kept but never sent, and keeps the ones that were sent (`RSO_SENT`). Slots are no longer cleared mid-probe.

## Proof and gain, v4b [E]

The v4b batch (22 of 22 played; cache pairs off vs `rso` on b30, a30, c10 to 120 s; off, off2, `rso`, `rsocheck` with statecrc and framecrc on b30, a30, c10, a10 to tick 6,000; `nomouth fixedticks fixedseed` on every run):
- **Records sent again** a frame: **b30 71 of 93** offered (no room 17), **a30 65 of 134** (see-through 25, resting 43), **c10 28 of 40** (no room 12). Against v4a: 66 / 0 / 0. Inputs changed is now at most 1.3 a frame anywhere (node matrices).
- **`rsocheck` 0 differ** on b30 (289k walks over two windows), a30 (251k) and c10 (79k). On a10, 1 differs in each of 2,016 and 3,569: a part-draw record's index-buffer address. The check compares before `rso_send`'s own test, which refuses an entry whose buffers have moved ("geometry moved"), so that record is never sent [D].
- **Play and both random seeds the same at all 6,000 ticks** on all four maps, `rso` and `rsocheck` against off.
- **Pictures**: framecrc against off differs at b30 1,467, a30 2, c10 2, a10 23 frames of ~2,990. Two runs with it off in the same batch differ at 1,486 / 1 / 3 / 19. That is inside the control on b30 and c10, and on a30 and a10 within a few frames of it.
- **Memory**: a10's heap free low is 220 KB both ways (largest block 200 vs 201 KB); a30's heap lines are the same. a10 counts 6 failed heap requests against 5 with it off [U: which request].
- **Gain**, the record lane in the cache model (visibility to the HUD, game time 5-110 s): **b30 17.36 -> 16.77 ms a frame (-3.4%)**, **a30 12.18 -> 11.63 (-4.5%)**, **c10 10.98 -> 10.56 (-3.8%)**. The model draw step falls by 0.48-0.76 ms. render_model's own step grows by 0.66 / 0.53 / 0.31 ms.
- **Priced** at the share of the PSP's record time [E x M]: **b30 ~1.09 ms a frame (gate 1.17: under)**, a30 0.81-1.10 (gate 0.63-1.18: over its low end), **c10 ~1.07 (gate 0.65: over)**.

**Not merged:** b30 sends more than v3 but falls short of its gate. Where the rest goes [U, from the counts]: on b30 and c10, room is made in nearly every frame (578 and 600 of 600). Each time, the whole block is moved down (up to 47 KB on b30), and still 12-17 offered objects a frame find no room. c10's block is 24 KB, because RSO_LEAVES_FREE halves it there. **v4c:** make room only when the entries it would drop free enough for the new one, so a full block of sent entries is left as it is, not moved every frame. Give c10 the room its 12 no-room objects need (~10 KB). Then look at the per-sent cost in render_model's own step: the key compare and the fade recompute, now 0.3-0.7 ms.
