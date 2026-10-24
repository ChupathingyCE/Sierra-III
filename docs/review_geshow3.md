# Review: the next frame replayed into a second display list (S10a's variant; keep words `geshow3`, `geshow3check`)

Under `geshow2` (review_geshow2.md) the frame is shown off the game thread, but the ME's replay of N+1 still waits for the GE's end of N: the hand-over waits for the forwarding thread to see the GE finish N, swap and start the one list again. On the PSP-1000 that puts the ME's job and the GE's tail in series, M + G' = 15.6 + ~23 = 38.6 ms on d40 [M] (rank_pipelined.md §7.3, the re-price). `geshow3` lets the ME replay N+1 into a **second list**, in a part of the same 768 KB display list, while the GE draws N's tail. The game thread's cycle becomes R + max(T, M) + rest and the GE's its own work, priced at ~26-31 ms on d40 (the 30 fps cap) with about plain's latency [U]. There's no new memory. It's **on by default in the campaign build** (Sheepdog01, 2026-10-22 00:28; `geshow3off` turns it off, `m6off` with the rest of M6 #2): it needs `geinflight` and `gemeon` (on by default too) and uses geshow2's forwarding thread. Its index ranges come from geshow2's ring of engine triangles (idxring, review_geshow2.md), which waits for the show of the frame whose range it would reuse, so the GE never reads a later frame's triangles; that wait holds under geshow3 because `show3_ask` sets the same `show2_owed` the ring's wait settles, and the settle returns only once the GE's finish of that frame has been seen. Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## The change (`psp/src/psp_ge.c`, `psp_trace.c`, `psp_tex.c`, `psp_geo.c`)

