# Review: perf-ge (the GE feed: two exact speed-ups, and bindblocks brought over)

Branch `perf-ge`, off `w140`. The question behind it: how the port uses the GE, and how to get more from it. The audit is `docs/ge_audit.md`; this walk-through covers the code. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference.

Every change has a keep word. Two are on by default (exact: the same play and the same GE work); bindblocks stays opt-in until its PSP A/B.

| change | default | keep word (turns it off or on) | proof |
|---|---|---|---|
| GL1, the geometry cache's lookup by a sorted index | on | `geoscan` (the old scan), `geoindexcheck` (both, compared) | [E] 0 of 171,109 answers differed on b30's route; play and seeds identical on d20, c20, a50; strict clean; ~1.1 ms a frame in the cache model ("Evidence") |
| GL2, the Xbox vertex-shader constants left unmade | on | `vshconstants` (made and stored as before) | [E] play and seeds identical on d20, c20, a50; strict clean; ~1.0-1.25 ms a frame in the cache model ("Evidence") |
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

Emulator batch, all [E], hands off, `nearauto assettrack geo2 fixedticks fixedseed` on the b139 bake and profile; "old" is `geoscan vshconstants`, "new" is the defaults.

**Exactness.**
- **Play and both random seeds: identical at all 9,000 ticks** on d20, c20 and a50 (`tools/statecmp.py`, old against new, `statecrc`). The whole state differs only in `objects`, `contrail point` (d20), `object looping sounds`, `first person weapons` (a50) and, on c20, `lights` at the one checkpoint at tick 1,020. These follow the emulator's speed and the heap's addresses, as in earlier exactness checks (`review_streamoom.md` and others list the same set, c20's `lights` included) [U: no old-against-old pair was run to show it for this build].
- **`geoindexcheck` on b30's route (300 s, game time 8,915): 171,109 answers compared, 0 differed**, counted in the report lines as they grew (0 at every one of 14 reports). The index answered 171,109 questions the slot table could not (against 1.90 million the slot table answered itself, ~9% more); in the old runs those were the scans, ~350 entries each.
- **Pictures** (`framecrc`, `tools/framecmp.py`, old against new): 65 of 4,497 frames differ on d20, 15 of 4,499 on c20 (all before game time 1,100, the loading), 28 of 4,499 on a50 (26 before 1,100). One build run twice already differs by this much with `readswait` (d20 234 of 6,538, c20 12 of 8,310, a50 26 of 10,855, from the earlier framecrc runs), and the differing frames here are a smaller share on d20 and the same few early frames on c20 and a50. So the pictures agree to the noise of the emulator [U]. The draw hash (`drawhash`) is not in this tree, so the GE's work itself was not compared; GL1 is checked answer by answer instead, and GL2 only removes writes that nothing reads [D].
- **a10 under the strict interpreter** (`-i`, 90 s, game time 2,624): 0 distinct sites, no crash.

**Cost** (cache model, 16 KB 2-way, b30's route to 90 s, 2,700 ticks = 1,350 frames at 2 ticks a frame; cycles = instruction fetches + 71 per miss, the figure `docs/ge_audit.md` uses). The model's clock isn't a PSP's [E].

| drawing phase, per frame | old | new | saved |
|---|---|---|---|
| `resident_entry` (GL1) | 2.34 ms | 1.27 ms | **1.07 ms** (fetches -61%, D misses -68%) |
| `constants_store` (GL2) | 0.78 ms | gone | 0.78 ms |
| `rasterizer_set_model_skinning` D misses (GL2) | 0.19 ms | gone | 0.19 ms |
| `memcmp`, 0.29 ms in the old run, below the new run's top-40 list | 0.29 ms | at most 0.14 ms | 0.15-0.29 ms (probably `constants_store`'s change check) [U] |
| the two together | | | **~2.0-2.3 ms** |

The drawing phase's whole count falls 5.1% in fetches and 10.7% in D misses; the tick phase moves -2.3% in accesses from the game's collision and sound-mix work, which a drawing change doesn't touch, so the whole-phase figure is not used. The budget lines in the same runs (windows not aligned to the same game time, so rough): the frame's CPU 32.4 -> 30.6 ms averaged over game time 300-2,700 (-1.8 ms), the GE renderer's part of it 18.3 -> 17.4 ms (-0.85 ms; -1.2 ms from 1,800 on, the heavy stretch). The two readings bracket the saving: about 1-2 ms a frame on b30 for GL1 and GL2 together, to be read against the PSP A/B.

`resident_entry` keeps 1.27 ms a frame: the last-locked and slot-table paths and the snapshot's rebuilds, not the scan. Counting calls by path would size what is left [U].

PSP A/B (`keepsets="- | geoscan vshconstants | bindblocks | geserial" repeat=2` on b30's route): results pending.

## Builds

Both build: the campaign (program 4.87 MB, heap 5,896 KB) and the multiplayer build (`PSP_MULTIPLAYER=1`; 5.94 MB, heap 5,164 KB). The public export: 0 findings.
