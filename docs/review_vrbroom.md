# Review: `blipdepth`, the motion sensor's target in the depth buffer (VRB2)

Part of buying back VRAM: the motion sensor's 64 x 64 32-bit target (16 KB at `0x0CC000`, between the depth buffer and the texture cache) is drawn in the depth buffer's first 16 KB instead, after the 3D pass. Its 16 KB go to the model copies' room (600 -> 616 KB); the texture cache keeps its size.

The other two ideas looked at alongside it, one line each:
- **VRB1, the buffers' 32-pixel stride tails as texture room: rejected.** `stripcensus` counts the HUD textures that would fit a strip: five small ones, 10 KB of texels, half of them in VRAM already, palettes unable to follow. At most ~5 KB a frame of reads.
- **VRB3, the depth buffer reused for more than the sensor: not built.** The window exists (no world draw after the sensor), but the next frame's 3D pass rewrites the buffer, so anything kept there would be copied in every frame, and what draws after the 3D pass reads little from main RAM.

## Why the depth buffer is free there [D]

The sensor is drawn in the HUD's turn: `psp_ge_blips_begin` flushes the world's see-through parts first, and every HUD and screen draw after it has the depth test off. The target is cleared to black before its blips are drawn, so nothing the 3D pass left in those 16 KB shows. The next frame's clear wipes the target before the world tests depth against it. The rows it covers are the top 16 rows' depth (512 x 16 x 2 bytes = 16 KB at `2 * FRAME_BYTES`).

## Key lines

`psp/src/psp_ge.c`:
- `blip_place()` (called as the GE starts): `blip_target` is `BLIP_TARGET_DEPTH` (`2 * FRAME_BYTES`) under `blipdepth` or `blipdepthcheck`, else `BLIP_TARGET` as before.
- `psp_ge_blips_begin`, `psp_ge_blip`, `psp_ge_blips_end` are split into `blip_target_open`, `blip_draw`, `blip_target_close` and `blip_frame_back`, so the check can draw the same target a second time. Off, they issue the same commands as before, in the same order. The composite's `sceGuTexImage` reads `blip_target`.
- `psp_ge_blip_room_freed()` tells `psp_tex.c` the 16 KB are free (`blipdepth` alone, never with the check, which still uses `BLIP_TARGET`).
- `ge_kind` and `transparent_flush_body` count world draws issued after the target was drawn in the depth buffer (`blip_depth_live`); the frame's end logs them by kind. None is expected.

`psp/src/psp_tex.c`:
- `vram_cache_start` replaces the fixed `VRAM_CACHE_START`: `0x0CC000` with `blipdepth` (not under `benchrun`), `0x0D0000` otherwise.
- The freed 16 KB are added to `vram_models_bytes`, so `vrammodelsN` gives N + 16 KB to the copies and the texture cache's end doesn't move.

**The check, `blipdepthcheck`** (test only): the target is drawn in the depth buffer as under `blipdepth`, and drawn again from the same recorded blips at `BLIP_TARGET` after the composite. At the frame's end, with the GE done, the two are compared word for word through the uncached view, colour only (the alpha bytes are the GE's stencil, which the composite doesn't read). A difference would mean something wrote the depth buffer between the target's drawing and its use. `stripcensus` (the HUD texture census behind VRB1) is test-only as well.

## Proof

All runs with `nomouth`, `fixedticks`, `fixedseed` and `readswait`, so that sides differing only in timing play the same.

- **Mechanism [E]:** no world draw after the target, by kind, in any run. The CPU check: 0 of 2,059 frames differ on b30's plain path through 6,000 ticks (108 frames not drawn again: the list was full or the blips overflowed the record); earlier, 0 of ~1,450-1,500 on b30 with and without the deferred GE words. a10's route shows the sensor in one frame, which matches.
- **Play [E]:** `blipdepth` and `blipdepthcheck` against off: the objects as played and both random seeds the same at every tick, b30 6,000 ticks (plain path, and with the deferred GE words) and a10 7,200 ticks. Without `nomouth`, b30 used to part on any change in timing; with it, it doesn't.
- **Pictures [E]:** with `nomouth`, repeats of the same side still differ in some pictures (b30 plain: 384 of 2,999 frames between two `blipdepth` runs, mostly in one central tile; a10: 6 of 3,589), so a single picture difference isn't read as inexact. Frames where every `blipdepth` repeat agrees and off differs: b30 165 (a control, the same count with one `blipdepth` run standing in for off, gives 167); a10 2 (control 5). The sensor's tile: 2 against the control's 2. No systematic difference.
- **Room [D]:** `textures: blipdepth: 16 KB freed at 0cc000, to the copies' room (616 KB; the texture cache 616 KB)`; off, 600 KB and the same 616 KB cache.
- **On the PSP:** the emulator has no GE caches. One `blipdepthcheck` run on b30 shows whether the hardware's depth writes and texture reads see the same 16 KB as the CPU does after the GE is done.

## Gain [U]

16 KB more room for the copies, times the measured copy-fit slope (0.5-1.9 KB a frame read saved per KB of room) = 8-30 KB a frame of GE main-RAM reads, about 0.07-0.29 ms of tick. Too small to ship by its own case; it goes with the larger copies' room (`vrammodels784`), which it extends to 800 KB. Whether the stretch it helps is bound by the tick or by drawing: the copies' room saves main-RAM reads, which cost both.

## Risk

- A later change that draws the world after the HUD's turn, or turns the depth test on for a HUD draw, would draw over the target or read the target as depth. The after-target count and `blipdepthcheck` catch both.
- `benchrun` keeps the fixed rooms.
- Hot code: one predicted-not-taken test in `ge_kind` (each change of draw kind) and one in `transparent_flush_body`; the sensor's draws are unchanged in number.
- With `vrammodels784`, the copies' room is 800 KB and the texture cache keeps the 432 KB it has under `vrammodels784` alone.

## Multiplayer

The sensor draws the same way in the multiplayer build; the change is in the GE layer and the texture cache only, nothing in the simulation. It compiles with `PSP_MULTIPLAYER=1` and `PSP_METRICS=0`.
