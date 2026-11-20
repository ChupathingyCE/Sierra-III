# Review: the timer calls (projects/sierra/research/timercalls, 2026-11-02)

Lever TIMERCALLS: fewer system calls in the hot paths. Written by Claude (syscount, the master's helper); Sheepdog01 reviews.
Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## Why

The cache model under-counts the main CPU's misses against the PSP's profiler, and the gap fits the
system calls: each one costs the firmware's lines, more where the game has turned the cache over
since the last call (the lab's `projects/sierra/docs/research/cache_model_plan.md`, "The firmware term" [E x M]).
On b30 (07c778e, emulator, lab tc1 41570210005) the tick makes ~282 system calls a tick and drawing
~446; `notimers` takes the tick to ~45 and drawing to ~80 [E]. The callers it removes are the
instrument timers (`psp_microseconds`, `ge_clock`, `psp_step`): `object_update` 4 sites at 21 a
tick each, `collision_get_features_in_sphere` 4 at 16-21, `biped_update_moving` 2 at 16,
`objects_update` 2 at 14; in drawing `psp_step` 145 a tick and `rasterizer_model_draw` 3 at 71.

## What they feed [D]

Statistics only: `psp_microseconds` says so ("statistics only, so notimers answers 0"); every use
read is an accumulation into a `_us` counter, a log line or `psp_object_account`. No gate, budget or
pacing reads them. The heartbeat's frame and tick times, the frame pacing (`GetTickCount`,
`QueryPerformanceCounter`) and the ME's bounded waits use their own clock reads, which stay.

## The change

| Where | What | Why |
|---|---|---|
| `psp_platform.c` keep_defaults | `{ "notimers", "timerson" }` for every build (it was the release's alone) | the timers' calls cost the cache in every build Sheepdog01 plays; `timerson` keeps them for a session that reads the profile windows |
| `psp_post.h` | `PSP_RELEASE_DEFAULTS` drops its notimers (now in the table) | one entry, so `timerson` alone turns it back |
| `psp_anim.c` psp_anim_lock | with notimers, the "lookup from another thread" check on one lookup in 256 | it is a diagnostic, 22 calls a tick in the tick; a lookup from the wrong thread still shows |
| `psp_metrics.h` | `extern int psp_timers_off` | for psp_anim.c |

Every edit keeps its file's line count, so the profile counted with the timers on still fits.

- **Off word:** `timerson` (the old behaviour exactly: timers on, the thread check on every lookup).
- **What changes for a reader of the logs:** the profile windows, the per-step and per-type object
  lines read 0 unless `timerson`. The heartbeat, metrics budget, hitches and hwprofile lines don't
  change. A session that reads the step profile adds `timerson`.
- **Not done:** a cheaper clock. The main CPU runs in user mode with CU0 off, so `mfc0 $9` faults
  there (`docs/layout_plan.md` [M]); the clocks left are decisions or rare.

## Proof [E]

- **Exactness** (lab tcx1 41575761499, 4850a92, the default against `timerson`): play and both random
  seeds identical on b30 (6,000 ticks), c10 (3,600), c20 (6,000), d20 (3,000), a30 (7,050). The whole
  state differs, in the parts drawing and sound write (objects, object looping sounds, the HUD's unit
  interface, first-person weapons), as the timers' calls move timing; play doesn't.
- **System calls a tick** on b30 (lab tcc1 41575841769, to game time 5,400): other 207 -> 129, tick
  282 -> 23, drawing 447 -> 80. What's left in the tick: `inflight_forwarder`'s delay, the engine's
  `QueryPerformanceCounter`, the ME ring's bounded waits (1 a tick each); in drawing the sound
  mixer's interrupt locks (`mix_command`, the DirectSound streams).
- **Its price** comes from a PSP pair (default against `timerson`, b30 timed route, hwprofile):
  the lab's per-call fit predicts ~3.7 ms a tick (the tick ~2.2, drawing ~1.3) [E x M, U]; the
  release's notimers measured +0.9% game speed (PSP-3000, 40084340672) and b136's -0.7 to -1.2 ms
  a tick (PSP-1000, 39030595621), so expect less: the pair settles it and tests the fit.
