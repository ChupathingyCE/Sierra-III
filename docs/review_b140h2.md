# Review: build 140, hand test 2 (b140h2)

For the owner to read and question before testing (CLAUDE.md, "How AI is used here"). b140h2 is `w140` at 34eed63: b140h1 (`review_b140h1.md`, w140 1d4a429) plus everything merged into `w140` since. Each change's own review has its key lines and proof; this page says what changed, what's on, what's yours to try, and what to watch.

Labels: [M] measured on a PSP (which one is named), [E] emulator, [D] read in code or data, [U] inference. Every change has a keep word that turns it off (or on, for the opt-in ones).

**Install:** the zip is `sierra-build/39873101650/halo_psp_b140h2.zip` on the lab's results branch (EBOOT 6c475f6a848a520f, heap 5,720 KB). It needs the **b140z** bake, the same as b140h1.

## What changed since b140h1

| Change | What it does | Default | Proof | Review |
|---|---|---|---|---|
| **The relaunch crash's fix** (avgme + texlines) | The ME's replay no longer allocates: the texture averages' table is made when the pack opens (it was freed and re-made on the ME, beside the main CPU's `malloc`); the tables the replay writes sit on whole cache lines of their own | on (`avgold`, `texlinesold`) | PSP-1000 with a forced-hazard control: the hazard broke the heap both runs, the fix 0 bad chunk headers in 3,000 checks a run [M] | review_avgme (dcaudit2), decisions 2026-10-22 |
| **Two more ME hazards** (mealloc) | A bending batch's sphere is worked out on the main CPU when its part is converted (the ME wrote it into a heap block); the button glyph sheet is never unpacked by the ME's work | on (`batchsphereold`, `glyphmeold`) | play + both seeds equal on b30, a10, c20; the ME write trace's shared heap lines 0 (8 back with `batchsphereold`) [E] | review_mealloc.md |
| **Exit drain** | Before a run's own exit (exitatN, ladderN) stops the ME, the frame in flight is waited out and the GE's list ended; a `GE: exit:` line says how long (1-2 ms on the PSP) | on (`exitdrainoff`) | play and the whole state equal to `exitdrainoff` to the exit [E]; every PSP exit of the relaunch series logs it [M] | review_exitdrain (exitdrain branch) |
| **UVB** (vehprobe) | One sphere about a moving vehicle; a mass point's ground query or ray is skipped only when the sphere found nothing there | on (`vehprobeoff`) | exact: play + both seeds equal on a30, b30, d20, a10 [E]; a30's fights **-5.6 ms a frame** [M PSP-3000] | review_vehprobe.md |
| **VISLAZY** (v1) | Scenery not drawn last frame has its node matrices worked out only when read | on (`vislazyoff`) | exact on five maps [E]; small: ~0.2-0.37 ms a tick on b30 [E] | review_vislazy.md |
| **lodpsp** (a compromise, your 07:28 lean) | Models pick their detail level for the PSP's 272-line screen; a per-model floor keeps every see-through part (the Pelican's cockpit glass) | **on** (`lodpspoff`) | b30 on the PSP-1000: **-1.8 ms** cinematic, **-1.1** flight [M] | review_lodpsp.md |
| **AICAD3** (a compromise, your 12:18 call) | The AI thinks on alternate ticks (half the actors each tick); aiming, firing, moving and cover search every tick | **on** (`aicad3off`; `aicad2` still wins when named) | b30 flight **-9.9 ms** [M PSP-1000], **-5.9** [M PSP-3000]; a30 later fight tick -2.2 [M]; the fights play differently (the beach cleared more often, more marines alive) [E census] | review_aicad.md |
| **texpretouch** | Out of the defaults again: the PSP-1000 froze at a run's exit on 45a3859 with it on (39844731177 r4). Nothing implicates it (both ME hazards found exist with it off, and the freeze was at the exit), but it ships off until it's shown clean | **off** (`texpretouch` turns it on) | the relaunch series below ran **with** it on | decisions 2026-10-22 |

## Yours to try

| Keep word | What it does | What we measured | What to judge |
|---|---|---|---|
| `aicad3off` | the AI back to every tick | see AICAD3 above | whether the fights feel the same with aicad3 on (marines, Covenant reactions, the beach landing) |
| `lodpspoff` | detail picked as on a 480-line screen | see lodpsp above | elites, marines and weapons up close; the Pelican's glass should look as before |
| `texpretouch` | textures read in as their draws are written down | ~1 ms of b30's frame [E]; an A/B on this build is queued on the PSP-1000 | nothing to look at; any freeze or crash with it on is the news |
| `geshow3off` | the old show path | d40's slow stretch 22.9 -> 29.2 fps with geshow3 [M PSP-3000] | whether geshow3's extra latency is noticeable on d40 and b40 |

## Proof of this build

- **Emulator** (h2smoke 39873105908): b30 and a30 routes under the strict interpreter, 0 sites, no crash; the front door boots to the menu and loads a10 (the return to the menu wasn't exercised in this run: proven in the emulator on m6menu's build only). The keep lines show `aicad3` added and `texpretouch` not.
- **PSP-1000 relaunches** (b30 route + deathless, exitat180, **texpretouch on**: the freeze's conditions): 39873195417 **5 of 5 exited**, each with `GE: exit: ... the GE done (1-2 ms)` before `exitat: exiting`; 39873229050 (5 more) ends ~20:15. Before exitdrain, the glyph fix alone ran 5 of 5 (39868852457), and 45a3859 ran 6 of 7.
- **The freeze itself** (39844731177 r4, its log recovered by psp-probe 39867323838): play stopped between game time 5,381 and the exit at 5,399, inside the exit frame's report; no cause proven. exitdrain removes the leading suspect (the exit stopping the ME with a frame still in flight).

## What to watch

- **Exits and relaunches:** exitdrain covers the game's own exits, **not HOME** (the callback thread). If quitting through HOME ever freezes, keep the log (`log/NNN_log.txt` holds the last 20 launches).
- **The menu:** boot to the menu, a level, back to the menu (the return leg is proven in the emulator only).
- **Fights with aicad3 on**, and the cockpit glass with lodpsp on.
- **b139's open notes** (the lab's `projects/sierra/coord/handoff/b139_hand4_notes.md`): the light-pass door colour, flicker at range, the texture cache short on b30's hilltop / c20's door / a10, the menu ring H2/H15, the motion tracker cross.

## Not in this build

VISLAZY v2 (exact but removes ~0: off, not merged), UNSEENPOSE and NMPRE (in progress), FX15, the frame-drop work (`hblow`: its PSP test is queued), sleepfix, TG.

PGO: campaign-w140c, 111 functions without counts (non-strict; a new profile comes with the next release build).
