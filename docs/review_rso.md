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
