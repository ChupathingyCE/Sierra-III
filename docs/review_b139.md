# Review: build 139

For the owner to read and question before testing (CLAUDE.md, "How AI is used here"). b139 is b138 (`review_b138.md`, d32c48e) plus everything merged into `w139` since: the b138 hand test's findings, the first hand test of b139 itself (findings H1 to H42), the boot and menu work, the bake as a player's tool, and the release's own tooling. It is the first public release candidate (`go_live.md`). The candidate is `w139` at 4e4b9b0 on the b139c bake; it differs from the commit its own PGO profile was recorded at (0696f71) by that profile file alone [D].

Labels: [M] measured on a PSP, [E] emulator, [D] read in code or data, [U] inference. A cost is a game-time comparison on the same stretch unless it says otherwise. Every change has a keep word that turns it off (or on, for the opt-in ones); a keep word's default is in the tables. Each branch's own walk-through holds the long form: `review_fix-memory.md`, `review_fix-lights.md`, `review_fix-models.md`, `review_fix-alpha.md`, `review_fix-alpha3.md`, `review_fix-cortana.md`, `review_fix-tracers.md`, `review_fix-sim.md`, `review_fix-frontend.md`, `review_bootscreen.md`, `review_menus.md`, `review_relief.md`, `review_ringdetail.md`, `review_flatround.md`, `review_angletint.md`, `review_bakever.md`. This file is the index and the decisions.

## What changed, by kind

| Group | Changes | Default |
|---|---|---|
| Memory and streaming (§Memory) | pools sized to what play uses (R4); level-copy carve; update rings; stencil block taken early; stick lock; checkpoint stage; dynamic vertices given back; HUD textures pinned; geometry cache second chance; models' VRAM room back to 600 KB; profile saves | on |
| Look, restorations (§Look) | relief baked in; see-through parts one-sided; Cortana's self-occlusion, gel, angle tints; see-through composites; alpha-test fixes (decals, trees); reflection folded into ambient; light-pass edges; lens-flare ray; edge-on nudge; flat glass rounding; contrail fade | on |
| Look and sound calls that are yours (§Calls) | `genericnoise`, `camoshimmer`, `particleranks`, `audio44`, `statedelta`, menu size, contrails | off, except the last two (on by your call) |
| Front end and loading (§Front end) | boot screen, hint and settle; menu glyphs and size; XMB tile; menu ring's detail pass; Select on release; action hold 180 ms | on |
| Sound and controls (§Sound) | the 344 Hz buzz; no unarmed melee in the campaign; menu music skips; the chant from the player's bake | on |
| Simulation (§Sim) | the Warthog flip wakes the vehicle | on |
| Bake and release (§Bake) | numpy palettes and face-based decals; guided and double-click bake; one bundle per OS; bake stamp and check; public export | player tooling |

## Where the hand test's findings stand

H-numbers are b139's first hand test (PSP-1000). "Fixed [E]" means the emulator shows it fixed; the PSP is yours to confirm (§Test).

