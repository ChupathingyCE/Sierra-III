# Build 116: the original game restored (2026-10-12)

Built overnight after b115's tests on four campaign maps and Sheepdog01's rule of 2026-10-11: where the port draws or behaves differently from Halo on the Xbox, the fix goes on by default, with a keep word that turns it off (`CLAUDE.md`). b116 carries b115.1's crash fix and fourteen restorations, plus the VRAM swap's second step for Sheepdog01's A/B. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory), **[D]** read in the code or tags, **[U]** inference.

Zip, EBOOT and commit: below, once the release is built. Heap 5,864 KB (b115: 5,888): the heap is now sized to 4 KB, not 64 KB, so the new code cost only what it is. a10's peak in b115 was 5,764 KB [E].

**The maps need the new bake** for the see-through and water tints (`/bake/campaign116` on the desktop: `~/psp-bake/campaign116/halo/maps`). With the old maps everything else still works, and those shaders draw untinted, as before.

## What changed

### The crash (from b115.1)

| Change | Why |
|---|---|
| Compressed animations' tables read with unaligned-safe loads, in the keyframe readers and in `animation_get_node_orientations` | b115 crashed on a10 (2,477) and c40 (1,424) on the PSP, and on a30 (118) in the emulator: Bungie's compressed animations hold tables 2 bytes past a multiple of 4, which the PSP's CPU faults on (`build_b115.1.md`). Clean under the strict emulator check on all ten maps |

### Restorations, on by default

| # | What's restored | How | Keep word (off) |
|---|---|---|---|
| R1 | The ocean stays behind the seabed (b30's flight) | Free see-through surfaces pushed back by their far corner (b115's `overlayfreefar`, Sheepdog01's test 5 [M]) | `overlayfreenear` |
| R2 | The ocean's colour, and every see-through shader's: thrusters, the a10 sky, glass | The bake works each shader out the way the Xbox's combiners would, with every map at its mean colour, and stores the tint for the one map the PSP draws (`seethrough.py`) | `notint` |
| R2b | Water's colour: b30's ocean and c10's swamp (both white) | Water draws its base map blended as the shader's flags say (alpha-blended when its colour modulates the background, added otherwise), tinted with its reflection and view colours | `waterwhite` |
| R3 | Doors, chairs and devices made with level shaders: textured (the map room bunker's door, a10's orange chairs) | Objects' environment-shader parts get their base map, as the level's do | `senvflat` |
| — | See-through parts fade as on the Xbox: thruster cones with the thrust, light cones soft at the sides, parts under active camouflage | The shader's fade source, its fade by the angle the surface is seen at, and camouflage | `seethroughflat` |
| — | Space and indoor gaps aren't pale blue | Each frame clears to the window's fog colour, as the Xbox clears (a10's opening) | `clearblue` |
| — | c10's swamp fog | Planar fog, when the eye is in it, as a distance fog in its colour (the Xbox's fog pass, reduced to what the GE has) | `planarfogoff` |
| b113 #2 | Plasma glows hidden behind characters | The flare's ray meets bipeds (not the player's own), stopping just short of the flare | `flaresnobipeds` |
| b113 #6 | Smooth gradients in dark areas and multiply passes | The world drawn with the GE's ordered dither (not the HUD) | `nodither` |
| b113 #7 | No brighter band 20-60 units out on terrain and walls | Detail passes fade to their palette's own neutral grey | `detailgrey80` |
| b113 #9 | A needle's pink flash faint, not solid | Max, min and invert screen flashes with the GE's MAX/MIN blends, as the Xbox draws them | `flashnearest` |
| b113 #17 | The Pelican's cabin not cut open on the bench seats | In a first-person seat the near plane stays at the game's own | `nearseatedfar` |
| b113 #16 | The plasma weapons' heat gauges and the Warthog's speedometer and engine meters | Meter shaders drawn: the engine side fills the HUD meters' constants from the shader and the object's values, as the Xbox's four combiner stages take them, and the GE works a palette out per value (the HUD's own routine). The bake gives meter maps the HUD's grid palette | `nometers` |
| R10 | Shorter checkpoint freezes | Checkpoints stored as each page's non-zero words (most of a page is zero) | `statepages` |
| R11 | No sound gap after a checkpoint | The log holds its writes while the checkpoint writes and for 2.5 s after, so the sound reader has the stick first | `lognohold` |

### Off by default: Sheepdog01's A/B

| Keep word | What it does |
|---|---|
| `vrammodelsN` (600 or 800) | The top N KB of VRAM hold copies of the model buffers the GE reads most per byte; draws read the copies (the same pixels). The textures give that VRAM up (b115 test 3: ~1.5 ms of GE time [M]); the models were 105 ms of the flight's GE time [M]. Presets 3, 4 and 5 |

### Instruments and fixes

- The geometry report counts the draws left out for a frame (R4's far objects vanishing).
- The eDRAM probe works in 64 KB slices, one a heartbeat (b115's stopped the ME on test 1).
- The log lists the restorations' keep words, the windows in planar fog, the see-through parts faded by angle, the meter parts drawn, the holds around checkpoints, and the texture pack's tint count.
- Keep words: `keep.txt` is read into 512 bytes (256 before), a preset holds 320 characters (200), and the menu's file writer can't run past its buffer. Preset 6 (every restoration off) is 199 characters: with b115's limits its last words could have been lost without a word in the log.

## Not in b116, and why

| Item | Why not |
|---|---|
| b113 #11, the flashlight on characters | Needs the GE's light 2 as a spot, tested against the cone per model: next |
| b113 #18, shields flaring when hit | Modifier shaders aren't drawn at all yet |
| R4's fallback (a resident level of detail instead of nothing) | Design; the counter comes first |
| R7, R8 (zoom overlays, tracers) | Wait on reference shots from the PC build |
| The sound rebake (S9, approved) | Its re-encoder isn't written yet: its own bake, after b116's checks |
| c40's white discs, c10's vines, a10's bridge glass | Being identified (tag censuses queued) |

## What the emulator showed [E]

(to follow: the strict check, the tour on the new bake, the checkpoints, vrammodels)

## On the PSP

(to follow)
