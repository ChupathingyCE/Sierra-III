# Builds

Every build Sheepdog01 ran on the PSP, and the commit it was made from. Build numbers are what the game's log prints ("bring-up build N"). Every zip is kept in one private GitHub Release, `build-archive`, with any game data removed (see the end). It's a record, not for sharing; a build gets a Release of its own only when Sheepdog01 shares it.

## With source (b87 onward)

Each of these is a commit, tagged `bNNN` (`git checkout b108` gives that build's source). Dates are when the source snapshot was taken (US Central).

| Build | Commit | Date | What it brought |
|---|---|---|---|
| b87 | `ec09d9cad9` | 2026-10-06 02:40 | Stencil bake, 16-bit level vertices (levelcompact), skincut, ME measure jobs |
| b88 | `b49b136db5` | 2026-10-06 08:07 | ME cycle counter fixed, exit log, learned textures saved in play |
| b89 | `84dc931443` | 2026-10-06 09:16 | Fixes from the code review |
| b90 | `40857881fd` | 2026-10-06 10:10 | Permanent decals on the level, vblank throttle back on |
| b91 | `54bdbb93f6` | 2026-10-06 11:51 | Detail passes by distance, decal planes remembered |
| b92 | `958d6e93f4` | 2026-10-06 16:10 | Double-buffered launch menu, stamped screenshots |
| b93 | `a1309cece2` | 2026-10-06 17:22 | benchrun bus tests, converted-table sweep |
| b94 | `948f31f05c` | 2026-10-06 17:43 | Geometry arena from the texture RAM cache |
| b95 | `674c03ba6a` | 2026-10-06 18:24 | Stack and buffer high-water marks |
| b96 | `6a1dd3d217` | 2026-10-06 20:52 | The load trace |
| b97 | `1dc05ad869` | 2026-10-06 21:37 | Crash report on screen |
| b98 | `cc6155447f` | 2026-10-06 21:55 | The blackbox |
| b99 | `4d572e44f1` | 2026-10-06 22:25 | Blackbox batched; crashplay |
| b100 | `e60d76f7d0` | 2026-10-06 23:47 | The Memory Stick lock |
| b101 | `6979c85d7c` | 2026-10-07 00:23 | crumbs; breakable surfaces fixed (The Library's freezes) |
| b102 | `a7baf694c2` | 2026-10-07 04:41 | Own crash handler, the one-shot bake, float colours |
| b103 | `9885c88932` | 2026-10-07 16:13 | Display-list accounting, flashlight split |
| b104 | `108d5c5b52` | 2026-10-07 17:05 | Lock waits bounded |
| b105 | `bfb9c92a61` | 2026-10-07 22:35 | Bus probe, heap reports, display list out of the program |
| b106 | `0dbdddc29e` | 2026-10-08 05:34 | Animation cache, the b105 profile's renderer fixes |
| b107 | `3c4d34cc6f` | 2026-10-08 17:51 | Campaign-only engine, 512 KB list, sound cache 1,536 KB |
| b108 | `89fdd6ab57` | 2026-10-09 05:03 | Every campaign map fits; scratchpad; `-Os` variant zip |
| b110 | `b482037644` | 2026-10-09 14:11 | ChupathingyCE as the engine base |

b109 was never shipped: its changes are in b110. `build_bNNN.md` and `hardware_bNNN.md` in this folder hold the longer reports.

## Binary only (b1–b86)

These builds' source wasn't kept, only their zips. The milestones below come from the write-ups in this folder.

| Builds | Dates | Milestones |
|---|---|---|
| b1–b18 | 2026-10-01 | The first bring-up EBOOT (b1); the crash handler PRX (b7); the null renderer measured (b13–b14) |
| b19–b41 | 2026-10-02 | The GE-native renderer finished (b17–b21); render interpolation off and single-precision maths (b19); textures in VRAM (b20); blob shadows (b25); checkpoint page map (b26); particle budgets (b36); the VFPU matrix product (b37); hot code at -O2 (b38); the frame throttle sleeps (b41) |
| b42–b60 | 2026-10-03 | Vehicle sleep (b44); the hardware profiler's phases and the learned VRAM textures (b46); cached display-list writes (b48); rigid model cuts at the near plane (b60). b51's zip is missing |
| b61–b70 | 2026-10-04 | The box test and palette cache (b66); the geometry and keyframe packs (b68); 16-bit collision (b70) |
| b71–b85 | 2026-10-05 | Synchronous geometry reads (b72); the geometry arena (b73); the clock fix that took play from under 1 fps to 25–29 (b75); the debug menu (b77) and overlay (b80) |
| b86 | 2026-10-06 | Sound mixed on the Media Engine |

## What was removed from the zips

Twenty early zips carried a map pack for testing. That is game data, so it was removed from the copy in `build-archive`. The EBOOT and PRX modules are unchanged.
- b20–b36 and b39, b49: `bloodgulch.ptex`, plus `ui.ptex` in b27–b34;
- b87: `bloodgulch.pstn`;
- b102: `c20_track.ptex`.
