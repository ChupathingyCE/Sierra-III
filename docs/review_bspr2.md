# Review: BSPR v2, a biped's last collision answers kept in its own datum (`bspsame`, `bspsamecheck`)

A standing biped, and a resting body, asks the collision BSPs the same question tick after tick: its level sphere at the same centre and radius, the same object's BSP in the object's own space, the same short rays from its nodes. A walk's answer depends only on its arguments and on tag data, so a question asked again, bit for bit, can be answered from the last walk. Each biped keeps its last 4 sphere and 2 ray answers in 640 bytes after its own datum; a match returns the kept answer and skips the walk.

The first version kept them in one global table (128 sphere and 64 ray slots). It was exact, but its 35 KB table thrashed the 16 KB D-cache: half the saving went in lookups, and it cost 40 KB of heap with the word on or off. This version has no table and no heap: the answers live where the biped's own update already has its data in the cache, and with the word off the datum is the engine's own size.

## Why a kept answer is the walk's answer [D]

- **The BSP and the map:** every key carries a generation. A map's load and every change of BSP move it (`psp_bspr_forget`, beside the sphere memo's own clear), so no answer from before matches again.
- **The question:** a sphere's key is 7 words (the BSP, the centre's and radius's bits, the generation, the breakable-surface count); a ray's is 11 (the BSP, its test flags, the start and vector, the maximum t, the generation and the count). Compared bit for bit: no tolerance.
- **The breakable surfaces:** the walks read their flags. The generation also moves whenever the flags read differ from the last seen (up to 8 words; more and the question isn't kept). The flags array is word-aligned on the PSP (the sphere memo already reads it as words).
- **Objects nearby:** objects moving into the sphere change what the caller does with the leaves (the clusters' object lists are walked after this, every time), not the BSP's answer.
- **What is not kept:** the early-out walk (`psp_sphere_any`) stops at its first find, so it is neither asked nor kept. Answers longer than the slot (36 indices for a sphere, 16 leaves for a ray, or an index over 16 bits) aren't kept. Questions are only asked inside `biped_update`, on the main CPU, for the biped being updated.

## Key lines

`psp_fixups.py`, `FIXES_BSPR`:
- `object_types.c#bspsame`: at each map's start, `biped_data_definition.game_datum_size` is `0x480 + 640` with the lever on, `0x480` (the engine's) with `bspsame`. A static assert in `bipeds.c` holds `sizeof(struct biped_datum) <= 0x480`; one in `collision_bsp.c` holds the slots in 640 bytes.
- `bipeds.c#bspsame`: `biped_update` points `psp_bspr_datum_at` at the biped header's datum pointer (only when its `data_size` has the tail) and restores the outer value at its one exit. The tail is found through the header at each question, so a datum the pool compaction moved is still found.
- `collision_bsp.c#bspsame`: `psp_bspr_sphere_take` / `psp_bspr_sphere_keep` around `collision_bsp_test_sphere`'s walk, before the sphere memo (a kept answer never reaches that table); `psp_bspr_ray_take` / `psp_bspr_ray_keep` around `collision_bsp_test_vector`'s. A slot is chosen by a hash of the key, so the same question lands in the same slot each tick; a slot under refill has `bsp = NULL` until its answer is whole.
- `game_state.c#bspsame`: after a game state loads (a revert, a saved game), `psp_bspr_forget_loaded` clears every biped's tail: they were kept under another run's generations.
- `objects.c#bspsame`: the sleepers' checksum skips the tail (it changes while the biped doesn't), and counts the words after it as far as before, so a biped sleeps exactly when it did; the reference sum (`checksumcheck`) skips it the same way. `statecrc`'s object hash reads listed fields only, so the tail never enters it.

**The check, `bspsamecheck`** (test only): a match walks as well, compares the walk's answer with the kept one field by field, plays the walk's, and counts any difference. Every 300 ticks the log has a `bspsame:` line (spheres and rays asked, answered, kept; under the check, the answers that differed).

## Proof

