# Review: LK22, the level bouncing against the models (branch `lk22`)

Sheepdog01's report (b122 on his PSP-1000, 2026-10-14): in Two Betrayals' (c40) opening cutscene "the terrain, the bridge, the doorway, the pillars curving up into the ceiling, all are bouncing back and forth every frame"; doors (objects) look steady; on The Pillar of Autumn (a10) the frame round the bridge glass "wobbles and bounces" in the exterior flyby. LK18's "bouncing textures" (`hardware_b120.md`) is very likely the same thing [U].

Branch `lk22` from `w123` (`15fd839`): `4f3c0f6` (the test instrument), `5f1aba1` (the fix). One restoration, **on by default**; `levelexactoff` turns it off. No fixup pairs.

Labels: [M] PSP, [E] emulator, [D] code, [U] inference. Every emulator figure below is from JIT runs with `fixedticks fixedseed`, so the two sides of a pair draw the same frames of the same game.

## 1. The cause: the GE drops the low bits of the level's translation

**The mechanism** [D]:
- The GE takes its matrices as 24-bit words: pspgu sends a float's top 24 bits (`sendCommandf`: the float's bits `>> 8`), so each matrix entry keeps 15 bits of mantissa and the rest is **truncated**. PPSSPP decodes them the same way.
- Models are safe: `relative()` (`psp_ge.c` 2386) takes the eye off each model matrix on the CPU in full floats, so what reaches the GE is a small number (a door three units away), and 24 bits carry it to a ten-thousandth of a unit.
- The level is not: its vertices are world positions, so `world_model()` (2395) loads the identity with the eye's **whole position** as the translation. In c40's control room the eye is at about (361, −714, 76) [E, the screenshot lines]. At 714 a 24-bit word steps in 1/64 of a unit, at 361 in 1/128, so the level is drawn up to 1/64 unit (about 5 cm) off where the models are.
- That error is not fixed: it is whatever the truncation drops from the eye's position this frame. As the cutscene camera drifts, the dropped part jumps between 0 and 1/64, frame by frame, so the level jumps against the camera and against the models. Near the eye that is pixels: the bridge floor half a unit below the lens moves up to ~6 px for 1/64 unit [U, from 194 px per unit at distance 1].
- a10's flyby is the same: the eye is at about (500, −151, −27), so x steps in 1/128 unit; the bridge window's frame is a few units off [E].
- Blood Gulch is near the origin (under 128 units, steps of 1/512 or finer), which is why b35's camera-relative change looked complete there.

