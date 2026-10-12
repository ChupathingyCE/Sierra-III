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
| R2 | Every see-through shader's colour: thrusters, holograms, Cortana, the sky's layers | The bake works each shader out the way the Xbox's combiners would, with every map at its mean colour, and stores the tint for the one map the PSP draws (`seethrough.py`). A shader brighter than its map (Cortana, the holograms, the sniper rifle's screen: up to 3x) is drawn with the GE's colour doubling and half the tint | `notint` |
| R2b | Water's colour: b30's ocean and c10's swamp (both white) | Water draws its base map blended as the shader's flags say (alpha-blended when its colour modulates the background, added otherwise), tinted with its reflection and view colours | `waterwhite` |
| R3 | Doors, chairs and devices made with level shaders: textured (the map room bunker's door, a10's orange chairs) | Objects' environment-shader parts get their base map, as the level's do | `senvflat` |
| — | See-through parts fade as on the Xbox: thruster cones with the thrust; surfaces that fade by the angle they're seen at (light volumes looked straight into, glows' rims); parts under active camouflage | The shader's fade source; its fade by angle, as the Xbox's vertex program for transparent shaders works it out (the second colour carries c = \|normal · camera forward\| and 1 − c; "fade when perpendicular" takes 1 − c and vanishes face on, "fade when parallel" takes c and vanishes edge on), the level's by each triangle's own plane (its copies have no normals); and camouflage | `seethroughflat` |
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
| `vrammodelsN` (600 or 400; at most half the texture cache's 1,216 KB) | The top N KB of VRAM hold copies of the model buffers the GE reads most per byte; draws read the copies (the same pixels). The textures give that VRAM up (b115 test 3: ~1.5 ms of GE time [M]); the models were 105 ms of the flight's GE time [M]. In the emulator 600 KB held 86 of b30's model buffers, and every model draw read a copy (gereads: the fit curve puts 96% of b30's model reads in the best 600 KB, 88% in 400) [E]. Presets 3 (without), 4 and 5 |

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
| The ocean's colour from above (b30) | The Xbox colours what lies under water with the water's own fog, seen from above too; the GE fogs only by distance from the eye. The ocean is now its reflection over the seabed, not white, but the deep water isn't blue yet: a fog by depth below the water's plane, per vertex, is a bigger change |
| c10's vines, a10's bridge glass | Still being identified |

## What the emulator showed [E]

PPSSPP with the local patches, PSP-1000 memory, nobody at the controls unless a test says otherwise; lab batch ids in brackets.

**The crash fix and the strict interpreter.** b115.1's fix ran clean through a10, a30 and c40 under the strict interpreter (range, unaligned and FPU checks), where b115 stopped; b116 at `d0315d3` was clean on seven maps (`strict-b1151b-b116`, 38202483724). The final check, every map at the release commit on the final bake, is below.

**Checkpoints (R10, R11)** (`checks-b116`, 38205131855). A checkpoint at tick 300 and a revert at 450 on six maps: every revert went back to tick 300 and the game went on from the same place as the first time; b30 with the old format (`statepages`) went the same way. The compact format writes 44-52% fewer bytes:

| Map | Pages kept | As non-zero words | As pages |
|---|---:|---:|---:|
| b30 | 307 | 688 KB | 1,228 KB |
| a10 | 209 | 431 KB | 836 KB |
| c10 | 280 | 598 KB | 1,120 KB |
| c40 | 190 | 368 KB | 760 KB |
| d40 | 205 | 392 KB | 820 KB |

The log held its writes around each checkpoint (`log: writes held 1 times around checkpoints`). The emulator's stick is no guide to the PSP's write time; the bytes are.

