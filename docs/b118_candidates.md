# The next build: candidates (2026-10-12, a draft while b116 waits for its PSP tests)

What comes after b116. Sheepdog01 saved build 117 "for something special", so the next ordinary build is 118 unless he says otherwise. Nothing here ships until b116's tests are done (`CLAUDE.md`: finish testing a build before making the next one). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code or tags, **[U]** inference.

## Restorations ready on the branch `hudbars` (on by default, Sheepdog01's rule)

| # | What's restored | How | Keep word (off) | Evidence |
|---|---|---|---|---|
| H1 | The sniper rifle's angle ticks: hidden unzoomed, scrolling blue ticks zoomed. b116 drew two white vertical bars on a50 instead | The ticks are two multitexture overlays, each a mask times a strip of ticks (map 0 to 1: multiply); the ticks scroll with the aim's pitch and fade in when zoomed (fade 0 unzoomed) [E, `hdump`]. The GE has one texture a draw, so the ticks are drawn in up to 32 strips and the mask is read from memory half a texel inside the strips' edges into their vertex colours. Each map's coordinates as the Xbox's screen vertex program works them out [D, `vshdump`]. Any other combination draws as before and is named once in the log | `hudonemap` | `hudmul-a50` (38228554871): unzoomed nothing, zoomed the ticks with their arrows, faded at the ends, moving with the aim [E]. After an independent review's fixes: `hudmul2-a50` |
| G1 | Glass on models lit, not full-bright: a10's bridge monitor (a glass shader, "monitor huge") showed a bright green pattern in the dark | The Xbox's glass diffuse pass lights the diffuse map [D]. Each vertex takes the model's ambient colour plus its distant lights by the normal's angle to each (`seethrough_lit`), as fade mode 3. The reflection (cube map) and tint passes still aren't drawn | `glassflat` | `glass-a10` |

Instruments on the same branch: `hdump` (each HUD quad of a screenshot frame with its bitmap's tag, place, colour and blend; a multitexture overlay's every map), and `tdump` now names each see-through draw's map.

## Exact speed-ups

**The profile-guided build (L4)**, branch `pgo116` (on b116). The first measurement used one profile, b30's opening flight. It ran in the cache models on b30's flight and a10's opening, both to the same 3,600 ticks (`exitat120`; lab run 38225204020) [E]. Cycles are estimated as instruction fetches plus 40 a miss [U].

| Build | Program | Heap | b30 tick | b30 drawing | a10 tick | a10 drawing |
|---|---:|---:|---:|---:|---:|---:|
| Profile, partial training, no unrolling, peeling, tracing or cloning ("lean") | −34 KB | +32 KB | −4.5% | −2.4% | −0.8% | −0.5% |
| Profile, GCC's defaults for unreached code (size) and its loop options | +52 KB | −52 KB | −3.3% | −4.0% | −0.4% | −1.0% |
| Profile, partial training and every GCC loop option (a local build) | +192 KB | −188 KB, under a10's peak | — | — | — | — |

- **Where the gain comes from:** mostly instruction-cache misses. b30's tick has 19% fewer with the lean build.
- **The stretch the profile never saw gains little.** A profile from five stretches is being made (`pgo-gen5-b116`): b30's flight and beach bench, and the openings of a10, c10 and c40.
- **Exact:** a10's game state was the same tick for tick in both builds. b30's parts at tick 1351, the tick where any drawing change parts it (#25, the Elite in slot 947) [E].
- **Only the PSP can say how much it's worth:** its branches and pipeline aren't in the model.

## Being identified

- **c40's pale discs:** model parts. `tdump-c40` lists that opening's see-through model parts. The likeliest: `levels\b40\devices\holo halo control` (the control room's hologram panel, added), `cinematics\effects\teleportation\bitmaps\teleport ring` (added, around the Chief as he arrives), and `levels\b40\devices\holo halo ring markers` [U]. Skipping each in turn tells which.
- **The level's glass** (c40's and b30's "glass grimy") is still unlit: its GE copies have no normals, and the Xbox lights it from the lightmap.

## From b116, still open

- b30's deep water: the Xbox fogs what lies under water by its depth below the surface; the GE fogs only by distance.
- c10's vines.
- G8's trims. The final tour's pool peaks (dashboard, Maps):
  - actors: 35 of 256, 457 KB;
  - props: 195 of 768, 234 KB;
  - glow particles: 0 of 512.
  - Openings only, so a full playthrough's peaks come first.
- L1 step 4: the Media Engine building the GE's lists.
