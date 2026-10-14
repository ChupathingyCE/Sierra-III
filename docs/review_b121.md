# Review: build 121, every change since build 120

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). One short section per change: **what**, **key lines**, **why**, **proven**, **default**. The long versions (every table, every lab run) are in `review_b121_tints.md`, `_aiperc.md`, `_me.md`, `_machines.md` and `_bipeds.md`. Labels: [M] measured on a PSP (here the PSP-3000 rig), [E] emulator, [D] read in code, [U] inference.

Branch `w121`. Start from b120 (`84646c2`).

| # | Change | Kind | Default | Off word |
|---|---|---|---|---|
| 1 | Files reopened after a sleep | fix | on | `noreopen` |
| 2 | The HUD 1:1 on whole pixels, top corners nearer the edge | your A/B (test 4b) | on | `hudnosnap hud150 hudnopushup` |
| 3 | The preset on the menu header and the screenshot stamp | tool | on | n/a |
| 4 | See-through tints from texels; the bake picks the drawn map | restoration | on | `drawnmapscale`; bake `--tints-from-means` |
| 5 | AI perception as a batch | exact speed | on | `percold` |
| 6 | The level sphere walk tests each surface once | exact speed | on | `surfacetestold` |
| 7 | Machines sleep (b102's `machinesleep`, fixed and re-judged) | exact speed | on | `machinesawake` |
| 8 | The ME job ring, and the slant sort split with the ME | exact speed (ME phase 1) | on | `meslantoff` |

Who wrote what: 1–3 and the merges by the master session; 4–8 by delegated sessions, each on its own branch, merged into `w121` after its proofs were read.

---

## 1. Files reopened after a sleep (`274f08a`)

**What.** On the wake, every file the game held open is opened again at the place its next read was to start.
**Key lines.** `psp_trace.c`: `reopen_table` (each open file's full path, flags and mode, by the descriptor its caller holds), `io_real()` (the wrappers pass on the kernel's new descriptor when it differs), `psp_io_reopen_all()`. `psp_platform.c`: `power_callback` calls it first on `PSP_POWER_CB_RESUME_COMPLETE`. The async calls (`sceIoWaitAsync`, `PollAsync`, `ChangeAsyncPriority`) are wrapped too (`psp_build.py`).
**Why.** Your test 2 sleep: after the wake every read failed (`reading texture 600 failed`), so streamed geometry, textures and sounds came back as garbage.
**Proven.** `reopentest` runs the reopening at 20 s of game time without a sleep, with the kernel's numbers shifted so every file lands on a new descriptor (6 of 6): play and both seeds identical at every tick on d20 (20,581 ticks) and a50 (33,403) [E] (lab 38509454132). A real sleep needs your hands (test 3 below).

## 2. The HUD (`274f08a`)

**What.** `hudsnap` and the 1.767 zoom are the default; the top corners' elements move up 7 window units (~12 pixels), leaving the ammo and shield bars ~6 pixels from the edge.
**Key lines.** `psp_ge.c` `hud_draw` (`hud_snap = !psp_keep("hudnosnap")`); `psp_fixups.py` (`PSP_HUD_PUSH_UP`, applied in `hud_draw.c` to corners without the bottom bit).
**Why.** Your A/B (test 4b) chose 1:1, and asked for it nearer the top.
**Proven.** Emulator shots on b30's landing, new against b120's HUD [E] (lab 38509454132).

## 3. The preset on screen (`274f08a`)

**What.** The launch menu's header reads `Halo PSP build 121  preset 3`; the screenshot stamp reads `b121  p3  t 341.0  f 1930`, `p3+` once an option was changed after applying it, and no `p` part when no preset was applied.
**Key lines.** `psp_menu.c` (`preset_label`, `psp_preset_label()`), `psp_ge.c` (the stamp; two new glyphs, `p` and `+`).

## 4. See-through tints from texels, and the drawn map (`cdc5641`, `1ecfb43`)

**What.**
- The bake works each see-through shader out over up to 4,096 of the drawn map's texels instead of every map's mean colour, and fits one of three GE models: modulate (as before), add (`TFX_ADD`), or flat (the tint alone; the map gives only its alpha).
- The bake also picks the map to draw: the one carrying most of the shader's output, instead of "the smallest scale".
**Why.** The fixes for your LK16 and LK18 reports:
- c40's golden column: one gold map layered on itself; means dimmed it to ~¼.
- a10's yellow window splotches: the glass adds a reflection the PSP never draws, and folding it into a multiplying tint turned pale texels yellow.
- c40's and b40's holograms: "smallest scale" picked the scrolling blue band over the planet and ring art.
**Proven.** Tint reports before and after on a10, c40, b30, b40, d20; the tints that were right stayed close; emulator shots old bake against new [E] (`b121_tints_*.png`). New bake `campaign121` (lab 38524010598).
**Needs your eye.** b30's ocean moved to the add model (looks a shade bluer); a few drawn-map changes nobody had reported (a10's command display, the Pillar of Autumn's engine exhaust, maybe LK17's shimmer). `drawnmapscale` shows b120's choice beside it.

