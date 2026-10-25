# Review: build 143 (the first playable tick of the w142 line)

For Sheepdog01 to read and question before testing. Build 143 is `w142` at `bea8aee`: build 141 (`review_b141.md`, 0915fb4) plus the w140 fixes after it and the tock's work. This page lists the net change per group, not each flip on the way. Every change has a keep word that turns it off (or on, for opt-in ones).

Labels: [M] measured on a PSP (which one is named), [E] emulator, [D] read in code, [U] inference. CAN'T TELL marks what the commits don't settle.

## 1. Fixes from your b141 hand test (24 Oct)

| Change | What and why | Key line | Word | Proof |
|---|---|---|---|---|
| **B1: the world no longer shows before it loads** (7056276, e3ff916, caa8707) | The settle's veil is drawn under the frame in flight. "Warm" now means nothing untextured and something drawn, counting model parts too (the menu has no level draws). One tick runs unseen first, so a10's settle draws its first view. Play starts a tick later (you, 12:44: fine). | `psp_loading.c:567` `early = !psp_keep("settleveiloff");` `:346` `warm = !psp_keep("settlewarmoff");` `psp_fixups.py:27617` `if (!psp_keep("settletickoff"))` | `settleveiloff`, `settlewarmoff`, `settletickoff` | settle2 40216672381: 157 veiled loading shots against 8 [E]. settle3 40218078646: no hang, a10's settle reads 21 textures against 0 [E]. |
| **B2: menu music runs through its tracks** (e1a94f8) | The attract countdown stopped the music before a movie the PSP doesn't have, so the music restarted every 75 s. | `psp_fixups.py:12017` `psp_attract = psp_keep("attractmode");` | `attractmode` restores it | bugs141: 8 restarts in 600 s [E]. The fix has no run of its own: CAN'T TELL. |
| **B3: HUD bars** (e55d737, 674fee1) | The meter palette cache also matches the bitmap's palette hash: a full bar drew empty until its words changed. The bake gives each meter its own four alpha levels, because 127 baked as 85. | `tools/texpack.py:684` `METER_ALPHAS = True` | `--no-meter-alphas` (bake) | Shield drawn on the PSP-1000 (40118652662) [M]. Bake b142m: health bar mean 149.7→152.3 [E] (meteralpha1 40219682500). Closeness to the PC is still to be shot. |
| **B6: doors whole, hands on the gun** (cfbc229) | b141's heap reserve refused skinning-plan allocations, so parts drew rigid. Plans now use plain malloc and the reserve keeps only the time-only tables. | none read | none (a fix) | PSP-3000 40110757027: failed plans 0 against 4 [M]. |
| **B8, B16: the Pelican and level draws no longer left out** (94e64da, 6cc50a8, 20aa1a8) | The ME's replay can't convert a stale model buffer or build a level copy, so it dropped the draw for a frame. Both are now made on the main CPU as the record is written, only when missing. Solid and see-through level draws are covered. | `psp_ge.c:14903` `model_pretouch = !psp_keep("modelpretouchoff");` `:10608` `level_pretouch_on = !psp_keep("levelpretouchoff");` | `modelpretouchoff`, `levelpretouchoff` | pelican5 40210280239: drawn on the frames it vanished [E]. b30 level copies missed 67→38, c10 1,019→487 (pretouch2 40210722973) [E]. PSP-1000 run 3 no flicker (40210756935) [M by eye, one run]. |
| **B22: marines' warped faces fixed** (915c809, ec54f7b) | modelpretouch rewrote buffers the ME still drew from. It now converts only when the ME is idle. | `psp_ge.c:14912` `if (!pretouch_me_busy())` | none | PSP-3000 40214028122 run 1 clean against run 2 [M]. |
| **B11: no console text on screen** (b71435a) | Output is drawn only while the console is open, after ChupathingyCE's `terminal_render 2` (Milenko, 3a4dee17, 827e688d). | `psp_fixups.py:11993` `psp_terminal_on = psp_keep("terminalon");` | `terminalon` | log keeps every line. No run id: CAN'T TELL. |
| **B13: level self-illumination** (466681f) | Level shaders had no glow pass. The bake makes a glow map per shader and colour, and the GE adds one pass. Needs bake b142b; older bakes draw as before. | `psp_ge.c:10017` `level_glow_off = psp_keep("nolevelglow");` | `nolevelglow`, `--no-level-glow-maps` | glow142 40126347679: Autumn's engines white again, control-room panels lit [E]. a10's cryo monitors barely change (open). |
| **B14: menu textures at full level** (7c47c90) | The GE's slope choice drew the hull blurred on hardware. ui.map only, under the showcase rule. | `psp_ge.c:5723` `tex_level_mode = psp_showcase_map && !psp_keep("showcasesharpoff") ? …` | `showcasesharpoff` | PSP-3000 shots match the emulator's (40108671359) [M]. |
| **B15: glow clamp** (6c12870) | The Xbox caps a lit part at its base. The clamp pass is now on, at the price of aliasing where the ring is edge-on. | `psp_ge.c:13085` `glow_no_clamp = psp_keep("glownoclamp") != 0;` | `glownoclamp` | api-job 40209002052. No shot comparison: CAN'T TELL. |
| **B19: dither lift on** (84f47f1) | The PSP truncates where the matrix assumes rounding. a10's frame means go from -4.8/-2.8/-4.6 to -1.0/+1.0/-0.8 against the PC. | `psp_platform.c:1073` `{ "ditherlift", "noditherlift" },` | `noditherlift` | look142 40113452183 [E]. You approved it 12:23. |
| **The canopy's smoked glass** (2f0806b) | The Xbox tints the frame behind glass before adding the reflection, and the port drew only the reflection. | `psp_ge.c:16460` `glass_tint_off = psp_keep("glassnotint");` | `glassnotint` | `review_canopy.md`: upper pane 33 33 42 against the desktop's 34 28 47 [E]. Play exact. Side panes still dim (listed there). |
| **Alert lights reach the walls** (a64f244) | Each wall takes the gel's face colour for its direction. | `psp_platform.c:1077` `{ "gelfaces", "nogelfaces" },` | `nogelfaces` | gelf142 40123748276 [E]. Provisional on (you, 12:23). Cost +8–15% in a10's hallway. Needs bake b142a on; older bakes fall back to the mean. |

