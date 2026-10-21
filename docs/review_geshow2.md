# Review: the frame shown off the game thread (S10a; keep words `geshow2`, `geshow2nocopy`, `showlat`)

With M6 #2 on (`gedefer geinflight gemeon`), the game thread waits at the next frame's first call (`psp_ge_window_begin` -> `inflight_sync` -> `inflight_wait_me`) for the Media Engine to end frame N. Then, in `frame_end_show`, it waits for the GE's tail (`sceGuSync`) and the swap, and only after that, in `window_begin_main`, does it start the one display list for N+1 and record it. On d40, b40's slow stretches and a10's opening the draw lane binds, so the frame is serial: R + max(T, M + G) + rest (frames_in_flight.md §4b). `geshow2` moves the GE's tail, the show and the list's start to the existing "ge forward" thread, so the game thread records N+1 while the GE finishes N. That's one record buffer, one list and no new memory. It's **off by default**: it needs `geinflight` and `gemeon` (on by default in the campaign build), and it changes input latency, which is Sheepdog01's call. Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## The change (`psp/src/psp_ge.c`, `psp/src/psp_tex.c`)

- **`show_picture()`**: the old block of `frame_end_show` from `sceGuSync` to the swap statistics, moved into a function as it was: the GE wait, the blip check, `framecrc`, the overlay, a shot, the swap, and the refresh counts. Without `geshow2` it runs where it did.
- **`frame_end_show`**: the finish and `vm_frame_end` stay first. Then, under `geshow2` (`show2_now`, set by `inflight_show` once the forwarding thread exists), `show2_ask()` wakes the forwarder instead of calling `show_picture()`. The rest (`psp_tex_frame_end`, `psp_geo_frame_end`, the wanted lists, trim, arena, follow) runs on the game thread as before. The frame's number and its shot request are taken at the ask (`show_frame_number`, `show_shot`), because N+1's start can ask its own shot before N is shown.
- **The forwarder** (`inflight_forwarder`, a stack of 0x4000 under `geshow2` for the log lines and a shot's file writes) runs `show_picture()` and then `show2_list_start()` (`sceGuStart` + `list_cached_frame_start`, the lines `window_begin_main` ran). It then clears `psp_ge_show_busy` and signals the `ge show` semaphore. `window_begin_main` skips the start when the show has made it (`show2_list_started`).
- **`show2_settle(why)`** waits for that signal, at most once per show, before anything of the game thread's own reaches the list or moves what the GE reads (the counts are on the `GE: geshow2:` line):
  - `inflight_hand_over` (N+1 goes to the ME only once its list is started and the swap has freed its buffer);
  - `defer_drain` (calls that run at once);
  - a window that isn't recorded, and `psp_ge_frame_end`'s serial path;
  - `psp_ge_text_changed`;
  - `arena_compact`, and `level_copies_follow` when copies move.

  `show2_list_drop()` finishes an unused started list before a loading frame starts its own.
- **What the GE may still read while N+1 is recorded**, kept while `psp_ge_show_busy` is set:
  - textures drawn last frame: `load_into`'s last-resort `recent` is 2, not 1;
  - level copies drawn last frame: `level_stream_let_go_oldest` passes them over and counts them as `spared`;
  - **the engine's index lists**: a level draw that dropped no triangle handed the GE `dynamic_triangles`' own buffer, which the next walk rewrites. Under `geshow2` the replay copies it into the list first. `geshow2nocopy` leaves it unprotected, for diagnosis only.
- **`showlat`** (a separate word, for both sides of an A/B): each frame's time from its window's start (ticks done, camera set) to the swap that shows it. It goes on a `GE: showlat:` line every 300 frames and on the heartbeat's `frameend` line.

## §7.3's ordering items, and how each is kept [D]

1. **The list starts after the GE's end, and the swap comes before N+1's first command.** `sceGuStart` runs on the forwarder after `sceGuSync` and the swap. The ME gets N+1 only after `show2_settle` at the hand-over. What N's list part reads from the main part needs no carrying: the main part of N+1 (`window_begin_main`: origin, near plane, shots) runs before its own record, and N's list part has already been replayed by the ME before the show.
2. **`vm_frame_end` runs only while the GE and the ME are idle with respect to it.** It's unchanged: before the ask, with the ME done with N. The GE may still be drawing N there, as it was before (`vm_frame_end` always ran before `sceGuSync`).
3. **What the GE reads by address:** textures and level copies are covered above. Geometry blocks: `RECENT_FRAMES` 2 already protects the last frame's blocks, and only one frame is ever out with the GE. Decals are copied already. The engine's dynamic index lists are copied, as above. Model indices are geometry-cache blocks or tag data [D]. Not audited: whether a light or shadow draw ever hands the GE an engine pointer. Both build their own lists as far as I've read.
4. **The wanted lists stay on the game thread**, in `frame_end_show` after the ask, as before.
5. **Calls that run at once wait for the show** (`defer_drain`, text changes, load frames, the serial frame end). An ME failure goes through `inflight_wait_me` -> `inflight_show`, with no show still owed.

## Evidence

**Exactness [E]** (emupool; EBOOT dc67317, PGO campaign-w140c, bake b140x; `nearauto assettrack geo2 fixedticks fixedseed statecrc framecrc nomouth showlat exitat200`, hands off, b30 on its route; every run reached game time 5,893-5,994):
- Play and both random seeds are **identical at all 6,000 ticks** on d40, b40, a10 and b30, plain against `geshow2`. The whole state differs only in `objects`, `object looping sounds` and the HUD, which follow the emulator's speed (as in every earlier check).
- Pictures differing out of ~2,990 common frames:

| map | plain / plain | plain / geshow2 (2 pairs) | geshow2 / geshow2 | geshow2nocopy / plain |
|---|---|---|---|---|
| d40 | 5 | 35, 34 | | 12 |
| b40 | 1 | 9, 8 | | 2 |
| a10 | 2 | 117, 119 (122) | 7 | 4 |
| b30 | 515 | 548, 503 (505) | 522 | |

  - b30 is inside its own spread.
  - d40, b40 and a10 differ beyond it, and the same way every time (`geshow2` against itself: a10 7).
  - The texture and geometry counters are identical on both sides (loads, evictions, untextured frames), so the cause isn't a texture or copy arriving a frame later.
  - With `geshow2nocopy` the pictures come back within about the plain spread, so **the index copy is what changes them**. How identical indices, copied into the list's cached room, draw differently in the emulator is **not yet found** [U]. Suspects: the cached-line creation (`list_take`'s 0x18 lines) under the replay, or the copy coming before `angle_sort`. Until it's explained, `geshow2` is not proven picture-exact, and default-on waits for it. It's harmless while off.