**It isn't the near plane** [E, D]:
- [D] Changing the near plane changes only the projection's depth row (`perspective()`, 2539; `sceGumPerspective`): x and y on the screen are `x/w`, `y/w`, and neither depends on it.
- [E] lab 38647675293 (batch `lk22a`): c40's opening and a10's flyby, 72 frames each in runs of six, with `nearauto` (b122's default), `near4` (preset 14) and the game's own plane. `nearauto` against `near4`: identical motion frame to frame; `nearauto` against x1: at most 12 pixels differ in any frame, and none where the level's position is concerned. Within the runs of six the near plane never changed while the jitter did.
- The near plane switching often in Sheepdog01's c40 log is real (the emulator shows it too: 153 frames at x2 and 146 at x1 in the first window, capped by the Chief's and the Monitor's spheres), but it moves only depth.

## 2. The fix: the GE's world origin on a value it keeps exactly

`5f1aba1`, `psp/src/psp_ge.c`:
- **2356–2393** (the comment, `ge_eye`, `ge_eye_camera`, `level_exact`, `ge_matrix_float`, `relative()`): the GE's world space now has its origin at `ge_eye`, the eye with each coordinate's low 8 bits dropped, exactly as the GE would drop them. `relative()` subtracts `ge_eye`. So the level's translation `-ge_eye` reaches the GE **whole**, and every model's translation is still made in full floats on the CPU.
- **3191–3216** (the view): after `sceGumLookAt`, `sceGumTranslate(ge_eye − eye)`. The view carries the remainder, under 1/64 of a unit, which its 24 bits hold to about a millionth. A point is then drawn at R·((p − ge_eye) + (ge_eye − eye)) = R·(p − eye) for the level and the models alike.
- **3246–3253**: `ge_eye_camera`, that remainder along the camera's right, up and forward, made once a frame.
- The CPU's own conversions of GE-space positions to camera space add it, so the near and guard-band tests see what the GE draws: `model_corner_out` (3343), `model_to_camera` (3357), the bending parts' rows (`depth_row`, 7836) and the rigid parts' (`rigid_rows`, 8282).
- What is handed to the GE in its world space is given from `ge_eye`: a bent corner's world position (8118), and the point lights (5938).
- Untouched: everything the CPU works out from world positions (the level's classification, the cut corners, decals, shadows, lights' pieces, the ME's jobs) keeps the true eye, and draws through `world_model()` as before, so the level and everything laid on it share one exact matrix.
- **`levelexactoff`**: `ge_eye` is the eye and the view a pure turn, i.e. b122. Listed in the log's start-up list (`psp_platform.c` 876).

**The instrument** (`4f3c0f6`): `shotrunN` makes each `shotevery` moment take N frames in a row (72 shots at most), and each screenshot's log line now gives the near plane.

## 3. Evidence

**The off word is b122** [E]: `levelexactoff` against lk22a's `nearauto` runs (the build before the fix): all 72 frames of c40 and all 72 of a10 identical, pixel for pixel (lab 38649550275 against 38647675293).

**The level moves, the models don't** [E, lab 38649550275 / 38649583678]: on against off at the same frames, the differences are lines along the level's edges: the bridge's rails and panels in c40 (up to 11,030 pixels a frame, shot 53, t 21.6), the window frame and bridge interior in a10's flyby (up to 1,134 pixels, t 23.4). The Monitor and the bridge crew barely change.

**The bounce is the off side's** [E]. Per frame, how far the off picture sits from the on picture (phase correlation over 64-px tiles), c40's frames 538–543:

| frame | 538 | 539 | 540 | 541 | 542 | 543 |
|---|---|---|---|---|---|---|
| median x, px | +0.09 | +0.06 | +0.03 | +0.19 | +0.13 | +0.10 |
| median y, px | +0.10 | +0.06 | +0.03 | +0.00 | −0.05 | +0.39 |
| largest tile, px | 0.71 | 0.41 | 0.28 | 0.71 | 0.77 | 2.99 |

The offset changes direction and size from frame to frame. That is the level jumping against a camera that moves smoothly.

Measured as the frame-to-frame change in motion (how much each tile's motion differs from the previous frame's: small when the camera glides, large when the level jumps), c40's bridge stretches calm down with the fix:

| c40 run of 6 frames | t 19 | t 21 | t 23 | t 12 | t 5 |
|---|---|---|---|---|---|
| off: median jerk, px | 0.65 | 0.70 | 0.66 | 0.45 | 0.35 |
| on: median jerk, px | 0.30 | 0.37 | 0.41 | 0.38 | 0.29 |

The ~0.3 px left is the measure's own floor (the camera's real acceleration, lighting). a10's flyby looks at the hull from farther off, and the medians barely move there; its worst frames are the window close-ups at t 19–23.

**Nothing else changed** [E]:
- c40, a10 and b30 shots on against off, looked through: the same pictures with the level's edges a fraction of a pixel apart. b30's larger counts after t 110 are the first-person weapon's sway, which follows the emulator's clock (the stamp's t differs by 0.5 s at the same frame), plus the rocks' edges.
- Models cut at the near plane: 0 in either; cut-buffer messages: the same count; no crash, no segv.
- The multiplayer build (`PSP_MULTIPLAYER=1`) compiles and links; the change is drawing only, so the server's simulation isn't touched.

**Play unchanged** [E], hands off, on against `levelexactoff`, `nearauto assettrack geo2 nomeaudio fixedticks fixedseed statecrc`, `tools/statecmp.py`: the play hash and both random seeds are the same at every common tick on **d20 (17,611 ticks), a50 (28,636) and c20 (26,260)**. The whole state differs only in what follows the emulator's speed (`objects`' render mark, `contrail point`, `object looping sounds`, `first person weapons`; c20's `lights` at checkpoint 13,260 as before) and d20's last checkpoint, cut mid-line by the wall.

## 4. What only the PSP can say

- Whether this is all of what Sheepdog01 sees [U]. The emulator models the GE's 24-bit matrix words and gives the level up to a few pixels of jitter near the eye. Sheepdog01's wording ("the pillars curving up into the ceiling", farther off) sounds bigger than that. At ~5 fps the PSP's camera moves three times farther between frames than these runs' fixed ticks, so the dropped part changes completely every frame; that fits "every frame". If the PSP's GE also folds the world and view matrices into one before it transforms (the emulator doesn't), the folded translation would be large again and this fix wouldn't reach it. Nothing in the docs says the GE does that, and it lights in world space, which points to the two matrices being applied in turn.
- **Sheepdog01's `near4` test (preset 14) should not stop the bounce**: the near plane doesn't move x or y [D], and pinning it changed nothing in the emulator's frames [E]. If `near4` does stop it on the PSP, this reading is wrong and the near plane is back in question.
- The test: c40's opening and a10's flyby with the build as it is, then with `levelexactoff`; the bounce should be gone on the first and back on the second.
