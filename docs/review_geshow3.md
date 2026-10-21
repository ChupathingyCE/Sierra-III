# Review: the next frame replayed into a second display list (S10a's variant; keep words `geshow3`, `geshow3check`)

Under `geshow2` (review_geshow2.md) the frame is shown off the game thread, but the ME's replay of N+1 still waits for the GE's end of N: the hand-over waits for the forwarding thread to see the GE finish N, swap and start the one list again. On the PSP-1000 that puts the ME's job and the GE's tail in series, M + G' = 15.6 + ~23 = 38.6 ms on d40 [M] (rank_pipelined.md §7.3, the re-price). `geshow3` lets the ME replay N+1 into a **second list**, in a part of the same 768 KB display list, while the GE draws N's tail. The game thread's cycle becomes R + max(T, M) + rest and the GE's its own work, priced at ~26-31 ms on d40 (the 30 fps cap) with about plain's latency [U]. There's no new memory. It's **off by default**: it needs `geinflight` and `gemeon` (on by default in the campaign build) and uses geshow2's forwarding thread. Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## The change (`psp/src/psp_ge.c`, `psp_trace.c`, `psp_tex.c`, `psp_geo.c`)

- **The list's start goes back to the game thread** (`show3_list_begin`, from `window_begin_main`). If N is still out with the GE, N+1's list starts in the larger part of the display list that N's list (`show3_list_from`..`show3_list_to`) doesn't use, and `list_bytes` is that part's size. The part must hold the largest list lately plus a quarter and 32 KB (at least 192 KB). If it doesn't, the game thread waits for N's show and takes the whole list, as before (`for list room` on the `GE: geshow2:` line).
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
- a VRAM copy's room given back;
- a list started over the part of the frame out (`list room`).

Moves (compaction, copies following the origin) and glyph changes wait for the show first, so nothing is out when they run. The `GE: geshow3check:` line gives the totals since the start; 0 reuses is the proof. Draws skipped for room in a part of the list are on the `GE: geshow3:` line and must be 0 too (`draws skipped for room in a part`).

## Evidence

**Compiles:** campaign PGO of e4abae0 (success; `pgo_mismatch.txt` lists the changed functions, so a strict link needs a new profile); `PSP_MULTIPLAYER=1` and `PSP_METRICS=0`: pending.

**Emulator [E]** (EBOOT e4abae0; readswait and nomouth on every run; d40, b40, a10, b30): pending.

**PSP-1000 [M]:** pairs specified for recon (d40, a10; `keepsets "- | geshow3"`, repeat 2, `showlat`): pending.

## Costs and risks

- The GE's finish is waited for through its callback. If the firmware ever left a finish unreported, the show would wait 400 ms and go on, counted.
- When the list room part is too small (b40's largest frames, up to 560 KB [M]), the frame waits for the last show as under geshow2's hand-over.
- The record and the ME now both run beside the GE's tail, so all three share the bus [U]. The PSP pair prices it.
- Latency: N is shown at the GE's finish of N as before. N+1's replay no longer waits for that, so `showlat` should come back to about plain's [U].