## 2. B21's flicker: four fixes, one residue

| Change | What and why | Key line | Word | Proof |
|---|---|---|---|---|
| **flipwait** (94d7e1c, 645e1d3) | The next frame drew into the buffer still on screen, so big level polygons showed half drawn while the view moved. It now waits for the vblank. | `psp_trace.c:1226` `flip_wait = psp_keep("flipwait") != 0;` | `flipwaitoff` | PSP-3000 40166746467: b30 flight level, cinematic -1.9% [M]. Whether it cured the flicker: CAN'T TELL (your 10:20 run said not fully). |
| **Cut pieces fogged under the ocean** (d207e1f) | Pieces of a near-plane-cut seabed triangle got no fog, leaving a hard edge. | `psp_ge.c:9881` `submerged_cut_old = psp_keep("submergedcutold") != 0;` | `submergedcutold` | You, 10:49, PSP-3000 40208550769: dark shoreline sections cleared [M by eye]. |
| **nearcine not in first person** (6be8b32) | The b30 flight is a cutscene with the cabin at the eye, so the quarter plane made depth 4× coarser. | `psp_ge.c:4931` `near_cine = psp_keep("nearcineoff") ? 0 : psp_keep("nearcinefp") ? 2 : 1;` | `nearcineoff`, `nearcinefp` | You, 10:52: sawtooth and structure flicker cleared with nearcine off [M by eye]. Keyes' close-up at a10 t236 is settled by nearcine (you, 10:20, `bugs.md`). |
| **farcut** (632d6a4) | The GE clips only at the near plane, so a triangle across the far clip (1024 on b30) was drawn or dropped whole. It is now cut at the far clip. | `psp_ge.c:12225` `far_cut = !psp_keep("farcutoff");` | `farcutoff` | PSP-1000 40229418415 run 2: black areas gone [M by eye]. Slight missing polygons remain (known issues). |

## 3. Speed levers on by default (all exact unless noted)

