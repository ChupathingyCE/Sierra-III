# B41: a sleep the game is ready for (design)

**Status:** built on the tock (`psp_sleep.c`, keep words `sleepready`, `sleepdryN`, `powerflags`; the stall reporter on unless `stallreportoff`), proven in the emulator (sleepdry1: play and seeds identical on three maps, 21 stops, the partition untouched) and on the PSP-3000 (sierra-hw 40822061362: 10 stops with the real ME halted and restarted, the partition untouched); `sleepready` on by default. A second ask while stopped (the switch slid twice) keeps the stop, and any wake releases it: proven in the emulator and on the PSP-3000 (sierra-hw 40832569434: 10 stops, each asked twice, all held, the partition untouched, back in 1.9–3.7 ms, no crash; the earlier crash, 40828919863, came from the stand-in wake reopening live files every stop, fixed by releasing without reopening). Left: step 0 (`powerflags`, Sheepdog01's slide of the switch) and Sheepdog01's one real sleep, until which b148 ships the stopgap (`psp_sleepguard.c`: the firmware's power lock held while the game runs; keep word `sleepallowed` lets the switch work again). This document is the proper fix that replaces it.

Evidence labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## What goes wrong today

On b148, a sleep from the power switch crashed the PSP-1000 as it went to sleep. The firmware's settings in flash1 came back corrupted (Sheepdog01, 2026-10-28, sleep 0) [M]. Sleeps with the mewake keep words hung after waking (sleep 1: about 50 frames drawn, then the main thread stopped with no log line) or crashed (sleeps 2 and 3) [M].

The cause is a race, not one bad call. Today's sleep is done **beside** the game rather than **by** it [D]:

1. **The power callback runs on its own thread** ("halo callbacks", priority 0x11). The game thread and the GE's forwarder ("ge forward", 0x10) keep running while it works.
2. **It stops the ME from that thread** (`me_stop("sleep")`), wherever the game is in a frame. Under geinflight and mechunk, a frame's job is usually out with the ME, and the GE is running a list whose stall address the forwarder passes on as the ME publishes. Stopping the ME there leaves the GE parked at a stall mid-list [D].
3. **It gives the volatile partition back while others may still use it.**
   - `psp_tex_suspend` unlocks the partition at once.
   - The game thread is held only at its next display record (`inflight_room_park`), and only for up to 300 ms. A thread in the tick or a load isn't held at all.
   - The geometry arena, the animation cache, the level carve and geinflight's records are all carved from that partition.
   - The kernel uses the partition during the suspend. A game thread still writing into it there, or a GE list read from it, can corrupt what the kernel saves [D for the writers; U for what the kernel keeps there].
4. **The kernel's own handler** (`meLibOnSleep`) then holds the ME in reset wherever it is, while the firmware suspends the GE and writes its settings [D].
5. **After the wake, some waits have no bound.**
   - `sceGuSync(0, 0)` at eight sites in `psp_ge.c`.
   - The forwarder's `show_picture` waiting for the GE's finish of a frame.
   - If the list it waits on didn't survive the sleep, the forwarder sits above the game thread for good. The game thread then waits on `show2_sema`. That would match sleep 1: frames, then silence [U].

b132's sleeps survived because the ME didn't yet own the GE's frames [U]. Since gemeon and geinflight it does (about 70% busy in play), so the race is now hit almost every time.

## The fix: the game thread puts itself to sleep

The rule from the proper-fixes policy: fix it where it runs. The ME and the GE are made quiet by the thread that drives them, at a point where nothing is out, and every wait has a bound.

### 1. Hear the request early, before the kernel acts

Two ways. Step 0 picks between them.

- **A. The power lock as a gate** (user mode).
  - Keep `scePowerLock(0)` held, as the stopgap does.
  - When the switch is slid, the callback should get `PSP_POWER_CB_POWER_SWITCH` while the suspend is held off. The callback only sets `sleep_asked`.
  - Once the game has parked (step 3), the callback calls `scePowerUnlock(0)`, then `scePowerRequestSuspend()` if the held request was dropped.
  - Unknown [U]: whether a held lock still delivers POWER_SWITCH, and whether the request stays pending or is dropped.
- **B. A sysevent handler** (kernel mode, through the kernel calls the ME code already makes: `kcall_*`).
  - At the suspend query (`PSP_SYSEVENT_SUSPEND_QUERY`), refuse while the game isn't parked, and set `sleep_asked`.
  - The game parks, then asks for the suspend itself (`scePowerRequestSuspend`). The second query finds it parked and is let through.
  - This works whatever the lock does, and the kernel waits for the handler's answer. It costs a kernel-mode handler that must stay registered and be removed at every exit.

**Step 0** (one hand test on the stopgap, no sleep): keep word `powerflags` logs every power callback's flags. Sheepdog01 slides the switch once while the lock is held. If POWER_SWITCH arrives, A is enough; otherwise B.

### 2. The game thread quiets everything where no frame of its own is open

At a frame's first window (`psp_ge_window_begin`, before anything of the frame reaches the GE layer) or a load's frame, with a sleep asked, the game thread does this in order (`psp_ge_sleep_quiet`, then `psp_sleep_point`):

| Step | What | Bound | On timeout |
|---|---|---|---|
| a | The frame out with the ME collected and shown (`inflight_sync`, as the next record would), anything written down carried out | the job's own watchdog | the ME is stopped as a failure (the existing fallback) |
| b | The forwarder's show taken (`show2_settle`), the list it started for the next frame ended empty (`show2_list_drop`, as a load's frame does), the GE's queue drained (`sceGeDrawSync(1)` polled) | 200 ms | `sceGeBreak(1)`, logged |
| c | The forwarder idle: with no frame out and no show asked it sleeps by itself | — | — |
| d | The ME halted from **this** thread (`me_stop("sleep")`: no job out, so it halts at its next look) | 100 ms (as now) | held in reset (as now) |
| e | Reads in flight finished: geometry's (`psp_geo_settle`, the port's only asynchronous reads) | the read's own | — |
| f | The partition's caches forgotten and the partition unlocked: the existing `psp_tex_suspend`, `psp_geo_suspend`, `psp_ge_inflight_room_suspend`, now called by the only thread that writes them | — | — |
| g | The game thread parks on an event flag (not a spin) until the wake | none: this is the sleep itself | — |

