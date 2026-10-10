# Build 108 on the PSP-1000: b30 to the level's end (2026-10-09)

Sheepdog01's PSP-1000 at 333 MHz. Keep words `nearauto assettrack geo2`, deathless; b30 from the stick's existing bake. About 49 minutes of real time, game time 46,600. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code or the tags, **[U]** inference.

**This run is polluted:** RemoteJoyLite video capture over USB was on from the start until partway through the map room bunker. It copies the frame buffer out every frame, so it takes bus bandwidth from a game that is bound by the bus. The 45 s pause at T≈2340 s (0 ticks/s, a steady 30 fps) is probably when the cable came out [U]. Speed figures before that point are pessimistic by an unknown amount.

## What happened

- The whole level played: the landing, the beach, the security room, the facility, the map room [M].
- **The mirror fix works:** the camera no longer turns over in the security room or the map room [M, Sheepdog01].
- **At the level's end the game exited to ARK-4's menu. That's not a crash:** the log says `level finished: the next map, levels\b40\b40, isn't on the memory stick` and `level finished: exiting` [M]. b107 crashed at that point; b108 leaves cleanly, as intended. Going back to the main menu instead would be friendlier.
- **The heap failed to place the level's vertex copies from T≈2070 s** (after the hunter fight) to the end: 5,290 `GE: no memory for a material's vertices` lines [M]. The materials that failed weren't drawn. See below.

## The heap: fragmentation, not size [M] [D]

| Game time | In use | Free inside the heap | Pieces | Requests failed |
|---|---|---|---|---|
| 74 | 4,453 KB | 52 KB | 81 | 0 |
| 14,836 | 5,381 | 265 | 178 | 0 |
| 26,467 | 5,746 | 224 | 95 | 0 |
| 28,058 | 5,656 | 421 | 107 | 1 |
| 29,737 | **4,657** | **1,421** | 293 | 682 |
| 41,151 | 5,468 | 610 | 347 | 12,915 |

The heap had taken 6,078 of its 6,080 KB from the system by game time 28,000. When requests began to fail, 1.4 MB was free inside it, but in 293 pieces, none large enough.

**What asked:** `level_stream_get` (psp_ge.c), found from the two caller addresses in the log (`0x8a41888`, `0x8a418c0`) with b108's link map. The rebuild of commit `89fdd6ab` is byte-identical to the shipped EBOOT, so the addresses are exact.

**How it fragments [D]:**
1. Each level material drawn gets converted copies of its vertices: the base and lightmap copies (20 bytes a vertex each, two blocks), the normals and the radiosity. A 5,439-vertex material asks for two blocks of 106 KB.
2. The copies have a 1 MB budget (`LEVEL_STREAM_BUDGET`). Over it, `level_streams_trim` lets the least recently used go, and they're rebuilt when seen again. This run: `level streams 1023 KB (766 let go)`.
3. Each rebuild asks the general heap for large blocks again, while the game's small, long-lived allocations settle into the holes the last ones left. In a long level, no 106 KB hole is left.

**b110 has the same code** (psp_ge.c is unchanged since b108), so it would fail the same way in a long level.

**The fix (exact; nothing on the screen changes):** give the level's copies their own arena, carved out once at start-up, so their churn never reaches the general heap; when a copy doesn't fit there, let go of copies not drawn this frame until it does. The arena's size comes from the budget, which the heap already gives the copies today.

## Speed against b107, same level, by game time [M]

`tools/abcompare.py` (A = b107 without capture, B = this run with capture until about game time 40,000):

| Game time | b107 tick / frame ms, fps | b108 tick / frame ms, fps | b108 against b107 (tick, frame) |
|---|---|---|---|
| 150-2,600 (the Pelican flight, the landing) | 61.7 / 129.2, 3.9 | 60.4 / 132.6, 4.0 | −2%, +3% |
| 2,600-8,000 (the beach) | 40.0 / 95.7, 6.0 | 38.7 / 95.6, 6.7 | −3%, −0% |
| 8,000-15,000 | 22.6 / 65.9, 9.4 | 26.9 / 63.2, 9.3 | +19%, −4% |
| 15,000-22,000 (the security room) | 11.1 / 50.1, 14.2 | 18.6 / 70.2, 10.4 | +67%, +40% |
| 22,000-30,000 | 25.6 / 75.4, 9.5 | 30.5 / 67.5, 9.0 | +19%, −10% |
| 30,000-38,000 | 21.2 / 53.0, 11.0 | 23.4 / 59.4, 11.1 | +11%, +12% |
| 38,000-46,600 (after the capture) | 23.4 / 75.2, 8.9 | 22.7 / 88.6, 8.7 | −3%, +18% |

**The security room, slower in b108 [M] [U]:** the player is on the floor (z 10.0, the mirror at 9.95) in both runs. The difference is what was there and what was drawn:
- 15.4 bipeds updated a tick in b108 against 9.5 in b107. The live ones were about the same (about 4 a tick, the player included); the difference is bodies: 11.2 corpses a tick in b108, 5.9 in b107. Nothing between b107 and b108 touches bodies, garbage collection or biped updates [D], and the collector cleared bodies in both runs (the counts rise and fall across the level), so it's how the fight went: where and when enemies died relative to entering the room [U].
- Most of those bodies were at rest ("idle past 2 s, offered to sleep") and still updated every tick: if a body at rest could really sleep, a room full of them would cost little [U]. A lever to look into.
- Model drawing 22.4 ms a frame against 8.9; dynamic lights 13.2 ms against almost none.
- b107 drew that room from the mirror's camera, under the floor, where most of the models were out of view: cheap for the wrong reason. b108 draws the room as the player sees it.

