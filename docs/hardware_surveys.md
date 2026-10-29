# The tick, system by system: what eight surveys found (directions 1 and 2)

2026-10-29, hardware session. Sheepdog01's directions (16:57, via the master): (1) the tick system by system, (2) compact hot data; the API credit spent on surveys (Sheepdog01 17:26). Seven read-only surveys of Sierra-III at w142 (4a38b79) and the engine at the pin ran as lab `api-job`s (Opus, agent mode); the eighth (needless work, a Sonnet sweep with Opus on its top ten) is still running and gets its own section when it lands.

Labels: [M] measured on a PSP, [E] emulator, [D] read in code or data, [U] inference. Nothing in this doc was built or run for it. Every ms figure is main-CPU time.

**Checking the surveys.** Two or more quoted lines from each answer were looked up at the pin and in w142's `psp_fixups.py` (18 in all: objects.c 2577, 3866, 4524; physics.c 1354; bipeds.c 4007; actors.c 4210; actor_looking.c 551; actor_perception.c 5901; ai_communication.c 3078; props.c 406; game.c 342; weather_particle_systems.c 93, 763; render.c 391; particles.c 681; collision_bsp.c 2191; bsp3d.h 45; scenery.h 33; damage.c 310; psp_fixups.py 4015, 4221, 8981, 25799-25802). All match.

| Survey | Its question | Lab run |
|---|---|---|
| passmap | object_update two levels down: which steps could run as passes | api-job 40966312801 |
| hotfields | which datum and definition fields each tick system touches | 40966547492 |
| physpass | biped and vehicle physics and the collision walks as passes | 40966552838 |
| aipass | actor_update split into passes | 40966621873 |
| fxpass | effects, particles, contrails and weather: passes or ME jobs | 40966627960 |
| collbake | a cache-friendly collision BSP made at bake time | 40966677405 |
| mecensus | tick and frame work that could be self-contained ME jobs | 40966682398 |
| needless | work the tick does that nothing reads | 40966715304 (running) |

## The ranked table

Worth in ms a frame on the heavy stretches. A per-tick figure counts twice (two ticks a frame).

