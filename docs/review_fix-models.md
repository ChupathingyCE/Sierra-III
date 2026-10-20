# Review: fix-models (b139's hand test: H1, H3, H17, H34, H35, H36, H39)

Branch `fix-models`, off `w139`. Every change has a keep word; restorations are on by default, the look call (H35) is off. Evidence labels as in CLAUDE.md: [M] measured on the PSP, [E] emulator, [D] read in code or data, [U] inference. Costs are game-time figures from the emulator on the same stretch, before and after.

Status: **code in; the a10 bake and the restorations' cost read back; H1/H3 shots and H39's repro pending** (their first window, a10 at 107-117 s, turned out to be the cryo room after the opening cinematic). Sections marked PENDING are filled in when they read back.

**Cost of the restorations [E]** (emulator, the same stretch, before = `seethroughtwosided nolayers nearseatcine`): b30's opening Pelican flight, game time 0-1,532: mean frame 31.45 -> 31.47 ms, tick 16.16 -> 16.16 ms; a10's opening, game time 0-3,468: frame 14.11 -> 14.13 ms, tick 2.10 -> 2.09 ms. Within noise: nothing to hold back.

## H1: Keyes' and Cortana's heads cut open in a10's opening (restoration, on)

**Cause [D], [U] until neardump reads back.** nearauto moves the near plane out to x2..x16 when nothing is near the eye. A model's bounding sphere that contains the eye normally holds the plane at x1, except while the player is seated (the vehicle he drives contains the eye and must not hold the plane in). In a10's opening the player rides the cryo tube, so `psp_player_seated()` said 1, and the spheres around the cinematic camera (Keyes', Cortana's) were dropped as if they were his seat. The hand test's log shows the plane at x16 through the cinematic with no model capping it.

**Fix** (`psp_fixups.py`, players.c's `psp_player_seated`): in a cinematic the camera belongs to the script, not the seat, so the function answers 0 there, as on foot:

```c
if (!seated_in_cinematics && cinematic_in_progress())
	return 0;
```

Keep word `nearseatcine`: the old answer. Cost: one call per frame; the plane may now sit at x1 during close-ups (coarser depth there, as on foot beside a model). At a10 107-117 s (the cryo room, light-bar test) both sides read x1, `seated 0`: the cinematic is over by then [E]. PENDING: neardump and shots across the opening cinematic (game time 2-100 s), before against fixed.

## H3: Cortana inside out (restoration, on) and her clipping tint (bake)

**EBOOT [D].** The Xbox culls see-through geometry's back faces unless the shader is two-sided (`rasterizer_xbox_transparent_geometry.c`: `D3DCULL_CCW` unless the two-sided flag). The port drew every see-through part from both sides, so Cortana's back faces were added over her front: inside out, and brighter. Now a one-sided flag rides bit 15 of the queue's blend word (`psp_seethrough.h`, `PSP_SEETHROUGH_ONE_SIDED_BIT`), set in `psp_transparent_map` for generic and chicago shaders (flags byte +41 bit 2) and in FIXES_MODELS `#glasscull` for glass (+40 bit 2); `psp_ge.c` turns culling on for those draws in `transparent_flush_body`. Keep word `seethroughtwosided`: both sides, as before.

**Bake [D].** cortanafade (the tint that matches the Xbox's clipping of her noise mask x4) returns, with the fault that failed four maps' bakes fixed: `generic_result` was handed other maps' texel samples of a different size from the drawn map's (2048 against 4096 rows). `_shuffled(texels, i, n)` now gives each other map exactly n rows (its own seeded order, repeated or cut). The a10 + ui bake now completes [E] and logs Cortana's tints (`cortana_body` ffc95cd9 doubled, `cortana_hair` 69d100d1 doubled, `cortana vol` 008b8768 doubled). PENDING: her close-ups, old against new, on that bake.

## H17: the Pelican's cockpit frame flickering light green

[U] the cockpit's see-through parts drawn from both sides: back and front faces at the same depth swap order from frame to frame. The H3 change (one-sided) should settle it. b30's opening flight from inside the troop bay, three frames a moment, shows the cockpit frame steady and grey on both sides [E]: not reproduced there. The hand test saw it on a Pelican seen from outside; PENDING: the extraction Pelican's cockpit from below (the H39 repro).

## H34: no flashlight under active camouflage (the original's behaviour; no change)

[D] `object_lights.c` fades every light of a camouflaged unit by (1 - camouflage) unless the light tag sets "don't fade active camouflage". The port doesn't touch this code: the flashlight going dark is Halo's own behaviour. ChupathingyCE's PC build runs the same `object_lights.c` unchanged, so the PC reference behaves the same [D]. The emulator check (a10, flashlight then `cheat_active_camouflage`) was inconclusive: at those ticks the player stood in a lit room where the beam isn't visible; it changes nothing either way.

## H35: camouflaged Elites invisible but for their weapon (Sheepdog01's call, off)

The Xbox draws active camouflage as a refraction of what's behind; the GE can't sample the frame it draws into cheaply, so the port draws the model faint (15% alpha, no depth write), which reads as invisible. `camoshimmer` (opt-in): the camouflaged parts drawn as their own map added faintly (`CAMO_SHIMMER_LEVEL` 48 of 255), unlit, drifting per frame. The a10 A/B drew no shimmering parts (`camouflaged parts shimmering 0` on both sides) [E]: the player's own camouflage shows only first-person arms, which don't take this path. PENDING: an A/B with a camouflaged Elite in view.

## H36: powerups lack their shader decorations (restoration, on)

[D] generic and chicago shaders' extra layers (the +72 block) were never drawn; the Xbox draws them after their shader (`transparent_geometry_layer_draw`). FIXES_MODELS `#layers` queues them (`psp_transparent_layers`, depth 2). Keep word `nolayers`. It is right, but it isn't H36: on b30's camouflage powerups `extra layers queued 0` [E], because neither of their shaders has layers [D]: `powerup glass` is a glass shader (type 8, no layers block), `active camoflage` a generic one with four maps (`shield noise` twice, `shield offset`, `shield mask`) and no layers. The port draws one map of a generic shader (`shield mask`, tinted by the bake), so the animated noise turning over the sphere, the decoration, is the part left out. **Not fixed yet**: restoring it means drawing a generic shader's other stages as extra passes (the noise maps with their animation), which needs the shader's stage block; next on this branch.

## H39: b30's Pelican off its path while its markers keep the place

[D] poseahead is exact by design (all RAM written back before the push; animation pages written back after reads). The repro needs b30's extraction script: `scriptsdump` (recon builds) names it, then the console reaches it with `poseaheadoff` / `poseaheadcheck`. b30's scripts include `extraction_build` (static), `pelican_fade` (dormant), `cutscene_extraction_exit` (static) [E]. On b30's opening flight `poseaheadcheck` compared 72,782 poses made ahead with the main CPU's: 0 differed [E]. PENDING: the extraction Pelican (`extraction_build` called at the hand test's spot, poseahead default, off and checked; and the extraction cinematic).

## Instruments added

`shotfromN` (shots start at N s), `neardump` (on shot frames: the plane's scale, what allowed it, the nearest sphere), `scriptsdump` (FIXES_RECON: scenario script names at load). The log gains "GE: models as the Xbox draws them": one-sided parts, layers queued and unmapped, shimmering parts.
