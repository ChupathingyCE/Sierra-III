# Renderer debugging notes (builds 52–90)

Lessons from chasing visual and timing bugs in the GE renderer (psp_ge.c), the platform layer, and the texture bake (tools/texpack.py). Each item: symptom → cause → how it was found. Epistemic status in brackets.

## Tools for reproducing a spot

- **teleport.txt (build 59).** ms0:/halo/teleport.txt holds `x y z fx fy fz`: the player's feet (as the heartbeat's "player: at" prints them) and a direction (a screenshot's "looking"). The player is put there 11 s in, then every 5 s for a minute.
  - Feet z is roughly the screenshot's camera z minus 0.62.
  - The file can be rewritten while the emulator runs, to sweep several views in one run.
  - Combined with input.txt, PPSSPPHeadless reproduces a spot from Sheepdog01's screenshot log with no hands. Codes: select+right = `0021` takes a screenshot; right = `0020` switches weapon. [tested in emulator]
  - /tmp/claude-0/sh87.sh `<label> "<keep words>" "<teleport line>|none" <downs> <rights>` runs one Blood Gulch view headless and can step through the debug menu (`WALL=` seconds). Runs share /tmp/ms, so never run two at once. [b87]
- **benchrun (b87, launch menu).** Four fixed Blood Gulch views × five switches (baseline, ME audio off, stencils old nudge, GE serial, level compact), ~4 minutes standing still. Each step is one heartbeat window and prints a self-contained `bench: RESULT view … | switch … | ms a frame …` line; `tools/benchparse.py` tabulates them. [hardware: b87 run 1; results in memory_traffic.md]
- **visdump keep word (b59).** Logs the game's visibility walk every 150 frames: camera, leaf, cluster, rendered clusters, surface count. It separates "the game chose not to draw it" from "our renderer lost it". [tested]
- **skydump keep word (b60).** Logs where sky parts' corners lie from the eye. [tested]
- **tdump keep word (b63).** On a screenshot frame, logs every see-through draw: key, kind, blend, distance, nudge, depth test. [tested]
- **walldump keep word (b64).** For each see-through level triangle asked about, logs whether it counts as on a wall, and the nearest structure surface on each side within 25 cm. [tested]
- **Debug menu (b77, select + left) and overlay (b80).** Live settings for the renderer, nudges, passes, stencils, audio mixer; changes are logged with `[t seconds]` stamps, so a run can be cut into windows by setting. The overlay (on unless `nooverlay`) shows FPS, ticks/s, frame ms, ME busy % and T (seconds since drawing started), to match against Sheepdog01's notes. Grenade switch moved to select + L. [hardware-tested]
- **Launch menu (b66).** The menu writes ms0:/halo/keep.txt and init.txt itself. A keep.txt hand-written before a run is read into the menu's toggles and rewritten on START; unknown words survive. ms0:/halo/input.txt or nomenu.txt skips the menu (scripted emulator runs rely on this). [hardware-tested]
- **Texture key → tag name:** `scratchpad/keys.py <key>...` maps a bitmap_data address (the key in the logs) to its tag. Examples:
  - 0996e75c = teleporter_shield
  - 09971b94 = lights small strips
  - 09cf8d34 = black light
  - 09cf8690 = cap_moss01b (the cave fungus)
- **Screenshot BMPs are 565.** The clear colour 0xFFD8B080 reads back as (131,178,222); exactly that colour means nothing drew there. [measured]
- **Check the build line first.** Every log starts "Halo PSP bring-up build N". b60_run1 was really build 59. [observed]
- **Log file dates show the PSP's clock state.** Dated 31 Dec 1984 = the clock was unset during the run. [observed b74/b75]
- **Limits of the emulator:**
  - It doesn't show texture-level choice (the bands, the grazing-angle blur), 16-bit depth disagreements, equal-depth fighting, or a broken real-time clock.
  - It runs no Media Engine code (psp_me.c simulates each job on the main CPU and says so in the log).
  - It does reproduce geometry loss near the eye, shading and state bugs.
  - Hardware runs are the only test for the first group. [observed]
- **Bisecting a hang with keep words (b66).** A build that printed nothing after "GE: ready" was found by running four times with one new feature disabled each (noboxtest / detailallfar / noangle / verifytags). The culprit was an array sized `ANGLE_BUCKETS` indexed up to `SORT_BUCKETS` — stack corruption. Lesson: when a bucket count doubles, grep every array dimensioned by the old name. [found and fixed in the emulator; never reached hardware]

## Timing bugs found on hardware (b70–b75)

### Game frozen at game time 0 (b70 → fixed b71)
- **Cause:** the PSP's C library answers `clock_gettime(CLOCK_MONOTONIC)` with the wall clock (`sceKernelLibcGettimeofday`). With the PSP dated 1984, the first frame's elapsed time came out negative and `Sleep()` asked for weeks.
- **Fix:** `GetTickCount` and `QueryPerformanceCounter` read `sceKernelGetSystemTimeWide()` (µs since boot). The frame pacer never sleeps longer than a frame. [confirmed on hardware]

### Frames slowing down without end, 30 → under 1 fps (b71–b74 → fixed b75)
- **Symptom:**
  - render_frame_present was 85–99% of each frame, and the frame time grew steadily. The `budget:` line's "the rest" bucket reached 500–1,650 ms.
  - The frame end itself was tiny: GE wait 0.2 ms.
  - No Memory Stick reads during play. The emulator held a flat 30 fps.
- **Cause:**
  - d3d8_gl.c emulates the Xbox's 60 Hz vertical blank with a thread that paces itself on `CLOCK_MONOTONIC`, which is the wall clock on the PSP.
  - Present waits on that thread once two frames are queued.
  - With the PSP's clock unset (battery pulled), the wall clock misbehaved and the thread's sleeps grew longer and longer.
  - The b71 fix only covered GetTickCount.
- **How it was found:**
  1. The b74 per-frame-end breakdown ruled out the GE, flush, swap and caches.
  2. Sheepdog01 reproduced it at will: setting any date cured it; pulling the battery and leaving the clock unset brought it back.
  3. `psp-objdump` of halo.elf listed every caller of `clock_gettime`.
- **Fix:** psp_compat.h maps `clock_gettime` to `psp_clock_gettime`, which answers the MONOTONIC clocks from `sceKernelGetSystemTimeWide()`. CLOCK_REALTIME stays the wall clock, for GetSystemTime's date and for WaitForSingleObjectEx's deadline, which pthread compares with `ftime`.
- **Status:**
  - [b75 hardware: "the rest" ≈ 3 ms; 25–29 fps moving, 17 fps at the hill vantage (CPU-bound).]
  - The b75 run's files are dated Oct 5, so that run had the clock set. A run with the clock unset is still to do.
- **Lesson:** grep the *binary* for every caller of a libc time function. The C library's mapping isn't what Linux code assumes.

### Wrong guesses on the way (kept for the method)
- **b72, "Memory Stick contention":** reads were made synchronous. Partly better, still slow. The b73 io: line then showed zero reads during play, which ruled it out.
- **b73, the texture cache carved for geometry:** a real mistake (the textures no longer fitted), fixed by giving geometry its own 1 MB heap arena. It wasn't the slowdown.

## The near-the-eye pipeline (how geometry reaches the GE)

- **Why the CPU has to help.** The GE has no real clipping. A corner behind the eye, or past ±2048 px (the guard band), wraps around and draws as a spike, or loses the triangle.
- **What the CPU does:**
  - Level triangles are classified against near plus four guard planes (6× the screen): kept, skipped, or cut (`clip_triangle`).
  - Rigid model parts are cut the same way since b60 (`model_cut`); see-through parts since b61 (`transparent_cut`). [both confirmed on hardware]
  - **Skinned (bending) parts since b87 ("skincut", on by default; `noskincut` reverts).** Before, a bending triangle reaching past the near plane was dropped whole, because its corners only exist after the GE blends bone weights: the tank's treads and the warthog's wheels opened holes with the driving camera close (Sheepdog01's b79 shots 5 and 6). Now its three corners are bent on the CPU with the same weights and node matrices, cut like the level's, and drawn already bent (identity model matrix), normals turned alike so lighting matches. The log counts "bending triangles cut at the near plane". [emulator-tested; hardware pending]
- **Box pre-test (b66).** Each level stream knows its bounds. If all eight box corners are inside near and the guard planes, the whole draw is kept without classifying a triangle. `noboxtest` reverts. Pixel-identical. [measured in emulator]
- **Cut once (b66).** Cut vertices are prepared once per draw with base uv, lightmap uv and world position, and each pass reads its columns. Exact. [emulator]

## GE state that must be right for each kind of draw

- **The model matrix:** `world_model()` on every exit from a model draw (b59).
- **The projection and depth slice:** world, sky, or first person. Every queued draw sets which one it wants (the level slots didn't before b63).
- **Depth test after HUD quads:** restored on early exits (b63).
- **Per-frame baseline (b63):** depth test on, GEQUAL, no depth offset.
- **Shade model:** Gouraud from b62 (`flatshade` reverts).
- **VRAM addresses are absolute (b80).** See the debug-menu bug below.
- **General rule:** a reused slot or a GE state must be fully set by whoever uses it, never inherited.

## Texture level choice (b58–b66)

The GE picks a texture's mip level in one of two ways:

1. **AUTO**, its own choice. On the PSP this goes blurry at grazing angles; the emulator chooses sharper and hides it. [hardware observation; the GE's formula is unknown]
2. **SLOPE**, by depth only: level = log2(2 × w × slope) + bias. Crisp, but it ignores the surface's slant. [hardware]

**b64 defaults (unchanged through b87):** SLOPE for base and detail passes; detail bias 0; detail fade 20–60 units; detail mips from the Xbox chain (pack 59); angle buckets on top.

**Angle buckets (b64):** each level draw's triangles are sorted by 1/cos(angle to the eye ray) into 7 buckets, half a level apart, each drawn with slope × factor. b64 hardware: ground wobble "much less". [Sheepdog01, b64]

**Far buckets (b66):** triangles wholly past the detail fade get their own buckets and no detail pass. ~~Exact.~~ Not exact (2026-11-12, the lab's row `detailnear_not_exact`): the buckets test each corner's distance from the eye, while the GE's fog fades the detail by view depth, so an off-axis triangle past DETAIL_FAR can still show some detail. `detailallfar` reverts. [measured in emulator]

**UV centring (b79, on by default; `uvwide` reverts):** each level draw's texture coordinates are shifted by whole repeats (`sceGuTexOffset`, after the pass's scale) so the vertex nearest the eye sits near zero. Exact for repeating textures. Aimed at the ground wobble's precision hypothesis (detail coordinates reach hundreds of repeats on Blood Gulch's ground).

**Keep words:**
- `noangle` turns the buckets off.
- `detailauto` gives AUTO for detail.
- `detailbias15` gives the old 1.5 bias.
- `detailnear` gives the old 8–24 fade.
- `mipauto` gives AUTO for everything.
- `mipbias1` / `mipbias2` give a coarser base level. [untested on hardware]

## Bugs and causes (b59–b76)

### Decals, stencils and painted symbols showing through walls for a frame or two (b76)
- **Symptom (Sheepdog01, b75):** every kind of 2D object (wall stencils and other decals, painted symbols) is visible through walls for a frame or two, in more and more places.
- **Cause (from the code):**
  - The slope part of the depth nudge (`slope_units`, b60/b65) divides by the distance from the eye to the surface's plane, so it explodes as a surface turns edge-on. It was capped only at 4096 steps.
  - A depth step is z²/(near × 61439). With nearauto ×2 that's 0.33 units (about a metre) at 50 units, so a few hundred steps far off is tens of units.
  - Decals made it worse: one `sceGuDepthOffset` per decal draw took the *largest* quad's need. A stencil seen edge-on beside the eye gave every decal in its batch, far ones included, hundreds of steps.
- **Fix, b76:** the slope part capped (`slope_units_capped`, the "polygon offset clamp" idea from graphics APIs); each decal quad carries its own slope part, and the draw's depth offset is 0. `nudgewide` removes the cap; `decalbatch` restores the single offset per draw. The cap was re-measured in b80 (below).
- **Possibly the same cause:** the occasional black specks on distant canyon walls (b75 shots 002/003), as decals or overlays from behind the wall winning the depth test. Unconfirmed. The lightmaps were checked and have no dark gutters, so lightmap bleeding is ruled out. [measured]

### Teleporter beam drawn through its frame near its axis (b64 wall test, b65 push-back)
- **Cause:** b60's slope nudge treated the free-standing beam like a symbol on a wall.
- **Fix:**
  - b64: `overlay_on_a_wall()` asks the collision code whether a structure surface lies within 4 cm along the triangle's normal; only on-wall triangles get the slope part.
  - b65: free-standing triangles are nudged *away* from the eye, so the solid frame wins the tie.
  - `overlaynowall` gives the b60–b63 behaviour.
- **Status:** Sheepdog01's b75 run says the teleporter's edges are still visible. A screenshot is needed.

### Decals cut off along ground seams (b65)
- **Cause:** decals had a fixed pull but no slope part, so on ground seen at a slant half a quad fell behind the surface.
- **Fix:** b65 added the slope part, made per-quad and capped in b76.

### Painted symbols, fungus and the beam showing through walls with the pistol out (fixed b63)
- **Cause:** `transparent_slot()` never set `first_person`. [confirmed on hardware]

### Faceted lighting on the weapons and terrain (fixed b62)
- **Cause:** `GU_FLAT`. [confirmed on hardware]

### The sky's planet vanishing while in view (fixed b61)
- **Cause:** see-through parts weren't cut at the near plane. [confirmed on hardware]

### Level vanishes at some spots; warthog-entry flicker (fixed b59)
- **Cause:** a model draw's early returns left a node matrix loaded. [confirmed on hardware]

### Wall symbols partly missing, cut off along a line (b60 per-triangle nudge; b62 two pixels)
- **Cause:** the GE's 1/16 px and whole-step depth rounding.
- **Status:** "much less severe" (b62); some shimmer remained (b64). Superseded by the b87 stencil bake for stencils on walls.

### Canyon bands (fixed pack 59)
- **Cause:** the bake ignored the Xbox detail mips' built-in fade. [confirmed on hardware]

### Near plane: depth precision vs walls cut open (b64 "nearauto")
- `near4` removes the rock-base shimmer but cuts open anything within 0.25 of the eye.
- **nearauto** sets the near plane per frame from a collision sphere test and the nearest model (candidates widened in b84, below).
- **b75 hardware:** ×2 for 300 of 300 frames at the hill vantage, ×4 mostly elsewhere. Sheepdog01 reports no clipping issues.

## Builds 77–87: what was learned

### Wall stencils and decals: why nudging lost, and the bake (b76–b87)
The nudge (pulling a decoration toward the eye by some depth) went through six revisions. Each fixed one distance or slant and broke another:
- **b76** capped the slope part at 0.03 units *along the line of sight*. b79 run 3 showed it held back 80–98% of slope nudges; the symbols at red base tore across and one sank.
- **b80** measured the cap as **lift off the surface** (a share of the distance, 2%, never under 0.01 units). A nudge along the view moves a slanted wall's point very little off its plane, which is exactly the case that needs a big nudge.
- **b82** added a fixed **floor** of 0/16/64/256 steps as an instrument. Sheepdog01's run: the base emblems needed 16 steps from the tank at 20–30 units (0.4 units of lift), none at 4 units, where 256 steps (0.27 units) saw through the pillar. A fixed step count lifts far stencils through hills (16 steps at 100 units is 10 units).
- **b83–b84** made the distance part a lift = min(share × distance, ceiling). The defaults became 1% and 0.2 units, Sheepdog01's close-range best, with the lift limit off.
- **The lesson:** sinking is counted in depth *steps*, while bleeding through what's in front is counted in *units*. The ratio between them changes with distance, so no single nudge is right at every distance. **b87 removes the guess:** the stencil bake cuts walls under stencils and draws the stencil on the wall's own pieces with depth EQUAL. See `stencil_bake.md`. The old nudge stays as a debug-menu mode and for stencils not on a wall. [hardware: b87 run 2 clean; cost measured in run 5, see stencil_bake.md]

### Equal-depth passes: only exact when they repeat the base draw (b85–b86)
- **Symptom (b79–b84):** bright dotted stripes beside the tank and on the bunker wall.
- **b85** tested the shadow pass three ways (its own copy with EQUAL; the wall's vertex array with EQUAL; a GEQUAL nudge). Sheepdog01: no difference, so the stripes weren't shadows.
- **b86** tested the dynamic light pass the same way. Copy + EQUAL striped, and so did the level's own array + EQUAL (whole triangles). **The GEQUAL nudge (1% of the distance to the nearest lit corner, ≤0.2 units) removed them** and looked right. It is the default since b87; the debug menu keeps the others. [Sheepdog01, b86 run 1]
- **The lesson:** on this GE an EQUAL pass is reliable only when it repeats the base draw's exact vertices *and cuts*. The lightmap and detail passes do; a copy doesn't. The level-array mode probably failed because the body had cut near-eye triangles at the near plane and the light pass drew them whole (a hypothesis, not tested). The stencil bake's layers copy the pieces' cuts for that reason.

### The debug menu purpled the HUD (b77–b79 → fixed b80)
- **Symptom:** the HUD's textures (loaded once) turned purple for the rest of the run, in about half the runs.
- **Cause:** the menu's text screen was set up relative to the *current* draw buffer. When that was the second buffer, its clear landed 256 KB further on, over the start of the VRAM texture cache.
- **Fix:** an absolute address. **Lesson:** "the current buffer" is one of two; derive every VRAM address from the absolute layout. [confirmed on hardware]

### GE antialias flag (b79 → removed b80)
`GU_LINE_SMOOTH` on triangles painted every texture as blocks and checkerboards (Sheepdog01's b79 shots 000/002/014/015). On the PSP it is a line feature; PPSSPP reads it as a screen-position texture lookup. [hardware]

### Temporal blend, "Soften" (b77–b78, off by default)
A cheap anti-aliasing pass: blend each frame with the last, with a quarter-pixel jitter.
- **Jitter vibrated** (b77), and has been off by default since b78. The blend feeds its own output back, so the two jittered positions get 2/3 and 1/3 weights alternately, and the image rocks by about a sixth of a pixel at 15 Hz.
- **The blend costs 7–8 ms a frame** on hardware (b78), because a full-screen buffer goes through RAM. That cost is the evidence behind `memory_traffic.md`. It stays in the debug menu, off.

### On-wall answers cached under a reused address (b79–b80 → fixed b81, enlarged b84)
- **Symptom:** symbols at red base tore along triangle edges, and one sank entirely.
- **Cause:** the "is this triangle on a wall" answers were keyed by the index pointer. Since b68 the indices live in geometry-cache blocks, and a block is reused once evicted. Red and blue base are mirror copies with the same counts, so red base found blue base's answers and nudged some triangles *away* into their wall.
- **Fix:** key by the pack address, which is the data's lasting identity. b84 grew the table from 512 to 2,048 entries, replaced round-robin, after Sheepdog01's b83 run overflowed it (31,250 lookups were treated as on a wall, the beam among them).
- **Lesson:** anything cached about streamed data must be keyed by the data's identity, never its current address. [confirmed on hardware]

### See-through draws skipped when the display list filled (fixed b82)
- **Symptom:** b79 run 3 at red base, in the tank: 188 draws skipped, a piece of the reticle's bracket missing, and 2D items flickering as the list's size crossed the limit from frame to frame.
- **Cause:** see-through draws are flushed last, so they were the ones dropped when the world filled the list.
- **Fix:** the world's solid draws stop 32 KB earlier (`TRANSPARENT_RESERVE`), so the see-through draws always have room.

### Near plane: finer candidates and the driven vehicle (b84)
- `nearauto` now tries ×16, ×8, ×4, ×2 and ×1. Each doubling halves the depth step at every distance.
- A model sphere that *contains* the eye (the vehicle being driven) no longer holds the plane down. b79's tank runs sat at ×1, four times coarser depth than on foot. A model that close is cut triangle by triangle anyway.

### b87 instruments
- **Ticks per frame histogram** in the heartbeat ("frames by game ticks since the last frame: 0..4+"). With interpolation off, the PSP default, frames alternating 1 and 2 ticks move the world in uneven steps. That is the candidate cause of the warthog's donut bounce (Sheepdog01 b80). `interpolation` (launch menu) turns the game's own interpolation on to compare. [hardware: b87 run 3, see frame_rate_targets.md]
- **levelcompact** (test, off by default): level vertices at 10 bytes instead of 20. See `memory_traffic.md`. The emulator shows the textures' mip choice slightly sharper with it, which is unexplained; the emulator's level choice isn't the hardware's, so judge on the PSP.
- **meclassify** (measure only, off): the Media Engine classifies the level's vertices again and the answers are compared. See `media_engine.md`.

### The exit never reaches the Memory Stick (found b87, diagnosis in b88)
- **Symptom:** no hardware log since b70 contains the exit callback's "HOME: exiting" line, and the learned-textures file (`maps/<map>.vram`, written at map close and HOME since b47) was never written. Every log says "learned binds not yet saved". b47–b64 logs read b46's file, which b46 wrote *during* play; it vanished when the b68 packs replaced the maps.
- **Cost:** the VRAM texture set has been "largest first" since b68. b87's bench served 90% of texture binds from RAM, where the GE's reads slow the CPU (memory_traffic.md).
- **b88:**
  - the learned counts are saved during play, from the heartbeat thread;
  - the exit callback writes `log/exit.txt` straight to the file at each step (no formatting, no lock), so the next run shows how far the exit gets;
  - the callback thread's stack grew from 4 to 16 KB;
  - log-lock waits during exit time out after 100 ms.
- **Lesson:** anything that must persist goes to the Memory Stick during play, not at exit. And when a log line expected every run never appears, check why before trusting the feature behind it.

### benchrun's first view was inside the floor (b87 → fixed b88)
The blue-base spot had z 0.20, while Sheepdog01's b86 standing spot reads 0.53. The game pushed the player out 0.8 units, and b87 re-placed him every window, so view 0's numbers were noisy (Sheepdog01 saw the repeated teleports). b88 uses z 0.55 and re-places only on a view change or a drift over 0.3 units.

### Builds 88–90 (b87 hardware runs 2–5)
- **The tank clipped open on foot (b87 shot 029; b84 → fixed b90).** b84 stopped a model sphere *containing* the eye from holding the near plane down, for the driven vehicle. On foot, pressed against the tank, its sphere contains the eye too, so the plane stayed at ×4 and cut the hull. b90 applies the rule only while seated (`psp_player_seated`). [emulator: ×1 against the tank on foot, was ×4; hardware pending]
- **Red base's large symbol cut into the wall (b87 shot 011, every stencil mode).** It is a *scenario* decal: Blood Gulch places 8 permanent ones (the blue and red base symbols), made once at load by `structure_decals_update`. The game builds them from the collision's triangles, which don't match the render triangles, so part of each sinks. b90 draws them on the level's own triangles: each decal quad's plane and texture mapping become a GE texture matrix (`GU_TEXTURE_MATRIX` from position), and the level triangles on that plane this frame (kept whole, or the near-plane pieces) are redrawn with depth EQUAL. Debug menu "Painted symbols (decals)": on the wall (default) or nudged (old). [emulator: symbols whole at both bases; hardware pending]
- **Level vertices 16-bit (levelcompact) opens the ground's edges** (b87 shot 020, gone with float, 021). Known from the code review (negative texture coordinates read as unsigned; the float passes no longer match). Leave it off.
- **The tank's shadow vanishes when pressed against it** (shots 024/025). Not explained yet; recheck on b90, where the near plane no longer stays at ×4 there.
- A decal on a wall at the left of blue base is missing or partly drawn (shots 014–019) in all modes. Not explained yet.
- **Object lighting read garbage since -G (fixed b89)**, the distant light and its shadow vector included: code_review_b89.md.

### Builds 90–92 (b90 and b92 hardware runs)
- **Brightness steps along triangle edges on the base floors (Sheepdog01, b90 run 1, shots 000–004; present since b64, emulator reproduces it).** The detail passes chose their texture level per triangle (the slant buckets). Bungie's detail maps fade level by level toward a colour that isn't their top level's average: measured on Blood Gulch, "detail metal sratches" loses 43% of its brightness by its eighth level, "detail grass" drifts purple. Two neighbouring triangles in different buckets so got different brightness. `noangle` and `nodetail` both removed the step, which located it. **b91:** detail passes use the GE's per-pixel level by depth (no buckets); base maps keep the buckets, since their levels keep their average. Debug menu "Detail map level": by distance (default) / slant per triangle (old). [hardware, b92 run 1: the floors look uniform; Sheepdog01: textures also look slightly more consistent in quality]
- **b90's decal projection cost 3–15 ms a frame** (benchrun: renderer CPU at red base 44.5 ms against b87's 31.7; the "decals" part 15 ms). It tested every triangle the level drew against every decal plane, each frame, and draws with near-plane pieces made that the ground's whole list. **b91** remembers the triangles under each plane (rescanned every 16 frames, every 4 while empty) and skips remembered triangles the classifier would have cut (the cut pieces are still matched each frame). Emulator: red base front 8.2 → 0.8 ms, blue base 8.0 → 1.2 ms; symbols unchanged. Hardware (b92 run 1, free play): 0.3–1.7 ms a frame.
- **levelcompact's apparent gain in b90's benchrun is that cost, not the format:** the projection doesn't run on 16-bit copies, so the switch also turned it off.
- **The learned VRAM set now saves during play** (b90 run 1: saved twice; run 2: "learned binds read; VRAM set by binds per byte"). Its effect on frame time was hidden by the decal cost; b91's benchrun is the clean comparison with b87.
- **b92:** the launch menu's options page fits the screen (lines of 67 columns: a full 68-column line wraps by itself and its newline adds a blank row) and is drawn into a second buffer and shown whole (no flicker); screenshots carry a stamp (build, run seconds, frame) and the seconds in the file name. [hardware, b92 run 1]
- **ARK-4's Kommander menu "Exit" skips the game's exit callback** (b90 run 1: `exit.txt` holds only "armed at start-up"; Sheepdog01 has only ever quit that way). It ends the game without the callback, like ARK's button combos. Anything that must persist is saved during play; nothing should depend on the exit.

## Open (after b90)

- **Hardware checks for b90:** the scenario decals on the wall (both bases, "Painted symbols" toggle); the tank on foot; the learned VRAM set saved during play and `log/exit.txt`; vblankthrottle as the default.
- **The stencil bake's cost** on stencil-heavy maps (5 ms a frame on Hang 'Em High): the cheaper path in `stencil_bake.md`.
- skincut near tank treads: no hardware report yet.
- **CPU-bound at the Blood Gulch blue-base spot** (b86 run 2, standing):
  - 62–63 ms a frame (16 fps): ticks 8.5 ms a frame (1.86 ticks × 4.5 ms), the frame 47.2 ms, the rest 6.6.
  - The GE renderer's own CPU work is 17.5–19.1 ms of the frame: level 8.2–9.3, model parts 5.3–6.3, see-through 2–2.4, decals 0.8–1.1.
  - Profile: structure_render_lightmaps 11.0 ms, rasterizer_model_draw 9.0, objects_update 7.8, structure_visibility_find_clusters 5.5, frame end 4.3, object LOD 2.4, decals 2.1.
  - The GE waits 0.3 ms a frame, so it is idle most of the frame. See `frame_rate_targets.md`.
- **Geometry streaming hitches:** most misses are at start-up (b86: 198 loads, 827 KB; worst frame 208 ms; b87 run 5 on Hang 'Em High: all 276 misses in the first 6 frames that missed, worst 211 ms, none after), then a few more through a run. Sheepdog01 feels them as brief pauses in the first ~30 s. Warming the cache at map load is next; see `geometry_streaming.md`.
- **Ground texture wobble:** UV centring has been on since b79. Its effect on the wobble is not recorded here; the next ground close-up should say. Untested alternative: `mipbias1`.
- **Black specks on distant walls; teleporter edges:** last seen before the b80–b84 nudge changes; screenshots needed.
- **Soft cutouts (softcutout):** anti-aliased alpha-tested edges (foliage, fences) were deferred in b87 as a nice-to-have.
- Shimmering plant at distance (b62 shot 11); a line where wall meets ground (b59 shot 49); flashlight fidelity.
