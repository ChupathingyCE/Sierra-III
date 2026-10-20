# Review: fix-memory (b139's hand test: H4, H40, H12, H21, H28, H10, H32, H5)

Branch `fix-memory` off `w139`. Every change below sits behind a keep word. Labels: [M] measured on the PSP, [E] emulator, [D] read in code or logs, [U] inference.

## Summary

| Finding | Change | Default | Keep word | Evidence |
|---|---|---|---|---|
| H4 | The checkpoint's 32 KB stage reserved for the whole run | on | `statestagemalloc` (from the heap, as before) | [D] the log; [M] the PSP-1000 A/B below did not reproduce the failure in 300 s (no failed malloc on either side), so the fix is unproven |
| H40 | Each checkpoint write timed (the line names the slowest write); `statedelta`: only the pages that changed, at fixed places | timing on; `statedelta` off | `statedelta` (opt-in) | [E] d20 below; [M] on the PSP-1000 `statedelta` is **worse** than compact: median 658 ms against 185-194, and more bytes |
| H12 | Sprite and contrail vertices given back once the GE layer has copied them | on | `dynamickeep` | [E] peak 2,066 to 1,280 (a30), 2,576 to 1,112 (b30) of 4,096; [M] PSP-1000, b30 to 300 s: 2,436 to 688 |
| H21 | The HUD's and screen draws' textures pinned in the texture cache and read past the frame's budget | on | `hudpinoff` | [E] untextured HUD draws 58 to 0 (a30), 101 to 0 (b30); [M] PSP-1000, b30 to 300 s: 88 to 0 |
| H28 | Contrails (the weapons' tracers) drawn by default; the particle census; the particle ranks | contrails **on**; census and ranks opt-in | `nocontrails` (turns them off), `particlecensus`, `particleranks` | [E] contrails cost 1.2 ms a frame in a30's fight; owner's call: on by default |
| H10, H32 | The geometry cache's ring passes over blocks drawn in the last 60 frames before evicting | on | `geofifo` (or `geokeepN`) | [E] not reproduced on the emulator routes (misses -3%); needs the hand-test stretch |
| H5 | `audio44`: the mix taken to 44.1 kHz by our own half-band filter, played on a 44.1 kHz channel | off | `audio44` (opt-in) | [U] needs a listening A/B on the PSP |

Instruments added: `saveeveryN` and `revertatN` (checkpoints on a clock, and a revert), `particlecensus`.

**Cost, in the emulator, game time, the same stretch** (`fixmem1`: the heartbeats paired at equal game time):

| Change | Stretch | Frame (ms a frame) | Tick (ms) |
|---|---|---|---|
| the fixes without contrails (H4, H12, H21, H10) against b139's behaviour | a30 fight, 44 heartbeats | -0.16 (se 0.12) | -0.65 (se 0.26; median 0) |
| the same, contrails on but few drawn | b30 to 720 s, 40 heartbeats | +0.03 (se 0.17) | +0.02 |
| the same | a10 to 720 s, 41 heartbeats | +0.006 | 0.000 |
| **contrails drawn** against not drawn | a30 whole fight, 60 heartbeats | **+0.23 (se 0.07)** | +0.07 |
| contrails drawn | a30's heavy stretch (game time 7,200-11,000, 9 heartbeats) | **+1.24 (se 0.33)** | +0.49 |
| `particleranks` against the defaults | a30, 24 heartbeats | -0.08 | +0.07 |

The fixes are free; the tracers are not: +1.2 ms a frame (7%) where there is fire to draw. b139's rule is that quality is what is free in frame time, so the build had them off by default. Owner's call: on by default, so `nocontrails` turns them off. The pictures are under H28.

## H4: the checkpoint's stage, reserved at start

**What the hand test's log shows [D]** (b139 test 2, b30): from game time 31,052 on, `heap: malloc of 32883 bytes FAILED` came before 12 checkpoints. Without its 32 KB stage the compact writer gave up and the checkpoint fell back to the page format, 2 MB written whole: 1,129, 236, 1,247, 348, 1,165, 232, 1,199, 232 and 1,165 ms, every other one over a second. The heap had 85 KB free, but in 107 pieces.

**The change** (`psp_fixups.py`, `psp_state_write_words`): the stage is a static, 64-byte-aligned array (32,831 bytes of BSS, so the heap is that much smaller from the start, the same as the request it replaces made at its worst). `statestagemalloc` takes it from the heap at each checkpoint, as before.

```c
static byte psp_state_stage_reserved[PSP_STATE_STAGE + 63] __attribute__((aligned(64)));
...
stage_block = from_heap ? (byte *)malloc(PSP_STATE_STAGE + 63) : psp_state_stage_reserved;
```

**On the PSP-1000 [M]** (b30's route to 300 s, a checkpoint every 15 s, one run a side; the first set is `statestagemalloc`): not reproduced. The heap reported 0 failed requests in all three runs, and all 22 checkpoints of the `statestagemalloc` side were compact ones (693-1,206 KB), so no side fell back to the page format. The failure came in the hand test after game time 31,052; this run ends at 8,977, with the heap at 5.1-5.5 MB of 5.8 MB and 14-63 KB free inside. The reserved stage costs 32 KB of heap from the start (the new side's free-inside figures run 14-27 KB against 41-63 KB, but no request failed). So the fix removes a known failure [D] that this run's length can't reach; a run past game time 31,000 on the PSP, or the hand test, is the proof.

## H40: why checkpoints still stall, and what to try

**Where the time goes [D, b139 test 2's log].** With the stage, a compact checkpoint (0.8-1.45 MB) took 159-249 ms in 15 of 19 cases: the stick's ~6 MB/s. Four took 604-1,467 ms. Each slow one holds a single write of about 0.8 s (`stick:` lines: `user_main write savegame.bin 823 ms`, `806`, `804`, `871`, `938`, `923`). The same 0.5-1.5 s single writes hit the screenshots (`shot073 1,472 ms`) and the log (`write log.txt 833 ms`, `close log.txt 685 ms`). So it isn't the checkpoint's write pattern (whole 4 KB pieces since b117) but the stick itself: a write now and then waits on the card's own housekeeping [U: erase-block reclaim, the usual cause on flash cards]. The page format's strict fast-slow alternation (236, 1,247, 348, 1,165, 232, 1,199, ...) fits that: every other 2 MB overwrite runs out of the card's spare blocks [U].

**What that means:** the stalls scale with bytes written [U]. Fewer bytes, fewer stalls; or the writes off the game thread (lever S1 in `levers_next_b138.md`, `streaming.md` §3).

**The changes:**
- Every checkpoint line now says how many writes it made and the slowest (`(%lu writes, the slowest %lu ms)`), so the next hand test tells a slow stick from a slow writer.
- `statedelta` (opt-in): the file holds each page of the game state at a fixed place (4 KB of header, then page n at 4 KB + n × 4 KB), and a checkpoint writes only the pages whose 64-bit hash changed since the last one, straight from the game state (no stage, no heap). Runs of changed pages are joined across gaps of up to 3 clean pages. The hashes describe the file, so they're forgotten when the file opens and after a failed write (the next checkpoint writes every page). Reads go by the header's map, page runs from their places.

**Evidence [E], d20 with a checkpoint every 10 s and a revert at 70 s (`fixmem1`, compact against `statedelta`):**
- Play is exact: the play hash (the objects as the game plays them) and both random seeds are the same at all 2,700 ticks, through the revert at tick 2,100. The whole-state hash differs from tick 42 in two allocations only, `contrail point` and `objects` (drawing-side data), before any checkpoint was written, so it is not statedelta's doing [U]. The off-vs-off control is `fixmem2` (two runs of each format): it should show the same two allocations differing between two compact runs.
- Bytes: d20 changes 70-100% of its pages early (239 to 308 changed of 239-393 kept), so `statedelta` writes more than the compact format there (1,056-1,392 KB against 514-977 KB), then fewer once the level settles (109 changed pages, 552 KB against 904 KB). On b30 in b139's test the compact checkpoints were 0.77-1.45 MB and 111-309 of 518 kept pages changed between them, so 0.45-1.2 MB for `statedelta` [D]: a gain there, not on d20.
- The hashing scan takes 24-33 ms of the emulator's checkpoint (the whole checkpoint 34-47 ms); on the PSP it is main-CPU time added to the stall, against the stick's time saved. Whether the stick's 0.8 s writes go away with fewer bytes is [U]: the PSP A/B (below) is the only test.

**On the PSP-1000 [M]** (b30's route to 300 s, a checkpoint every 15 s, one run a side; times are the log's "written in", the slowest is the longest single write):

| Side | Checkpoints | Median | Mean | Max | Over 1 s | Slowest single write over 500 ms | Bytes (mean, range) |
|---|---|---|---|---|---|---|---|
| compact, stage from the heap (before) | 22 | 194 ms | 265 ms | 1,076 ms | 2 | 2 (max 823 ms) | 976 KB (693-1,206) |
| compact, stage reserved (now) | 22 | 185 ms | 439 ms | 4,475 ms | 2 | 2 (max 846 ms) | 993 KB (693-1,230) |
| `statedelta` | 20 | 658 ms | 791 ms | 1,399 ms | 7 | 7 (max 855 ms) | 1,232 KB (960-1,436) |

- **The reserved stage removed no fallback, because neither compact side had one** (see H4). The page-format stalls of the hand test (1.1-1.25 s every other checkpoint) are not in this run.
- **The 0.8 s stalls are the stick's, as the analysis above said [M].** Both compact sides have the same two single writes of 810-850 ms, at ticks 3,150 and 4,500, whatever the stage; the stick's own log lines name them (`user_main write savegame.bin 846 ms`). The typical checkpoint is 140-215 ms for 0.7-1.2 MB. The one 4,475 ms checkpoint (ticks 4,500, reserved side) had the 809 ms write plus a rewrite of the map's cache file on the stick at the same moment (`rename b30.vram.tmp 773 ms`, `remove 545 ms`, `close 528 ms`: 8 calls over 250 ms in that report) [U: the card was busy with two writers].
- **`statedelta` is worse here, not better.** It wrote more bytes (mean 1,232 KB against 976-993 KB) because b30 changes 190-320 of its 330-460 pages between checkpoints (the compact format keeps only non-zero words of those, the delta writes whole 4 KB pages, joined across gaps), and the stick took them slower: median 658 ms against 185-194, 15 of 20 over 400 ms, 7 over a second with a single write of 810-855 ms. The 39-43 ms hash scan is a small part. The likely cause is overwriting a file in place: every checkpoint rewrites clusters the card already holds, where the compact writer's file is also overwritten but in one forward run [U]. The estimate in the emulator section (0.45-1.2 MB on b30) was wrong about the bytes [D] and the stick's cost is not a function of bytes alone. **`statedelta` stays opt-in and is not worth pursuing in this form**; the writes-off-the-game-thread designs below are what could hide a 0.8 s write.

**Not done here: S1 (the writes off the game thread).** A background writer needs the checkpoint's bytes to hold still while it writes, and there's no room for a 1-1.5 MB copy in main RAM (133 KB free on b30). Two designs, for pass 2 or later:
1. **The copy in the ME's eDRAM.** The ME packs the changed pages into its 2 MB eDRAM (23 MB/s measured, `hardware_b115.md`: ~60 ms for 1.4 MB, in slices under the audio job's 40 ms timeout), the game goes on, and a low-priority thread has the ME hand back 32 KB at a time to write. Needs: eDRAM confirmed free during play (G4, `edramcheck`; the ME programme's stage 0 measures it tonight), a revert or core save waiting for the writer, and the emulator can't run it (eDRAM is simulated there), so it's proved on the PSP.
2. **Optimistic pages.** A thread writes pages while the game plays, hashing what it wrote; at a tick boundary a second or two later, the game writes the pages that changed meanwhile and that becomes the checkpoint. The saved moment moves a little later than the one the game chose as safe, a behaviour change, not pre-approved.

## H12: the dynamic vertices given back once copied

**Cause [D].** Sprites (particles, muzzle flashes, impacts) and contrails take their vertices from the dynamic buffer (4,096 vertices of type 6, 1,024 sprites), a stack per vertex type cleared at the frame's start. `build_sprites_begin` reserves room for the group's most sprites (for a particle definition, every particle of it alive), and on the Xbox the GPU reads them later in the frame, so every group held its room until the frame ended. a30's fights reached 4,092-4,094 of 4,096 in 295 heartbeats of b139 test 1, and `build_sprite failed to allocate dynamic vertices`: what was drawn after the stack filled was left out. The PSP's GE layer copies sprite and contrail vertices into its display list on the spot (`psp_ge_sprites_body`, `psp_ge_contrail_body`, and gedefer's records), so the room is free once that returns.

**The change:** `psp_dynamic_vertices_release` pops a group's vertices off its type's stack when the group is the top (sprite groups last first, after `build_sprites_end`'s loop; each contrail after `psp_ge_contrail`). Exact: the same vertices are copied either way. The heartbeat's `dynamic geometry` line keeps the stack's true peak and adds the groups and vertices given back. `dynamickeep`: held to the frame's end. Not the HUD (it draws straight from the game's screen vertices), which is H21.

**Evidence [E]** (`fixmem1`): the heartbeat's frame peak of type 6 vertices, b139's behaviour (`dynamickeep`) against the new default: a30 2,066 to 1,280 of 4,096 (48 to 30 KB), b30 2,576 to 1,112; 124,775 groups (6.8 million vertices) given back on a30. No `build_sprite` failure in any run. **On the PSP-1000 [M]** (b30 to 300 s): the type 6 frame peak 2,436 of 4,096 with `dynamickeep`, 688 with the default (832 in the `statedelta` run), with 39,459 groups given back; no failure on any side (this route never reaches the cap, as in the emulator). The emulator's hands-off route doesn't reach the hand test's 4,092; what matters is the headroom, and the heaviest frames' peak is cut by 40-57%. Cost: none measurable (above).

## H21: the HUD's textures pinned

**Cause [D].** The texture cache keeps a texture drawn in the last 60 frames while it can, then, when both caches are full, only one drawn in the current frame (`load_into`'s second pass). The HUD draws last in a frame, so for most of the next frame its textures count as last frame's; in a fight that fills the caches with this frame's textures, the second pass evicted them, and the HUD missed them when its turn came. At low frame rates the frame's read budget (96 KB) was often spent by then, so they stayed missing for frames: parts of the HUD vanishing and coming back under load.

**The change** (`psp_tex.c`; `psp_ge.c`'s `ge_kind` marks the screen-space kinds): a texture bound during the HUD's or the screen quads' draws is pinned. A pinned texture counts as protected for 60 frames in either pass, and a pinned texture missing at its bind is read even past the frame's budget (HUD textures are small). A heartbeat line counts the HUD's misses, those read past the budget and those drawn untextured. `hudpinoff`: as before.

**Evidence [E]** (`fixmem1`, the HUD's and screen's draws over the run): a30 with `hudpinoff` 78 missing when drawn, 58 drawn untextured; pinned: 30 missing, 21 read past the budget, **0 drawn untextured**. b30: 128 missing, 101 untextured; pinned: 26 missing, 9 past the budget, 0 untextured. Cost: none measurable. **On the PSP-1000 [M]** (b30 to 300 s, the heartbeat's cumulative HUD line): with `hudpinoff` 110 missing when drawn and **88 drawn untextured** (24 by the first report); pinned, 23 missing (9 read past the frame's budget) and **0 untextured** (22, 9 and 0 in the `statedelta` run). The 9 read past the budget are the reads the fix adds; their cost is in the table below.

## H28: tracers

**Cause [D].** Halo's tracers, for the rifles as for the Covenant weapons, are contrails: the projectile's attached strip (`b113_candidates.md` #1; with `contrails` on, `hardware_b114.md` test 7: "plasma and rifle fire show long streaks"). The PSP has drawn them since b113, but only with the keep word `contrails`, which never became a default, so every hand test since b114 has had none. The particle budget (256 free particles) and the burst thinning cut free particles, which carry impacts, sparks and smoke, not the tracers themselves.

**The changes:**
- **Contrails on by default; `nocontrails` turns them off** (a restoration that costs frame time; owner's call: on by default). `contrails` is still accepted and changes nothing. The launch menu's entry is `nocontrails`, to match. Their look against the PC reference: not compared with the PC reference build overnight: the tracers are the original's (contrails are tag data and `hardware_b114.md` test 7 saw the streaks); the emulator shows thin strips from the rifle along the shot (below).
- **`particlecensus`** (instrument): every 30 s, per particle definition, free particles made, refused over the budget and left out thinning, and attached ones made; contrails made and failed for room; effects that failed for room.
- **`particleranks`** (opt-in): the hierarchy asked for after the hand test, by the definition's tag path. 1, a slow projectile's own visuals (`grenade`, `needle`, `fuel rod`, `rocket`, `projectile`, `bolt`): never refused or thinned. 2, `tracer`: never refused, thinned only by its own one-in-N. 3, the rest (impacts, sparks, flashes, blood): as before, and past three quarters of the budget only those within 10 world units of the eye (nearest first). 4, smoke, debris, dust, ambient (`smoke`, `debris`, `dust`, `smolder`, `drifting`, `lingering`, `ambient`, `gravel`, `chips`, `casing`, `spray`, `steam`, `flame`): thinned from 48 free particles (two in three from 112) and refused at 192, so they give way first. With the word off nothing changes (every particle is rank 3).

**Evidence [E]** (`fixmem1`, a30 on the b139 route; compare `a30-new/shot005_t363.png` (contrails drawn) with `a30-noct/shot005_t363.png`, the same moment): with contrails, strips cross the picture from the muzzle; without them nothing. No contrail failed for room in any run (the census: 0). The cost is the table above. **Owner's call: on by default** (+0.23 ms a frame over a fight, +1.24 ms in its heavy stretch); `nocontrails` in `keep.txt` or the launch menu turns them off. The PSP A/B below doesn't carry a contrails set (b30's route has little fire), so a later run on a30's fight would price them on hardware.

**The census [E]** (a30, the heaviest 30 s: free particles 7,081 made, 477 refused over the budget, 10,970 left out thinning): the cuts land mostly on smoke, flashes and `beam smoke` (3,692 left out), and also on `plasma electric bolts` (65 refused, 477 thinned of 256 made: a projectile's own visual). With `particleranks` the bolts are never cut (661 made, 0 refused, 0 thinned); the cost moves to smoke and sparks (`smoke white c generic` 191 refused against 24, `sparks trail` 142 refused). Frame time -0.08 ms. `particleranks` stays opt-in (what is drawn changes).

**Later:** the same ranking for the 2 dynamic-light slots per object.

## H10 and H32: the geometry cache's churn

**Cause [D].** The geometry cache (2 MB with `geo2`) is a ring that evicts whatever it reaches, protecting only blocks locked in the last 2 frames. Blocks are evicted in the order they were loaded, however often they're drawn, so once a level's working set nears the cache's size, each wrap throws out blocks still in view and reads them back within frames. The hand test's logs: a10 at 600-720 s and b30 at ~690-704 s read and evicted 140-250 blocks (0.7-1.6 MB) about every 20 s.

**The change** (`psp_geo.c`, `allocate_skipping`): before evicting by position, the ring passes over any block drawn in the last 60 frames (a second chance), up to 16 times; only if it finds nothing older does it fall back to the old rule. A heartbeat line counts the blocks passed and the fallbacks. `geofifo` or `geokeep0`: as before; `geokeepN` sets the window.

**Evidence [E]** (`fixmem1`, `geofifo` against the default, a10 and b30 to 720 s): not reproduced. a10's totals to 720 s: with `geofifo` 970 misses, 795 blocks (7.4 MB) loaded, 588 evicted; with the second chance 939, 762 (8.3 MB), 573, and 6 times a draw waited for room. b30: 1,117 misses, 6.3 MB, 867 evicted against 1,091, 6.2 MB, 828. In 600-720 s the counters don't move on either route (no churn on the emulator's walk). The hand test's a10 log had 3,866 misses and 27.8 MB loaded by 720 s, about three times what the route does, so the player's own walk loads far more than the route. The second chance is neutral on frame time (+0.006 ms) and a few percent fewer misses; its effect on the 20 s churn is unproven and needs the hand test's own walk (`route_from_log.py` on its `player: at` lines) or the PSP. The PSP A/B's `geofifo` set against its default covers b30 to 300 s only.

## H5: the fizzy treble (`audio44`, opt-in)

No bug found; one opt-in built for a listening test. **Read [D]:** the bake's half-rate filter is a sinc at 9.9 kHz (`soundpack.py`), so the 22,050 Hz sounds carry nothing that could alias into the band; the fizz isn't there. The mix is played at 22,050 Hz and the hardware's own converter takes it to 44.1 kHz; its images above 11 kHz are a possible cause of fizzy treble [U]. **`audio44`** (opt-in, `psp_dsound.c`): the mix is taken to 44,100 Hz by our own half-band filter and played on a 44.1 kHz channel (`sceAudioChReserve`), under 0.1 ms of the audio thread a block. The emulator can't judge sound, and a bake with `--sound none` is no clean A/B (the 44.1 kHz sources are decimated by a step of two). **Next test:** listen, `audio44` against the default, in a loud fight.

## Costs

See the table under the summary for the emulator. **On the PSP-1000 [M]** (b30's route to 300 s, game time 8,908-8,977, a checkpoint every 15 s, three sets in one job: b139's behaviour `statestagemalloc dynamickeep hudpinoff geofifo`; the defaults; `statedelta`; **one run a side**, so `abreport.py` gives no verdict). The frame is by the clock, so it includes the checkpoints' stalls:

| Stretch (game time) | Frame before / now / `statedelta` (ms) | Drawing (ms) | Tick (ms) |
|---|---|---|---|
| 300-1,500 | 194.6 / 196.2 / 198.7 | 95.3 / 96.3 / 96.0 | 46.4 / 46.5 / 46.5 |
| 1,500-2,700 | 216.6 / 217.0 / 228.6 | 115.8 / 117.5 / 120.2 | 46.6 / 46.2 / 49.4 |
| 2,700-3,900 | 139.0 / 140.3 / 152.5 | 49.7 / 50.2 / 53.5 | 39.7 / 39.8 / 42.1 |
| 3,900-5,100 | 100.8 / 98.2 / 95.3 | 37.4 / 38.0 / 39.5 | 27.6 / 24.7 / 22.3 |
| 5,100-6,300 | 102.3 / 99.5 / 83.5 | 50.3 / 50.6 / 43.8 | 23.8 / 22.4 / 16.9 |
| 6,300-7,500 | 116.7 / 122.4 / 123.0 | 54.1 / 57.8 / 57.9 | 28.2 / 28.8 / 28.4 |
| 7,500-8,900 | 139.3 / 131.2 / 137.3 | 39.9 / 37.1 / 41.7 | 44.6 / 42.4 / 42.0 |
| **300-8,900** | **144.4 / 143.8 / 144.7** | **62.9 / 63.5 / 64.1** | **36.9 / 36.2 / 35.3** |

- **The defaults against b139's behaviour: no gap beyond the spread.** Whole run: frame -0.6 ms (-0.4%), drawing +0.6 ms (+0.9%), tick -0.8 ms (-2.1%). The `statedelta` run has the same code as the defaults between checkpoints, yet its tick differs from the defaults' by up to 25% and its drawing by 13% in one stretch (5,100-6,300): the runs' play drifts apart (the route's timing and the game's real-time inputs), so one run a side can't resolve a gap under about 10% in a stretch. The whole-run figures agree within 0.5%.
- **Drawing is slightly higher with the defaults in six of seven stretches** (+0.3 to +3.7 ms; -2.8 in the last) and the whole run +0.9%. The emulator measured -0.16 to +0.03 ms a frame. The HUD pin turns 88 untextured draws into textured ones and reads 9 textures past the budget [D]; that is the likely source of any real drawing cost [U]. It is below what this A/B resolves; a `repeat=2` run of the two sets would settle it if the cost matters.
- **`statedelta` adds its stalls to the frame** (2,700-3,900: +12 ms a frame over the defaults, from the 0.8 s writes) and is not free in the tick either.

The conclusion for the build: H12, H21, H4 and H10/H32 ship on by default at no cost the PSP can resolve; contrails ship on, at their emulator cost (owner's call), and `nocontrails` turns them off.

## The review's checks

- `PSP_MULTIPLAYER=1` compiles: the 13 changed units compile on the campaign build, unit by unit, 0 failures; the `PSP_MULTIPLAYER=1` full build did not run yet: the first dispatch of the lab's build job failed at its checkout (a short commit name), and it is to be dispatched again with the full hash. The campaign build with the release's PGO switches did build and run on the PSP-1000 for the A/B above (the hardware job builds it first).
- `tools/public_export.py --report`: 0 findings.
- Comments say why, not when; build history in the commit messages.
