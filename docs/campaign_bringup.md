# Bringing up a10, c10, c40, d20 and d40 (2026-10-08, b107 dev tree)

**Headline:** the five maps that fit the PSP-1000's window all load and play their openings in the emulator. a10 found three bugs on the way, and the worst of them would also hit b106 on b30 at its BSP switch. All three are fixed in the dev tree. The other thing all five show is a sound cache that's too small for campaign maps.

Labels: **[E]** emulator, **[D]** read in the code, **[U]** inference.

## How

- **Bake:** each map with the b106 bundle's flags (`bake.py`: `-M -Y 512`, keyframing, texture and decal bakes), one at a time.
- **Build:** b107r–s (the campaign-only engine, the data trims and these fixes), on the PSP-1000 layout (the emulator set to 32 MB).
- **Keep words:** `assettrack geo2 nearauto`. Deathless (`commands.txt`).
- **Input:** a generic script (`bringup_input.txt`): idle for 60 s through the opening, then walk, turn, fire in bursts and throw a grenade now and then, for 700–800 s of wall time.
- **Screenshots:** c40 and d40 again with screenshots along the way (select + right).

## Results [E]

| map | tags | animation cache in the window gap | heap peak | tick / frame (emulator) | sound loads failed for room | lens flares a frame (peak) | fullest pool | result |
|---|---|---|---|---|---|---|---|---|
| a10 | 5.14 MiB | 64 KB | 4,923 KB | 2.4 / 21.4 ms | 116 | 348 | hs thread 41% | after the fixes: opening and cryo bay, 10,435 ticks |
| c10 | 5.58 MiB | 72 KB | 4,360 KB | 14.9 / 19.0 ms | 2,139 | 18 | object render states 100% | 9,594 ticks, clean |
| c40 | 5.40 MiB | 248 KB | 3,875 KB | 4.3 / 9.2 ms | 15 | 78 | hs thread 38% | 10,740 ticks, clean |
| d20 | 4.57 MiB | 288 KB | 4,549 KB | 9.5 / 17.9 ms | 6,744 | 68 | object render states 100%, objects 72% | 6,640 ticks, clean |
| d40 | 4.84 MiB | 512 KB | 3,840 KB | 4.2 / 28.9 ms | 11,043 | 12 | hs thread 57% | 11,476 ticks, clean |

Notes on the table:
- **Heap:** the heap is 6,080 KB in b107 (b106: 5,376). a10's 4,923 KB peak would also fit b106's.
- **Game state:** 3,283 KB of 3,348 on every map.
- **Openings only:** the runs cover the openings, not the late fights.
  - a10's input never passes Keyes's look-up/look-down test.
  - c40's walks the player off the Control Room's platform.
  - On a10 and c40 the input's first button presses skip the opening cinematic, which restores the save made at its start (game time goes back to ~40).

## The three bugs a10 found (all fixed; b107_candidates.md item 11)

1. **A BSP switch filled the animation cache's pages with 0xCD** (the crash, 40 s in, b106 too). Every BSP load filled the tag window's free part with the Xbox debug build's fill. That part now holds up to 512 KB of animation cache pages (the "window gap" column), so the next keyframe read went through 0xCDCDCDCD offsets.
   - The fill is gone on the PSP.
   - On b106 the keep word `animnowindow` avoids it, and Sheepdog01's b106 runs use it. b30 would hit it at its BSP switch [U].
2. **A cinematic's recorded movement read a short from an odd address.** The PSP faults on that; the emulator's JIT doesn't, so only the strict interpreter saw it. The struct is now read a byte at a time. [E] The strict interpreter now gets past the spot (a10 to game time 1,960 and the cinematic skip); b30's opening is clean to 1,309 ticks on b106.
3. **The animation cache could take a range across its two blocks as a hit**, reading past the first block's end. Not seen to fire. Fixed on the way.

`tools/animcheck.py` checks a baked map's compressed animations as the engine reads them. a10's 628 pass, so the map data itself was sound.

## What the five maps say

- **The sound cache is too small for campaign maps.** It is 1,152 KB. In their openings alone, d40 failed 11,043 sound loads for room and d20 6,744 (b30's beach: 1,035). A failed load is a sound that doesn't play. b107's heap has the room to double it, if the space doesn't go to the tag window instead.
- **The object render state cache fills** (256 of 256 on c10 and d20, as on b30): b107_candidates.md "looked at, not done".
- **c40's Control Room light shafts draw as large opaque white quads** (b106 too: the "wild node matrix" lines, scale over 100). A rendering issue to look at, not a crash.
- **Lens flares:** up to 348 submitted a frame on a10, 318 on The Library, so the 1,024 kept (40 KB) stay for now. d40's 4,616-marker BSP wasn't reached.

## Next

- ~~a50, b40 and a30 don't fit the window yet.~~ They do in the full campaign bake (2026-10-09): all ten load and play their openings on b115 (`campaign_window_fit.md`, 2026-10-11).
- Deeper routes per map (a teleport into each BSP), so the late fights' pools and timings get measured, not just the openings'.