## 5. AI perception as a batch (`761f2d4`)

**What.** Inside `actors_update`: each unit's head, centre of mass and underwater test worked out once and shared; every actor's line-of-sight ray predicted and cast in a row at the start, each taken only if its inputs match bit for bit.
**Why.** AI is ~17 ms of b30's 57 ms tick; perception is its largest part.
**Proven.** Play and seeds identical on d20, a50, c20 [E]; `perccheck` 0 mismatches over 1.8 M facts and 158,000 rays. **On the rig** [M] (lab 38524100365, 38524222985): b30's beach tick **49.2 ms against 52.0 with `percold` (−2.8 ms, −5.4%)**; a50 −0.3 to −0.6 ms a tick (−1 to −2%).

## 6. The level sphere walk (`3c48620` … `c17bc51`)

**What.** `collision_bsp_test_sphere`, which every biped asks ~20 times a tick: each surface tested once per walk, the plane stack scanned only when it could match, a surface's edges walked once. A walk picks the engine's own code when walks have been small (c20) and the new one when large (a50, b30).
**Why.** a50's walks cross 47 leaves and test half their surfaces twice: a quarter of a50's tick instructions.
**Proven.** Play and seeds identical on a50, c20, d20 [E]; `surfacetestcheck` 0 differing walks over 1.4 M. Cache models: a50 −2.4%, b30 walk −0.5 ms a tick, c20 walk at parity [E]. Rig A/B queued.

## 7. Machines sleep (`f6d2617`, `e1f683f`)

**What.** b102's `machinesleep`, now on: a machine whose update would leave it byte-identical sleeps; a sleeping machine still reconnects its lights. The relink memo built alongside stays opt-in (`relinkmemo`; worth ~0).
**Why.** c20's 53 machines cost 6.5 ms a tick on the rig. b102 shelved this on the emulator's clock; the cache models give −2.6% of c20's tick, −2.7% of d20's [E].
**Proven.** Play and seeds identical on c20, d20 (across its BSP switch), a50; the light lists now match too [E]. Rig A/B queued.

## 8. The ME job ring and the slant split (`c21d91e`, `76a59fd`)

**What.** ME phase 1's plumbing: an 8-job ring beside the old slots (two counters on their own uncached lines, each written by one side), transfer checks, a stack sentinel, and a 10 ms watchdog after which the main CPU does the job itself and the ring stops for the session. The first job: the ME buckets the back part of each large level draw's triangles while the main CPU does the front.
**Why.** The ME sits ~95% idle; this is the hand-over L1 failed on, proven on a small job first.
**Proven on the rig** [M] (lab 38524258259, 38524293692): `meslantcheck` 0 of 414,000 triangles differing; ring test 0 wrong; 0 failed jobs. The sort costs 0.7–1.4 ms a frame less on b30 and 0.6 ms on c20; c20's drawing −0.4 to −0.5 ms. Small, but exact and real, so on.

## Campaign and multiplayer

- Everything compiles in the multiplayer build.
- The lab can't play a networked game on b120 or on b121 (the batch harness runs out of display-list memory; the netplay run sits at the menu on both), so multiplayer play is untested in the lab either way.
- AI runs on the server only, so a client never runs change 5. Changes 6, 7 and 8 give bit-identical results, so a client stays in step with the server [D].
