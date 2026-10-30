# SKINPACK: each skin part's room asked for before its scratch

**What it changes [D].** `model_convert` used to ask for `node0` (3 bytes a vertex) and `skin_plan` for the triangle list (6 bytes a triangle) and `batch_of` (1 a triangle), and only then for the part, which stays for the map. Freeing the scratch left a hole under every part made in play: 70-95% of the heap's free space at its peak (the HEAPFRAG census, `docs/heapfrag.md` on the census branch).

Now `model_convert` first asks for the part's room, at a size no plan of these indices can pass: header + 64 batches + 6 bytes a triangle. The scratch follows, and the part is built in the room. Once the scratch is freed, the room is shrunk to the plan's bytes (`skin_shrink`: newlib shrinks in place, and the part's own pointers follow it should it ever move). The room is asked for through `psp_heap_try_memalign`, so a refusal isn't a heap failure: it leaves that conversion in the old order. The part's bytes are the same either way.

**Keep words:** on by default; `skinpackoff` gives the old order. `skinpackcheck` also makes each part the old way and compares the two field by field, every index included, counted on a `GE: skinpack:` line every 300 frames.

**Proof [E]** (emulator, default build, no PGO, bake w148s, fat=1):
- Play and both seeds are identical at all 3,600 ticks on b30, c10, a30, d20 and a10, on against `skinpackoff`, and at all 5,400 on a30.
- `skinpackcheck`: 142 / 78 / 89 / 145 / 22 parts compared, 0 differed.
- The front door (menu into a10) is fine.
- Strict: c10 shows 0 sites.
- 0 failed requests and 0 rooms refused on every map.

**The heap (metrics heap ends, off -> on, KB):**

| Map | Largest free block at its lowest | Free low |
|---|---|---|
| b30 | 201 -> 251 | unchanged |
| c10 | 232 -> 294 | unchanged |
| a30 | 114 -> 160 | unchanged |
| d20 | 268 -> 306 | unchanged |
| a10 | 262 -> 333 | unchanged |

The same bytes are in use, in fewer holes: +38-71 KB of contiguous room.

**Risk:** low.
- The change touches only the order of the conversion's requests and a shrink.
- Conversions run on the main CPU, as before.
- Each conversion briefly needs up to about 2.8 KB more (the unused batches and degenerate triangles) until the shrink.
