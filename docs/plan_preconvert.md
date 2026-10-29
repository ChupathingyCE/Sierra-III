# PRECONVERT: model vertex buffers baked in the GE's form

Lever PRECONVERT (the lab's levers.tsv; agreed 2026-10-25). Why: every model or level of detail first drawn is converted on the main CPU (b30 ~170 buffers a minute); the Media Engine's replay left such a draw out (B8), and modelpretouch, which converts as the draw is written, costs 0.9 ms a flight frame on the PSP-1000 and must wait for the ME.

## As built (branch preconv)

The plan below was the starting point; what was built differs where noted.

- **The bake** (`--preconvert`, opt-in; `tools/modelconvert.py`, run last for each map but ui): every model vertex buffer an opaque draw converts is converted here with model_convert's own arithmetic in float32 in the C's order (unpack_normal, pack_normal, vertex_nodes, triangle_list, skin_plan's colouring and batching, the records, batch_sphere, near_clusters_make). One block a buffer, appended past the pack's data: a 64-byte header (`struct pcv_header`: what the draw must pass, the raw buffer's address, the plan's `struct skin_part`), the records at +64, then a skinned buffer's batches, triangle lists and runs' spheres. The raw bytes stay where they were. [D]
- **Found without a search** (instead of the plan's hash): each model part's `vertex_buffer.pad` word in the map, unused by the engine, holds its buffer's table index + 1; the table on the heap is one block address a buffer (~3 KB a campaign map). A block whose header names another raw buffer (a map and pack from different bakes) falls back to the raw buffer. [D]
- **Marked by header word 15** (`'PCV1'`), not a format bit: word 10 is left alone, so an older build sees an ordinary pack (it reads to word 3 only) and its tag-layout check stays quiet. A build that meets another version ignores the region and converts at run time. [D]
- **The runtime**: `psp_vertex_lock` hands an opaque draw (and rso's re-lock) the baked block; the see-through queue locks the raw buffer (`psp_vertex_lock_raw`) and converts its own rigid records as before. A skinned block's offsets become addresses as the block is read (`psp_ge_pcv_relocate`, before the read's write-back), checked to lie inside it. `model_convert` answers a baked buffer at once on either CPU: no conversion, no heap plan, nothing for the ME's work to leave out, and `model_converted_now` tells modelpretouch there is nothing to do. [D]
- **Off**: `preconvertoff` (the A/B's other side, one bake, one build), `model14` (its own records), and the main menu (its arena is sized to its pack) convert at run time. **Check**: `preconvertcheck` converts each block's raw buffer at run time once a load and compares records, plans and sphere bits. [D]
- **What stays at run time**: see-through draws (b30 41 conversions in 3,600 ticks, c10 18), and the few buffers two parts draw with different triangles (0-2 a map). The plan's v2 rigid twin would take the first. [E]

### Measured

- Python against the C, on the host: 1,900 synthetic buffers (strips and lists, degenerate joins, ±0, failing plans), records, plans, spheres and runs bit for bit. [E]
- The bake (every map): 290-653 buffers a map baked, 0 plans failed; the pack grows by 2.5-5.0 MB a campaign map (b30 11.7 -> 16.5 MB, c10 17.5 -> 21.8 MB; about 42 MB over the ten), the baked blocks 5-10% larger than the raw vertices (a skinned block carries its triangle lists). [E]
- b30 route (3,600 ticks) and c10 hands off (7,200), on vs preconvertoff: play and both seeds identical every tick; preconvertcheck 0 differ (b30 225 blocks, c10 136); pictures differ from the off side as much as two off runs differ from each other (b30 692 / 689 of 1,798 frames, c10 6 / 6 of 3,599); strict run 0 sites; the front door (menu, then a10) clean. [E]
- Run-time conversions: b30 235 -> 41, c10 158 -> 18 (what is left is see-through); modelpretouch 194 / 140 -> 0. Geometry misses unchanged (513, 346), bytes read +5%, evictions +5 / +4; heap at the end -228 / -181 KB (no plans). [E]
- The frame's price is a PSP pair's to give (the lab has the spec).

## The plan (as drafted, against w142 609a0de)

Open before building: the pack grows by about the raw model vertices (2-3 MB a map on the stick), the geometry arena takes the skinned parts' batch lists (more eviction, unmeasured), and see-through parts stay converted at run time unless the v2 rigid twin is added.


I read the code and the bake scripts only. Nothing was built or run. Labels: [D] read in code or data, [U] my inference. Paths are relative to the Sierra-III checkout (609a0de).

## Findings

| # | Finding | Label |
|---|---|---|
| 1 | The pack holds the raw Xbox vertices (32 bytes, or 68 for uncompressed). `model_convert` rewrites them in place at first draw. Nothing in the bake produces GE records today. | [D] |
| 2 | The conversion needs only the vertex bytes, the part's index buffer, `strip` and `count`. It does not use the node-matrix count, which appears only in the draw-body guard. A bake can therefore reproduce it. | [D] |
| 3 | Whether a part is skinned or rigid depends on the caller, not the data. `skin == NULL` (the see-through queue) means rigid, and the first draw to convert a buffer decides for good. | [D] |
| 4 | The engine routes an ordinary model-shader part through the see-through queue when a model effect or active camouflage is on. The same buffer can be drawn both ways, so classifying parts by shader at bake time is not safe. | [D] |
| 5 | The bake already has a post-pass pattern, `levelslim.py`, that rewrites the pack and the map's pointers. The pack header has room for new words. | [D] |
| 6 | The geometry cache has two load-completion sites, and both already write back and invalidate the block's cache lines. A relocation hook fits there. | [D] |
| 7 | Recommended design: append baked blocks to the end of the `.pgeo`, keep the raw bytes, and look up the baked block by the raw address at lock time. Anything unusual falls back to the raw path, so `preconvertoff` and old runtimes both keep working. | [U] |
| 8 | Conversion work stays at run time for see-through draws and for any anomaly. The count goes to 0 only for opaque parts, unless the v2 rigid twin is added. | [U] |

I could not verify the 0.9 ms a flight frame. I did not find that figure in the files I read, and I did not look for it in the results.

## Evidence for the findings

**The current conversion**
- `psp/src/psp_ge.c:11867`: `static int model_convert(void *vertices, int count, int uncompressed, const unsigned short *indices, int index_count,`
- It writes in place: `psp/src/psp_ge.c:12057`: `unsigned char *record = (unsigned char *)vertices + 24 * index;`
- Skinned records use the plan's stride (`psp/src/psp_ge.c:12036`). The stride is `psp/src/psp_ge.c:11306`: `part->stride = (slots <= 4 ? 4 : 8) + 8 + 4 + 12;`, so 28 or 32 bytes against the raw 32.
- The 68-byte path packs down in place (`psp/src/psp_ge.c:12000`). The sky uses it.
- The skin plan reads the vertices' node and weight bytes (`vertex_nodes`, `psp/src/psp_ge.c:11079`) and the triangle list built from `indices`, `index_count` and `strip` (`triangle_list`, `psp/src/psp_ge.c:11114`). [D] It reads no node-matrix count.
- The plan can fail. More than 8 mutually-connected nodes gives `skin_failures++; return NULL;` (`psp/src/psp_ge.c:11228`). More than 64 batches gives the same (`psp/src/psp_ge.c:11271`). The part then stays rigid.
- The plan and its tables live on the heap (`psp/src/psp_ge.c:11293`: `part = malloc(bytes);`). `converted[]` and `skinned[]` are 2,048-slot tables, and `converted_sweep` frees dead plans. The comment at `psp/src/psp_ge.c:11346` says "the plans made reached 1,160 KB by game time 20,000" in b108 run 1. [D]

**Caller-dependent behaviour**
- The see-through queue calls `model_convert(vertices, vertex_count, uncompressed, NULL, 0, 0, NULL)` (`psp/src/psp_ge.c:16019`), so it always converts rigid.
- `psp/src/psp_ge.c:11918`: a buffer already in the table with the same generation returns the stored result (`*skin = skinned[slot]`). [U] A part first drawn camouflaged therefore stays rigid until its block is reloaded.
- `engine/source/rasterizer/xbox/rasterizer_xbox_models.c:1683` submits a part to `_rasterizer_model_transparent_geometry_submit` when `local_parameters->effect.modifier_shader` is set. Line 1717 does the same under `_render_model_effect_type_active_camouflage`.

**How the GE gets model vertices today**
- `psp_vertex_lock` locks `count * rasterizer_geometry_get_vertex_size(type)` (`psp/src/psp_geo.c:1443`).
- `psp_model_draw` and `psp_ge_transparent` both call it (`psp_fixups.py:2167`, `psp_fixups.py:2196`).
- `psp_model_draw` passes `strip ? triangle_count + 2 : 3 * triangle_count` as `index_count` (`psp_fixups.py:2205`). [D]

**The pack and the bake**
- The header check is `header[0] != 0x4F454750 /* PGEO */ || header[1] != 1` (`psp/src/psp_geo.c:1204`). Header words 3, 4–6, 8–9 and 10 are used (`psp/src/psp_geo.c:1214`, `psp/src/psp_geo.c:1217`, `psp/src/psp_geo.c:1332`, `psp/src/psp_geo.c:1344`). [D] Words 11–15 are unread in `psp_geo.c`. [U] I did not grep `psp_anim.c`.
- The pack is written by `invader-build -G` (`bake/bake.py:362`). The upstream fork patch is `upstream/invader/psp-fork.patch`.
- `bake/bake.py:373-376` runs `levelslim.py <map> <pgeo>`, which rewrites both files.
- `levelslim.py:16-18` describes the model entries: "the models' vertices and indices, one struct each, reached through the tag data header's per-part arrays (12 bytes a part, the pointer at 4)".
- `levelslim.py:51`: `ENTRY = 12  # ...{u32, pointer, u32}`
- `levelslim.py:157-159` refuses when model data lies after the first level block.
- `levelslim.py:129-131, 229` rebuilds the pack from `data[HEADER:HEADER+size]`, which drops anything appended earlier. A new pass must therefore run after it.
- `docs/asset_baking.md:135` planned a 14-byte model vertex ("14 B: s16 uv, s8 normal, s16 pos"). That was never built; the port uses the 24/28/32-byte records. [D]

**Load completion in the geometry cache**
- Async completion: `psp/src/psp_geo.c:483-486` is `sceKernelDcacheWritebackInvalidateRange((void *)e->at, e->size); e->state = 3; e->generation = ++generation_counter;`.
- Sync read: `psp/src/psp_geo.c:621-624`, the same sequence.
- A model block's address can be reused after eviction. The header says so: "psp_geo_generation(), which changes with every load into the block" (`psp/src/psp_geo.c:23-26`).

**The b30 conversion rate** (`run-results/sierra-hw/40212908907/run3/log.txt`)
- Line 234: `196 model parts (28 buffers converted, 0 again`.
- Line 879: `129 model parts (150 buffers converted, 0 again`.
- Line 1732: `291 model parts (171 buffers converted, 0 again`.
- `psp/src/psp_ge.c:23574` logs the pretouch count. Run 3 shows `119`, `128` and `136` buffers converted at record time (lines 901, 1312, 1754).

## (1) The design

### The idea: appended baked blocks, raw bytes kept
`tools/modelconvert.py` (new) runs after levelslim. It appends one baked block per opaque-form model vertex buffer to the end of the `.pgeo`, plus a lookup table. The raw vertices and the tag pointers stay as they are. [U] This choice has four consequences:
- **Old runtimes still work.** An old runtime reads only up to `header[3]`, so it never sees the appended region. [D: `psp_geo.c:1214`, `pack_size = header[3]`]
- **`preconvertoff` is exact.** The raw path is untouched, so one bake and one build give the A/B.
- **Camouflage and effect draws need no classification.** They keep locking the raw bytes (see "Runtime").
- **The cost is stick space.** The pack grows by about the size of the raw model vertices. Blood Gulch's are 2.63 MB at 32 bytes ([D] `docs/asset_baking.md:48`), so about 2–3 MB a map. [U] I did not check the stick's capacity.

### What the bake writes per part (a "PCV block")
- Each block is 64-byte aligned.
- **A 64-byte header comes first, then the records.** The runtime hands `vertices = block + 64` to the unchanged `model_convert` callers, so no code that takes the record base needs to change.
- **Header fields:**
  - magic and version;
  - flags (skinned, rigid, plan failed);
  - `count`, `stride`, `uncompressed`;
  - `index_count` (the value `psp_model_draw` will pass), `strip`;
  - `slots`, `batch_count`;
  - offsets to the batch table, the index lists and the cluster spheres.
- **Records:** byte-for-byte what `model_convert` writes. Rigid is 24 bytes: uv floats, three `(signed char)(n*127)` bytes, the node byte at +11, then the position (`psp_ge.c:12057-12063`). Skinned is `stride` bytes, with weights by slot first (`psp_ge.c:12036-12051`). For the 68-byte path, `pack_normal`/`unpack_normal` and the `node*3` rule at `psp_ge.c:12015` must be reproduced.
- **Batch table**, as `struct skin_batch` (`psp_ge.c:11026`) minus pointers:
  - `node[8]` (slot to node);
  - `index_count`;
  - an index-list offset;
  - sphere `center[3]` and `radius`, computed as `batch_sphere` does (`psp_ge.c:13404`) so `radius >= 0` and the draw never recomputes it;
  - cluster count and a cluster-sphere offset, computed as `near_clusters_make` does (`psp_ge.c:13534`, 16 triangles a run, `psp_ge.c:2867`).
- **Index lists:** the batch triangle lists as u16, the same output as `skin_plan` (`psp_ge.c:11318-11324`).
- **Slot assignment:** the bake can port `skin_plan`'s greedy colouring to Python and run it unchanged. The draw path reads only `node[]`, `weights` and `stride`, so the slot table need not be stored. [U]
- **Failed plan:** the bake writes a rigid block with a flag, which is what run time does today.

**Where it goes.** Header words 11–14 take the baked region's start and size, the table's offset and entry count, and a PCV version. `header[10]` gets bit `0x20`. `header[3]` and `header[1]` stay as they are. The table's entries are `{raw address, baked address, size}`, sorted. The runtime loads it into an open-addressed hash on the heap.

### Bake changes
- **New `tools/modelconvert.py`**, modelled on `levelslim.py`:
  - `read_map` already gives the vertex entries (`m.vb_ptr`, `m.vb_count`) and the index entries (`m.ib_ptr`, `m.ib_count`) (`levelslim.py:80-85`).
  - The pass pairs each vertex buffer with its index buffer, reading the part's type and counts from the tag.
  - It refuses and leaves both files untouched on anything it cannot place, as levelslim does (`levelslim.py:28-33`).
  - It is idempotent by header bit `0x20`, as levelslim is (`levelslim.py:242`).
- **`bake/bake.py`, after line 376:** `run([sys.executable, os.path.join(LIB, 'modelconvert.py'), final, pgeo], ...)` and an `echo_from(name, log_path, start, ('modelconvert:',))`. This must come after levelslim, because levelslim drops anything appended. [D `levelslim.py:129-131`] It runs before the track and stencil steps, which only read the level blocks. [U]
- **Bit-exactness:** use numpy float32 in the same operation order as the C. The batch-sphere sum is `dx*dx + dy*dy + dz*dz`. PSP MIPS has no FMA. [U] `pack_normal` and `unpack_normal` must be ported literally. The runtime check below proves it.

### Loader and runtime changes
1. **`psp/src/psp_geo.c`, `psp_geo_map_opened` (~line 1217).** Read words 11–14. When bit `0x20` is set, the PCV version matches and `preconvertoff` is absent, enable the sidecar and read the table. Extend `pack_size` for the lock bound (`psp_geo_lock`, `psp_geo.c:766`). Log `geometry: preconvert: N parts, M KB`. Without the bit, or with the word, nothing changes.
2. **`psp_vertex_lock` (`psp_geo.c:1433`).** On an external address, probe the hash for a baked block. If found, `psp_geo_lock(baked_address, baked_size)` and return the result plus 64. The cost is one probe a draw, like `find()`. [U] Add `psp_vertex_lock_raw` for the see-through path.
3. **Load completion (`psp_geo.c:483` and `psp_geo.c:621`).** A `geo_entry` flag, `pad[0]`, marks a PCV block. On completion, call `pcv_relocate(e)` before the existing writeback-invalidate. It writes the absolute `batches` and `indices` pointers and `clusters` into the block's own `skin_part` and `skin_batch` structs, at most 64 batches. The existing `sceKernelDcacheWritebackInvalidateRange` right after covers the ME's reads. [D]
4. **`model_convert` (`psp_ge.c:11867`), new first branch.**
   - Ask `psp_geo_baked(vertices)` (the resident entry's flag via `resident_entry`, which the ME already uses; `psp_geo.c:951`).
   - If baked and the part is rigid, return 1 with `*skin = NULL`.
   - If baked and skinned and `skin != NULL`, set `*skin` to the block's `skin_part`. No table entry, no malloc, no free: the plan lives and dies with the block.
   - If baked and skinned but `skin == NULL`, return 0 and count it. See "Runtime" for why this should not happen.
   - Call `vm_note` once per load, gated by a per-entry flag, since `vm_redirect` compares generations (`psp_ge.c:11703`).
5. **`model_converted_now` (`psp_ge.c:14805`)** returns 1 for baked, so modelpretouch does nothing for them.
6. **`psp_model_draw` (`psp_fixups.py:2178`).** After the lock, compare the header's `index_count` and `strip` with the draw's. On a mismatch, lock the raw buffer instead. The raw path is exactly today's.
7. **`psp_ge_transparent` (`psp_fixups.py:2147`)** locks raw: `psp_vertex_lock_raw`. This keeps today's rigid conversion for camouflage, effects and see-through, whatever the bake's guess.
8. **Counters.** Add `preconverted N, fallbacks M` to the `psp_ge.c:23479` line, next to `buffers converted`.

**ME replay.** Baked parts are ready the moment their block is resident. The ME replay already skips parts whose block is not resident; this is the same as geometry still on its way. The B8 case (a draw whose buffer is not converted) no longer arises for baked parts. [U]

**Keep word `preconvertoff`.** Read in `psp_geo_map_opened`. When present, the runtime ignores the sidecar and every path is today's. It needs no second bake.

### Memory cost
- **Vertex records:** rigid parts shrink from 32 to 24 bytes (−25%). Skinned parts go from 32 to 28 or 32. The 68-byte sky parts shrink the most.
- **Skinned parts add** their batch index lists (6 bytes a triangle), spheres and clusters. The cluster table is 16 bytes a 16-triangle run, so about 1 byte a triangle.
- **Where it moves to:** the batch lists and spheres move from the heap into the 1 MB geometry arena (`psp_geo.c:54-56`). [U] Bigger skinned blocks mean more eviction in the arena, which is the contended resource (the Library walk loaded 1,899 blocks at 1 MB, `psp_geo.c:115-117`). The heap gains what the arena loses; the b108 plans peaked at about 1.1 MB (`psp_ge.c:11346`).
- **Unmeasured:** I have no rigid/skinned split of the bytes. CAN'T TELL whether the arena's miss rate rises. It needs a `geo` miss count in the A/B.
- **Skinned parts still lock the original strip index buffer** in v1. The skinned draw does not read it; it is locked and drawn rigid only when the plan failed. That is wasted stick traffic. Skipping it for baked skinned parts is a v2 change and needs a non-NULL dummy for the guard at `psp_ge.c:14429`. [U]

## (2) Risks and what stays at run time

- **Still converted at run time, by design:**
  - see-through, camouflage and effect draws (item 7);
  - maps with no pack;
  - maps built without a PCV region;
  - a draw whose `index_count` or `strip` differs from the bake's (item 6);
  - a baked skinned block asked for rigid (item 4).
- **Not reaching 0 on b30.** Run 3 converted 171 buffers by frame 1732. Some of those are see-through parts (the ocean, glass), which this design leaves to run time. The conversion count falls by the number of opaque buffers, not to 0. [U] A v2 twin fixes this: the bake also writes a rigid block for parts whose shader class is `shader_transparent_*`, and the see-through lock takes the twin. A wrong guess is then only a missed saving, because the raw fallback remains. To go to exactly 0, add the twin.
- **Behaviour change (an improvement):** today a part first drawn camouflaged stays rigid (`psp_ge.c:11918`). Baked, the opaque draw is always skinned. The run-time A/B can differ from the bake on exactly those parts. [U] The proof below separates them.
- **Float parity.** The bake's `batch_sphere` and cluster values must match the C. A one-ulp difference changes a near-eye cut decision, not the bytes. The check word catches it.
- **Cache coherency.** The relocation writes into the block, and `sceKernelDcacheWritebackInvalidateRange` follows (`psp_geo.c:483`). The GE reads records the CPU never wrote, so no writeback is owed beyond that. [U] Hardware only.
- **Entry table pressure.** `GEO_ENTRIES` is 512 (`psp_geo.c:51`). A part now has a raw and a baked entry when both are drawn. This is rare (see-through twin parts only).
- **Pack write-time order.** A re-run of levelslim on a PCV pack would drop the region. `levelslim.py` should refuse when bit `0x20` is set. [U]
- **Not decided here:** whether the batch spheres should also be baked for rigid parts. The `rigid_runs` table (`psp_ge.c:14319`) is separate, and its spheres are built at run time. Out of scope for v1.

## (3) The proof

**Exactness**
1. **`preconvertcheck` (new keep word, debug).** On a baked block's first draw, convert the raw copy into a scratch buffer with the run-time code, then `memcmp` the records, the batch table and the sphere values. Log `precheck: N parts, M differ`. [U] This tests the bake's arithmetic directly. Run it on every campaign map, emulator only (`tools/emu.sh`, the lab's sierra-batch).
2. **`statecrc` against `preconvertoff`.** In one batch with `keepsets="- | preconvertoff"`, compare the per-tick `statecrc` (`docs/testing_pipeline.md:58`). The simulation must be identical, since rendering does not change game state. [U]
3. **Shots and drawhash.** Use fixed-input batches (the README's `nearauto nomeaudio fixedseed fixedticks statecrc nomouth`, `psp/sierra/batches/README.md:23`) and compare the shots on both sides. They should be pixel-identical except for camouflaged or effect parts. If any differ, list them: they are the item-3 difference.
4. **Plan failures.** `skin_failures` must match between baked and run-time conversion.

**The count going to 0 on b30**
- Today the pretouch count is 119–136 and `buffers converted` reaches 171 (run 3).
- Expected with PRECONVERT: `buffers converted` equals the see-through parts only, not 171. With the v2 twin it should be 0 and `preconvert fallbacks` 0. [U] Run the same b30 stretch (`nearauto assettrack geo2 hwprofile exitat60`, as run 40212908907) under both sides of one keepset.
- Also read `geo` misses and `heap` peak in the log, to cost the arena move.

**PSP frame pair**
- `sierra-hw`, one job: `keepsets="- | preconvertoff" repeat=3`, so on and off alternate from one build. Read it with `tools/abreport.py`. Keep both sides of the A/B in one job. [D `../CLAUDE.md`]
- Target figure: the per-flight-frame model cost. The comparison is against the 0.9 ms the brief quotes for modelpretouch (unverified by me), and against the `modelpretouchoff levelpretouchoff` side of run 40212908907.
- Run the emulator batches and the release-check batch first, so the PSP finds its build made.

## Order of work (for the master)
1. `modelconvert.py` plus the check word, run against the emulator batches.
2. Loader, relocation and the `model_convert` branch behind `preconvertoff`.
3. The statecrc and shots A/B.
4. The b30 count and the PSP pair.
5. The v2 rigid twin, then skipping the strip index lock for baked skinned parts.
