# aicaller: the main CPU's AI by caller (PSP_AICALLER=1, keep word `aicaller`)

_The lab's briefs/me_aicol.md task 4, decisions 2026-11-27 04:1x and 04:4x (3); the builder, 2026-11-27 05:08. Cut from builder/meaicol 28bf69a3. Log only._

## What it prints

- **`aicaller: game time T | ...`**, beside each `aime:` line (psp_aime_log_due, every 900 ticks), over the window since the one before: `ai_update`'s us a tick (mean, peak) and its ticks; the inline think pass's us a tick (mean over all ticks, peak), us a pass (mean, peak), passes and passes a tick (mean, peak); ticks a frame (mean, peak) and frames. The fight's window is the one ending past 1801, as meaicol's.
- **`ME: aicol: ring by yield: inner X us a job (N drains), the rest Y us a job (M drains)`**, after the `ME: aicol:` line, only with `PSP_MEAICOL=1` too: the aicol line's ring time split into psp_me_yield_inner's drains and the rest.

## The key lines, and why

- **The clock reads sit at the call sites** (psp_fixups.py's aicaller_cut, last in the file so it reads the fixed-up text): round game_tick's `ai_update` / `network_actors_drive` if-block in game.c (either form: bare, or `PSP_STEP`'s wrap), and round `psp_aime_think()` in psp_aime2a_tick_begin's owed branch in actors.c. Not inside `ai_update` or `psp_aime_think`, whose placed text would move further. The clock is psp_heartbeat_clock (sceKernelGetSystemTimeLow, us), which notimers leaves on.
- **A tick closes at `ai_update`'s end**, after the tick's inline pass (psp_aime2a_tick_begin runs at game_tick's start), so the inline pass's per-tick peak and passes a tick are the tick's. **A frame closes at psp_aime2a_frame_end**, called once a frame before game_frame; ticks a frame are the ticks between two of them. The open tick and frame carry into the next window.
- **Off by keep word:** without `aicaller` in the keep list the hooks read no clock and count nothing (psp_aicaller_begin returns 0, the ends return at once); the calls stay.
- **The inner drain** (psp_me.c, psp_me_yield_inner): under `PSP_AICALLER` its cycles go into nested_cycles_total, as psp_me_yield's drain does, so the ME busy share takes them off once. Without the switch the busy share counts them twice, so busy shares from builds without it stay upper bounds by the inner ring time (decisions 04:4x (3)). The inner yield's audio stays out of nested_cycles_total, as psp_me_yield's audio does. With `PSP_MEAICOL=1` the same cycles also go to two new aicol_stats counters (taking two of its pad words, so the struct stays 64 bytes and the ME's data layout doesn't move) and print apart.
- **Off:** aicaller_cut returns before reading the tree, psp_build.py passes `-DPSP_AICALLER=1` to psp_me.c only, and every psp_me.c change is inside `#if PSP_AICALLER` (or `PSP_MEAICOL || PSP_AICALLER` for the one clock read the drain needs), so the off EBOOT is 28bf69a3's.

## Placed text (w_tgme2m1mp_tgc90, the pair's layout; toolbox/placedin.py over a fixed-up tree)

On, four placed functions grow: game_tick (layout line 51; shifts 52-164 to COLOUR 0x60f9c at 165), psp_me_report (221, the new print; as under PSP_MEAICOL) and psp_aime_log_due (242; together shift 222-364 to COLOUR 0xd4640 at 365), psp_aime2a_tick_begin (500; shifts 501-508 to COLOUR 0x10d300 at 509). psp_aime2a_frame_end, psp_me_yield_inner and the new functions aren't placed. So the run's tick figures aren't slice 1's; its AI split is what it's for.

## Size

No heap or pool spend. One static window record in actors.c (14 words, about 64 bytes), on every map; psp_me.c's aicol record stays 64 bytes.
