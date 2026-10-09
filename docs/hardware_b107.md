# Build 107 on the PSP-1000: b30 to the level's end (2026-10-08)

Sheepdog01's PSP-1000 at 333 MHz. Keep words `assettrack geo2 nearauto`, deathless; the b106 bake of b30. **The first campaign level played start to finish on the PSP**: game time 46,525 (about 26 minutes of play). Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code or the tags, **[U]** inference.

## What happened

- The landing, the beach, the security room, the facility (the BSP switch into b30b), the map room, the closing cutscene [M].
- Stable all the way; the old crash at 88 s (b106) is gone [M].
- At the level's end the game asked for b40, which isn't on the stick, and crashed in the error loop that followed (below). Fixed for b108 [E].

## Speed [M]

| | b106 run 1 | b107 | change |
|---|---|---|---|
| Same stretch (game time 150-2,600: the Pelican flight and the beach) | | | |
| fps, mean | 3.82 | 3.86 | +1% |
| tick ms, mean | 72.3 | 61.7 | −15% |
| frame ms, mean | 110 | 129 | +17% |
| The whole run (b107 only) | | | |
| fps, mean (10th percentile) | | 9.0 (3.9) | |
| ticks/s, mean (30 is full speed) | | 15.0 | |

- The ticks got cheaper (`objects_update`, `ai_update`), and the frame got dearer: models that b106 skipped for room are drawn now. Same as the emulator said (build_b107.md).
- The beach is the slow part (about 4 fps). The facility runs 6-12 fps.

## Memory [M]

- Heap top 5,956 KB of 6,016 at its highest (60 KB to spare); 5,893 KB in use at most. User RAM outside the heap: 65 KB free.
- Sound cache (1,536 KB): 96 loads failed for room over the whole level (b106: 204 in its 88 s).
- The game state: 3,283 of 3,348 KB.

## What Sheepdog01 saw, and what the log says

### The camera under the map, the look turned over (fixed for b108)

**Cause [D] [M] [E]: b30's floors are mirrors.**
- The tags: the security room's floor is a mirror at z 9.95 (BSP b30a, cluster 48); the map room's at z −14.92 (b30b, cluster 14).
- With a mirror in view, the Xbox renders the level twice: first from the mirror's camera (the player's camera reflected through the floor) into a second target, then the player's view, with the first laid over the mirror's surface (render.c `render_player_frame`).
- The PSP draws one window a frame, from the first window's camera (`psp_ge_window_begin`). So the whole frame came out from under the floor, with the look's pitch turned over. Hence "specific positions and specific look directions": wherever a mirror floor is in view.

**The proof:**
- Sheepdog01's screenshots: 10 of the 101 have the camera at z 9.28 where the others in that room have 10.62, each 0.67 from the floor's 9.95. In b30b, 2 more are at −15.59, the reflection of −14.24 through −14.92.
- The emulator at shot 21's spot: b107 shows camera z 9.28 [E]; b108 shows 10.62, looking the right way [E].

**The fix:** no mirror window on the PSP (psp_fixups.py FIXES_108 `render.c#mirror`). The floors draw as they do on the Xbox when no mirror is found.
- It's cheaper too: at that spot the frame's CPU work went 21.5 → 6.9 ms, and the display list 207 → 83 KB, in the emulator [E]. The rooms were drawing the level twice.
- The frameend: line now counts any window for another target than the screen, should one come back.

### The crash at the level's end (fixed for b108)

**What happened [M]:**
1. `couldn't find map 'b40' on the DVD`.
2. The game's "damaged disc" path: an error dialog and `main_loop_of_death`, which renders frames forever.
3. Its first frame ran nearauto's `psp_player_seated`, which read the disposed player's unit (object index #906): asserts in data.c, then a bus error in `object_get_and_verify_type`.

**Reproduced [E]:** b107 plus `game_won` crashes at the same address with the same object index.

**The fixes:**
- **The run ends at the level's end** when the next map isn't on the stick: `psp_next_map_check` logs `level finished: the next map, levels\b40\b40, isn't on the memory stick` and exits to the XMB. Checked [E].
- **The PSP helpers that read the player** (heartbeat, teleport, nearauto, benchrun, colltest) now answer "no player" unless a game is in progress (`psp_local_unit`). nearauto tests the level only in a game.

### Very dark (open)

- Lightmaps multiply in once, as on the Xbox (build 35) [D]. Objects are lit from the real lightmaps since build 102.
- So the darkness may be faithful: b30's interiors are dark, and the PSP-1000's screen is dim with no gamma control [U].
- **To tell:** a reference screenshot of the same spots (Xbox, PC or MCC) next to the PSP's.
- If it's only the screen, the cheap fix is a brighter curve baked into the lightmap palettes. That costs nothing per frame [U].

### The water (open)

- **Late to appear:** the texture cache waited on 681 requests and drew untextured frames, up to 12 in a row. At 4 fps that's about 3 s [M]. The ocean's texture is probably one of the late ones [U].
- **Flat:** one layer is drawn; the Xbox's reflection cube map is passed over (build 107) [D].
- **Jagged edges:** 480×272 with no anti-aliasing [D].

### Objects disappearing in a few scenes (open)

- In the Pelican flight the display list still fills: 35.6 model part draws a frame were left to the size floor (up to 8 px). This is a known b108 item [M].
- In the mirror rooms the list held two scenes (above) [E].
- Where else Sheepdog01 saw it would help.

## Fixed in source for b108 [E]

| | Where |
|---|---|
| No mirror window | psp_fixups.py FIXES_108 `render.c#mirror` |
| The run ends when the next map is missing | FIXES_108 `main.c#nextmap`, `psp_next_map_check` (psp_platform.c) |
| Player helpers only in a game | psp_fixups.py players.c (`psp_local_unit`, `psp_game_running`) |
| nearauto only in a game | psp_ge.c `psp_ge_window_begin` |
| Windows for another target counted | psp_ge.c `psp_ge_window_target`, the frameend: line |

Six b106 screenshots (beach, labelled b106) were still on the stick and came along in this run's tgz.