| H | Finding | Now |
|---|---|---|
| H1 | Keyes and Cortana close-ups cut open | `nearseatcine`: in a cinematic the camera isn't the seat. A no-op in a10's opening in the emulator (heads whole on both sides): unproven [E] |
| H2, H15, H16 | Menu ring: dark hull, flicker, low-quality sky | Diagnosed, not fixed. Ring flicker not reproduced [E]; the hull lacks the bake's dropped reflection cube map; the sky draws one map of several. Opening design in `review_fix-frontend.md` §4 |
| H3 | Cortana inside out, seam | One-sided see-through parts, self-occlusion and part order, light gels (the seam was a10's alert lights drawn without their gel). Fixed [E] to the level's own faint edge |
| H4, H40 | Checkpoint buffer failure; 1 to 1.5 s checkpoint stalls | Stage reserved at start (fixed [D], unproven: not reproducible in 300 s on the PSP). Stalls are the stick's own [M]; `statedelta` is worse, off |
| H5 | Fizzy treble | No fault found in the bake's filter [D]. `audio44` opt-in for a listening A/B |
| H6, H24 | a30 cliff streaks; flat walls | Relief on, from a prefiltered bump [E]; `norelief` is the other side |
| H7 | Cutouts as squares | a10's floor marking: fixed [E] (alpha-tested decal). a30 trees: fixed [E] (cut out by bump alpha). Grating, corridor sign, blast-door windows: open |
| H9 | Glass over pipes black | Reflection drawn for glass without a diffuse map; the a10 pit's glass never reaches the see-through queue, so no verdict. Open |
| H10, H32 | Geometry pop-in, churn | `geofifo` second chance (not reproducible on the emulator's routes [E]); a10's level copies: carve [E] |
| H11, H13 | Menus small; XMB tile shifted | Menus 115% and glyphs 125% by default (your call); tile's glass frosted [D][E] |
| H12, H21 | Dynamic vertices at cap; HUD loses parts | Fixed [E][M]: peak 2,436 to 688 of 4,096 (b30, PSP-1000); untextured HUD draws 88 to 0 |
| H14, H18, H37 | Tree line cards; water layers; Jackal shield | Trees fixed [E]; water better, not fixed; shield see-through, definition as before |
| H17 | Pelican cockpit green flicker | Not reproduced [E]; the one-sided change is the likely cure [U] |
| H19, H20, H38 | Thrusters, Autumn's drive disc, plasma glows | Unchanged by composites [E] (H19); no clean pair for H38; H20 untouched |
| H22 | Shaded objects too dark | Reflection folded into ambient (`noreflection`); gap to the PC not closed on all shots |
| H23, H33, H41 | Seam holes under the flashlight; edge-on line; light through a wall | Per-edge split and unlifted band (H23, partly); edge-on nudge dropped (H33 fixed [E]); flare ray to the flare (H41 fixed [E]) |
| H25, H26, H27 | Vehicle clip and wobble; costs | Not the simulation: the PSP, the emulator and the PC drive the same to float rounding [M][E]. Render-side recon is next. Per-type costs as an instrument |
| H28 | Tracers absent | Contrails on by default (your call), faded by alpha as the Xbox's combiner does [D] |
| H29, H30 | Wet-looking floors; low-quality floors and sky | Not fixed. `dithershift` A/B measured, off |
| H31 | Warthog flip regressed | Fixed [E] |
| H34 | No flashlight under camouflage | Halo's own behaviour [D]; no change |
| H35, H36 | Camouflaged Elites invisible; powerups plain | Opt-in looks, off (see §Calls); neither confirmed on screen [E] |
| H39 | Pelican off its path | Not reproduced; poseahead cleared (0 of 199,833 poses differ) [E]. A PSP run waits for your word |
| H42 | Bunker doorway light (a bright wedge beside the door frame) | **Fixed (restoration), bake side** [E]: the cause was the bake's lightmap halving averaging across a chart seam on the door-frame surface; the baker now halves each lightmap chart on its own with its edge texels clamped (`docs/review_fix-h42.md`; `--no-lm-chart-halve` turns it off). b30 doorway wedge gone, 0-37 px a shot elsewhere, play identical, same size (lab 39542362955). Needs the b139d bake. |
| Test 3 #3 | a10's decals and particles vanish in lit firefights (hand test 3, shots 010-011) | **Fixed (restoration)** [M cause, E cost]: the light pass filled the display list first (708 of 768 KB; 25,408 draws skipped), so decals and particles were the draws left out; light draws now stop 64 KB short of the limit (`docs/review_fix-listres.md`; `nolightreserve` turns it off). No cost in the emulator; the firefight wasn't reached there, so the PSP log's 'light draws held back' line confirms it (§Test). In the fullest frames the farthest lit surfaces lose dynamic light instead |
| H8 | Signs through the floor | Not touched |

## §Memory

**R4, the pools (`psp_capacity.h`).** The campaign's arrays were the Xbox's; long play peaked at 63 of 256 actors, 254 of 768 props, 338 of 896 lights and 128 of 512 glow particles [M, long-play census]. Now actors 160, props 512, lights 896, glow particles 256; the game state is 280,960 bytes shorter and the heap 5,900 KB against b138's 5,696 (the last pool commit's figure; later merges moved it a little, and the build log has the final one). A scripted actor that can't be placed could hold a mission up, so actors and lights keep the margin the every-encounter bound asked for (b40: 139 actors, lights to 896). `PSP_POOLS_XBOX=1` builds the old sizes. A full prop pool logs once and play runs on, a unit or two missing [D].
```c
#define HALO_PORT_MAXIMUM_ACTORS 160 /* (256) */
#define HALO_PORT_MAXIMUM_PROPS 512 /* (768) */
```
c10 was the one map the census could not cover: props passed 512 only with the test driver's 84 to 90 live actors and bodies left lying (586 at player pace; 414 with bodies erased); real play peaked at 254 [M]. Reverts at the release's sizes: 5, with 0 failed heap requests [M]; the c10 crash needs the Xbox's heap (272 KB smaller) plus the driver dropping the player out of the level [E]. 768 props would cost 78 KB of the ~148 KB spare [D]: kept at 512. Untested: a revert with the prop array completely full.

**The level carve (`levelcarveN`, on; `levelcarve0` off).** a10 from the main menu draws ~1.67 MB of level copies in the cryo view against the arena's 1,056 KB, so 15,152 draws were skipped (the floor grating, walls) [M, since b136]. A second room is carved from the texture RAM block, only when copies need it, in 128 KB steps to 768 KB, and handed back after 300 empty frames. a10: 0 draws skipped, play identical [E]. It never grows on the menu map, on Blood Gulch, or with `noarena`.

**Smaller items.**
- Update rings sized for a local game, 16 and 1 (not 128 and 32): 74 KB of `.bss` back to the heap, campaign only (`PSP_FULL_RINGS` builds the old).
- Stencils (`stencilsold` keeps the old order): c20's 162 KB stencil block failed on b138's ladder because the load's small requests ate the heap's only large hole; the block is now taken as the old map is let go, and asked again at the load's end. No memory set aside.
- The stick (`logflushgame`, `geolockeach` keep the old): Blood Gulch stalled 1.7 s when a geometry read waited behind the log's 1.4 s write [M]. The heartbeat now writes a three-quarters-full log, and a read's seek and read share one lock.
- Models' VRAM room back to 600 KB (`vrammodelsN`): at 784 b30's texture cache was 432 KB, with 193 untextured frames (a run of 85) on the PSP-1000 against 21 at 600 [M].
- Profile saves (`shrinkold`): a deleted profile stayed listed (SetEndOfFile couldn't shorten), a 64 KB copy buffer overran the 48 KB profile-write thread (the b138 crash; now on the heap), and ghost save folders held their names for good.
- Logs: the last 20 launches are kept on the stick, not 8.

**The checkpoint stage (H4; `statestagemalloc` takes it from the heap as before).** The 32 KB stage is a static array, so a checkpoint no longer falls back to the 2 MB page format when the heap is in 107 pieces.
```c
stage_block = from_heap ? (byte *)malloc(PSP_STATE_STAGE + 63) : psp_state_stage_reserved;
```
It costs 32 KB of heap from the start. On the PSP-1000 the failure did not reproduce in 300 s (it came after game time 31,052), so this is unproven until a long run [D]. Each checkpoint's log line now names its writes and the slowest (0.8 s single writes are the stick's [M]).

**Dynamic vertices (H12; `dynamickeep`) and HUD textures (H21; `hudpinoff`).** Sprite and contrail groups pop their vertices off the stack once copied (exact: same vertices copied). A texture bound during the HUD's or screen quads' draws is pinned and, if missing, read past the frame's budget. PSP-1000, b30 to 300 s [M]: frame peak 2,436 to 688 of 4,096; untextured HUD draws 88 to 0. Whole-run frame -0.4%, drawing +0.9%, tick -2.1%: inside the spread; the HUD pin is the likely source of any drawing cost [U].

**Geometry cache (H10, H32; `geofifo`, `geokeepN`).** The ring passes over any block drawn in the last 60 frames before evicting (16 tries). On the emulator's routes the counters don't move (a10: 970 misses against 939) [E]: its walk loads a third of what the hand test did, so the churn needs your stretch.

## §Look

Restorations are on by default; the costs below are emulator game-time figures.

**Relief (H6, H24; `norelief`).** Bump maps' shading is baked into a second copy of each wall's base map; the PSP picks it with `psp_relief_key` (one pack lookup per level draw). The bake now box-filters the bump over each base texel's footprint and leaves relief out where a base texel covers more than 4 bump texels (the streaks on a30's stretched cliffs). a30 cliffs, 37.5 / 14.2 ms with and without [E]: no cost. The one fixed light direction is the known limit.

