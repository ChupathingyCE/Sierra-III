# Review: perf-ge (the GE feed: two exact speed-ups, and bindblocks brought over)

Branch `perf-ge`, off `w140`. The question behind it: how the port uses the GE, and how to get more from it. The audit is `docs/ge_audit.md`; this walk-through covers the code. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference.

Every change has a keep word. Two are on by default (exact: the same play and the same GE work); bindblocks stays opt-in until its PSP A/B.

| change | default | keep word (turns it off or on) | proof |
|---|---|---|---|
| GL1, the geometry cache's lookup by a sorted index | on | `geoscan` (the old scan), `geoindexcheck` (both, compared) | pending: see "Evidence" |
| GL2, the Xbox vertex-shader constants left unmade | on | `vshconstants` (made and stored as before) | pending |
| BB, bindblocks (a texture's bind as a CALLed block) | off | `bindblocks` | exact on the emulator before (draw hash, statecrc); PSP A/B pending |

## GL1: `resident_entry` answers misses by halving, not scanning (`psp/src/psp_geo.c`)

What it was: every pass of every model part asks the geometry cache "which resident entry holds this address?" A 64-slot table answers the repeats; a slot miss scanned all ~350 entries (the 12 KB `entries` table), the second most-missed data in b30's drawing phase in the cache model, ~2.4% of that phase [E].

What it is now:
- `by_place_build()` keeps a 2 KB snapshot of keys, each `(block start in 64-byte units) << 9 | entry index`, sorted (Shell sort, no calls, no heap). The search reads no entry until its answer.
- The snapshot is rebuilt only when `place_stamp()` (generation counter, evictions, entry count) moves, and at most once a frame.
- An answer is believed only after `entry_holds()` confirms it. A stale snapshot can therefore only send a question on to the scan, never return a wrong entry: the answers are the scan's.
- Main CPU only; the Media Engine's questions keep the scan (the snapshot isn't shared with it).
- The index is off when the arena is too large for the key's 23 bits (more than 4 MB; the arena is 1-2 MB, `geo2` the largest).
- The 300-frame geometry report gains a line: answers by the slot table, by the index, by the scan; how often the index was made; and, under `geoindexcheck`, how many answers were compared and how many differed (must be 0).

Cost: a sort of ≤512 keys when the resident set changes (loads and evictions are rare after a level's first seconds), against ~350 entry reads per slot miss.

## GL2: the vertex-shader constants left unmade (`psp_fixups.py` FIXES_GEFEED, `psp_ge.c`)

On the PSP, `d3d8_gl.c` is the null renderer: it never makes a GL device, so the constants `D3DDevice_SetVertexShaderConstant` stores are read by nothing [D]. The GE draws models from the engine's node matrices and lighting (`psp_ge_model_begin`). Yet each model draw still:
- wrote its node matrices out as shader constants (`rasterizer_set_model_skinning`: `vsh_constants__nodematrices` is read only by its own SetVertexShaderConstant call [D]);
- built its lighting constants (`rasterizer_set_model_lighting`: a local struct, then SetVertexShaderConstant [D]);
- stored both.

FIXES_GEFEED puts an early return, under `psp_vsh_constants_skip()`, at the top of SetVertexShaderConstant and before the work in the two `rasterizer_set_model_*` functions (after their asserts). ~1.5-2% of b30's drawing phase in the cache model [E]. The one other thing skipped is the debug statistics' lighting-constant byte count (statistics mode only).

## BB: bindblocks (brought over from the renderer branch)

The same commits as there (the bind words written once into a block per resident texture, 192 slots, CALLed instead of a dozen pspgu calls). Two comments lost the lab's name and a run number (the public export). Off by default until the PSP A/B says what it saves.

## Evidence

Batch `pge1` (emulator; results in the lab's findings): statecrc pairs old (`geoscan vshconstants`) vs new on d20 (with the draw hash), c20, a50; `geoindexcheck` on b30's route; a10 under the strict interpreter; the cache model on b30's route to 90 s, old vs new. Results: pending.

PSP A/B (to queue after pass 1 and the ME's stage 0): `keepsets="- | geoscan vshconstants | bindblocks | geserial" repeat=2` on b30's route. `geserial` bounds the bus tax that any "fewer GE bytes" lever could recover. Results: pending.

## Builds

Both build: the campaign (program 4.87 MB, heap 5,896 KB) and the multiplayer build (`PSP_MULTIPLAYER=1`; 5.94 MB, heap 5,164 KB). The public export: 0 findings.