## What Sheepdog01 saw

| Seen | What we know | Status |
|---|---|---|
| Water mostly white, partly textured | `water simple` starts with a reflection cube map; b107 skips to the next map. White streaks remain (hardware_b107.md) [D] | open, known |
| Pelican thrusters white cones | `shader_transparent_chicago`: several maps multiplied; the GE path draws one, added, at full strength [D] | open, known: flatten such shaders in the bake |
| The farthest trees vanish for a few frames and come back on the approach | Likely the display list's size floor: when the list is full, the smallest models on the screen go first (b107), and far trees are the smallest. 41,042 draws were skipped with the list full over the run [M] [U] | to confirm with `nosizefloor` |
| Plasma weapons' glow shines through characters and bodies | Lens flares are hidden by rays cast against the level, vehicles, scenery and machines, but not bipeds; the flares draw with the depth test off [D, corrected 2026-10-10] | triaged: `b113_candidates.md` #2 |
| Dark areas much darker than the Xbox | Not explained yet. The Xbox may brighten lightmaps in the shader where we don't [U] | new: compare a shot with the Xbox |
| Some objects lit, others beside them not | Object lighting samples the lightmap under each object (b102); 7 samples fell back to the page's last colour, 2 to white [M]. One sample per object can land on a bright or a dark texel [U] | new |
| The flashlight doesn't light characters or objects | The light pass lights the level only; models get the GE's directional lights [D] | known gap (visual_pass_recon.md) |
| The flashlight's edges jagged, and it shows the geometry's seams; decals over seams too | The light pass splits triangles near the light (952,816 split, 2.79 M pieces added); cut edges that don't meet the neighbour's edge exactly show as cracks [U] | open |
| Plasma projectiles have no tracers | Not looked into (contrails aren't inspected yet) | new |
| Needler impacts flash the screen solid pink | Magenta is the colour of a missing texture or the clear colour; likely a screen effect drawn with no texture [U] | new: reproduce in the emulator |
| A gradient across terrain and wall textures | Not explained yet: 16-bit colour or the 4-bit palettes are the candidates [U] | new: needs a screenshot |
| Floor light through the left hallway wall entering the map room bunker (T≈1850) | The light pass painting over what's in front of it, the class b89 fixed by distance bands [U] | open |
| The Warthog flip prompt shows only briefly, once | Not looked into; the prompt's timing may count frames where the Xbox counts ticks [U] | new |
| Active camouflage: the needler's needles stay visible | Camouflage draws the biped faint (b30); attached projectiles aren't part of it [U] | new |

## Tests A and D, without capture (evening, 2026-10-09)

Two short runs of b30, from the Pelican flight to the Warthog drop (game time about 6,000 each), keep words `nearauto assettrack hwprofile geo2`, deathless, no USB capture [M]:
- **A:** b108 as shipped (`halo_psp_b108.zip`, heap 6,080 KB).
- **D:** the same code with the hot engine at `-Os` (`halo_psp_b108_os.zip`, heap 6,400 KB).

Both clean: no crash, 0 heap requests failed, heap peak about 4,970 KB in use.

`log.txt` is removed at every launch (start-up in psp_platform.c); `profile.txt` appends. Pull the log after each run.

### `-Os` loses: the hot code stays at `-O2` [M]

| Game time | A (`-O2`) tick / frame ms, fps | D (`-Os`) tick / frame ms, fps | D against A (tick, frame) |
|---|---|---|---|
| 150-2,600 (the flight, the landing: scripted) | 60.2 / 129.6, 4.0 | 78.4 / 139.5, 3.4 | +30%, +8% |
| 2,600-6,200 (the beach: hand-played) | 35.3 / 86.1, 7.1 | 46.2 / 103.0, 5.8 | +31%, +20% |

- D is slower in every stretch (`abcompare.py`, six windows: tick +9% to +40%). The game logic carries the loss: `objects_update` +30%, `ai_update` +28% (ms a frame). Model drawing and the GE renderer's CPU are within ±4%.
- The caches did worse, not better. Over the flight, per million cycles: instruction cache misses 4,000 → 4,373 (+9%), cached stores 24,603 → 28,112 (+14%); data cache misses fell 2,184 → 1,954 (−11%).
- Why [U]: `-Os` turns off GCC's code alignment and its hot-path block layout (`-freorder-blocks-algorithm=stc`), and inlines less. The code is smaller, but the hot paths spread over more cache lines, and the extra calls save and restore registers (the extra stores).
- The 320 KB of heap `-Os` frees doesn't pay for 30% on the tick. The level-stream arena solves the heap exactly.

### What the capture cost: little [M]

Over the flight (scripted, so comparable across runs), A against run 1 with capture: tick 60.2 against 60.4 ms, frame 129.6 against 132.6 ms (−2%). b108's flight is about b107's (61.7 / 129.2). The security room's difference (the bodies, above) stands.

## Next

1. The level-stream arena (above): the one fix this run makes urgent, and b110 needs it too.
2. Keep-word checks on the PSP, still to run: `nosizefloor` for the trees (B), `sceneryhalf` (C), the flares with objects in the occlusion test.
3. Reproduce in the emulator: the needler's pink flash, the missing plasma tracers, the brief flip prompt.
4. Settled: `-Os` on the hot code (stays `-O2`); the USB capture costs about 2% of the frame, but runs stay without it.
