# Review: the boot screen and a visible load (branch `bootscreen`, on `w139`)

Sheepdog01's b138 hand test on the PSP-1000 (2026-10-17): the start-up showed the log scrolling; he asked for the loading screen there instead, a few seconds' note on how to open the development (launch) menu, the main menu settled before it's shown, and the menu-to-level load, black and silent, made visible. Labels: [M] PSP, [E] emulator, [D] code, [U] inference. Nothing here has run yet: it compiles and links in both builds [D].

| Change | Where | Default | Switch |
|---|---|---|---|
| The boot screen: the loading screen from keep.txt's reading to the game's first frame | `psp_loading.c` (thread), `psp_ge.c` (`psp_ge_boot_start`), `psp_platform.c` | on | `bootlog` (the log on the screen, as before) |
| The hint: "Hold L as the game starts for the development menu", the first 3 s | `psp_loading.c` (`boot_hint`), `psp_ge.c` (`loading_notice`) | on, except after the launch menu | `boothintN`, `boothint0` |
| The main menu's settle: its first frames under the veil, its input held | `FIXES_LOADING #bootscreen` (main.c, ui_widget.c), `psp_loading.c` | on | `bootsettleoff` |
| A progress bar on every load without the bake's picture | `psp_ge.c` (`loading_overlay`) | on | `loadingnobar` |

## §Boot The boot screen

- **Why a thread.** The start-up is one blocking chain on the game's thread, and the GE drew nothing until the menu's first frame. A thread of its own (`boot_thread`, priority 0x1f: above the game's 0x20, below the stick's reader) draws a loading frame about every 30 ms and sleeps otherwise. It has to outrank the game's thread: the tag read waits in `SwitchToThread` (`sched_yield`), which never hands the CPU to a lower priority.
- **Why its own display list, in VRAM.** The full list (768 KB) comes from user RAM or the heap, and taken first thing it lands where the tag window must go (build 105's note at `display_list_acquire`). The boot list is 64 KB of benchrun's VRAM region (0x1C0000), which nothing uses before the first frame: the texture cache's VRAM fills only after `psp_tex_screen_free`, the models' copies only as frames draw. pspgu writes a list through the uncached view, so nothing needs writing back.
```c
sceGuStart(GU_DIRECT, ge_ready ? (void *)display_list : (void *)(0x04000000 + BOOT_LIST_AT));
```
- **Hand-over.** `ge_initialize` (the menu's first frame) first calls `psp_loading_boot_screen_stop`: the thread is told to stop and waited for (1 s at most), its stack freed before the display list looks for room. `sceGuInit` isn't called a second time (it would register the GE callbacks twice); `draw_buffer` is reset to what `sceGuDrawBuffer` says. Without the boot screen (`bootlog`) both frame buffers are cleared there instead, since the menu's veil draws its trail from the one on the screen.
- **What it shows.** A cold start's black (`captured` NULL, as the Xbox's), the progress of the start-up's load (ui.map's tag read, or init.txt's map), and the hint. The renderer's picture (loadingband) is read by the thread's first `psp_ge_loading_capture`, whose guard now also passes while the boot screen has the GE.
- **Crash.** The report goes into both frame buffers; `psp_crash.c` freezes the boot screen first (`psp_ge_boot_freeze`), and a frame in flight is not swapped in.
- **Cost.** RAM: a 16 KB thread stack during the start-up only (outside the heap); the campaign build's heap 5,692 KB against 5,696 at `786ad08` (text +5 KB); the multiplayer build links, heap 5,232 KB [D]. CPU: a frame is a few hundred microseconds [U], so ~1% of the start-up.
- **Not covered:** the first few log lines (before keep.txt is read) still reach the screen for a moment, as `bootlog` can't be known before then.

## §Hint The hint

pspdebug's 8 x 8 font (`msx`, linked already for the log), made into a 512 x 8 4-bit texture in the display list each frame (2 KB; no VRAM or heap kept), drawn 1:1 over a black patch. The launch menu's rule is checked once, at start-up (`psp_menu`), so the hint says how to reach it next time; it's left out when this launch came through the launch menu. The menu's settle doesn't end before the hint has had its seconds.

## §Settle The main menu's settle

- **Why b137's settle hung at boot** [D]: at `main_setup_connection` the port's vertical blank thread hasn't started (`main_initialize_time` starts it via `rasterizer_set_vblank_callback`), and with interpolation off `D3DDevice_Present` waits while two flips are pending, which only that thread counts down: the third frame never returns. That's loading2's a10/b30/Blood Gulch sitting at game time 0.
- **So the menu settles on main_loop's own frames**, after that thread runs: `psp_loading_boot_frame`, just before `render_interpolation_frame_end`, draws the veil and counts the texture cache's reads (`psp_loading_settle_frame`, as a level's settle), and `process_ui_widgets` returns early while it runs, exactly as it does for the Xbox's own loading screen just above. Unlike a level's settle the menu ticks meanwhile (its scenes and music run as before); only what's shown and when input is taken change.
- It ends when warm (3 quiet frames, progress shown whole, the hint's time past) or at the cap (`settleN`, 3 s, stretched to the hint's end). If its frames ever stop coming, `psp_loading_holds_input` lets go a second past the cap.
- Init.txt's levels still have no settle at boot (a level's frames tick the game; that settle would need its own fix).

## §Bar The load from the menu

The menu fades to black before a load, so the trail had nothing to show: 2.1 s of black on the PSP-1000 [M, b138 test 1]. Each loading frame and veil now draws a thin bar (160 x 2 at y 252), over a black patch so the trail doesn't carry it out behind itself. With the bake's picture the band is the Xbox's own progress, so the bar stays off: `loading_with_picture` is the hook for loadingband (set it where `loading_picture_read()` succeeds at a load's capture; loadingband's own `loading_picture_ready` is cleared by the settle's first frame, so it can't serve).

**A shortest showing?** Not here. At 2.1 s the load from the menu isn't a flash, and the sound session's `loadholdN` (sound-b139 `0f7a347`, 6 s) is Sheepdog01's feel call on the same point. It merges with this: its hold becomes `settle_not_before_us = begin_us + hold_us` (the field the boot's hint uses), plus its clock-paced progress in `update`.
