# Build 111 on the PSP-1000: b30 to the level's end (2026-10-10)

Sheepdog01's PSP-1000 at 333 MHz. Keep words `nearauto assettrack hwprofile geo2`, deathless (`commands.txt`), no USB capture. b30 from the start to the level's end: game time 36,308, about 41 minutes of real time (the log's file times). Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

The question this run had to answer (`build_b111.md`): does b30 play past the hunter fight, where b108 began to fail, with no `GE: no memory for a material's vertices` lines? **Yes.**

## The heap held [M]

| | b108 run 1 | b111 |
|---|---|---|
| Requests failed | 682 by game time 29,737; 12,915 by 41,151 | **0** |
| `no memory for a material's vertices` lines | 5,290 (from game time ≈29,700) | **0** |
| Heap taken from the system, peak | 6,078 of 6,080 KB (by game time 28,000) | **5,047 of 5,952 KB** (game time 30,923) |
| Heap in use, peak | 5,746 KB (game time 26,467, the highest in `hardware_b108.md`'s table) | 4,897 KB (game time 32,265) |
| At the end | 5,468 KB in use, 610 KB free in 347 pieces | 4,491 KB in use, 483 KB free in 143 pieces |

- **The copies' arena** at the end: 1,013 of 1,056 KB used. Over the run, 1,362 copies were let go to make room, and 364 pieces came from the general heap when a view's own copies filled the arena (none were left there at the end). It was compacted 41 times (35 MB moved) and 76 copies were brought back from the heap.
- **The skinning plans** peaked at 502 KB. One sweep forgot 368 dead entries, leaving 127 KB at the end.
- The level ended as designed: `level finished: the next map, levels\b40\b40, isn't on the memory stick`, then `exiting` to ARK-4's menu.

So the general heap no longer fragments in a long level, and the peak leaves about 900 KB of the heap untaken.

## Speed against b108 [M]

`tools/abcompare.py`, A = b108 run 1, B = this run. b108 run 1 had USB capture on until about game time 40,000, and both runs were played by hand, so only the scripted flight compares fairly.

| Game time | b108 tick / frame ms, fps | b111 tick / frame ms, fps | b111 against b108 (tick, frame) |
|---|---|---|---|
| 150–2,600 (the flight, scripted) | 60.4 / 132.6, 4.0 | 61.4 / 127.8, 4.1 | +2%, −4% |
| 2,600–8,000 (the beach) | 38.7 / 95.6, 6.7 | 37.5 / 92.0, 6.3 | −3%, −4% |
| 8,000–15,000 | 26.9 / 63.2, 9.3 | 18.3 / 62.5, 12.1 | −32%, −1% |
| 15,000–22,000 (the security room) | 18.6 / 70.2, 10.4 | 28.2 / 77.1, 9.3 | +51%, +10% |
| 22,000–30,000 (the map room) | 30.5 / 67.5, 9.0 | 24.1 / 48.3, 10.3 | −21%, −28% |
| 30,000–36,400 | 23.3 / 58.5, 11.6 | 26.3 / 85.1, 8.0 | +13%, +45% |

b111 changes memory, not speed. The flight matches b108; the hand-played stretches differ with how each fight went.

## What Sheepdog01 saw

| Seen | What we know | Status |
|---|---|---|
| One flicker on entering the map room bunker's third level | The player reached that level at game time ≈24,600 (z −5.2, from the `player:` lines). The report covering 24,470–24,990 has 1 frame that drew something untextured, 17 texture requests that waited a frame, and 21 copies' pieces from the heap (the new view's copies filled the arena) [M]. Most likely flicker: surfaces whose texture wasn't read yet, drawn without it for a frame. A frame reads at most 96 KB from the stick (`LOAD_BUDGET_BYTES`), so a view that brings many new textures at once waits [U, the timing matches]. Over the run: 57 such frames, the longest run 12 | `b113_candidates.md` #15 |
| Covenant weapons' holographic displays missing in first person | **The GE never draws meter (type 9) or plasma (type 10) see-through parts.** `psp_transparent_map` handles types 5–8, and a part with no map is dropped silently, with no counter [D]. That these displays use meter or plasma shaders is [U] (the tags settle it). The same parts would be missing in third person, if those models carry them [U] | `b113_candidates.md` #16 |
| The camera clips through the Pelican on the bench seats | **The near plane went out to 16 × the game's in half of the flight's first 300 frames** (152 at ×16, 148 at ×2) [M, the `near plane` line]. Seated, a model sphere that contains the eye doesn't cap the plane (b90, meant for the driven vehicle), and in the air the level test finds nothing. So the plane sits at 1.0 world unit, inside the cabin, and the cabin's own triangles are cut at it [D] | `b113_candidates.md` #17 |

## Next

1. main moves to b111 (this run was its test).
2. b112's tests on top of b111 (`b112_candidates.md`; the ceiling bench and Test 3 are in `big_levers.md`).
3. The three sightings above join the b113 list.
