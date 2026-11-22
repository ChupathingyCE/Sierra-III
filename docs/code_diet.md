# The code diet: hot code that carries more than it needs

**Status:** nine words on by default in the campaign build since 2026-11-21: batch 1 and 2's three on the tick, batch 3's `tagmemotick`, and five on the Media Engine's replay (`gustate`, `bindwords`, `gudraw`, `bakedraw`, `bakedonce`), priced as a group.

The PSP-1000's instruction cache holds 16 KB, 256 lines of code, on each CPU. The tick's profile is flat: no one function is the cost, the cost is how many code lines each call touches times how many calls there are, and the same answer looked up again a few lines later [E, the lab's census of the tick by function and by call site]. The code diet goes after that: for each target, a short form of the common case, with the general path kept behind it.

Every word here is **exact**: play and both random seeds are identical at every tick with the word on and off, on b30, c20 and a30 (3,900 ticks each, the strict interpreter) [E]. Each has an off word, so one build tests both sides.

## The words

| Word (off word) | What it does | Where |
|---|---|---|
| `objlean` (`objleanold`) | `object_update`'s common path as a second function: no per-type clocks or accounts, the damage update handed the object and its definition instead of looking them up again, and the node-matrix post-process's first tests made by the caller. The general function is untouched and still serves the instruments: the short form is taken only with the timers off (`notimers`, the campaign's default), so a `timerson` run times every object through the general function and prints the bipeds', vehicles' and machines' sub-accounts as before the word. | `psp_fixups.py`, `FIXES_DIET1` |
| `verifygate` (`verifygateoff`) | The units' and bipeds' vector checks are not called at all unless `verifyvectors` asks for them. Before, each call was made and returned at once: 16 calls a biped a tick. | `psp_fixups.py`, `FIXES_DIET1`; `psp_verify_call` in `psp_platform.c` |
| `tagmemo` (`tagmemooff`) | Each of 17 typed tag getters (`object_definition_get`, `unit_definition_get`, `biped_definition_get` and the like) keeps its last answer: a load and a compare at the call site in place of a call to `tag_get`. A miss falls to `tag_get`'s own fast test and remembers the new answer. | `psp_fixups.py`, `FIXES_DIET2`; `psp_tagmemo_on` in `psp_platform.c` |

### `tagmemo` and the Media Engine

The getters also run on the Media Engine, which has its own data cache over the same memory. The rules that keep that safe:

- **Only the main CPU writes the remembered answers.** The Media Engine reads them and never stores one.
- **The answers are forgotten at every change of the tag table** (six places in `cache_files.c`), written back to main memory.
- **The Media Engine holds no stale copy** because its job runner (`me_serve` in `psp_me.c`) writes back and drops its whole data cache at the start and end of every job except the audio job taken by ranges, which asks no getter. `tag_get`'s own reads of the tag table on the Media Engine rest on the same drops.
- **Neither holds for a job running across a map change, so that is checked:** if any Media Engine job is in flight when the tag table changes, the log says `tagmemo: FAIL` once. It has not fired in any run.
- A hit compares the asked index against the remembered instance's own index, read from the live table, so a remembered pointer can never answer for a different tag.

## Proof and price

- **Exact [E]:** each word alone and all together against none, as above. The Media Engine side of `tagmemo` cannot be shown by the emulator (it has one view of memory); the PSP run below is its proof.
- **Counts [E], a check that each word did what was meant, not a price:** on c20's held stretch the three words together take about 113,000 instructions off a tick, 89,000 of them `tagmemo`'s (`tag_get`'s instructions fall by three quarters). Instruction-cache misses in the tick fall 3.5% there and are flat on b30. These miss counts move with the code layout by more than the words move them, so nothing is ranked on them.
- **Price [M], PSP-3000, c20's held stretch, the three words together, two runs a side:**

| Stretch (game ticks) | Tick | Frame by the clock | Drawing |
|---|---|---|---|
| opening (300-1069) | 21.9 to 21.2 ms, -3.0%, real | 52.5 to 51.7 ms, -1.6%, real | 12.8 to 13.1 ms, +2.1%, real |
| first (1069-3600) | -2.1%, inside the runs' spread | -0.7%, inside the spread | +1.9%, inside the spread |
| play (3600-4382) | -5.2%, inside the spread | -2.0%, inside the spread | +1.0%, inside the spread |

  The Media Engine's own times did not move, and no run stopped, crashed or logged a failure.

## What is known to be wrong with it

- **The drawing was slower with the first three words on** (+0.3 ms where it could be told). `tagmemotick` was built as the candidate fix: with it the drawing moved the right way in all three stretches of batch 3's pair, inside the runs' spread, and nothing got worse (below). The instruction counts did not show the memo as the cause [E], so the cause is not established; the rise is no longer measured.
- `objlean`'s short form and `tagmemo`'s miss function have no place in the text layout yet, so their own code lines miss more than they need to [E].

## Batch 3 (`tagmemotick`, `bakedraw`, `bakedonce`)

| Word | What it does | Where |
|---|---|---|
| `bakedraw` | A baked wall's draw is looked over once for a triangle with all three corners flagged before any triangle is looked up. Most draws hold no replaced triangle, and each of their triangles cost a lookup call that returned "none". | `bake_substitute` in `psp/src/psp_ge.c` |
| `bakedonce` | A model part's place in the geometry cache is asked once a draw, by `psp_ge_draw_model`, and handed down to `model_convert`, which asked again for the same vertices. A part that goes through the pretouch (which can move the answer) asks again. | `psp/src/psp_ge.c` |
| `tagmemotick` | `tagmemo`'s answers are remembered only by the tick. Outside it a miss goes straight to `tag_get` and stores nothing, so the drawing's getters (different tags asked in turn) no longer replace the tick's answers or pay for the miss function. A hit is a hit anywhere. On by default; off word `tagmemotickoff`. | `psp_phase` in `psp/src/psp_platform.c` |

`bakedraw` and `bakedonce` are drawing-only and exact by construction; their proof is the GE's display lists hashed list by list, as well as play and seeds.

- **Proof [E]:** play and both seeds identical every tick on b30, c20 and a30 with each set of words, and every display list of the game's frames identical by hash (the loading screen's bar is clocked by real time and is left out of the compare).
- **Price [M], PSP-3000, c20's held stretch, two runs a side, against the build's defaults:**

| Side | Tick | Drawing | The Media Engine's replay |
|---|---|---|---|
| `bakedraw bakedonce` | opening -0.1 ms, first -0.4 ms (inside the spread); play +0.9 ms (+4.7%, outside the runs' spread; a short stretch, and a drawing-only change has no way to reach the tick) | +0.1 ms, inside the spread | 34.4 against 34.6 ms, inside its 1.0 ms spread |
| with `tagmemotick` | play -1.6 ms (-7.9%), real | -0.2 / -0.4 / -0.0 ms by stretch, inside the spread | 34.4 ms |

  So `tagmemotick` went on by default, and the two Media Engine words showed no gain alone; they are on by default with batch 4's group (below). Off words: `tagmemotickoff`, `bakedrawoff`, `bakedonceoff`.

## Batch 4: the Media Engine's replay (on by default)

The replay of the frame's draws runs on the Media Engine from its code copy in eDRAM, a 256 KB room that is full. Three families of pspgu's calls are its busiest, and each word writes that family's command words itself, as pspgu's source does, so the GE's display list is the same byte for byte:

| Word | What it does | Where |
|---|---|---|
| `gustate` | The GE's one-word state switches (blend, depth test, fog and the rest) are written from a table by one 144-byte function. pspgu's `sceGuEnable` and `sceGuDisable` are a kilobyte each, a case a state, called from about 170 places. The scissor and the fragment's doubling keep pspgu's function. | `gu_state_set` in `psp/src/psp_ge.c` |
| `bindwords` | A texture bind that is sent writes its own words (palette mode and load, texture mode, an image a level, the filter) in pspgu's order, in place of 6 to 13 calls into as many small functions. No block or table is shared, so it holds on the Media Engine, where `bindblocks` does not run. | `tex_bind` in `psp/src/psp_tex.c` |
| `gudraw` | A draw's words (vertex type, addresses, primitive) and its stall-address hand-on are written by the port's `gu_draw`; the matrix check before it stays pspgum's. pspgu's draw functions are not in the Media Engine's copy, so through them every draw ran its matrix check and stall update from main RAM, beside copies of both that never ran. | `gu_draw`, `gum_draw` in `psp/src/psp_ge.c` |

- **Off words:** `gustateoff`, `bindwordsoff`, `gudrawoff`.
- **Proof [E]:** play and both seeds identical every tick and every display list of the game's frames identical by hash, each word alone and the five together, on b30, c20 and a30.
- **Price [M], PSP-3000, c20's held stretch, the five Media Engine words together against the build without them, two runs a side:** the Media Engine's render job 33.8 to 32.6 ms a job, -1.2 ms (-3.6%), both runs of each side apart from the other side's. The main CPU's frame did not move (inside the runs' spread): on c20 the Media Engine is not what the frame waits for, so the time is freed on it, not yet turned into frames. Which word carries how much was not split.
- **Room:** the copy is a full 256 KB, and a short form kept beside its old form needs room in it. With `gustate` on by default pspgu's two switch functions (2 KB) left the copy's list (`psp/layout/mexec_rbtsw.txt`); they run from main RAM for the two states the table leaves to them and under `gustateoff`. Nine functions the Media Engine does not fetch on the bench stretches are last in the list, where the build's fill cuts first when the copy outgrows its room.
- **Earlier work:** writing the renderer's words without pspgu was tried for the main CPU's drawing and stopped: fewer instruction misses there bought no time on the PSP. On the Media Engine code misses are measured to cost (about 55 cycles each, several milliseconds of the replay a frame), which is why it was tried again there, and there it pays (the price above).

## Co-op

Every word here is exact and changes nothing a host or client could see. They ship in the campaign build's defaults only, as every default word does; the multiplayer build has no default words.
