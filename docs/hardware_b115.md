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
- **a10 crashes at game time 2,477, the same tick in both runs** [M]: an unaligned float load in `animation_get_keyframe_translation`, inside one of Bungie's compressed animations in the opening cinematic. The emulator's normal mode lets it pass. Fixed on the `b116` branch (unaligned-safe loads), not yet shipped.
- **a10's opening space shots run at the primary target** [M]: 28 fps with 30 ticks a second (35 ms a frame), the first stretch of any map to get there. Its interiors run 11–15 fps, and there the CPU's drawing is the limit (51 ms a frame, against a 7 ms tick and 27 ms of GE).
- **The map file's re-reads are real on the PSP** [M]: after loading, 93–95% of what it read had been read before. Over the whole bench that's 3% of the stick's time, up to 6% in the beach's busiest reports.
- **Test 1: my eDRAM probe stopped the Media Engine** for the whole run. Its first job (the fill) kept the ME busy 56 ms, the audio job behind it timed out at 40 ms, and a timeout stops the ME for the session. The main CPU then mixed the audio (6–7% of its time), so test 1 ran ~10% slower than b114's (215.3 ms against 195.6 on the cinematic) and the probe answered nothing. Its other instruments are sound: they measure the GE and the stick, not the CPU.
- **The models dominate the GE on every stretch** [M, test 1]: 77–87% of its time, 32 ms of the beach's 42.
- **The stick's long waits are the checkpoints** [M, test 1]. A checkpoint writes 1.3–1.9 MB of game state on the game thread and took 211–1,136 ms: the game stops for that long. Right after one, the log's next write took ~600 ms, and the sound reader waited behind it up to 610 ms; one report had 3 s of silence (5 gaps, the longest 774 ms).

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

## Test 1: the bench, b115's instruments

