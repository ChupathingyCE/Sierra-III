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
- **c40's pale discs** are the control room's hologram of Halo [E]: `levels\b40\devices\holo halo ring\shaders\holo halo outer ring`, `... holo halo inner ring` and `levels\b40\devices\holo halo solar system\shaders\holo halo solar system`.
  - What's drawn: each draws its brightest map, `static blue band vert`, an animated band (mean 0.67 0.88 0.90), over the whole ring or disc. Tints are 0.1-0.2, added (`tdump3`, 38242088390, with the shader named; `tskip2-c40`).
  - On the Xbox the shaders' other maps shape the band into the hologram's rings [U].
  - The fix, like a10's: the maps' functions first. Then either draw the shaping map, or tint by it.
- **The same in a10:** `levels\a10\shaders\cryo glass` draws its frost on both the level's panels and models (65 draws in one frame); the Pillar of Autumn's engine exhaust (`poa engine exhaust`) also draws the static band.

## Exact speed-ups from an independent review (branch `speed118p`)

A second review read the GE layer for exact speed-ups, keeping the picture byte for byte. Five are built on `speed118p` (on `pgo116`, for `exitat`):
1. **The overlay flags:** the see-through level lists' "on a wall" flags remembered per list. Each triangle had searched a table of up to 2,048 lists.
2. **The texture lookup:** it remembers its recent places, since a draw asks 2-3 times.
3. **`-fno-math-errno`** for the port's own C units, as the engine's have.
4. **The level light pass:** each shared vertex is worked out once a draw (its reach and its place from the camera). Each triangle no longer clears its 33 corner floats.
5. **The level shadow pass:** the same, sharing one 256-entry table with the light pass (each pass call takes a new number). 9 KB of bss; heap 5,840 KB against b116's 5,852.

**Measured** in the cache models against b116, each run to the same 3,600 ticks [E]:

| Build | Map | Drawing | Tick | Play |
|---|---|---:|---:|---|
| 1-3 (`speed118-ab`, 38244859198) | c40 | −2.3% | +0.4% | the same tick for tick |
| 1-3 | d20 | −1.2% | +0.3% | the same |
| 1-4 (`lightmemo-ab`, 38247017012) | d20 | −3.8% | +0.2% | the same |
| 1-4 | a10 | −2.4% | +0.2% | the same |

- **The tick's +0.2-0.4%** is the code moving in the cache, not more work: it fetches the same instructions [U].
- **Pictures can't be compared** between two builds: the effects animate with the clock, and the clock runs differently in each (`lightmemo-shots`).
- **So the memo checks itself instead** (keep word `cornermemocheck`, first `lightmemocheck`): it works every remembered value out again and compares bit for bit.
  - The first run found 1,296 mismatches in 201 million on d20. When two corners of one triangle share a slot, the earlier corner read the later one's place.
  - After the fix: 0 in 220 million on d20, c40 and b30 (38251011761) [E].
  - With the shadow pass sharing the table: 0 in 265 million on a10, a30, d20 and c40 (`cornermemo-check`, 38252959341) [E].
  - Its timing on a30 and a10, where shadows cost most, is running (`cornermemo-ab`, 38252969070).

**Still to do:**

| What | Cost now | Saving [U] |
|---|---|---|
| `transparent_cut` runs on every see-through model part, even one wholly in view | 1.3-3.5 ms a frame [E] | most of it. Exact only if the bounding sphere holds every vertex and no triangle reaches past the guard band, so it needs a self-check like the light memo's |
| HUD strips: one bind and one draw for all of an overlay's strips | only while zoomed | 0.1-0.3 ms |

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
