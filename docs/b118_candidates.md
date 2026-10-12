# The next build: candidates (2026-10-12, a draft while b116 waits for its PSP tests)

What comes after b116. Sheepdog01 saved build 117 "for something special", so the next ordinary build is 118 unless he says otherwise. Nothing here ships until b116's tests are done (`CLAUDE.md`: finish testing a build before making the next one). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code or tags, **[U]** inference.

## Restorations ready on the branch `hudbars` (on by default, Sheepdog01's rule)

| # | What's restored | How | Keep word (off) | Evidence |
|---|---|---|---|---|
| H1 | The sniper rifle's angle ticks: hidden unzoomed, scrolling blue ticks zoomed. b116 drew two white vertical bars on a50 instead | The ticks are two multitexture overlays, each a mask times a strip of ticks (map 0 to 1: multiply); the ticks scroll with the aim's pitch and fade in when zoomed (fade 0 unzoomed) [E, `hdump`]. The GE has one texture a draw, so the ticks are drawn in up to 32 strips and the mask is read from memory half a texel inside the strips' edges into their vertex colours. Each map's coordinates as the Xbox's screen vertex program works them out [D, `vshdump`]. Any other combination draws as before and is named once in the log | `hudonemap` | `hudmul-a50` (38228554871): unzoomed nothing, zoomed the ticks with their arrows, faded at the ends, moving with the aim [E]. After an independent review's fixes: `hudmul2-a50` |
| G1 | Glass on models lit, not full-bright | The Xbox's glass diffuse pass lights the diffuse map [D]. Each vertex takes the model's ambient colour plus its distant lights by the normal's angle to each (`seethrough_lit`), as fade mode 3. The reflection (cube map) and tint passes still aren't drawn | `glassflat` | `glass-a10` (38233051980): lit 140-300 glass parts per 300 frames, but no change visible in a10's opening. a10's green pattern turned out not to be glass (below) [E] |

Instruments on the same branch: `hdump` (each HUD quad of a screenshot frame with its bitmap's tag, place, colour and blend; a multitexture overlay's every map), and `tdump` now names each see-through draw's map.

## Exact speed-ups

**The profile-guided build (L4)**, branch `pgo116` (on b116). The final measurement used a profile from five stretches (`pgo/campaign5.tgz`, lab run 38231877703, merged with `psp-gcov-tool`): b30's opening flight, b30's beach bench (b113 test 1's route), and the openings of a10, c10 and c40. The "lean" build uses partial training, without unrolling, peeling, tracing or cloning:
- program 3,292 KB against b116's 3,343;
- heap 5,900 KB against 5,852.

It ran in the cache models against b116, each run to the same 3,600 ticks (`exitat120`, lab run 38234283909) [E]. Cycles are estimated as instruction fetches plus 40 a miss [U].

| Stretch | In the profile? | Tick | Drawing | I misses (tick, drawing) |
|---|---|---:|---:|---:|
| b30's flight | yes | −6.1% | −9.2% | −21%, −22% |
| b40's opening | no | −1.6% | −5.5% | −16%, −13% |
| d20's opening | no | −1.9% | −5.0% | −15%, −15% |

- **Where the gain comes from:** mostly instruction-cache misses: the profile puts the code that runs together side by side.
- **Exact:** b40 and d20 played the same tick for tick against b116 (game state's play hash). b30 parts at tick 1357, where any drawing change parts it (#25, the Elite in slot 947) [E].
- **A first try with one stretch's profile** (b30's flight; lab run 38225204020) gained under 1% on a10, which wasn't in it. GCC's own defaults also grew the program by 52-192 KB; at the top end that put the heap under a10's peak.
- **Only the PSP can say how much it's worth:** its pipeline and branches aren't in the model.
- **The PSP test:** `halo_psp_b116_pgo.zip` is b116 built this way. Test 7 in `handoff_2026-10-12.md` times it against b116.
- **The build:**
  ```
  PSP_PGO=use PSP_PGO_PROFILE=campaign5 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone
  ```
- **When the code changes:** a profile goes stale. GCC drops the counts of any function whose branches changed, so make a new profile before a release (`PSP_PGO=generate`, `gcovdumpN`, the lab's `pgo-gen5-b116` batch).

## Identified tonight, not fixed

Both are see-through shaders that combine several maps, of which the GE draws one. The skip tests left out one map at a time (`tskipN`) [E].

- **a10's green pattern** is `levels\a10\shaders\cryo glass`, on the level's panels around the bridge.
  - The evidence: it goes with `nooverlays` and with the cryo glass map left out, and stays with the monitor glass or the space sky's star mask left out (`a10window`, `a10window2`).
  - The shader: a chicago shader drawn alpha-multiply-add. Its map 0 is a cube map (a reflection) the GE can't draw, so map 1, the frost, is drawn with a tint from the shader's mean colour (`20469494`, doubled) [D, bake log].
  - Why it shows: the mean is dark (0.04), but the frost's sparse bright texels show at full contrast. On the Xbox the reflection modulates them [U].
  - Its maps (`sierra-tags`, 38237182457): `cube map glass` and `cryo glass`, plus an extra layer, `cryo glass tint`.
  - The fix: read the maps' functions. Then either the bake keeps the frost's contrast down, or a two-pass draw stands in for the cube map. The 5650 framebuffer has no destination alpha to combine maps in [D].
- **c40's pale discs** are `levels\b30\devices\interior tech objects\bitmaps\static blue band vert`, an animated band on the control room's hologram panels.
  - The evidence: with it left out, every disc goes (`tskip2-c40`); the hologram panel, teleport ring, ring markers, the Monitor's glow rings, the Forerunner text and the soft flares made no difference.
  - The shader isn't certain. The panel's own, `interior tech objects\holo control`, holds the band as map 0, with the panel's picture and a readout as maps 1 and 2 (`sierra-tags`, 38237207232). But the bake says it draws map 1, so another shader holding the band may be what's drawn [U].
  - Why it shows: on the Xbox, another map (a mask or the band's scroll) confines the band. Alone, it covers the whole disc [U].
  - The fix: find which shader draws the band (a `tdump` line that names the shader, not only its map), then give it its other maps' say.

## When it becomes the build

- `PSP_BUILD_NUMBER` 118 (`psp_platform.c`).
- `hudonemap` and `glassflat` go into the log's list of the restorations' keep words, and into preset 6 ("the b115 look", every restoration off).
- Merge `pgo116` once the PSP shows it gains (an exact speed-up: no A/B of the look needed); it touches `psp_build.py` and `psp_platform.c`, and `hudbars` neither.
- The usual final checks: the strict interpreter on all ten maps and the tour, on the release commit.

## From b116, still open

- b30's deep water: the Xbox fogs what lies under water by its depth below the surface; the GE fogs only by distance.
- c10's vines.
- G8's trims. The final tour's pool peaks (dashboard, Maps):
  - actors: 35 of 256, 457 KB;
  - props: 195 of 768, 234 KB;
  - glow particles: 0 of 512.
  - Openings only, so a full playthrough's peaks come first.
- L1 step 4: the Media Engine building the GE's lists.