Then the callback (A) or the game thread itself (B) lets the suspend go ahead.

The audio thread is the one other thread that touches memory. It mixes on the main CPU once the ME is stopped (step d), and nothing of the sound code carves the partition [D].

### 3. The wake

The callback sees `PSP_POWER_CB_RESUME_COMPLETE`, reopens the files (`psp_io_reopen_all`, first as before) and sets the event flag. The game thread, still at its quiet point:

1. Retakes the partition (`psp_tex_resume`; the records' front room is checked at the window's start, as after any sleep).
2. Starts the GE afresh. Any list ID from before the sleep is forgotten (`stall_me_qid`, show3's gate, show2's list), and the next frame opens a new list. Nothing waits on a list that didn't survive.
3. Restarts the ME through the mewake path (`psp_me_wake_poll`), each of its waits bounded. If the restart doesn't answer in 100 ms, the ME is held in reset and the main CPU runs its jobs, logged. The game plays on, slower, rather than hanging.
4. Continues the frame.

### 4. Bounded waits everywhere a sleep can reach, and a stall reporter

- The eight `sceGuSync(0, 0)` sites and `show_picture`'s wait each name themselves in one word before they wait: a store, nothing on the hot path.
- The heartbeat thread already wakes every 5 s. A watched wait found at the same count on two looks running (over 5 s) is logged once (`stall: ...`). On the next look (over 10 s) the GE's queue is stopped (`sceGeBreak(1)`), so the waiter returns. The ME's restart is only named: a kernel call can't be broken from outside.
- This turns any future hang of this kind into a log line, not silence.

## How it's proven without Sheepdog01's hands

- **`sleepdryN`** (a keep word; recon-style, no cost when off). Every N seconds of game time, it runs steps 2a–g and the whole wake, but parks for 2 s instead of asking for the kernel's suspend. The real ME halts and restarts on the PSP; the emulator simulates the ME.
  - **The scribble check.** In dry mode the partition isn't unlocked. Instead, after step f, it's filled with a pattern. After the park, the pattern is checked. Any changed word means something kept writing after it was told to stop. The log names the address, and so the cache it belongs to.
- **In the emulator:** a batch of sleepdry runs at the stretches that load hardest: mechunk frames, b30's landing (loads), a10's cutscenes. Each run gets 20 dry sleeps, and must be exact against its own run without them (statecrc), with 0 scribbled words.
- **On a test PSP:** one 4-run ask on the PSP-3000 (sleepdry on, a run ending with `exitatN`, no hands). It proves the real ME's halt and restart and the GE's drain on hardware, with 0 scribbled words and 0 stall lines.
- **What only hands can prove:** the kernel's own suspend and the firmware's wake. Once the dry runs pass, that's one plain sleep by Sheepdog01, on the PSP-3000 first if he prefers, since the flash1 risk is the settings screen ("O" restores the defaults). The rig can't do it: a PSP the game puts to sleep needs the switch to wake, so a rig self-suspend would take the PSP off the lab until Sheepdog01's hands.

## The order of work

1. Step 0: the callback's flags logged (a few lines, into the next tick) and Sheepdog01's one slide of the switch. Picks A or B.
2. The quiet-at-frame-end path, the wake, and the stall reporter on a tock branch, behind keep word `sleepready` (on by default once proven). The stopgap stays as the default until then.
3. sleepdry in the emulator (exactness, 0 scribbled), then one PSP-3000 pair.
4. Sheepdog01's plain sleep. If it passes, the stopgap leaves the release, and `sleepready` becomes the only path.

## Open questions

- Does the kernel write into the volatile partition during the suspend, or only expect it free? Either way the fix gives it back unused. [U]
- Does the GE driver keep a queued list across a sleep? The design assumes not (step 3.2). [U]
- Does b147h do the same? It has gemeon and geinflight, so likely yes. Not tested; the fix covers both.
