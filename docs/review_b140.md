# Review: build 140 (the release)

For the owner to read and question before testing (CLAUDE.md, "How AI is used here"). Build 140 is `w140` at ccf6011: everything since b139 (`review_b139.md`). Three hand builds were cut on the way, and their walk-throughs hold the detail; this page is the whole list in one place, with what changed after the last of them.

Labels: [M] measured on a PSP (which one is named), [E] emulator, [D] read in code or data, [U] inference. Every change has a keep word that turns it off (or on, for the opt-in ones).

## Since b139: the frame in flight and the speed-ups (`review_b140h1.md`)

| Change | Default | Off word |
|---|---|---|
| M6 #2: frames recorded on the main CPU, replayed by the ME, drawn while the next ticks run (gedefer, geinflight, gemeon, mevram, inflightkb224, heaproom256); fewer ME polling wakes (mepoll) | on | `m6off`, each its own `...off` |
| geshow3: the ME replays the next frame into a second list while the GE ends this one | on | `geshow3off` |
| The ME ring rests 1 s (doubling to 16 s) after a late job instead of ending for the session | on | `ringgiveup` |
| RSO v4b, PRS, the sphere memo off, LFS, blipdepth | on | `rsooff`, `percall`, `spherememo`, `lfsonceoff`, `blipdepthoff` |
| The CLUT4 look set; decals fade over 2 s and go at 30 s; the XMB tile's glass | on | bake `--no-clut4-look`; `decalfadeoff` |
| Sound: muffling, Doppler and cones (a restoration) | on | `audioold` |

## Since b140h1: crashes fixed, new defaults (`review_b140h2.md`, `review_b140h3.md`)

| Change | What | Default | Off word |
|---|---|---|---|
| **The relaunch crash** (avgme + texlines) | The ME's replay no longer allocates; the tables it writes sit on whole cache lines. Proven on the PSP-1000 with a forced-hazard control [M] | on | `avgold`, `texlinesold` |
| **Two more ME hazards** (mealloc) | Bending batches' spheres on the main CPU; the button glyph sheet never unpacked by the ME | on | `batchsphereold`, `glyphmeold` |
| **Exit drain** | The frame in flight and the GE's list finished before a run's own exit stops the ME | on | `exitdrainoff` |
| **The restart after a death** (restartfix) | A checkpoint read takes its 4 KB page from the checkpoint's own stage, not the heap: a full heap no longer turns a death into a crash (latent since 10-12) | on | `stateentrymalloc` |
| **UVB** (vehprobe) | One sphere about a moving vehicle before its mass points' queries: a30's fights -5.6 ms a frame [M PSP-3000] | on | `vehprobeoff` |
| **VISLAZY** v1 | Undrawn scenery's node matrices made only when read | on | `vislazyoff` |
| **lodpsp** (a compromise, your call) | Models' detail picked at the PSP's size, with a floor for see-through parts | on | `lodpspoff` |
| **AICAD3** (a compromise, your call) | The AI thinks on alternate ticks: b30's flight -9.9 ms a frame [M PSP-1000] | on | `aicad3off` |

## Since b140h3

| Change | What and why | Default | Off word | Proof |
|---|---|---|---|---|
| **AIMCACHE** | An actor's aim reused when its inputs haven't changed (exact) | on | `aimcacheoff` | b30's landing frame -3.3 ms [M PSP-3000, 39895522147]; 0 of 193,359 cached answers differ, play + seeds equal on five maps [E] |
| **texpretouch back on** | Without it textures arrive late and models draw white for a few frames: your hand sessions showed white Master Chief, Grunts, Jackals and c10's dropship | on | `notexpretouch` | untextured frames on b30's route 268-271 a run off, 39-50 on [M PSP-3000, 39883807843]; it costs 1.2-4.9 ms a frame there, the price of not drawing white |
| **hblow back to opt-in** | The heartbeat below the game thread starved the log writer (lines dropped in every run with it: 9-33 against 0-1) | off | `hblow` turns it on | the frame-drop gain it brought (an ~8 ms spike every 5 s) is given back until the log writes are moved off that thread |
| **The a10 freeze** (profwait) | Inverting the look saves the profile; the save first waited for the last save's thread in an empty loop, and that thread runs below the game thread, which the PSP never preempts: stuck for good, HOME still answering. The wait now sleeps 1 ms a turn | on | `profwaitold` | two inversions on one tick: stuck at game time 783-824 before, played on to 7,558 after; play + seeds equal on b30 and a10 [E] |
| **The look by real time** (lookreal) | Below 15 fps the engine capped a frame's time at 1/15 s and turned the view by that, so the stick turned slower and unevenly as the frame rate moved. The stick's turn now uses the frame's real length (bounded at 0.25 s); local games only | on | `lookrealoff` | a full-stick turn on a30: 6.12 rad a second at 28 fps but 3.14 at 7.7 fps before; 6.13-6.14 at every rate after [E] |
| **The menu ring's far band** | The detail pass tested equal depth, and far out a rounding made it miss on some frames: the band flickered (H15). Now nearer-or-equal with a 64-unit pull | on (ui.map only) | `detailequal` | pixels changing and changing back over three frames: 98.5 before, 31 after, 19 with the detail pass off [M PSP-3000, 39910007068]; coarser detail levels changed nothing (39908363801), so it was the depth test |
| **Build number** | 140 in the log, the menu and the crash report | | | |

## Not in this build

The tutorial lights staying yellow on a10 (not reproduced: it likely needs a legacy stick preset, the path that also offers the inversion); animations looking cut short (not started); VISLAZY v2 and UNSEENPOSE (sized ~0, shelved); PREDEC on the ME (built, its PSP pair waits for a profile at its code); the large levers' sizings are in the lab's rank_pipelined §10.

## Proof of this build

To fill: the profile counted at ccf6011 (pgo-gen140ra/rb), the strict link, the release check against b139, the PSP ladder, relaunch and front-door runs.
