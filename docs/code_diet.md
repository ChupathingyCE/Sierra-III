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

## Batch 6: work that makes nothing (`peldust`; off by default)

The first cuts from a census of what the tick's objects do that changes nothing ([E] the lab's rows `fightobj1_vehicles` and `fightobj1_scenery`: b30's fight, the two Pelicans and the trees).

| Word | What it does | Where |
|---|---|---|
| `peldust` (`peldustcheck`) | A Pelican's thruster rays are not cast where they can't reach anything. `create_pelican_effect` runs on every vehicle update: for each of the 14 thruster markers it draws a direction in a 15-degree cone on the local random seed and casts a ray at the level and its water; a hit makes one dust effect and nothing else reads the ray [D]. b30's two Pelicans cast 28 rays a tick, about one in seven hits, and from the squad's landing on neither Pelican is drawn [E]. Every ray of a marker's cone lies in one sphere, so while that sphere is clear of every surface and fog plane the ray is skipped. The seed is still drawn. | `psp_fixups.py`, `FIXES_PELDUST` (`source/units/vehicles.c`) |

**Why it is exact.** The skipped ray could not hit, so the same effects are made; the draw is kept, so the local seed runs the same. The sphere is the cone's own bound with a little to spare (radius 0.56 of the axis' length against 0.533), the sphere test is the port's any-hit walk (`psp_collision_test_sphere_any`, which answers yes for any surface, facing either way, and for a centre outside the level), and every fog plane of the level is tested, whichever clusters use it. `peldustcheck` casts each left-out ray anyway and counts the ones that hit: the proof is that count staying at zero with play and both seeds identical to the word off.

**Proof and size [E]** (the lab's row `peldust1_proof`; b30 and a30 held to 130 s, the word off, on and `peldustcheck`): left-out rays that hit, 0 of 13,612 on b30 and 0 of 4,526 on a30; play, both seeds and every game display list identical on against off. While b30's Pelicans land, hover and sit, 41-60% of the rays are not cast, about 12 of 28 a tick, for under one sphere asked a tick; in flight none, and nothing is asked. The census counted 24 of 28 rays missing; about half of those miss with a surface or the sea's fog plane inside the cone's sphere, which this bound cannot clear. No PSP price yet.

**What is kept, and for how long.** One answer a marker: the sphere asked about is 0.25 units wider than needed, and holds while a later tick's sphere still lies inside it. A marker that moved more than 0.25 units in one update, or whose reach changed by more than 0.125, is in flight and is cast as before with no question asked, so a flying Pelican pays nothing new. Nothing is kept across a level change or a time taken back.

**Not built here:** leaving out the rays and effects of a Pelican nobody can see. That one plays the same rather than being exact (dust would start when the camera turns onto it), so the exact form goes first and the PSP's price of what is left decides whether it is worth its proof.

### Cut 2, a design: animated scenery asleep (`treesleep`, not built)

**What runs today [D, E].** A tree's sway is not state: an object flagged to animate automatically takes its pose's frame as (object index + game time) modulo the animation's length (`OBJECT_FRAME_INDEX_GET` in `object_compute_node_matrices`). b30's fight updates about 33 trees a tick and no word of their datums changes but the pose (the lab's row `fightobj1_scenery`). They split in two (the counts are read off the poses that change, 12-15 a tick, against the 33 updates [U]):
- **Drawn last frame (about 15 a tick):** `psp_nms_skip` refuses them (reason 6), so their pose is made in the update on every tick, twice a frame at 15 fps.
- **Not drawn (about 18 a tick):** `psp_nms_skip` leaves the pose out and keeps an entry holding the inputs and the tick; a reader or the renderer's list makes it from the entry (`psp_nms_make`, with `psp_nms_settle_time` standing in for the game time). Because the pose would have moved, the skip names the object in `psp_vl_moving` so the sleep check keeps it awake. It is awake only to stamp a fresh tick on its entry.

**Superseded 2026-11-22, the same day, by two things read in the code [D]; the cut as first written follows for the record.**
- **The Media Engine's rays bounce off an owed pose.** On the ME, the collision walk hands a ray back to the main CPU for any object whose pose is owed in the snapshot taken at each hand-over (`psp_nms_pending_for_me`). Today only the undrawn trees are owed. Trees asleep with their pose owed until read would be owed at every hand-over, the drawn ones too, and the ME would keep far fewer of its rays. So a drawn tree's pose still has to be made on every tick.
- **A simpler form keeps the gain that is left.** The entries, the snapshot and the saves stay as they are, and only `object_update`'s body goes: in the sleep check (`psp_sleepy_update`), a scenery object that would be asleep but for its sway runs the sleeper's path and the pose step alone (the skip's test, else the compute), not the whole update. It stays on the object walk. It needs a checksum that leaves the node blocks out, and a check word that runs the whole update beside it and compares.

What this form can take is `object_update`'s share of the trees' 1.1-1.2 ms a tick, not all of it; a census of where scenery is kept awake for its pose (b30's trees, c10's lily pads and plants) sizes it before it is built.

**The cut as first written.** An object that animates automatically and passes the skip's other tests (no parent or children, no interpolation running, root still and unmoved) is not kept awake for its pose and its pose is not made in its update, drawn or not. It sleeps like the rest of the scenery, off the object walk. A bit by object index marks it; a reader (`object_get_node_matrices`, `object_get_node_matrix`) or the renderer's list finds the bit and makes the pose for the tick the object's own update would last have run at, if the pose it holds is of another tick.

**Which tick that is [D].** The game time is T all through tick T and moves on after it (`game_time_update`: the tick, then the interpolation's copy, then the count); the object walk visits headers in index order. So for a reader:
- inside the walk of tick T, a tree whose index is below the walk's is at T, one above it still at T - 1;
- anywhere else, the tick of the last walk that finished (before the walk in tick T that is T - 1, after it T, and T again while the frame is drawn, when the game time already reads T + 1).

That is the tick the eager update stamps today, so every reader sees the pose it sees now.

**What else the compute sets.** The bounding sphere, which collision and visibility read without the accessor. The skip already requires the root still and unmoved, and `nmskipcheck` counts a root or sphere that moved under it; the cut keeps that test and that check.

**Why this can be exact where `nmskip` was called not exact.** NMS's verdict rested on the whole-state checksum (play and both seeds were identical at 9,000 of 9,000 ticks), with no control pair of two runs with the word off. Two runs of one build with the same inputs differ in that checksum by themselves: the visit stamp at +0x8 of an object's datum (the lab's lesson of 2026-11-22). So the proof here is play, both seeds, the frames and the game lists, with `treesleepcheck` making every pose eagerly as well and comparing it with the one made on demand; and one thing does differ by design and is not play: a sleeping tree nobody reads holds an old pose in the saved state, as VISLAZY's undrawn objects do.

**What it can take.** At most the trees' 1.1-1.2 ms a tick on the PSP-1000 in b30's fight ([M] the lab's row `fightmap1_objects_by_type_1000`), less the poses still made for trees drawn or read: once a frame instead of once a tick. Only a PSP pair prices it.

**To read before code:** the per-object double-speed test in `object_update` (a tick on which the eager update does not run must not count as the tree's last), and the three later pairs that re-anchor `psp_nms_skip`'s tests.

## Co-op

Every word here is exact and changes nothing a host or client could see. They ship in the campaign build's defaults only, as every default word does; the multiplayer build has no default words.