On w140 c4ee05f (M6 #2 on both sides), the PGO build, `fixedticks fixedseed nomouth` on both sides.

- **The check [E]:** under `bspsamecheck`, 0 kept answers differed from the walk's: b30's timed route, 8,908 ticks (46,656 sphere matches and 8 ray matches checked); a30's, 4,316 ticks (9,084 sphere matches).
- **Play [E]:** on against `bspsame`, the objects as played and both random seeds the same at every tick: a30 4,500 ticks, b30 9,000, c10 hands off 3,600. The whole-state hash differs, as it must: the object pool holds the tails (allocations `object`, `objects`), and the allocations that differ between runs whose timing differs (`actor`, `object looping sounds`, `hud unit interface`, `particle`), as in earlier pairs.
- **How often a question is answered [E]:** b30 43 sphere questions a tick, 12% answered from the tail (5.2 walks a tick saved); a30 9.9 a tick, 21% (2.1); c10 3.7 a tick, 15%. Rays: b30 12 a tick, a30 1.1, and almost none repeat (8 of 105,290 on b30): a resting body's rays move with it.
- **Compiles:** campaign (PGO, and `PSP_METRICS=0`) and `PSP_MULTIPLAYER=1 PSP_METRICS=0`.

## Gain

Cache model [E] (tick-phase cycles: fetches + 72 x D-misses + 64 x I-misses), the PGO build, on against `bspsame`, `fixedticks fixedseed nomouth` on both sides:

| Stretch | On (M cycles a tick) | Off | Saved | Fetches | D-misses | I-misses |
|---|---|---|---|---|---|---|
| a30's fight (game time 60-150 s, 2,700 ticks: the run to 150 s less the run to 60 s) | 9.446 | 9.521 | **75k (0.78%, 0.22 ms at 333 MHz)** | -0.57% | -1.24% | -0.65% |
| b30's timed route to 125 s (3,750 ticks) | 12.128 | 12.217 | **90k (0.73%, 0.27 ms)** | -1.09% | -1.16% | -0.01% |

Drawing is unchanged (a30's fight 2.387 against 2.390 M cycles a tick). Each walk saved is worth ~35k cycles on a30 (2.1 a tick) and ~17k on b30 (5.2 a tick). The first version, a shared table, cost b30 0.40% of its tick; this one gains 0.73% there.

Bottleneck: a30's fight is 68-79% ticks on the PSP, so a tick saving reaches the frame there; b30's beach is mostly drawing.

**Verdict: below the gate.** The gate is ~1% of a30's fight tick (0.29 ms of the model's 9.5 M cycles); the per-frame gate, halved for a tick lever, asks 0.32-0.59 ms a tick on a30 and 0.59 on b30. 0.78% (0.22 ms) and 0.73% (0.27 ms) are short of it on both stretches, so the lever is shelved, not merged. Rays are pure overhead (almost none repeat); the one idea not built is to drop the 2 ray slots for 6 sphere slots, which would keep more of b30's 43 sphere questions a tick. The memory-bound part (D-misses -1.2%) could be larger on the PSP than the model's flat miss price says; only a PSP pair would show it.

## Risk

- Hot code: a 7-word (or 11-word) compare and a hash per question, only for the biped being updated; ~60-100 cycles a query against a walk of thousands. Questions outside `biped_update` pay one NULL test.
- The object pool: 640 bytes more per biped while the word is on (~40 bipeds on a busy map, ~25 KB of the pool). The heap is unchanged by the tails; the code adds a few KB.
- A future change that makes a walk read something not in the key (another global, a per-object flag) would make a kept answer stale. `bspsamecheck` catches it.
- A biped deleted in its own update would leave the pointer on a freed header for the rest of that update. `biped_update` has one exit after its profile section [D]; anything it calls that deleted the biped would already break the engine's own use of the biped after the call [U].

## Multiplayer

The answers are the walks' own, so the simulation is the same and the multiplayer build plays in step with the server. The datum size is local; nothing in it goes over the network. It compiles with `PSP_MULTIPLAYER=1` and `PSP_METRICS=0`.
