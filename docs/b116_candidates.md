# Build 116: candidates (2026-10-11, a draft while b115's test 1 runs)

What comes after b115, from its PSP tests so far (`hardware_b115.md`) and Sheepdog01's notes. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator or estimate, **[D]** read in the code or tags, **[U]** inference.

## Decisions (Sheepdog01, 2026-10-11)

- **Restoring the original game isn't a change** (`CLAUDE.md`). Where the port draws or behaves differently from Halo on the Xbox, the fix goes on by default with no A/B. It still has a keep word that turns it off, and the build notes say what it restores.
- Departures from the original for speed (draw distance, fewer effects) stay Sheepdog01's call, after an A/B.
- Targets as before: 30 ticks and 30 fps; 15 fps at 30 ticks at least (`b115_candidates.md`).

## 1. Restorations from b115's tests (on by default)

| # | What's wrong | Cause | Fix | Ready? |
|---|---|---|---|---|
| R1 | The ocean shows through the seabed | The free see-through surfaces' depth push, sized at their nearest corner [D] | `overlayfreefar` becomes the default; a keep word brings the old push back | Yes: works on the PSP (b115 test 5) [M] |
| R2 | The ocean is white | "Water simple" shaders draw their brightest map untinted; the cube map and the stage constants are dropped [D] (`b113_candidates.md` #5) | A tint per shader, baked from the cube map's mean colour and the stage constants | Designed; an emulator shot checks it |
| R3 | Objects made with level shaders draw flat and untextured: the map room bunker's locked door | The model hook gives a part its base map only for model shaders; environment shaders on objects get a flat colour per shader [D] | Give an object's environment-shader parts their base map and detail maps, as the level's draws already do | Cause found; an emulator shot of the door checks it |
| R4 | A far object vanishes for a few frames as you approach | Its vertices aren't read yet: the draw is left out that frame, and a frame reads at most 192 KB of geometry [D]. 198 frames missed some in test 5 [M] | First count the draws left out (the counter exists, never logged). Then draw a level of detail that is resident instead of nothing | Counter: yes. Fallback: design |
| R5 | Pinholes along edges under the flashlight; maybe the assault rifle display's dots | Neighbouring pieces split their shared edge differently (T-junctions) [D] (#12) | Split per edge, not per piece (`lightedges`) | Design exists (b113); the rifle's dots may instead be depth ties between its chicago overlays and the display [U] |
| R6 | The walkway seen through the map room bunker's wall | Pieces cut at the near plane get one depth nudge per draw [D] (#13) | Run cut pieces through the same distance bands (`lightbands`) | Design exists (b113) |
| R7 | The zoom overlays are misaligned: the pistol (b114) and the sniper rifle (multiplayer) | The mask or the HUD's scope overlay, placed from 640×480 onto 480×272 [U] | Needs a reference shot from the PC build to aim at | Waits on a reference |
| R8 | Tracers as long streaks; the plasma pistol's bolt like a cloud (b114 tests 7–8) | Likely the effects bake's half-size 16-colour textures [U] | Native DXT for effects (S6), checked against a reference | Waits on a reference |
| R9 | Lines around the base of rocks | Open: edge seams, the lightmap bleeding across a seam, or the ocean through cracks in the ground [U] | An emulator A/B at the same rocks, `overlayfreefar` on and off, first | Recon |

## 2. b113's restorations never built

Each was found in b113's review (`b113_candidates.md`) and set aside while speed came first. Under the new rule they go on by default once built and checked in the emulator.

- **#2** Plasma glows shine through characters (the flare's ray skips bipeds): `flarebipeds`.
- **#3** Needles stay visible under active camouflage: `camofade`.
- **#4** The Pelican's thrusters draw as white cones (chicago shaders untinted): `chicagofade`.
- **#6, #7** Dark areas too dark; a gradient across terrain (no dithering; the detail fade's neutral): `dither`, `detailneutral`.
- **#9** Needler impacts flash the screen solid pink (flash types): `flashtypes`.
- **#11** The flashlight doesn't light characters or objects: `flashlightmodels`.
- **#15** A frame drawn without a texture that isn't resident yet: its average colour instead (`missaverage`).
- **#16** Heat gauges and the Warthog's meters missing (meter shaders: 4 in b30) [D]: `meters`.
- **#17** The camera inside the Pelican's walls on the bench seats (the near plane while seated): `nearseated`.
- **#18** Shields don't flare when hit (b30's 2 plasma shaders) [D]: `shieldflare`.

## 3. Speed and measurement

- **First, trim code** (gap G8): b115's program sits 0.7 KB under the next 64 KB heap step, so any code b116 adds costs 64 KB of heap unless something leaves. The instruments could move to the cold, `-Os` half, or behind a build switch.
- **`vrammodels`:** the most-read model buffers into VRAM (~600 KB, b115's fit curve [E]). The models are 88–89% of the GE's time [M]; this also splits that time into reading and transforming.
- **The log at start-up:** written a line at a time until the first frame, it took 8.9 s of the stick's time in test 2 [M]. Loading barely waited on it, so it's low on the list.
- **Why E16 isn't exact** (lab): needed before the fraction change can come back.
- Test 1's results (the bench, the eDRAM probe, the stick on the beach) slot in here when they arrive.

## 4. Order (a draft for Sheepdog01)

1. Trim code (G8).
2. The restorations with a found cause: R1, R3, R2, and R4's counter. Emulator shots before the build.
3. `vrammodels`, behind its keep word. It draws the same pixels, so it needs no approval, but a cinematic A/B on the PSP measures what it buys.
4. Recon in the lab: R9's A/B, the rifle display, and emulator shots for #2–#18. References from the PC build for R7 and R8.
