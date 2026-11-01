# The drawing rebuild: the main CPU's model walk and the ME's replay (design, 2026-11-01)

Sheepdog01, 2026-11-01 06:51-06:55: rebuild the ME's work and the main CPU's drawing path where that makes them efficient. This is the census and the shape; each stage is built behind a keep word, proven exact in the emulator, and priced on the PSP (cache work).

## What the census says (lab cachecensus1 41308914906, b30's b113 route to 90 s, cache model [E])

**The ME's replay** (the frame end, simulated):
- instruction misses 96.6 M against data misses 45.1 M: its code is the problem first;
- half its fetches come from 5 KB of code, nine tenths from **29 KB**: the hot path is about twice the 16 KB I-cache;
- the per-part glue misses most: `psp_ge_draw_model_body` 16.1 M misses on 637 M fetches (15.7 KB of code), `psp_tex_bind` 6.3 M on 231 M, `model_lighting` 5.4 M on 101 M, `defer_replay` 4.2 M, `resident_entry` 2.8 M, `entry_holds_for` 1.9 M, `halo_matrix` 1.8 M. Together ~43% of the replay's instruction misses come from code that runs once a model part;
- the loops are fine: `model_geometry` misses 5.0 M on 2,263 M fetches.

**The main CPU's drawing:**
- nine tenths of its fetches from **45 KB** of code;
- the model path leads: `render_model` 6.3 M instruction misses, `rasterizer_model_draw` 5.4 M, then a chain of small per-part functions at ~1.3 M each (`psp_geo_lock`, `_rasterizer_model_begin`, `psp_ge_model_multipurpose`, `psp_model_draw`, `psp_ge_model_begin`, `psp_ge_model_reflection`, `psp_ge_model_detail`, `resident_entry`);
- its data misses: the level BSP 9.3%, the model tags 8.1%, the stack 8.0%, objects 7.0%.

The two halves have the same shape: for every model part, a long chain of different functions runs once, so each part re-fetches the chain from main RAM. Between two parts the chain is evicted by the part's own data and the next object's.

## The shape: stage-major instead of part-major

Today, part by part: part A's bind, lighting, geometry, draw; then part B's.

Rebuilt, stage by stage over a run of parts: every part's bind words, then every part's lighting, then every part's geometry, then the draws. Each stage's code is fetched once a run and stays in the cache for the whole run. Its inputs are read in order from one compact array per stage.

**The GE stream stays the same.** Each stage writes its words into the part's own slot of a per-run buffer; the draw stage then emits each part's words in the original order. The emulator's GE stream compare (`gestream`) and the pictures prove it.

## Stages

1. **Measure each per-part function's hot set.** The cache map's hot lines (`-g1` builds) show how much of each function the common part runs. A function whose hot lines are a fraction of its size gets a hot/cold split first (rare cases into `noinline, cold` helpers): the cheapest gain, and it shrinks what stage 2 has to move.
2. **The ME's replay, stage-major per record run.** Bind and state words, then lighting, then geometry, then emit. Keep word `replaypart` gives today's order.
3. **The main CPU's model walk, stage-major.** The same for `render_model` → `rasterizer_model_draw` → `psp_ge_model_*`, over the frame's visible objects: LOD and node matrices for all, then the parts' records. Keep word `drawpart`.
4. **Data second.** The replay's data misses are led by the level replay (14.8%), `model_geometry` (11.2%) and the model body (10.6%). Once the code fits, the per-stage arrays are laid out for the stage that reads them.

## What could go wrong

- **The order of side effects.** Some per-part functions may change state a later part reads: texture residency, the state memo, the geometry cache's locks. The memo (MEMEMO) already proves the state can be tracked per run. Each stage keeps a part's side effects in its own slot, and the emit stage applies them in order.
- **Memory.** A run's per-stage buffers. The ME's eDRAM or the record buffer is the place; sized in stage 1.
- **The gain is in misses removed, not instructions.** Priced on the PSP (the lab's cache rule), never in the cache model.

## Labels

Census figures [E]; the stage gains are unsized [U] until stage 1.