**See-through parts one-sided (H3, H17; `seethroughtwosided` = both sides).** The Xbox culls see-through back faces unless the shader is two-sided; the port drew both, so Cortana's back faces were added over her front. A one-sided bit rides the blend word and the GE turns culling on. Changes only the bridge's hologram grid and green panels in a10's window [E]; frame 31.45 to 31.47 ms.

**Cortana (H3).** Three parts:
- *Self-occlusion and order (`noselfocclude`):* her biped's tag sets "transparency self occludes"; the Xbox first writes her see-through parts' depth only, then draws, and sorts her parts last. Now `transparent_occluder_depth` does the depth pass. Cleaner hair and face [E]; frame 31.78 to 31.78 ms. The extra depth-only draw is the GE's: the emulator can't see it, so the PSP is the only measure [U].
- *Gels (`nogel`):* the seam was the pillar's red edge behind her: a10's alert beacons are lights with a rotating gel cube map, and the light pass never read it, so each flooded the walls. A light without a cone takes its gel's average over all six faces (the bake's `gel_average`). Edge red 37 and 46 to 11 and 21 levels, about what turning the light pass off gave [E]; cost nil; b30 unaffected (no gels). The beams' sweep is not restored.
- *Tint (bake):* the clipping tint returns with the sample-size fault fixed (`_shuffled`, which failed four maps' bakes in b139a); applied, but below one 565 step on her dark map [D].
- *Angle tints (`noangletint`):* the bake keeps face-on and edge-on tints (`TNA1`); each vertex blends by |cos|. She was evenly bright and magenta.

**See-through composites (`nocomposite`; bake `--no-composites`).** 62 of 195 see-through shaders drew a map that isn't the one carrying their shape [D]. A shader whose look comes from two maps now gets a texture of its own, worked out texel by texel (`composite()`, packed under `shader address | 1`, table `TNC1`); the other maps' scrolling is frozen at time 0. b30 flight and beach: tick 14.97 against 14.98 ms, frame 21.90 against 21.88 [E]. Pack +22 KB (a10), +163 KB (a30), +90 KB (b30). Water layers' hard blotches gone, the Jackal's shield see-through [E]. Composites are never baked as detail maps (b40 and c40's bakes had failed on that).

**Alpha tests.**
- *Decals (H7, `decalsquare` = as before):* an alpha-tested multiply decal used `GU_TCC_RGB`, so its alpha stayed opaque, the test passed everywhere and the whole quad drew (a10's floor marking, a green square). `GU_TCC_RGBA` lets the texel's alpha reach the test; the multiply blend reads no alpha. Green 31% of the frame to 10% [E]; a10 to 16,000: frame 20.84 against 20.85 ms.
- *Trees (H14; bake `--no-cutout-bump`):* the Xbox alpha-tests an environment shader's stage 0, the bump map; the bake had dropped bump maps, so a30's tree line drew as solid cards. Cut out by the bump's alpha: cards become serrated crowns [E]; a30 opening +0.12 ms a frame, inside the noise. Side effect: a30's Pelican canopy now draws lighter and bluish glass, not yet compared with the PC [U]. Palettes are also fitted on each side of the alpha test (effects maps stay 8-bit: +157 to +257 KB at the top levels per map).
- *Flat glass (`flatnoround`):* a10's cryo glass added (9,10,7) per pane, which the GE's 565 rounding cut to (8,8,0), so two panes were olive; flat tints are rounded to the nearest step. A handful of draws a frame.

**Light pass.**
- *Reflection (H22; `noreflection`, `modelambientN`, `modellightN`):* the Xbox's model shader adds cube map x specular mask x reflection tint; the port never drew it, so polished metal went near black in shade. The reflection is folded into the part's ambient. b30's beach 25.9 against 25.6 ms (+1%) [E].
- *Glass reflection (H9; `glassnoreflection`):* level glass with no diffuse map draws its cube map's patch. No frame-time cost; never reached a10's pit (above).
- *Edges (H23; `lightsplitold`, `nolightsplit`, `lightliftall`):* the spotlight's split is decided per edge, not per triangle, and a whole triangle is no longer lifted toward the eye. The dashed bright line along a tunnel seam goes away [E]; 0.11 ms a frame. The PSP's tunnel splits where the emulator's doesn't, so dashes may remain on split pieces.
- *Edge-on nudge (H33; `nudgeedge0` = as before, `nudgeedgeN`):* level overlays and decals got a depth nudge with a slope term; edge-on that is thousands of depth steps a pixel, so a light strip behind a pillar was drawn through it (the Xbox uses no bias at all). `slope_units_capped` drops the slope term past cosine 0.10. Dashes 46 to 0 pixels, other shots 0 to 9 stray pixels [E]; cost none (6.83 against 6.84 ms).
- *Level overlays one-sided (`overlaysnocull`):* the Xbox's environment passes cull back faces. Free; not the cause of H33.
- *Lens flare (H41; `flaresshort`):* the level-only ray ran short of the flare, so a light glowed through a wall; it now runs to 2 cm short of the flare. Same 3,088 rays; frame 29.47 against 29.48 ms [E].

**Contrails (H28; `contrailsflat` = as before, `nocontrails`).** The Xbox's effect combiner multiplies colour by vertex alpha on added blends; the GE's added blend never reads alpha, so every segment drew at full colour from end to end. `contrail_faded_color` scales each corner by its alpha; the map wrap now follows the shader's clamp flags.
```c
case 3: case 4: case 6: case 7: /* add, subtract, max, alpha-multiply-add */
	r = r * a / 255; g = g * a / 255; b = b * a / 255;
```
a30's fight: 87 to 133 pixels a shot brighter by more than 40 levels on the unfaded side [E]; whole run -0.21 ms (se 0.20). Drawing contrails at all costs +0.23 ms a frame over a30's fight and +1.24 ms in its heavy stretch [E].

## §Calls

These are look or sound changes that are not restorations, so they are yours. Where one is on, you chose it; the A/B is in its walk-through.

| Keep word | What | Default | Evidence | To decide |
|---|---|---|---|---|
| `nocontrails` | tracers drawn | **on** (your call) | +1.2 ms a frame in a30's heaviest stretch [E] | pick on a30's fight |
| `menuzoomN`, `menuglyphN` | menus 115%, glyphs 125% | **on** (your call) | cost none; the page is drawn about its centre, so no box overlaps | `menuzoom100 menuglyph100` is the TV's size |
| `genericnoise` | a generic shader's second map drawn again over it, moving | off | fires on the camouflage powerups (3 to 1,324 passes per window) but whitens the sphere [E] | look |
| `camoshimmer` | camouflaged parts as their own map, faint and drifting | off | no camouflaged Elite in view in any emulator run: unseen | look; needs the Elites' bunker |
| `particleranks` | projectile visuals never cut by the budget | off | plasma bolts 477 thinned of 256 made, to 0 [E]; cost moves to smoke and sparks; -0.08 ms | look |
| `audio44` | mix filtered to 44.1 kHz by our own half-band | off | none: needs a listening A/B | ears |
| `statedelta` | checkpoints write changed pages only | off | worse on the PSP-1000: median 658 ms against 185 to 194 [M] | shelve |
| `dithershift` | the GE's dither moved each pass (build switch) | off, out of default builds | frame 52.1 to 59.2 ms, one run a side [M] | look; not free |

## §Front end

- **Boot screen (`bootlog` = the log, as before).** A thread of its own (priority 0x1f, above the game's) draws the loading frame ~30 times a second from keep.txt's reading to the first frame, from a 64 KB display list in a VRAM region nothing uses that early. The first 3 seconds say how to reach the development menu (`boothintN`, `boothint0`). The main menu settles under a veil on `main_loop`'s own frames (`bootsettleoff`): b137's settle hung at boot because the vertical-blank thread isn't running that early [D]. Every load shows a bar (`loadingnobar`) and the loading screen holds 6 s at least so the chant's crescendo plays. 16 KB of stack outside the heap during start-up only.
- **Menus as the PSP's (`xboxglyphs`, `glyphmapfixed`, `menumultiplayer`, `menucampaign`, `menuwarnall`, `xboxwording`).** PSP button glyphs on every footer and the name-entry keyboard; each build's main menu without the other mode; a designed FALSE no longer draws the red error. Menus 115% / glyphs 125% (§Calls). The all-levels profile ("All Levels", every level open at every difficulty) ships in the zip, built from the engine's own struct and checksum rules (`seed_profile()`); it has a slot of its own, so unzipping never touches a player's profiles.
- **XMB tile.** The Sierra-III mark on a smoked-glass tile (title "Sierra-III: Halo CE port"); the tile's glass blur 0 to 14, because the XMB doesn't place the tile exactly where the wallpaper patch assumes and a sharp patch showed shifted (H13) [D][E]. Audiowide (OFL) credited.
- **The menu ring (ui.map only).** The detail pass draws the outer ring's panel lines over the base (`nomodeldetail`, `modeldetailbiasN`); the glow clamp is off (`glowclamp` turns it on: the clamp is what shimmers on the far band [M]); `showcasebiasN` is off by default after b138 (the "panels"). Tried and taken out: per-pixel level selection (twice the shimmer [M]).
- **Select in menus (`menuselectpress`).** Select is Back, so the screenshot chord (Select + Right) closed the pause menu in the frame before its shot. Back now goes on release, and only when Select was pressed alone.
- **Action hold 180 ms, not 250 (`actionholdN`).** Your b138 hand test.

## §Sound

- **The buzz (`adpcmold`).** Each 36-byte Xbox ADPCM block holds its first sample in the header and 63 more in nibbles; the 64th only pads. The decoder dropped the header's sample and played the padding, so every 64th sample was held and the next skipped: a 344 Hz buzz on every ADPCM sound. After ChupathingyCE's fix (Jeff Clark, `5f3a0f98`); the bake's `soundpack.py` and `snd0.py` take the same convention, so a re-bake is needed. On the chant's own bytes block edges stepped 348 against 178 on average, 178 after [D].
- **No melee with no weapon in the campaign (`unarmedmelee`).** The Xbox and PC block it; ChupathingyCE lets it through for multiplayer loadouts of none, which also reaches the campaign. The multiplayer build keeps upstream's rule. Upstream has since fixed it; our block can go with the next pin.
- **Menu music (`streamnolink`).** A permutation shorter than the queue left the channel with no playing permutation, so the fill never sent the queued one and music skipped; it is now carried on, with 16 stream packets.
- **The chant from the player's bake.** The tone is 18,396 bytes of the disc's executable; the EBOOT carries none. The bake finds it by SHA-256 (`chant.py`) and writes `chant.adp`; without it the loading screen is silent (`research/chant_provenance.md`).
- **Instruments:** `cachedump`, `silentoverrun`, E1 and E2 (`lruvoldscan`), loop voices' loudest gain, packets that skip.

## §Sim

- **The flip (H31; `flipwakeoff`).** Wake-on-write checksums a sleeper only when a known writer marked it; the player's flip wrote the vehicle's upending flag into a vehicle asleep since it came to rest upside down, so its torque never started.
```c
if (!PSP_KEEP_ONCE("flipwakeoff")) psp_wake(player->action_object_index);
```
  The flip fixture: up.k -0.99 to 0.95 in 29 ticks; `flipwakeoff` stays at -0.99 [E]. One call per flip.
- **H25, H26: not the simulation.** One scripted Warthog drive agrees on the PC (both engine pins), the emulator (default and every speed-up off) and the PSP-1000 to four decimals through the ram, clip and bounce, and to 0.001 and 0.003 wu after the circles [M][E]. The PSP wobbles no more than the PC (13.4 against 15.1 deg/s). What you saw is how the vehicle is drawn (camera, or pose at the PSP's rate): the next recon.
- **Instruments:** `vehiclecost` (per-type tick cost), `drivetest`, `transwalk` (the player is put in each loading zone in turn: all 68 BSPs on ten maps load, every mission end loads the next map [E]), `lightdump`, `sdump`, `flycam`, `neardump`.

## §Bake

The EBOOT and pack format changed little; the bake changed a lot, because a player runs it.
- **Re-bake needed.** Palettes by a numpy k-means (scikit-learn out), decals cut by faces and ear clipping (Triangle out), Python 3.9 to 3.14: packs and decal files differ byte for byte but draw the same (`bake_changes.md`).
- **Guided bake:** drop two folders, double-click `Bake Halo.cmd`/`.command`/`bake-halo.sh`; Python fetched at run time by pinned hash when none is found. Faster bake (campaign by default, palette cache, biggest maps first; 101 to 9 minutes for the campaign, the same bytes [M]); `--sound s9` default.
- **One bundle per OS** (Windows, macOS, Linux) from `bake-bundle.yml`, tags only and by hand; the Linux bundle can be built locally with no Actions minutes (not for players: glibc 2.39).
- **The bake check (`bakeany` skips it).** The bake stamps `halo/bake.txt`; at start-up the build refuses maps of another format with a screen naming both bundles.
- **Public export:** `tools/public_export.py` with a deny-list, LICENSE (MIT with carve-outs, held off main), names and infrastructure scrubbed from kept text. `play.md`, `for_other_ports/`, `upstream_reports.md` are docs only.

## Cost summary

Nothing added to the release costs more than the emulator's resolution, except the two you chose: contrails (+0.23 ms a frame over a fight, +1.24 ms in the heavy stretch) and the reflection fold-in (+1%, b30's beach). The light-pass edge fix is +0.11 ms a frame. Self-occlusion's extra GE draw is unmeasured: the emulator times only the CPU [U]. Everything exact is exact: composites, one-sided parts, relief and relief's bake add no frame time within the pairs' spread.

## Release checks

- **Release check b139rc2** (strict, 13 runs): PASS 13/13. No segfault, no `crash.txt`, strict sites 0 in all 13 (the menu and a10 strict walks too). d20, a50 and c20: play and both seeds identical to b138 at all 12,000 ticks. `hop-menu-a10` loads a10 through the menus. Frames per second, rc2 against the first candidate's check: d20 11.0 / 10.9, a50 12.9 / 13.0, c20 12.1 / 12.1, b30 17.2 / 17.6, c40 28.7 / 29.1, a10 18.9 / 19.0 [E]. d20's sound cache failed for room 51 times (64 before).
- **PSP ladder** (PSP-1000, all ten maps, quit by itself): PASS. No crash, 0 heap failures, Media Engine 0 failed jobs; sound cache failed for room 8 times (c10 7, d20 1) against 48 in the earlier ladder [M]. Against that ladder, which ran on the PSP-3000 on b139a: fps within 3% on all ten maps (a10 16.3 / 16.8, a30 7.8 / 7.8, a50 11.3 / 11.0, b30 4.5 / 4.3, b40 17.7 / 17.4, c10 9.0 / 9.2, c20 11.0 / 11.0, c40 20.7 / 20.8, d20 7.4 / 7.5, d40 24.9 / 25.1). **To watch:** tick c10 18.4 against 16.7 ms (+10%) and d20 36.3 against 34.6 (+5%), more than the two PSPs' usual 3%. The two ladders differ in PSP, bake and length (45 s against 90 s), so only a ladder on one PSP would tell; it is not a blocker.
- **c10 faults** (§Memory): prop 512 kept; nothing blocks b139.
- **The build itself** (strict PGO, b139's own profile `campaign-b139c`): linked with 0 functions compiled without their profile counts; program 4.96 MB, heap 5,796 KB.
- **Not in these checks:** the emulator pictures of the 24 shots a run were not looked at.

## §Test

On the PSP-1000, newest build, logs as always:
1. **b30, the long run (game time past 31,000):** checkpoints (H4/H40): any `heap: malloc of 32883 bytes FAILED`, stalls over a second; geometry churn at 600 to 700 s and 2,000 to 2,100 s (H32); the corner at ~2,600 s (H33), the light through the wall at 2,268 to 2,278 s (H41), the bunker doorway (H42); shade on the beach (H22); the beach's water layers (H18); the Pelican's cockpit (H17) and its path at ~3,300 s (H39); a Warthog flip (H31), then vehicles for H25/H26 with the chase camera in mind.
2. **a30:** cliff walls (H6); the tree line (H14); a fight with tracers on (H28; `nocontrails` if the frame rate hurts); HUD parts under load (H21).
3. **a10:** the opening (Keyes' and Cortana's heads, H1/H3; the seam), the floor marking (H7), the maintenance tunnels under the flashlight (H23), the floor glass (H9), and the lift switch on c10 (the emulator's 426 to 464 ms).
4. **Front end:** boot screen and hint, menu size (§Calls), the XMB tile (H13), pause menu with Select, a profile deleted and made again by the same name.
5. **Listen:** the buzz is gone? If treble is still fizzy, try `audio44`.
6. **c10 and d20 tick:** if you play them, say if they felt slower than b138.

## Credits for what we borrowed

- The Xbox ADPCM block layout: ChupathingyCE's PC audio, Jeff Clark (`5f3a0f98`): commit, this file, README Credits.
- The vector palette for bump maps: ChupathingyCE's `bitmaps.c` (`bake/lib/vector_palette.py`), as the relief's walk-through says.
- The rest read ChupathingyCE's Xbox rasterizer files to restore what the Xbox draws; no code is copied from them.
- halo-ce-vita (BirchWoodGod) and DamnationCE (xshxdex98): credited since b126, unchanged here (`research/research_vita.md`).
- The unarmed melee's upstream cause is c9ee319a (MrBruh); already fixed upstream, nothing taken.

## Still open (not in b139)

H2, H15, H16 (ring); H8; H9 (glass reflection never queued in a10's pit); H19, H20, H29, H30, H38; the far ocean's violet (the bake's cube mean is the likely cause [U]); the Pelican canopy's lighter glass against the PC; a10's grating and corridor views (dark in every BSP: the original's lighting, or a lightmap fault, needs your shots); the beacons' sweep (a per-pixel gel, a look call after an A/B); H35 and H36's looks.