- **The list's start goes back to the game thread** (`show3_list_begin`, from `window_begin_main`). If N is still out with the GE, N+1's list starts in the larger part of the display list that N's list (`show3_list_from`..`show3_list_to`) doesn't use. The part must hold the largest list lately plus a quarter and 32 KB (at least 192 KB). If it doesn't, the game thread waits for N's show and takes the whole list, as before (`for list room` on the `GE: geshow2:` line).
- **The limits stay the whole list's** (`list_bytes` is the display list's size in a part too), so the same draws are skipped for them as without geshow3. The part's own end is `show3_room`'s, asked after the limits pass in `list_full` and `list_take`: a list that outgrows its part waits for the GE's finish of N (an uncached word the finish callback writes, `SHOW3_GE_WORD`; the ME spins on it serving audio, the main CPU sleeps 200 us at a time), then grows. A part below N grows into the rest of the list above it. A part above N writes BASE + JUMP to the list's start (as `sceGuGetMemory`'s jump over its region) and goes on there, up to its own start; `gu_list->start` moves back by what's used so `sceGuCheckList` counts the whole frame. The frame after a jump waits for its show (its list is in two pieces). A finish not reported in ~0.4 s skips the draw, counted. `parts grown up`, `by a jump` and `draws skipped for room in a part` are on the `GE: geshow3:` line; the last must be 0. The jump's stall address is below the GE's address until the GE reaches the jump: the emulator stops at an equal address and recomputes at a jump [D]; the PSP's GE is taken to do the same [U] (its pair counts the jumps).
- **The swap's pointers move when N's show is asked** (`show3_ask`, in `frame_end_show`). That's on the game thread, before N+1's list starts and before its replay reads `draw_buffer`. `sceGuSwapBuffers` runs with `psp_display_hold` set, so the `sceDisplaySetFrameBuf` wrapper keeps the display's address and doesn't set it.
- **N's finish carries an id** (`sceGuFinishId`, never 0). The GE's finish callback (`show3_finish_callback`, chained to gebusy's) records it and sets an event flag. The forwarding thread waits for that id, not for the whole queue: N+1 is in the queue already, so `sceGuSync` would wait for it too. It then hashes, overlays and shoots the buffer N drew into (`show3_buffer`), sets the display (`psp_display_release`), and marks N shown.
- **The stall gate** (`ge_stall_pass`, used by every main-CPU stall pass, the wrapper's included, and by the forwarder). A stall address for the list started while N was unshown is kept rather than passed until N is on the screen, so the GE can't start N+1 on the buffer that's still displayed. The forwarder passes the newest kept one after the show. The game thread keeps the address first and then looks at the show mark; the forwarder (above it) marks and then passes. So whichever runs between the two, the newest address reaches the GE.
- **The game thread takes N's show at the ME's end of N+1** (`inflight_show`, `SHOW3_ME_END`), before N+1's frame end lets anything go. So wherever something the GE reads is reused (the frame end, the record, the calls that run at once), only N+1 is unshown. geshow2's protections cover exactly one unshown frame, so they hold as they are: textures (allocate's last resort 2), level copies spared, index lists copied, the geometry cache's `RECENT_FRAMES` 2, the bind blocks' quarantine 2, and the VRAM copies' quarantine. `psp_ge_show_busy` now follows the game thread (set at the ask, cleared when the show is taken), so what is kept doesn't depend on the GE's timing. The settles at the hand-over and at the window's list part are geshow2's only. The rest stay: calls run at once, compaction, copies moved, text changes, the load's frames, the serial frame end.
- **The forwarder runs beside the ME now**, so it writes no pspgu state, and the variables the show writes (the GE wait, swap and shot counts, `frame_end_us`, `show_shot`) are `PSP_MAIN_SIDE`. It skips the two recon-only pieces that write what the replay writes (`vram_busy`'s calibration, `blipdepthcheck`'s compare and flag).
- **A finish never reported** (the wait gives up after 200 x 2 ms) is shown anyway and counted (`finishes not reported` on the `GE: geshow3:` line). It must be 0.

## The check word `geshow3check` [D]

At each hand-over the frame's frame counters are kept (the texture cache's for textures and bind blocks, the geometry cache's, the level copies' stream frame, the VRAM copies' frame). They're dropped when its show is taken. Each place that reuses something the GE reads counts a **reuse** when the thing's last use is stamped at or after the oldest frame still out:
- a texture evicted (`allocate`);
- a bind block refilled;
- a geometry block evicted (in the arena, or with its entry);
- a level copy let go;
- a VRAM copy's room given back (its last read stamped at its release at the latest: a released copy is never handed out, though later lookups still move its frame);
- a list started over the part of the frame out (`list room`).

Moves (compaction, copies following the origin) and glyph changes wait for the show first, so nothing is out when they run. The `GE: geshow3check:` line gives the totals since the start; 0 reuses is the proof. Draws skipped for room in a part of the list are on the `GE: geshow3:` line and must be 0 too (`draws skipped for room in a part`).

## Evidence

**Compiles:** campaign PGO, `PSP_MULTIPLAYER=1` and `PSP_METRICS=0` of the fixed branch: all success (`pgo_mismatch.txt` lists the changed functions, so a strict link needs a new profile).

**Emulator, first read [E]** (before the three fixes; readswait and nomouth on every run; d40, b40, a10, b30, 6,000 ticks each; a second plain on d40 and a10): **not ready for the PSP.**
- Play and both random seeds the same at every tick on all four maps; plain against plain again the same whole state.
- No finish went unreported (0 on every map). No stall address was ever held for a show (0): the emulator's GE ends each list before the next stall, so the gate's hold path is untested here.
- `geshow3check` is **not 0**: VRAM copies d40 1607 of 1655, a10 1616 of 1844, b30 102 of 538, b40 1 of 129; textures, bind blocks, geometry, level copies and list room 0 everywhere. [D] these look like the check's own error: a copy's stamp (`e->frame`) moves at every lookup, in `vm_redirect` before its released test and in `vm_me_fold` for any tallied entry, so a released copy that is still looked up (and drawn from the original vertices) is stamped with the current frame. No draw reads a copy after its release, and `vm_place` gives the room back only `VM_QUARANTINE_FRAMES + 1` frames after it. The fix to the check: stamp the reclaim with the release frame (`e->released - VM_QUARANTINE_FRAMES - 1`), or move `e->frame` only when the copy's address is returned. A rerun must then show 0.
- **Draws skipped for room** (must be 0): b30 123 (63 in the first 300 frames, 58 in the next 300, 2 later), a10 3. The part is sized from the recent peak, which starts low after loading, and a list that outgrows its part skips draws to the frame's end rather than waiting for the last show. a10 also skips more with the whole list full than plain does (plain 7,982 in all, geshow3 8,699; frames 1,500 to 2,400, the same stretch where its pictures differ).
- Pictures (frames hashed on the ME's end of the next frame lose their `framecrc` line to the ME's log, so only part of each run is compared: d40 376 of 2,991, b40 2,459, a10 2,365, b30 2,423): d40 2 differ (plain's own spread 3), b40 3 (game time 300-306), a10 115 against a spread of 1 (the same clusters as geshow2's index copy: 1214-1298, 3182-3342, 3750-4226), b30 89 (66-122 with the room skips, and scattered to 2,572; no second plain on b30). Nothing seen beyond geshow2's, but the room skips are a picture difference of geshow3's own.
- `showlat` (window start to swap, mean over the run) plain / geshow3: d40 67.6 / 71.8 ms, b40 74.4 / 78.7, a10 86.0 / 93.2, b30 125.2 / 139.5; the game thread's wait for the GE at the frame's end 4.5 / 2.7, 2.8 / 1.0, 6.4 / 4.8, 18.5 / 16.6 ms. The emulator's ME is simulated: neither is a PSP figure.
- Before the PSP: the check's stamp, the room's sizing (no skips), and the `framecrc` line logged from the main CPU. All three fixed since (the stamp at the release, the part grown rather than skipped, `framecrc` through the main CPU's log: the forwarding thread shows the frame while the simulated ME's job marks the work as the ME's); rerun pending.

**Emulator, the rerun with the three fixes [E]** (readswait and nomouth on every run; d40, a10, b40, b30, 6,000 ticks each, plain twice on every map; a10 also geshow2 alone; each map's runs on one host): **ready for the PSP.**
- Play and both random seeds the same at every tick on all four maps; plain against plain again the same whole state (b30's own varies in objects only).
- `geshow3check`: **0 reuses** of every kind on every map (VRAM copies 0 of 1,655 d40, 1,844 a10, 129 b40, 542 b30; list room 0 of 2,699 each).
- **Draws skipped for room in a part: 0** everywhere; finishes not reported 0; stall addresses held 0. Parts grown 0 and jumps 0 on every map: the emulator's GE ends N before a part runs out, so the grow and jump paths never ran here. Lists in a part / in the whole list: d40 2,603 / 96, a10 1,804 / 895, b40 2,699 / 0, b30 1,332 / 1,367 (largest 198, 689, 155, 574 KB).
- Draws skipped with the whole list full: a10 plain 7,982, geshow2 alone 8,696, geshow3 8,696 (geshow2's index copy, its own question); d40, b40, b30 0 in every run.
- Pictures (`framecrc` now on every frame; each against plain-vs-plain-again on the same host): d40 35 of 2,994 differ (spread 3), b40 8 of 2,997 (spread 1): both as geshow2 alone gives on those maps (d40 25-35, b40 8-9 in earlier runs), the index copy. a10: geshow3 122, geshow2 alone 121, geshow3 against geshow2 9 of 2,984 (spread 3). b30: 447 against its own spread of 422. geshow3 adds nothing seen beyond geshow2's.
- `showlat` mean plain / geshow3: d40 67.6 / 71.9 ms, a10 86.0 / 93.2 (geshow2 alone 91.3), b40 74.4 / 78.8, b30 125.4 / 139.6. The emulator's ME is simulated: a direction only.

**PSP-3000 [M]** (the fixed branch before the merge; `keepsets "- | geshow3"`, repeat 2, `gebusy`, `showlat`; every run ended in the game; frame by the clock, plain -> geshow3, each side's two runs within 0.2 ms):

| Map, game time | Frame, ms | Drawing, ms | Tick, ms |
|---|---:|---:|---:|
| d40 150-570 (slow stretch) | 43.7 -> 34.3 (22.9 -> 29.2 fps) | 38.3 -> 21.4 | 1.3 -> 1.9 |
| d40 300-2000 | 41.3 -> 34.5 | 31.4 -> 20.1 | 2.7 -> 3.1 |
| d40 2000-2700 | 35.4 -> 34.7 | 19.7 -> 14.6 | 5.3 -> 5.0 |
| d40 2700-3600 | 34.5 -> 33.4 | 22.0 -> 13.8 | 5.1 -> 4.5 |
| a10 300-2000 | 37.7 -> 36.1 | 17.8 -> 16.3 | 6.6 -> 6.8 |
| a10 2000-2700 | 45.8 -> 45.1 | 24.4 -> 23.9 | 9.7 -> 9.6 |
| a10 2700-4700 | 80.1 -> 79.6 | 38.6 -> 38.4 | 20.9 -> 20.8 |

- d40's slow stretch reaches the 30 fps cap: the ME's replay of N+1 no longer waits for the GE's end of N (geshow2 alone reached 38.6 ms there on the PSP-1000). a10 is bound by the CPU's lane, so it gains little.
- `showlat` (window start to swap), plain -> geshow3: d40 mean 36.4 -> 41.6 ms; its slow windows 46.0 / 39.6 -> 58.5 / 55.1 (+12 to +16), its 30 fps windows +1 to +3. a10 mean 54.7 -> 57.9 (+3 to +7 a window). Less than geshow2's +17 to +21 in d40's slow stretch, but not back to plain's: N+1 is recorded beside N's tail, so a frame's start comes earlier against its show. **Default-on is a call on this latency.**
- Counters: draws skipped for room 0, finishes not reported 0 on both maps. Stall addresses held for a show: d40 237 and 239, a10 2 and 0, with no hang: the gate's hold works on a real GE. Parts grown 0 and **jumps 0** on both maps (largest lists 198 KB d40, 684 KB a10): the jump path has still never run on a real GE. Until a run shows jumps above 0 and ends in the game, it is [U]; a hang there would mean the GE doesn't stop at a stall address equal to its PC while the jump is pending, and the jump would have to wait for the GE's finish instead.
- Draws skipped with the whole list full on a10: plain 8,008-8,033, geshow3 8,635-8,706, as in the emulator (geshow2's index copy).
- The PSP-1000 is the target; these are the PSP-3000's figures, which have matched the 1000's within ~3% a map on every build run on both.

**Merged with gebusy2's own GE callbacks.** With `gebusy2` on, every list gets gebusy2's callbacks instead of pspgu's, so pspgu's finish callback (geshow3's) is never called: gebusy2's finish callback hands geshow3 the finish id (`gebusy_ge_finish`). The wait marks gebusy2 puts round the frame end's GE wait sit in `show_picture`, where geshow3 moved that wait. geshow3 with `gebusy2` has not run yet.

## Costs and risks

- The GE's finish is waited for through its callback. If the firmware ever left a finish unreported, the show would wait 400 ms and go on, counted.
- When the list room part is too small (b40's largest frames, up to 560 KB [M]), the frame waits for the last show as under geshow2's hand-over.
- The record and the ME now both run beside the GE's tail, so all three share the bus [U]. The PSP pair prices it.
- Latency: N is shown at the GE's finish of N as before, but N+1 is recorded beside N's tail: `showlat` +12 to +16 ms in d40's slow stretch, +1 to +7 elsewhere [M, PSP-3000].

## On by default, on idxring's ring: proof (idxring3, w140 2637bed) [E]

Sierra-III idxring3 c29425a (idxring's three commits on w140 2637bed + geshow3 in M6 #2's default table); lab batch idxring3a 39729577438 (a10 and d40 hands off to game time 6,000, fixedticks fixedseed statecrc framecrc nomouth, two runs a side; `on` = the defaults, `off` = `geshow3off`; bake b140z):
- **Play and both random seeds identical at all 6,000 ticks**, on vs off and between the runs of each side, on both maps. The whole-state hash parts on vs off (looping sounds, the objects allocation) and also between runs of one side (a10 off/off2 from tick 3,682, d40 on/on2 from 1,458), so it doesn't judge this change.
- **Pictures within the runs' own spread:** a10 on/off differ at 23-42 of ~2,986 frames, against 22-45 between runs of one side; d40 2-5 against 3-6.
- `geshow3check`: 0 reuses of what the GE reads before its frame was shown (a10: textures 0 of 52, geometry 0 of 209, level copies 0 of 26, VRAM copies 0 of 1,825, list room 0 of 2,699; d40 likewise). Draws skipped for room in a part 0; finishes not reported 0. The ring's waits 0 (no range caught up with the frame being shown). List-full skips equal on and off (a10 8,158 both; d40 0 both). Strict 0 sites on every run.
- Front door: menu -> a10 with the defaults (hop2-on), as m6menu2's hop; the return leg is the PSP's (the master's fd-return).
- Compiles: campaign PGO 39729492710 ok (heap 5,764 KB; 86 functions without counts, the base 83: the ring's).
