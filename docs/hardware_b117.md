# Build 117 on the PSP-1000 (2026-10-12)

Sheepdog01's PSP-1000 at 333 MHz, `halo_psp_b117.zip` (EBOOT `83f3037a…765b`) with the `campaign116f` maps. The tests are listed in `build_b117.md`. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

In so far: test 2 (the ladder), in two launches: from a10, and from c10 (`b117_test3.tgz`).

## In short

- **The game runs out of file handles at its sixth map in one session** (test 2) [M]. Every map's file stayed open after the next one loaded; at b40 the log lost its handle, and at c10 → c20 the next map couldn't be opened, back to the XMB. A playthrough in one sitting would hit it too. Fixed for 118 (`w118` `8697bbc`, below), checked in the emulator with the PSP-1000's handle limit [E].
- **The checkpoints are fast again** [M]: b30's first in 181 ms, against b116's 3,102 and b115's 219.
- **R4 holds on the PSP** [M]: draws are left out only as each map loads (a10 16, a30 102, a50 39, b30 0, b40 0; c10 111, c20 11, c40, d20 and d40 0), none in play.
- **Every campaign opening has a PSP baseline now** except b40's (its log was lost to the handles) [M]: from 3.8 fps (b30's landing) to 14.5 (a10).

## Test 2: a10, preset 2 (the ladder, 90 s a map)

Keep words `nearauto gebusy assettrack geo2 iostats ladder90`. Sheepdog01 saw a10, a30, a50, b30, b40 and c10 play, then the XMB. No crash, no `crash.txt`.

**Why it stopped** [M, D]:
- The engine keeps six slots for map files (`NUMBER_OF_CACHED_MAP_FILES`), and the port's slot opens the map's file when the map is first asked for (`psp_fixups.py`, `cached_map_files_find_map`). A slot closed its file only when the slot came round again.
- At b40's start the log reported `OUT OF FILE HANDLES` (0x80020320): `a10.map` to `b40.map` all open, with the geometry and texture packs, `stderr.txt` and the save file: 9 handles, a peak of 10. The PSP-1000 refuses its 13th descriptor (b104).
- From then the log's own handle was gone, so the log ends early in b40. At c10 → c20 the map's file couldn't be opened, and the game went back to the XMB as for a missing map.

**The fix** (`w118` `8697bbc`, keep word `mapfilesopen` for b117's): before a map file opens, every other slot but the open map's is closed and forgets its header, so at most two map files are open.
- **Checked** (`ladderfd118`, lab run 38343032541) [E]: the lab's emulator now hands out a PSP-1000's descriptors (`PSP_MAXFD=13` for every run with the PSP-1000's memory; it allowed 64 before, which is why `ladder117` went through all ten maps). b117 and `w118` with `mapfilesopen` run out of handles at b40 and then crash in the emulator; `w118` plays all ten maps and exits ("ladder done").

**Each map's opening** (the second half of its 90 s, from the heartbeat lines) [M]:

| Map | Frames a second | Frame (ms) | Tick (ms) | Game speed (ticks a second) | Draws left out (all as it loaded) | First checkpoint |
|---|---:|---:|---:|---:|---:|---:|
| a10 | 14.5 | 55.7 | 6.9 | 24.8 | 16 | 162 ms |
| a30 | 6.6 | 122.4 | 11.9 | 12.9 | 102 | 124 ms |
| a50 | 8.8 | 42.6 | 33.1 | 17.6 | 39 | 149 ms |
| b30 | 3.8 | 137.2 | 58.9 | 7.6 | 0 | 181 ms |
| c10 | 8.5 | 62.6 | 25.8 | 16.7 | 111 | 194 ms |
| c20 | 7.8 | 69.5 | 26.7 | 15.2 | 11 | 190 ms |
| c40 | 12.6 | 41.5 | 16.7 | 25.0 | 0 | 120 ms |
| d20 | 7.3 | 100.9 | 16.9 | 13.4 | 0 | 135 ms |
| d40 | (its heartbeats lost with the log's handle) | | | | 0 | 182 ms |

- **The second launch** (from c10, `b117_test3.tgz`): c10, c20, c40, d20 and d40, then "ladder done". At d40's start the handles ran out again (c10.map to d40.map open), but the game had nothing more to open; the log kept a few lines through its reserve.

- b30's 90 s end during the landing's fight; the budget line there: 228 ms a frame by the clock, ticks 85 ms (1.9 a frame), the GE renderer's CPU 76 ms.
- a50's time is in the tick (63 ms of its 109 ms frame by the clock); a30's in drawing (the GE renderer's CPU 76 of 122 ms).
- c10, c20 and b30 spend about as long in the tick as drawing; d20 and a30 are drawing-bound (the GE renderer's CPU 64 of d20's 102 ms frame).
- **Audio:** the Media Engine mixed every block, none failed; one voice ran dry once (29 ms) in b40's report, none in the second launch.
- `d40.pstn` read as 64 bytes ("unreadable"): d40's stencil file holds no walls [U, to check against the bake].
