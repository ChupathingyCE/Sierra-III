# Review: build 140, hand test 1 (b140h1)

For the owner to read and question before testing (CLAUDE.md, "How AI is used here"). b140h1 is `w140` at 1d4a429: b139 (`review_b139.md`) plus everything merged into `w140` since, w139's own fixes included (merged at 5a38845). Each change's own review has its key lines and proof; this page says what changed, what's on, what's yours to try, and what to watch.

Labels: [M] measured on a PSP, [E] emulator, [D] read in code or data, [U] inference. Every change has a keep word that turns it off (or on, for the opt-in ones).

**Install:** the zip is `sierra-build/39812970619/halo_psp_b140h1.zip` on the lab's results branch (EBOOT 1b5e99703a676ed2). It needs the **b140z** bake (`/bake/b140z/halo/maps`): b139e's maps won't match the CLUT4 look set and the slim level packs.

## What changed, by kind

| Group | Changes | Default | Review |
|---|---|---|---|
| The frame in flight (M6 #2) | frames recorded on the main CPU, replayed by the ME, drawn while the next ticks run (gedefer, geinflight, gemeon, mevram, inflightkb224, heaproom256); fewer ME polling wakes (mepoll); **geshow3**: the ME replays the next frame into a second list while the GE ends this one, on idxring's index buffer | on (`m6off` all; each its own off word; `geshow3off`) | review_m6default.md, review_sv.md, review_geshow3.md, review_geshow2.md |
| The main-menu hang | the loading veil left out while geinflight writes the frame down (w140 hung loading the menu on the PSP-1000) | on (`veilold`) | m6menu commit 420c88f |
| The ME ring | a late ME job rests the ring for 1 s (doubling to 16 s if late again) instead of ending it for the session; the ladder's c10/c20/d20 ticks are back [M] | on (`ringgiveup`) | review_ringfix.md |
| Speed, exact | RSO v4b: still objects' draw records sent again (record lane -3.4 to -4.5% [E]); PRS: perception skips what its refresh only stamps; the sphere memo off (it cost more than it saved); LFS: lens-flare axes worked out once; blipdepth: the motion sensor's target in the depth buffer (16 KB of VRAM back) | on (`rsooff`, `percall`, `spherememo` turns the memo on, `lfsonceoff`, `blipdepthoff`) | review_rso.md, review_prs.md, review_smemooff.md |
| Look | the CLUT4 look set (lightmaps and HUD sheets at 16 colours where they pass the strict bar; your 10-21 call); dynamic decals fade over 2 s and go at 30 s (your 20:28 call); the XMB tile's glass see-through | on (bake `--no-clut4-look`; `decalfadeoff`) | memory_ledger.md §3, review_decalfade.md |
| Sound | muffling, Doppler and cones: a restoration of the Xbox's sound | on (`audioold`) | audio_fidelity.md |
| Bake | the level's binormal and tangent out of the pack (LVLSLIM: a10 30.2 -> 25.6 MB on the stick) | b140z has it | geofmt review |
| Fixes from b139's hand test | everything in `review_b139.md` §Fixes (w139 merged in), the crouch race, the sleep tables emptied at each load | on | review_b139.md |
| Crash guard | **texpretouch out of the defaults**: on some builds its touches let a heap chunk header be overwritten (route runs crashed at b30's load). The cause (tables the ME writes sharing cache lines with heap headers) has a fix in review; until it's proven, texpretouch stays off (~1 ms of b30's frame [E]) | off (`texpretouch` turns it on) | decisions 2026-10-22 |

## Yours to try (off by default: A/B them)

| Keep word | What it does | What we measured | What to judge |
|---|---|---|---|
| `aicad3` | AI thinks on alternate ticks (half the actors each tick); cover search and re-planning stay every tick; aiming, firing and moving every tick | b30 flight frame **-9.9 ms (-6.1%)**, tick -4.8 [M PSP-1000]; beach and a30 being measured today; fight outcomes in the emulator's census: the beach still cleared, marines survive at least as well [E] | whether fights feel the same: marines, Covenant reactions, the beach landing |
| `lodpsp` | models switch to lower detail nearer (as on a 480-line screen at the same size in pixels) | <= ~2.5 ms on b40's cinematic only [U]; the Pelican's cockpit glass goes black with it (being fixed: don't judge the glass) | elites and marines up close in cinematics |
| `geshow3off` | the old show path (geshow3 is on) | d40's slow stretch 22.9 -> 29.2 fps with geshow3, latency +12-16 ms there [M PSP-3000] | whether geshow3's input lag is noticeable on d40 and b40 |

## Measured on the PSPs today (ladder45, the openings' first 45 s)

w140 a457800 (ringfix, texpretouch still on), PSP-3000 two runs agreeing within 0.1 fps, PSP-1000 the same within 3%: d40 28.7-28.9 fps, c40 28.6, b40 27.7-28.0, c20 14.5, c10 10.9, d20 9.3, b30 6.7. b30's flight on the PSP-1000: frame 163 ms (6.1 fps), tick 58 ms (2 ticks a frame). Where the heavy maps' frames go: rank_pipelined §9 in the lab (animation + node matrices are 19-40% of the tick on every map).

## What to watch

- **Restarts:** the crash showed only on scripted route runs, never by hand, and texpretouch is off; if any launch dies at a level's load, keep the log (`crash.txt` names it).
- **The menu:** boot to the menu, a level, back to the menu (the return leg is proven in the emulator only).
- **b139's open notes** (`projects/sierra/coord/handoff/b139_hand4_notes.md` in the lab): the light-pass door colour, flicker at range, the texture cache short on b30's hilltop / c20's door / a10, the menu ring H2/H15, the motion tracker cross.
- **Sleep** (`docs/review_sleepfix.md`): not in this build; the real-sleep test waits for it.

## Not in this build

texlines (the crash's real fix: proof pending), AICAD on by default (your call after the A/B), lodpsp's glass fix, UVB (vehicle physics probe, being built: ~6-7 ms on a30's fights [E]), FX15, sleepfix, TG.

PGO: campaign-w140c, 101 functions without counts (non-strict; a new profile comes with the next release build).
