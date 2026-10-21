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
