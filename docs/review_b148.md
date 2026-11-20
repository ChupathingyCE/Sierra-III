# Build 148: review walk-through

b148 is the next release candidate (a tick). It is b147h (`review_b147.md`, the release Sheepdog01 passed on 2026-10-28) plus the tock's work that passed its proofs by noon on 2026-10-28: Chief's armour as the Xbox draws it, the bake changes behind it, the frame-timeline instrument (compiled to stubs in a release), and, at Sheepdog01's ask (18:55), the Media Engine's sleep fix as opt-in keep words. After Sheepdog01's hand test it also carries B38's fix (Chief's armour flickering white), B41's stopgap (the sleep switch held off in game) and the bake's kept cache and menu music. The tock's later levers (VFPUQD, SCRATCHBSP, carveshare) go in the tick after this one.

Labels: [M] measured on a PSP, [E] emulator, [D] read in the code, [U] inference.

## 1. Chief's armour as the Xbox draws it (chiefsheen, chiefdetail: restorations, on by default)

On the Xbox, Chief's armour is its lit, change-coloured base, with the reflection added over it where the multipurpose map's red marks it, and the armour's detail map multiplied over that. The port folded the reflection into the ambient light (multiplied by the base, so the armour stayed a flat saturated green) and drew no detail map on him.

- **The sheen** (`psp_ge.c`, `sheen_pass`; 00491a30). The bake keeps Chief's reflection mask as a CLUT just before the change mask's (`texpack.py`, `reflection_mask`; flag 32). A second pass adds the reflection cube's average, times the reflection's strength, masked per texel, depth-equal. `sheengainN` sets its strength: 70 by default, the PC's brightness (Sheepdog01 10:46; 4dfa3b7d).
- **The detail pass** (`model_detail_pass`). It now draws on Chief's armour in every level, not only on the menu's ring. That's why a model's detail key is cleared at its start (b147, 4a75ff2b): the menu's last key must not reach a level's models.
- **The defaults** (`psp_platform.c`; 82017e17): `chiefsheen` and `chiefdetail` join the campaign's defaults, beside b147's `chiefvisor` (gain 150). `nochiefsheen` and `nochiefdetail` turn them off. The defaults' room grew from 192 to 256 bytes (2b2d5f3d), because with these the campaign's list reaches ~200 bytes and a full keep.txt would have lost its last words silently.
- **Its price, accepted (B37; Sheepdog01 18:50, "worth the cost, it's a restoration").**
  - [M] The detail pass is the cost: 1.7-2.6 ms of drawing in the openings of a30, a50, b30 and c10. That's where Chief fills the screen.
  - [M] The sheen costs 0-0.4 ms.
  - [M] In play, his first-person arms take the detail pass: ~0.3 ms of drawing on a10.
  - [E] A size gate (`chiefdetailpxN`, branch `chiefdetailpx`) was tried and isn't in. In the close-ups the camera sits inside Chief's bounding sphere, so the pass always draws (rightly: that's where it shows). Where he's small, the pass changes no visible pixel, and it costs little.

## 2. The bake (texpack.py): Chief's textures, and the reflection cube's average

- **`chiefdetail` is the bake's default class** (6095871a), with **`chief256`** (8339b75e). Chief's detail maps go in at full size; his other maps go in at 256 rather than the 128 every character gets.
  - All of him at full size (`chief`) made a10's bridge refuse 3,786 texture requests and draw 413 frames with something untextured [E].
  - Detail maps only, with the rest at 128, gave 0 refusals, but the c40 close-up's base came out soft.
  - 256 is a quarter of his own 512's bytes.
  - `--full-detail` picks other classes.
- **A model reflection cube's average is taken over all six faces** (3fdd2eeb), not the first face alone. The sheen adds this average, and every shiny part folds it into its lighting. One face is only one side of the cube's room, which read warmer and flatter than the PC. `--cube-first-face` gives the old average.
- **So b148 wants bake w148a** (or any bake made with b148's tools). On b147h's bake it runs, but Chief's textures stay at the old size, and on bakes before w142v the sheen finds no mask and skips.

## 3. The Media Engine after a sleep (mewakefw, mewakeearly, mewakeprobe: opt-in)

**Background.** By default the ME is stopped for a sleep and left stopped; the main CPU carries its jobs until the next launch.

**The b132 test 7 findings** (`mewake`, the restart):
- The ME restarted but answered no job.
- The reset register read 0x20 after our restart: the AVC block was left in reset. The firmware's own ME boot releases it last; ours never did.

**The words** (projects/sierra/research/mewake faef3c7; `psp_me.c`, `psp_platform.c`):
- **`mewakefw`**: at the sleep, record whether the AVC block was powered; at the wake, power it, then let go of the AVC and AW resets that launch found let go, with the firmware's memory flushes around the release.
- **`mewakeearly`**: the release happens in the kernel's wake handler, as the firmware and the ME library do. Jobs still go back at the frame's end, once the ME answers.
- **`mewakeprobe`**: after a restart the ME checks its FPU, a cached read, a written-back write, eDRAM and a deep stack, marking each step.
- All are off by default, and only the sleep and wake path runs them.
- The main CPU carrying the jobs stays as the safety net, used only if the ME doesn't answer within 300 ms.

**The proof so far:**
- [M] Rig, PSP-3000 (sierra-hw 40770504048): both sets restart in 23 ms (early: 33 ms), the probe's five steps pass, and jobs are served at the usual rates. 0 failed.
- That run had no power-down, so it shows the words are harmless, not that they work.
- [M] **Sheepdog01's sleeps on the PSP-1000 all failed** (2026-10-28 22:45-23:00; results hand/b148_sleep1-3):
  - Set 1 woke and reopened its files, then the main thread stopped with no restart line, and the exit hung.
  - Sets 2 and 3 crashed hard (vertical lines), with nothing logged after the sleep.
  - A plain sleep crashed too (B41, section 5).
- So the words stay off, and the fix goes back to research with B41.

## 4. B38: Chief's armour flickering white (a fix)

Sheepdog01's 240 fps video of b147h (19:30) showed the armour drawn white for single frames.

- **The cause** [E]: the ME's replay writes each part's state (the change colour, the glow and reflection words, model_cut's corners) on cache lines the main CPU writes while it records the next frame (`psp_rso_object` and the other `psp_rso` words, the frame's `stall_force`). The two CPUs' caches don't keep each other coherent. Either side's write-back put its stale copy of the other's words back in RAM, so the ME drew a part with an older change colour. The cache model's trace (flickercohere 40796129769) found three such lines.
- **The fix** (b9e6154d, research's 47395bb cherry-picked): the main CPU's 13 writers are placed with `PSP_MAIN_SIDE`, on lines of their own, as the recorder's words already were. It's placement only, so the profile still fit.
- **Proved:** [E] the trace with the fix finds no line written by both on those words (b38sides 40801970038). [M] Sheepdog01's eye on b147h with the fix: "no flicker at all"; on b148: no flicker in the first two cutscenes.

## 5. B41: the sleep switch held off in game (a stopgap, Sheepdog01's OK 23:01)

- **The fault** [M]: on b148, a plain sleep (Sheepdog01's own keep.txt, none of section 3's words) went black at the switch. The PSP shut itself down and came back with the firmware's "Setting information is corrupted" screen (O restores the defaults). It was on AC, with no battery pull.
- **Likely cause** [U]: the kernel's sleep handler holds the ME in reset wherever it is. Since b132, the ME carries the GE's frames, about 70% busy in play. So the reset can land mid-frame, while the firmware suspends the GE and writes its own settings. b147h has the same ME work and probably does the same.
- **The stopgap** (`psp/src/psp_sleepguard.c`): once the keep words are read, the game takes the firmware's power lock (`scePowerLock`), and every exit gives it back. In game, the switch does nothing; in the XMB it works as before. `sleepallowed` turns it off, for the proper fix's own tests.
- **The proper fix** is B41 in the lab's bugs list, next in the tock: the ME and the GE made quiet, with bounded waits, before the kernel's sleep.
- It changed two counted functions (`exit_game`, `keep_load`), so b148 has a new profile, campaign-w148d.

## 6. The bake: a kept cache and the menu's music

- **Palettes and sounds kept between bakes** (`bake/bake.py`, `bake/lib/soundpack.py`, `tools/texpack.py`) in the user's cache folder (`~/.cache/halo-psp-bake` on Linux; `HALO_PSP_BAKE_CACHE` or `--cache-dir` to move it, `--no-cache` to skip it).
  - Each entry is keyed by its input and the code that makes it, so a change to either makes it again.
  - The guided bake asks once, the first time, whether to keep it.
  - [M] The first bake took 11.3 min, about as before; a second took 3.6 min. Both made the same 55 files as w148a, byte for byte (40812279077, 40813395101).
  - The cache holds about 423 MB; deleting it is safe.
- **The menu's music** (`bake/lib/snd0.py`): the XMB plays b30's "A Walk in the Woods" (variation 1, 38.4 s, chosen by Sheepdog01 by ear), as a loop that wraps cleanly.
  - snd0 now reads the retail map decompressed (it crashed on it before).
  - The bake can write previews of each track's loops to choose from.
- **Keeping an old bake:** none of this changes the maps the bake makes, so a b148 player can keep a w148a bake. The menu's music comes from a bake run with the release EBOOT (`--eboot`), which writes the EBOOT with the sound in it.

## 7. Under the hood

- **lanetrace** (0e84b5d0 and after): the main CPU's, the GE's and the ME's work on one clock, frame by frame, for the time map (keep word `lanetraceN`).
  - With `PSP_METRICS=0`, the hooks keep their tests (never true) and the stubs carry names of their own (`*_off`; dadf34ad, 2573b830). That keeps the release's code the profiled code's shape.
  - rc148a and b148rel had refused until they did.
- **walkapart and mevis** (option C's groundwork) sit behind the build switch `PSP_WALKAPART=1`, so a default build leaves them out.
- **model14** went on by default and then off again (95bb7e54). On the PSP-1000 the 14-byte records cost the GE more than the bytes saved: a30's frame was 3.5-3.9% slower [M]. `model14` is opt-in.
- **B33's instruments:** the sound cache's map at its first refusals, and `soundloopstop` (opt-in).
- **objsteps:** per-object step timing, in recon (R1) builds only.
- **pspgu's small variables** use the tock's form, the same placement as b147g: last in `.sbss`, on the line below the ME's `.bss` block.
- **CLAUDE.md:** placement is decided by measured fit; exactness follows the multiplayer boundary (Sheepdog01 12:27).

## Proofs

| What | Run | Result |
|---|---|---|
| Profile campaign-w148a (13 counting runs at 9b0de4c2, bake w148a) | 40741156426, 40740526285 | 13/13 ok |
| b148 2573b830: strict build and release check (rc148b) | 40761387716 | 0 functions without counts; 14/14, 0 strict sites |
| The seven intros, PSP-3000 | 40765642054 | no crash, heap requests failed 0; ticks level with b147 |
| The seven intros, PSP-1000, twice | 40768861523, 40776543407 | within 2% run to run, except one Memory Stick stall in b30 |
| B37: Chief's look off on b147's bake draws as b147 (PSP-1000) | 40771212112 | a30 40.4, a50 23.7, b30 38.9 ms |
| B37: the cost split by pass (PSP-3000) | 40774707313 | detail 1.7-2.6 ms, sheen 0-0.4 ms |
| B37: the size gate (emulator) | 40778856227, 40779686423 | no gain in the close-ups; not merged |
| The sleep fix's words on the rig (PSP-3000) | 40770504048 | restart 23-33 ms, probe 5/5, jobs served, 0 failed |
| b148 acf511ac (with the sleep fix): its own profile, campaign-w148c | pgo-gen148ca/cb 40791626033, 40791703497 | 13/13 ok |
| b148 91636ee1 (with the sleep fix and campaign-w148c): strict build and release check (rc148d) | 40792522798 | 0 functions without counts; 14/14, 0 strict sites |
| b148 b9e6154d (with B38's fix): strict build and release check (rc148e) | 40807738112 | 0 functions without counts; 14/14, 0 strict sites |
| B38's fix in the cache model (b147h + 47395bb) | b38sides 40801970038 | no line written by both on the change colour or the reflection words |
| Sheepdog01's hand test of b148 b9e6154d (PSP-1000, a10, 18 min) | results hand/b148_test1 | 18.2 fps, 19.8 ticks/s; no flicker; no sound or texture refused for room; heap failures 0; ME failures 0 |
| The bake's kept cache (b148's bake tools) | 40812279077, 40813395101 | 11.3 min, then 3.6; the 55 files byte for byte w148a's |
| b148 8938acf2 (with B41's stopgap): its own profile, campaign-w148d | pgo-gen148da/db 40816288415, 40816368647 | 13/13 ok (161 units) |
| b148 f12f865d (with B41's stopgap and campaign-w148d): strict build and release check (rc148g) | 40817194705 | 0 functions without counts; 14/14, 0 strict sites; release EBOOT d7aebb7710abde6b |
| The stopgap by hand (PSP-1000) | results hand/b148_guard1 | the lock granted; the switch does nothing in game; sleep works in the XMB after the exit; holding the switch ~8 s still powers off |

## Sheepdog01's hand test (PSP-1000)

Before the three sleeps, play b148 as usual on bake w148a (`$psp/halo` from b148's zip and bundle).

**The three sleeps** (one session, from research's plan, `projects/sierra/coord/handoff/mewake_2026-10-28.md` in the lab). Each can end in a hung exit that needs a battery pull, as on b124 and b132, so run them in order and **stop at the first success**.

| # | keep.txt adds | Steps | What the log should say |
|---|---|---|---|
| 1 | `mewake mewakeprobe mewakefw` | play 1 min, sleep ~10 s, wake, play 30 s, HOME, exit | `ME: restarted after wake`, the probe's five steps, then jobs served; the exit returns to the XMB |
| 2 | `mewake mewakeprobe mewakefw mewakeearly` | the same | the same |
| 3 | `mewake mestash` (as b132 test 7, only if 1 and 2 both fail) | the same | where the ME stops |

If 1 or 2 works, the next tick turns that set on by default.

## Things to question

- **Chief's detail pass is the frame's price in cutscene close-ups:** ~2 ms of drawing on a frame of 25-45 ms there.
- **The bake changed again** (Chief's classes, the cube's average). Players need b148's bake tools, not b147h's: the release must ship both together.
- **The sleep fix is unproven where it matters.** It's off by default; nothing changes for a player unless keep.txt turns it on.
