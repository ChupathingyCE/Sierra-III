# Build 115 on the PSP-1000 (2026-10-11)

Sheepdog01's PSP-1000 at 333 MHz, b30, `halo_psp_b115.zip` (EBOOT `d22eb2eddda5139a`). The tests are listed in `build_b115.md`; the figures come from `tools/benchreport.py` and the log's `gebusy by kind` and `iostats` lines. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

This page fills in as the tests come back.

## In short

- **b115 costs nothing on the cinematic** [M]: 197.0 ms a frame against b114's 195.6, inside the 1% that runs of one build vary by.
- **The models are almost all of the GE's time** [M]: 88% of it in the cinematic (33 of 37 ms a frame) and 89% in the flight after it (105 of 118 ms). The emulator's byte count said the same (83–88% of the GE's reads from main RAM, `build_b115.md`). Both of `big_levers.md` §0's conditions for "models first" now hold: the models dominate the GE's time, and a few buffers carry most of their reads.
- **Textures from main RAM cost the GE little** [M]: with VRAM's texture cache cut from 1,216 to 400 KB (test 3), the GE took 1.5 ms a frame more in the cinematic, all of it in the level's draws (2.5 → 4.1 ms); the models' time didn't move. The frame was within run-to-run noise (198.0 against 197.0 ms). So giving ~800 KB of VRAM to models costs about 1–1.5 ms of GE time, and the models' 105 ms in the flight is what it's for.
- **`vfpuskin` gains nothing measurable** [M]: the cinematic took 196.1 ms a frame against 197.0, and drawing 72.6 against 72.4, both inside run-to-run noise. It stays off (E24).
- **`overlayfreefar` fixes the ocean through the seabed** (test 5, Sheepdog01's eye) [M]. The shoreline is now jagged where the water meets the sand at a shallow angle, like the Scorpion's shadows on Blood Gulch: the 16-bit depth buffer can't tell which is in front there [U]. The ocean is still white (the tint, b116).
- **Sheepdog01's other sightings in test 5** map onto known items: the far rock that vanished for a few frames (geometry not yet read; 198 frames missed some), the flashlight's dots along edges (b113 #12), the walkway seen through the bunker wall (b113 #13, still present), plus two new ones: lines around the base of rocks, and the locked door drawn flat and metallic (found: objects made with level shaders get no texture). Details below.
- **The map file's re-reads are real on the PSP** [M]: after loading, 95% of what it read had been read before. They cost only 1.0–1.5% of the stick's time on this stretch, and nobody waited behind them.

## Test 2: the cinematic, base

Keep words `nearauto gebusy assettrack geo2 iostats`. No fault; Sheepdog01 stopped in the Pelican at game time 1,151 (HOME).

**Against b114** (`benchreport.py`):

| | b114 T1 | b114 T4 (`lean`) | b115 T2 |
|---|---:|---:|---:|
| The cinematic (150–570): frame by the clock, ms | 195.6 | 190.5 | **197.0** |
| drawing, ms | 72.2 | 70.4 | 72.4 |
| tick, ms | 59.9 | 58.3 | 60.3 |
| GE busy, ms a frame (share of the clock) | 44 (20%) | 47 (22%) | 45 (20%) |
| Heap top, KB | 5,520 (whole bench) | 5,156 | 5,160 |

**The GE's time by kind of draw**, ms a frame, weighted by frames:

| Kind | The cinematic (150–570) | The flight after it (570–1,151) |
|---|---:|---:|
| models | **32.7** (88%) | **105.3** (89%) |
| level | 2.5 (7%) | 9.8 (8%) |
| see-through drawn | 1.5 (4%) | 2.4 (2%) |
| shadows | 0.3 | 0.5 |
| HUD, see-through queued, other | 0.3 | 0.2 |
| **All** | **37.2** | **118.3** |

- **What it means for the targets:** at 30 fps the GE has 33 ms a frame. The cinematic is just over that; the flight after it is 3.6 times over, and the models are 105 ms of it. Faster CPU work alone can't take the flight past ~8 fps [U, from the GE's time alone].
- **What it doesn't say yet:** whether the models' time is the GE reading vertices from main RAM or the GE transforming them. Test 3 rules out texels (below). `vrammodels` (b116) splits the rest: if the models' GE time drops with their buffers in VRAM, reading was the cost. Blood Gulch's A/B [M] found most of the models' cost was reading. (An earlier note said test 4 would split it too; it can't, since `vfpuskin` changes only the CPU's work.)
- `gebusy by kind` adds a chunk at each change of kind, so the total can differ a little from b114's; here it's 45 against 44 ms (benchreport's weighting).

**The stick by stream** (`iostats`, every ~30 s) [M]:

| Report (game time) | Map file: s in the stick (KB, re-read share) | Log: s (worst ms) | Textures: s | Geometry: s | Waits behind others |
|---|---|---|---:|---:|---|
| start to 299 (loading included) | 1.44 (16,088 KB, 58%; worst 332 ms) | **8.86** (47) | 0.55 | 0.67 | 0.5 s in all, mostly behind the log |
| 299–597 | 0.43 (3,885 KB, 92%) | 0.28 (46) | 0.02 | 0.05 | none |
| 597–826 | 0.30 (2,929 KB, 98%) | 0.28 (16) | 0.04 | 0.05 | none |
| 826–983 | 0.38 (3,210 KB, 97%) | 0.25 (21) | 0.06 | 0.00 | 3 ms |
| 983–1,151 | 0.44 (3,691 KB, 94%) | 0.68 (**280**) | 0.06 | — | 9 ms |

- **The re-reads:** 13.0 of the 13.7 MB the map file read after loading had been read before (95%) [M]. The emulator's guess was 83% [E]. At 1.0–1.5% of the stick's time they aren't a bottleneck here; the beach (test 1) is the busy case.
- **The log writes are the stick's biggest user while loading:** 8.9 s of the first report's 13.3 s. Most of that is the heartbeat thread (6.0 s); until the first frame, lines are written one at a time rather than batched [D]. The game thread waited only 0.1 s behind it, so loading barely felt it [M].
- **One 280 ms log write** in the last report, as a checkpoint was written ("other": 1.3 MB). Log writes of that length are the hitch candidates `streaming.md` §2 names; test 1's longer run will show how often.
- Sound: one voice ran dry once (75 ms), at the exit.

## Test 3: the cinematic, textures from RAM (`vramtex400`)

Keep words as test 2 plus `vramtex400`; the log confirms the cut (`the VRAM cache cut to 400 KB (of 1216)`). No fault; stopped at game time 1,182.

| | b115 T2 (base) | b115 T3 (`vramtex400`) |
|---|---:|---:|
| The cinematic (150–570): frame by the clock, ms | 197.0 | 198.0 |
| drawing, ms | 72.4 | 73.4 |
| tick, ms | 60.3 | 60.5 |
| GE, ms a frame: all | 37.2 | **38.7** |
| GE: level | 2.5 | **4.1** |
| GE: models | 32.7 | 32.4 |
| The flight after it (570–1,151), GE: all | 118.3 | 118.3 |
| GE: level | 9.8 | 10.9 |
| GE: models | 105.3 | 104.0 |
| Textures loaded over the run (KB) | 128 (2,188) | 199 (2,836) |
| Textures read from the stick after loading, KB a report | 109–209 | 109–346 |

- **The cost of texels in main RAM is the level's, ~1–1.5 ms a frame** [M]. The level's textures are the big, repeated ones; the models' time doesn't change, so their 105 ms isn't spent on texels.
- **That matches Blood Gulch's ±1 ms** (`big_levers.md` §0) and settles M4: the swap's first step is cheap.
- **Streaming barely moved:** the stick read the same textures except 137 KB more in the last report. In the real swap the RAM cache grows to take what VRAM gives up (`big_levers.md` §0), so even that goes.

## Test 4: the cinematic, VFPU skinning (`vfpuskin`)

Keep words as test 2 plus `vfpuskin`. No fault; stopped in the Pelican.

| | b115 T2 (base) | b115 T4 (`vfpuskin`) |
|---|---:|---:|
| The cinematic (150–570): frame by the clock, ms | 197.0 | 196.1 |
| drawing, ms | 72.4 | 72.6 |
| tick, ms | 60.3 | 60.6 |
| GE, ms a frame (models) | 37.2 (32.7) | 38.1 (33.4) |
| The flight (300–2,000): drawing, ms | 152.3 | 149.7 |

- **No measurable gain on the cinematic** [M]: every figure is inside the 1% that runs of one build vary by. The flight's drawing is 1.7% lower, but the flight after game time 570 varies by 7% with where the player looks, so that isn't a result.
- **Why so little** [U]: `vfpuskin` moves only one job to the VFPU, the camera-space corners of skinned parts cut at the near plane. That's the marines beside the camera in the cabin, a small share of the cinematic's drawing. The emulator found it 2–4% slower; the PSP finds it level.
- **Verdict:** it stays off. E24 closes as "no gain on b30"; c40's close fights are the only place it might still matter.

## Test 5: the flight, the ocean pushed back by its far end (`overlayfreefar`)

Keep words `nearauto assettrack geo2 overlayfreefar` (no instruments). No fault. Sheepdog01 went past the preset's stop: after the Warthog, a medium run straight to the map room bunker (game time 7,064; 14 screenshots).

**The ocean (what the test was for)** [M, Sheepdog01's eye and shots t479–488]:
- **It no longer shows through the seabed.** The fix holds on the PSP as it did in the emulator.
- **The shoreline is jagged** where the water meets the sand, "similar to how the shadows from the Scorpion in Blood Gulch behaved, but it looks better geometry wise". Where two surfaces cross at a shallow angle, the 16-bit depth buffer gives both the same depth over long strips, so which one wins flips from pixel to pixel [U]. It's the cost of a smaller push; a push that grows slightly with distance would trade some of it back.
- **The ocean is still white.** That's the tint (`b113_candidates.md` #5), planned for b116.

**Speed** [M]: no instruments in this preset. Where it overlaps the other runs it matches them: the cinematic 195.1 ms a frame (b114 test 1: 195.6; b115 test 2: 197.0), the beach 167.9 (b114 test 1: 165.3). b115 has no speed changes, so the smoother feel past the bridge is the place, or the instruments being off (b114's preset 7 had smaller spikes for the same reason) [U]. Past the bridge, in the map room bunker, it ran at 17–20 fps with 28–29 ticks a second.

**Sightings, and what each maps to:**

| Seen (Sheepdog01) | Known as | Cause | Confidence |
|---|---|---|---|
| The farthest visible rock vanished for a few frames as he approached | `b113_candidates.md` #22 (objects appear late), not #19 (the display list never filled: 0 draws skipped) | **The geometry cache hadn't read its vertices yet.** A miss queues the read and leaves the draw out for that frame, and a frame reads at most 192 KB (`psp_geo.c`, `psp_fixups.py`'s `psp_vertex_lock`) [D]. This run: 198 frames missed something, 984 blocks evicted from the 2 MB cache, worst frame's read 134 ms [M]. A rock that changes level of detail as you come close needs a new block [U] | medium-high (mechanism [D], this rock [U]) |
| When using the flashlight, dots or lines along the edges of geometry, "like I'm seeing through the seams" | #12 (jagged flashlight edges, seams) | **Neighbouring pieces split their shared edges differently** (T-junctions), so the light pass leaves pinholes along them [D, b113]. `lightedges` was proposed, never built | medium |
| Lines around the base of rocks, "like a water line" (shots t374–419) | new | Open. The shots show a thin bright band where rocks meet the ground, and dotted lines across the ground. Candidates [U]: the same seams (#12) where the rock meets the terrain; the lightmap's texels bleeding across the seam (b30's lightmaps are baked at reduced size); the ocean showing through cracks in the terrain under it (the smaller push lets it win where nothing solid covers it). An emulator A/B at the same rocks, `overlayfreefar` on and off, sorts the last one out | low |
| Walkway seen through the wall before the locked door (map room bunker) | #13 (floor light through the hallway wall) | **Pieces cut at the near plane get one depth nudge per draw**, up to ~1 unit of lift at 8 units [D, b113]. `lightbands` was proposed, never built. Still present | medium |
| The door that stays locked until the security room looks flat and metallic, as if textures or effects were missing | new | **Found [D]:** the door (`levels\b30\devices\doors\door small`, a machine) is built from the level's own environment shaders (`metal strips wide`, `metal panels generator`, …; census run 38190217292). The port's model hook gives a part its base map only when its shader is a model shader; any other kind is drawn in a flat colour per shader (`psp_fixups.py`, the `rasterizer_model_draw` hook). So every object made with level shaders draws untextured | high |

## Sheepdog01's notes after test 5 (2026-10-11)

- **A rule:** fixes that restore the original game (the ocean, the texture issues) go on by default without an A/B (`CLAUDE.md`). So `overlayfreefar` and the water's tint go on by default in b116.
- **The assault rifle's display, very occasionally:** dots like the flashlight's along the edge where the display meets the housing. The display is a model-shader part (`fp\shaders\display`) with three see-through chicago overlays drawn on it (`compass`, `numbers`, `ar data`; census run 38190196843). Candidates [U]: the overlays and the surface under them tie in the 16-bit depth buffer along that edge, or the same edge seams as the flashlight's (#12).
- **The sniper rifle's zoom overlay is misaligned** (seen in the multiplayer test; b30 has no sniper rifle). It's the same family as the pistol's zoom overlay, offset and stretched in b114's tests 7–8. Two things draw while zoomed: the mask (`zoommask`, off by default, kept round in the middle of the screen) and the HUD's scope overlay, placed from the 640×480 HUD onto the 480×272 screen. Which one is off needs a reference shot from the PC build [U].