| Change | Key line | Word | Proof |
|---|---|---|---|
| **MIXLOCK** (1c0c219): the game's sound calls never wait for the mixer's lock | `psp_dsound.c:1878` `mixlock_old = psp_keep("mixlockold") != 0;` | `mixlockold` | ml1 40164907768: play and seeds the same, a10 strict 0 sites [E]. PSP-1000 a10 40164905230: waits 6–9 a window→0, no audio gaps [M]. |
| **mechunk** (gated): the frame handed to the ME in pieces, only where the main CPU waits for it | `psp_ge.c:2192` `chunk_on = psp_keep("mechunk") && …` | `mechunkoff` | PSP-3000 ladder120 (40135302778): a30 7.7→12.7 fps, d20 11.4→13.7, b30 6.5→6.6 [M]. Exact within emulator noise (mechunk3 40121853488). |
| **HOIST, OBJALL, HDRBLK, TAGSNAP, DGFAST, UNITHOIST**: handle lookups done once | `psp_fixups.py:23519` `const int psp_hoist = !PSP_KEEP_ONCE("hoistold");` | `hoistold`, `objallold`, `hdrblkold`, `tagsnapold`, `dgfastold`, `unithoistold` | hoist1 40141826638: 1.3 M pointers, 0 differed [E]. hand1 40137748442, hand2 40146853007, hand2b: play and both seeds equal on b30, c20, d20, a30 [E]. Cache models: b30 about -1.4% fetches, c20 -5.3% tick fetches [E]. |
| **Mirror pass skipped** (7b32b0b): the pass never opens on the PSP | `psp_fixups.py:24209` `psp_mirror_pass = psp_keep("mirrorpass");` | `mirrorpass` walks it | mirror1 40129001627, mirror6 40130798979: play equal [E]. |
| **Ascending surface lists skip the sort** (fc80cf5) | `psp_fixups.py:15718` `in_order = !psp_keep("surfaceinorderoff");` | `surfaceinorderoff` | lplan5 40137889430 (lightplan exact) [E]. |
| **LVW** (7d9c9a8): the lruv caches' integrity walk is out of release builds, after halo-ce-vita's change (BirchWoodGod) | `psp_fixups.py:35008` `psp_lruv_walk = psp_keep("lruvverify");` | `lruvverify`, `lruvarraypass` | Exact on a30 and c10 but no gain there. d20's decal_new costs 1.0–1.4 ms (40116643721) [M]. No run shows the gain: CAN'T TELL. |
| **bsp3d_test_point trusted map** (7b9688a, 9dcdacf) | `PSP_MAP_CHECKS` guards it (`psp_fixups.py:11966`) | `PSP_MAP_CHECKS=1` | bsp3dg 40094684375 found a play difference without 9dcdacf; fixed [E]. |
| **BSPR v2** (d606a35): a biped's last collision answers kept in its own datum (4 sphere, 2 ray slots) | `psp_fixups.py:31874` (above) | `bspsame` | Exact on this base (40071989304). b30 -1.0 to -1.3 ms a frame [M PSP-3000, 40072862303]. a30 in the noise [M PSP-1000]. `review_bspr2.md` still says shelved: needs an edit. |
| **geowindow** (9cf4d17): the idle tag window becomes geometry cache | `psp_platform.c:1069` `{ "geowindow", "geowindowoff" },` | `geowindowoff` | Both ladders, 40092726147 and 40092793841: same fps, hitches and level changes [M]. |

PSP-1000 ladder45, tock against its levers off, at 86fa057 [M, one run each, `measurements.tsv:8989`]: b30 6.8/6.5, b40 27.8/27.2, c10 14.2/12.3, c20 14.5/13.2, c40 29.9/29.8, d20 8.0/8.2, d40 29.6/29.6 fps (on/off). This is before the late fixes in sections 1 and 2. A ladder for this ref: CAN'T TELL.

## 4. Off by default, or tools only

- Off by default: `particleprio` (a compromise, `psp_fixups.py:7715`, no gain at full budget, 40122713783). `lightplan` (`:23138`, d20 drawing -2 ms but tick +2 ms, 40138280704). `meterload`, still opt-in. `mebcc1` (the ME unit's word set to 1; after m-cid's psp-media-engine-reload, MIT, only the register value taken). `showcasesharp` is now default; see section 1.
- Tests and recon only: `spadexec`, `vfpuround`, `workcensus`, `soundclass`, the particle step labels, `hdump`, and the report line's rigid-by-why counts.
- Tooling: code layout `w142a_tgc90` (a build switch; the tick's release check and zip use it), `halo.secalign.txt`, the overlay's `RUN N/M` line (f452dd1, `psp_debug.c`), the layout fix for main and cold parts (3af0ad5), `tagdeps.py`.

## Things to question

- **Pretouch cost.** The b30 flight costs +2.4 ms (1.6%) on the PSP-1000 [M] against 0.9 ms in plan_preconvert. Which is right?
- **B8 on hardware** has one run, by eye. The Pelican vanishing was seen again at 21:27 and 21:52 before the fix merged, per `bugs.md:16`.
- **gelfaces' price** in a10's hallway, and flipwait's effect on the flicker.
- **Strict profile.** Done: `pgo/campaign-b143.tgz`, 13 counting runs at bea8aee (lab pgo-gen143a 40232744898, pgo-gen143b 40232748131).

## Proof to collect before this is a release

- The profile is made (campaign-b143). The release check rc143 (strict builds, `PSP_PGO_STRICT=1`, with and without `PSP_METRICS=0`, bake b143) is running; a PSP ladder on both PSPs is still to come.
- The bump to 143 in `psp_platform.c` is in.