| # | Move | Worth | Label | Exact? | Next step | From |
|---|---|---|---|---|---|---|
| 1 | **AIME step 2a: the thinking after a frame's last tick on the ME, during the record, before L15** | b30 ~10-12, c20 ~4-5, d20 ~6-7, c10 ~0.5 | U on M x E | no: an AI-timing change (step 1's outcome census) | the ME job (below); its eDRAM row first | mecensus |
| 2 | **c10's weather on the ME**: step A the update and box tests, step B the sprite quads too | c10 net ~2.8-3.1 (A), ~3.9-4.3 (B); b30 0 (no weather) | U on M x E | can be bit-exact (seed in and out, FPU word handed over) | census the dead BSP walk first; then step A behind a word with a check mode; c10 PSP pair | fxpass, mecensus |
| 3 | **The sleep after-checksum on the ME during the record** | b30 ~1-2.7 (tick 2's list only before L15) | U from E (a 2026-10-15 fetch share) | no by construction: a later in-tick write is now seen | a fresh census of its share on four maps | mecensus |
| 4 | **A start-node grid for the collision walks** (the hot top levels skipped) | b30 up to ~1.1 gross (0.56 a tick) | U on E | yes, if the bake's margins are conservative | census at load in a recon build (how often a query fits a cell, nodes skipped); then `bspgrid` / `bspgridcheck` at load | collbake |
| 5 | **The function values and change colours as a pass** after the walk | CAN'T TELL (code ~3-4 KB, every awake object) | U | yes, except families with a hidden weapon child, once the sleep's after-checksum moves after the pass | a cache-model count, then a PSP pair (a cache change) | passmap |
| 6 | **The shared hot fields packed** (direction 2): the generic path's 132 B sit on 5 of the datum's 7 lines and would fit in 3 | CAN'T TELL; bipeds gain least (16 of 18 lines touched) | D fields, U lines | yes (a layout change) | priced by the hotdata helper's data-section run first, then a field copy's census | hotfields |
| 7 | **Sound obstruction rays on the ME** | b30 <=0.55 | E | already tolerates staleness | under every gate today | mecensus |
| 8 | **B1: a compact copy of the level around each biped** for its sweep (`plan_biped_collision_me.md`) | D-misses -0.2 to -0.5 a tick on a50 and b30's beach | U | yes, by construction (a check word compares every answer) | as the plan says | physpass |
| 9 | **8-byte bsp3d nodes** (12 -> 8 B, every index kept) | tick in the noise; frees ~151 KB of b30's tag window, which pays for #4 at bake | U | yes | after #4's census, under the cache rule | collbake |
| 10 | **V1: each vehicle's mass points split into sub-passes** in its own slot | small, only where vehicles are | U | yes | a cache-model run, then a pair | physpass |
| 11 | **The AI's control half as five passes on top of AIME** | <= ~2% of the tick's I-misses | E bound | no: only the control steps' draws reorder | after AIME on the ME | aipass |
| 12 | **The lights relink as a pass** on link ticks | small | U | yes (LD1's own argument) | none planned | passmap |

## What each survey settled

**Passes across objects mostly don't fit (direction 1).**
- The type updates of units, bipeds, vehicles and projectiles are each over 12 KB with their callees [U, sizes from source lines]. One biped_update call already runs more code than the 16 KB cache holds [E, cache_work.md]. So none of them can become a pass that keeps its code warm.
- **Physics:** no cross-object pass is exact. Each biped's sweep reads the clusters, velocities and machines of objects moved earlier in the walk, and moving only the bipeds' relinks already parted play (bwrelink [E]). Vehicle-vehicle contact depends on index order in the engine itself (physics.c:1354). What stays exact is smaller: V1 (#10) and B1 (#8).
- **AI:** the thinking steps read each other's results across actors, and every step but the timeslice, input and unit control draws from the global seed, so any split changes play. The thinking's ~19% of the tick's I-misses is reachable only by moving it off the main CPU (AIME), not by splitting it. On top of AIME the control half splits into five passes under 12 KB each, worth <= ~2% of the I-misses (#11).
- **The node matrices** fit the cache as a pass (0.56-0.76% misses on their own pair [E]) but are exact only for the set the build already defers (VISLAZY/NMSKIP); anything with a parent, children or a moving root changes what later collision tests read.
- **The best clean pass** is the function values and change colours (#5): nothing in the walk reads another object's outputs from this tick (effects_update runs before objects_update), so the pass is exact once the sleep check's after-checksum moves after it and a hidden weapon's export into its parent is handled.

**The hot fields (direction 2).**
- `object_datum` is 0x1A4 B. The generic path touches ~132 B per awake object a tick (168 with the damage fields), on lines 0-3 and 6. Line 6 is pulled only for the node-block pointers at 0x19C-0x1A3 [D].
- `object_definition`'s ~64 hot bytes sit on 4 lines; each function element (360 B) has hot fields on 2-3 lines with a 252-byte cold gap in the middle [D].
- **What limits any packing:** the sleep checksum reads the whole datum (up to 2 KB) for objects allowed to sleep but not settled, marked sleepers, and always for machines and controls; and the node block (52 B a node) is written whole [D]. Settled scenery already reads no datum line in the walk (WS + OBJPLACE).
- The object pool doesn't align datums to lines, so every line count is a best case [U].

**Effects and weather.**
- Weather is the only system that's self-contained end to end: its particles live in a plain `data_new` array (not game state), and only `weather_particle_systems_render` reads them, inside the record [D]. c10's weather is 4.66-5.05 ms a frame on the PSP-1000 [M]; the 2.5 ms in the bake census was the emulator's.
- **A side finding:** each weather particle's `point_physics_update` still walks the BSP once a frame through `collision_test_vector`'s nothing-to-test branch (`scenario_location_from_point`), and the answer lands in `system->location`, which the render resets before any read (weather_particle_systems.c:763) [D]. The walk looks dead [U]; its share of the update's 52% is unknown.
- **A correction to hardware_big_moves.md #4:** particles_update can spawn an effect on a structure collision (particles.c:681), so it isn't purely local. It stays a pass, not a self-contained ME job.
- particle_systems_update's own `point_physics_update` calls aren't on mepoints yet (no fixup for particle_systems.c) [D]: they could join the existing points job.

**The collision BSP at bake time.**
- Renumbering the nodes (vEB, treelets) is out: the draw walk and the leaf surface references index the collision nodes by number, and the nodes are already 95% within 4 lines of their parent [D, lab]. Two layout tries measured nothing on the PSP (BSPW, BSPDFS).
- The grid (#4) saves instructions, not misses: a lookup of a few tens of cycles instead of the walk from the root. The 8-byte nodes (#9) are a memory win first.
- The object partitions can't be baked: they're runtime lists in the game state.

**The ME census.**
- **AIME doesn't need L15 for half its value.** aicad3 rests half the actors on each tick, so the thinking pass after a frame's last tick (which has the record after it) is about half the frame's thinking: b30 ~10-12 ms movable today [D + U]. This corrects big_moves #2's "why it needs L15".
- **Three hazards AIME's ME job must clear** that the plan didn't list: perception creates props with `datum_new` (props.c:406, an allocation); it calls libc `qsort` (actor_perception.c:5901); emotion raises communication events, and that module calls `unit_speak` and `csprintf` [D]. The fixes: a prop pool reserved by the main CPU before the push (or props queued and made at the hand-back), a port-own sort, and an event queue drained at the hand-back. Perception's rays run inline on the ME instead of being pushed to it.
- **A rule to settle:** media_engine.md says job code makes no library calls, while levers.tsv's ME row lists libm among the shared code the ME runs. `mewalk.py` decides which is true; weather's `fmodf` and the sprites' `sine`/`cosine` need the answer.
- **The ME's room is the record window, not its idle time:** b30 ~38-40 ms (45 less mevisland's 5-7), c20 ~25, d20's fly-through ~24; c10 and d20's swamp ~0 (the main CPU already waits for the ME) [D + M -> U]. So c10's weather fits only with PDC's ~8-9 ms off the replay.

## AIME step 2a, as the surveys shape it
1. At the record's start, after the main CPU's whole-cache write-back, push `psp_aime_think` for the pass after the frame's last tick. Tick 1's pass stays inline.
2. Before the push, the main CPU reserves a prop pool; the ME takes props from it. A port-own sort replaces `qsort`. Communication events go into a queue.
3. Collect at the next `ai_update`, with a bounded wait; the thinking must finish before `units_update` writes the world (a done word checked there). Drain the event queue and release unused props at the hand-back.
4. `mewalk.py` clears the reachable code (no VFPU, no system calls). The eDRAM row goes in findings first if the job's arena uses it.
5. Price it with one c20 pair (`keepsets="- | aime"`), counting the record's slowdown from the ME's bus traffic (+4.5-11% [M]).

## Sources
- The answers: `api-job/<id>/answer.md` on the lab's results branch, ids above.
- Lab rows cited by the surveys: measurements.tsv (weather 10325, 10341; ME busy 10320; AI 9883, 9884, 10330; sleep checksum 1797); levers.tsv (BW 116, BSPW 204, BSPDFS 313, WXGRID 394, NW2 387, AIME 396).