**The restorations, by eye** (`verify-b116`, 38211669473; `verify2-b116`, 38212616299; on a bake of five maps with the corrected tints):
- b30: the sky is blue with its clouds and the ring, and the ocean is green-blue in the shallows and blue-grey further out, where the tours before showed white. The thrusters fade with the thrust.
- c10: the swamp water is dark blue, mostly see-through (it was solid white).
- a10: space is dark (it was pale blue); the bridge's big window still shows a green pattern (open).
- c40: Cortana and 343 Guilty Spark draw with their glows; the room's pale discs are still there (open: they look the same with the level's fade by angle as without, so they are probably not the faded volume lights [U]; the final tour's `nooverlays` run says whether they are level surfaces at all).
- Meters: c40 has one in view every frame, drawn (300 a 300 frames, none left out). The first try left every one out: the meter's map had never been bound, so its palette wasn't in memory (fixed, `psp_tex_resident`). b30 with every vehicle and weapon dropped by the player didn't put a Warthog in view.

**The VRAM swap's second step** (`vrammodels-b116`, 38206322935). With `vrammodels600`, 86 of b30's model buffers sat in 524 KB of VRAM and every model draw read a copy; the pictures are the same as without, frame for frame within the timing. `vrammodels800` was over the limit (half the texture cache's VRAM) and was left off without a word: now it says so, and preset 5 is 400 KB. Only the PSP can time it.

**The sound bake (S9)** (`s9-meters-b116`, 38209149541). Four maps baked with S9 played through their openings with no audio error or crash. The maps are 31-48 MB smaller (a10 233 to 201 MB, c40 221 to 173), and over ten minutes the stick read about a third of the sound data (a10: 17.5 MB against 48.6; c40: 17.4 against 53.0). The bake's own SNR check, over every re-encoded sound: median 28-33 dB a map. Only ears can judge it.

## On the PSP

The maps are a new bake: the tints, the meters' palettes and the S9 sounds are in them (`campaign116f` on the desktop). Pick the map on the launch menu's first page, then R twice to the presets, cross on the test, START. Deathless, as before.

**Before the first test: install the build and the maps**
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b116.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b116 is on the stick"
set maps ~/psp-bake/campaign116f/halo/maps
cp -v $maps/ui.* $psp/halo/maps/
for m in a10 a30 a50 b30 b40 c10 c20 c40 d20 d40
    cp -v $maps/$m.* $maps/$m"_track.ptex" $psp/halo/maps/
end
rm -f $psp/halo/shots/*
umount $psp
```
The copy is 53 files, 2.0 GB (350 MB less than the last campaign bake: the S9 sounds), a few minutes. The bake also made the multiplayer maps; this build doesn't need them. The log's first line should say `bring-up build 116`, and a line `textures: see-through tints for N shaders` should follow each map's load.

**After each test: pull the log.** Change the name for each test:
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b116_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

Tests 1-4 matter most; 5 and 6 when there's time. Screenshots (the usual button) of anything that looks wrong help most.

| # | Map, preset | Play | Stop | Time | What it answers |
|---|---|---|---|---|---|
| 1 | b30, 1 (play) | Through the flight, then a minute on the beach. Look at the ocean below, the sky, the Pelicans' thrusters; listen to the music and the voices | One minute after landing | ~5 min | By eye and ear: the ocean and sky (R1, R2, R2b), the thrusters' fade, the S9 sounds |
| 2 | b30, 3 then 4 then 5 | Leave the controls alone through the opening cinematic, once with each preset | When the cinematic ends and you have control in the Pelican | ~3 min each | The model buffers in VRAM (600 KB, 400 KB) against none: the GE's time by kind (`gebusy`). `tools/benchreport.py` compares them |
| 3 | c40, 1 (play) | The opening in the control room, then a few steps | 3 min after you get control | ~8 min | The pale discs gone (the volume lights fade by angle); Cortana's colour (doubled tints) |
| 4 | c10, 1 (play) | The opening, then walk to the swamp water | 3 min after you get control | ~6 min | The swamp water (no longer white) and the fog |
| 5 | b30, 1 (play) | Play until you can drive a Warthog, and pick up a plasma rifle from a dead Elite | When you've seen both | ~10-15 min | The meters: the Warthog's speedometer and engine meter, the plasma rifle's heat gauge |
| 6 | b30, 6 (the b115 look) | as test 1 | as test 1 | ~5 min | The same flight with every restoration off, for side-by-side shots |