Keep words `nearauto gebusy assettrack hwprofile geo2 iostats gereads edramcheck`. No fault; Sheepdog01 stopped at game time 5,708, on the beach (b114's test 1 went on to the bunker, 7,408).

**The eDRAM probe stopped the ME (my bug)** [M]:
- The probe's first job filled 1.25 MB of eDRAM in one go: 56 ms on the ME. The next audio job waited behind it, timed out at 40 ms, and a timed-out job stops the ME for the rest of the session (`ME: stopped (a job timed out)`, at game time 1–5, the first heartbeat of play).
- From then on the main CPU mixed the audio: 6–7% of its time. That's most of why this run is slower than b114's test 1 everywhere: the cinematic 215.3 ms a frame against 195.6, the beach 172.8 against 165.3. b115's speed is test 2's to 5's, which match b114.
- The probe never got to check anything, so whether anything else writes eDRAM is still open (G4, [D] only).
- **One figure came out of it** [M]: the ME wrote eDRAM at 23 MB/s (172 ns a word) in that loop, close to the 158 ns a read measured before. The level positions' copy for L1 (~190 KB) would take ~8 ms of the ME to fill [U, if the copy writes a word at a time].
- **Fix for b116:** fill and check in slices of at most 64 KB (~3 ms), one a heartbeat, so an audio job never waits more than a few ms behind one.

**The GE's time by kind** (`gebusy`), ms a frame [M]. The CPU's audio work doesn't change what the GE does:

| Stretch | GE, all | models | level | see-through | shadows, lights, decals |
|---|---:|---:|---:|---:|---:|
| cinematic (150–570) | 37.8 | 33.1 (87%) | 2.7 | 1.5 | 0.2 |
| flight (570–2,000) | 95.5 | 82.6 (86%) | 9.4 | 2.3 | 0.6 |
| landing (2,000–2,700) | 42.4 | 34.1 (80%) | 5.1 | 1.8 | 0.5 |
| beach (2,700–5,708) | 41.8 | 32.1 (77%) | 6.4 | 1.4 | 1.4 |

At 30 fps the GE has 33 ms a frame. The beach's models alone take 32 ms.

**What the GE reads** (`gereads`), KB a frame from main RAM [M; the bytes don't depend on the CPU]:

| Stretch | From main RAM | Models' share | VRAM holding the best 400 KB | 600 KB | 800 KB | Emulator, 600 KB [E] |
|---|---:|---:|---:|---:|---:|---:|
| flight | 3,838–6,775 | 84–88% | 77–88% | 86–94% | 90–97% | 85–92% |
| landing | 2,970 | 84% | 82% | 90% | 95% | 96% |
| beach | 1,165–3,826 | 78–87% | 62–91% | 73–97% | 80–100% | 92–100% |

- **The flight matches the emulator; the beach is harder.** Sheepdog01's beach fight has more characters in view than the emulator's route, so more buffers share the reads. 600 KB takes 73–97% there, 800 KB 80–100%.
- **So the models' share of VRAM is 600–800 KB**, not 600. With textures at ~400 KB that's 1,000–1,200 of the 1,216 KB, and the level's copies get little or nothing.

**The stick** (`iostats`, a report every ~30 s) [M]:

| Report ending at game time | Checkpoint ("other") written | Its worst call | Log's worst call | Sound reader's worst wait (behind) |
|---|---|---:|---:|---|
| 1,231 | 1,322 KB | 65 ms | 586 ms | 469 ms (the log) |
| 2,892 | 1,446 KB | 60 ms | 14 ms | 10 ms |
| 3,235 | 382 KB (and 2.7 s in all) | 792 ms | 64 ms | none |
| 3,923 | 1,666 KB | 419 ms | 581 ms | 534 ms (the log) |
| 5,296 | 1,878 KB | 897 ms | 614 ms | 610 ms (the log) |

- **The checkpoints** (`checkpoint:` lines): 330–469 pages of game state (1.3–1.9 MB), written by the game thread in 211, 211, 1,072, 1,136 and 369 ms. Most pages change between checkpoints (283–429 of them), so writing only the changed ones saves little. Similar sizes took 211 or 1,100 ms; the stick's own housekeeping (extending the file, or erasing) is the likely difference [U].
- **The chain after a checkpoint:** the stick is still busy, the heartbeat's log write takes ~600 ms, and the sound reader waits behind it. In the report ending at 5,296 that gave 2.96 s of silence in 5 gaps, the longest 774 ms. This is G3's answer: b114's 767 ms wait was very likely the same chain [U].
- **The sound re-reads:** after loading, 93% of the 112 MB read from the map file had been read before. 19.4 s in the stick over the run, 3.2% of the time; up to 1.8 s in a beach report (6%).
- **Levers this points to:** write checkpoints compressed and into a file kept at full size (fewer bytes, no file growth); hold the log's flush while a checkpoint writes; keep more sound read ahead so a 1 s stall isn't heard. All three restore the original's behaviour (no freeze, no silence).

## Test 6: play, no instruments

Keep words `nearauto assettrack geo2`. No fault; stopped at game time 4,889, on the beach.
- Beach 123.9 ms a frame (8.1 fps), against 165–173 in the other runs: the beach depends on where the player goes and looks, so it isn't a comparison.
- The "cinematic" stretch took 245.7 ms a frame against ~196 in every other run, with the drawing up ~30% and the tick unchanged. The geometry cache served 38% more lookups over it, so more was drawn: the opening was likely skipped and the player looked around the cabin [U].
- 8 sounds failed for room; heap peak 5,348 KB.

## The campaign ladder on b115: a10 (two runs)

Preset 2 (`nearauto gebusy assettrack geo2 iostats`), a10 from the campaign bake. Both runs crashed at game time 2,477, in the opening cinematic.

**The crash** [M, the crash screen; D, the disassembly]:
- `bad address (load)`, thread `user_main`, epc `08944ae4` (offset `0x140ae4`: `animation_get_keyframe_translation`, called from `animation_get_node_orientations`), badvaddr `09af9e36`.
- The faulting instruction copies a translation keyframe (three floats) with word loads, and the keyframe sits 2 past a multiple of 4, inside the animation cache's part of the tag window.
- Our own keyframed animations pad every table to 4 bytes (the fork's `psp_keyframe_compress`), and the header and node header loads before it were aligned. So this is one of Bungie's own compressed animations, whose tables can start 2 past a multiple of 4 [D/U].
- The emulator ran a10's opening in the lab with no fault: its JIT forgives unaligned loads. The strict interpreter reports them, and a strict batch on all ten maps is running.
- **The fix** (branch `b116`, commit `086d337`): the translation and scale readers load their floats and node headers through packed structs (`PSP_UNALIGNED_REAL`, `PSP_UNALIGNED_REAL3`, `PSP_UNALIGNED_ULONG`), which GCC turns into `lwl`/`lwr` pairs. +168 bytes of code.

**Speed** (`benchreport.py --stretches space:150-400,interior:400-2470`) [M]:

| Stretch | Frame, ms (fps) | Drawing | Tick | GE (share of the clock) |
|---|---:|---:|---:|---:|
| Space: the ship, the ring, the planet (150–400) | 35.2 (28.4) / 39.6 (25.3) | 7.1 / 7.7 | 4.0 / 3.3 | 4 ms (9%) |
| The bridge and halls (400–2,470) | 68.4 (14.6) / 89.0 (11.2) | 50.7 / 52.7 | 7.1 / 7.4 | 27 ms (30–36%) |

- **Space runs at the primary target:** 28 fps with 30 ticks a second.
- **Indoors, the CPU's drawing is the limit** (Sheepdog01's read too): 51 ms a frame against a 7 ms tick and 27 ms of GE. The GE layer's own CPU work is 28–33 ms of it: level draws 8–15 ms, model parts 7–9, the lights up to 11 and shadows up to 7 as the cinematic moves.
- The GE's time: models 61%, the level 30% (b30: models 77–89%).
- Run 2 is slower indoors because it took 19 screenshots: each is a ~400 KB write to the stick.

**What Sheepdog01 saw** (run 2, 19 screenshots) and what each likely is:

| Shot | Seen | Likely cause | Known as |
|---|---|---|---|
| 000 | The sky pale blue instead of space; the planet black; the ring a thin outline with no texture | a10's sky models use shaders the port doesn't draw (a census of a10's sky is queued) [U] | new |
| 001–006 | The ship's engine glows as white cones, like the Pelican's thrusters, with a depth fault inside the cone | Chicago shaders draw one map, untinted, added 1:1 [D] (`b113_candidates.md` #4); the cone's inside, see-through drawn far to near by draw, not by triangle [U] | #4, plus new |
| 007–010 | The bridge windows show a marbled pattern | The glass shader's map choice: the pattern looks like a bump or reflection map drawn as its colour [U] (census queued) | new |
| 008–009 | The orange chairs untextured | **Objects made with level shaders get a flat colour per shader** [D]: the same cause as b30's locked door (R3) | R3 |
| 010, 016–017 | The holographic displays and consoles dim and still | Animated see-through shaders draw one map with no scrolling or tint; environment self-illumination isn't drawn [D/U] | #4 family |
| 011–015 | Keyes and the crew underlit; a crewman's face with odd texture | Object lighting indoors, and the dark-areas items (#6, #8) [U] | #6, #8 |