- The emulator's ME is simulated: the job runs inside the hand-over, so the hand-over waits out the GE's tail on the game thread there. Neither the frame time nor `showlat` from the emulator prices the change. d40's `showlat`: 64-69 ms plain, 70-75 `geshow2`, in emulator milliseconds.
- Index copies: d40 22 draws a frame, ~27.5 KB of list a frame; b30 ~12.6 KB; b40 0; a10 ~1.7 KB. They stay well inside the 768 KB list (d40 168 -> 197 KB a frame).

**Compiles:** campaign PGO (build2), `PSP_MULTIPLAYER=1` and `PSP_METRICS=0` (build) of dc67317; the merge with w140 bc12443 (015c471, build). `pgo_mismatch.txt` lists the changed psp_ge.c functions (`psp_ge_draw_level_body`, `frame_end_show`, `defer_drain`, the forwarder and others): **a strict release link needs a new profile**. The PSP pairs run with `allowmismatch=yes`. Both sides run one EBOOT, so the A/B is fair.

**PSP [M]** (PSP-1000, EBOOT 015c471, `nearauto gebusy assettrack geo2 nomouth showlat exitat120`, `keepsets "- | geshow2"`, repeat 2: on, off, on, off; every run exited in the game at game time ~3,570; the repeats agree within 0.1 ms):

| d40, by game time | frame, plain -> geshow2 | drawing | tick |
|---|---|---|---|
| 150-570 | 43.9 -> 38.6 ms (22.8 -> 25.9 fps) | 38.7 -> 25.0 | 1.3 -> 1.4 |
| 300-2,000 | 41.4 -> 38.6 (24.2 -> 25.9 fps) | 31.4 -> 22.3 | 2.7 -> 2.7 |
| 2,000-2,700 | 35.0 -> 34.2 | 17.1 -> 13.8 | 4.9 -> 4.7 |
| 2,700-end | 33.7 -> 33.4 | 16.0 -> 12.6 | 4.4 -> 4.2 |

- **-3 to -5 ms where d40 is slow, not to the 30 fps cap.** On the PSP-1000 the GE's tail after the ME is 16-18 ms a frame (plain's `waiting for the GE`) and the ME's job ~15-16 ms. Once the record hides inside that tail, the frame is M + G ≈ 38.6 ms, §7.3's model with this PSP's G; §7.3's 33 ms came from the PSP-3000's figures. In the slowest window the game thread waits 11.4-11.7 ms a frame at the hand-over for the show: the GE's tail outlasting the record.
- **Latency (`showlat`, window start to swap, 300-frame lines):** plain 36.9 / 46.3 / 39.7 / 34.9 / 33.2-36.7 ms; `geshow2` 41.3 / 63.0 / 60.5 / 40.7 / 35.7-39.8. That's **+17 to +21 ms in the slow stretch** and +2-3 ms where the frame is already at 30 fps. That's more than §7.3's +8 ms, because N's ME now waits for the GE's whole tail of N-1.
- So on the PSP-1000 d40 trades ~+18 ms of latency for ~+3 fps in its slow stretch. That is Sheepdog01's call; it stays off. A gain that reaches the cap needs the GE's tail itself shorter (S10c can't beat M + G).

## Costs and risks

- The record now runs while the GE draws N's tail, so on the PSP the two share the bus [U]. The PSP pair prices it.
- `level_copies_follow` waits for the show whenever copies move: 150 of 300 frames on b30's flight. b30 is tick-bound, so the wait finds the GE done [U].
- On frames whose record is shorter than the GE's tail, the hand-over waits the rest: frame = max(R, G) + max(T + rest, M), §7.3's S10a model.
- Latency: N is shown at the GE's end as before, but N's ME may start late by up to the GE's tail of N-1 minus N's record. §7.3 puts that at +~8 ms on d40 [U]. `showlat` measures it.
- Shots and the debug overlay are drawn on the forwarder (priority 0x10, above the game thread).
