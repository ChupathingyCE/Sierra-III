# Review: build 138

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b138 is b137 (`review_b137.md`) plus the day's merges: the heap fixes behind b137's stencil failures, the VRAM rooms made whole and the models' room enlarged, the menu ring's sparkle, the multiplayer build as a client only, and four exact speed-ups. It is the dress rehearsal for going public (`go_live.md`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Change (commit) | What | Kind | Default | Switch |
|---|---|---|---|---|
| the chant's voices static (`695f936`) | The loading chant's four streams in static slots, not memalign'd per load | fix | on | `chantheap` |
| lens flares (`6ffb351`) | Each light's flare results kept for one view, not the Xbox's four | memory (21 KB) | on | — |
| thread stacks (`c23d929`) | The CreateThread floor 64 → 48 KB | memory (48 KB a thread) | on | — |
| see-through table (`72457f1`) | From the free RAM above the heap when 80 KB stays free there | memory (50 KB) | on | `seethroughheap` |
| glyphram (`8659549`, `63a0fca`) | The glyph sheet in the texture RAM block, out of VRAM | memory | on | — |
| vramroom (`443f66f`), 784 (`d32c48e`) | The models' VRAM room may pass half; **784 KB by default** (the texture cache 432 KB) | speed | on | `vrammodelsN` |
| TP (`1f15429`) | Each object's update timed one tick in eight | speed | on | `objecttimeall` |
| sound instruments (`66f5f67`) | sound_render's steps, dry gaps and cut-offs in the log; streams queued deeper | instruments | on | `streampackets` |
| BONUS (`12f4f01`) | An object's `location.bonus` set to NONE where it's stored | fix | on | `bonusold` |
| hotwords (`6079a8d`) | Three hot words in one cache line | speed, exact | on | — (a layout) |
| showcasebias (`fcd3973`, `4895425`, `4d16ae3`) | The menu ring's model maps 20 quarter levels coarser, ui.map only | look (Sheepdog01's sighting) | on | `showcasebiasN`, `showcasebias0` |
| DRAWCOLD (`064a5d9`) | The debug vertex display's call made only when it's on | speed, exact | on | `debugvertexcall` |
| LD1 (`a7a9860`) | Attached lights re-linked once a frame, not every tick | speed, exact | on | `nolightsframe` |
| mpcut (`12d9613`, `a5b7607`, `bd5dbdd`, `68b27ff`) | The multiplayer build as a client only (Sheepdog01, 09:48) | multiplayer only | in that build | `PSP_MP_HOST=1`, `PSP_MP_COOP=1` (build switches) |

## §Heap The heap after a level change (b137's known issue)

b137's PSP-1000 ladder failed b30's and c20's stencils after a10 → a30 → a50: the campaign heap peaks at 5,504–5,576 of 5,600 KB, and the chant's four streams, memalign'd for each load and freed after it, split the holes the level's large blocks needed (stencilheap, lab 39040129189, reproduced it in the emulator: 451 KB free inside the heap, no 156 KB block).
- **The chant's voices** (`psp_dsound.c`): four static slots; `stream_start()` and `stream_unlink()` share a stream's set-up with the heap path, which `chantheap` brings back.
- **Headroom:** lens flares' per-light results for one view (21 KB of `.bss`), thread stacks at 48 KB (the deepest measured use is under 11 KB in 38,193 `stacks` lines), the see-through table above the heap when it fits (50 KB).

stencilheap2 (lab 39044318534) [E]: the ladder a10 → d40 with no heap failure in all four modes (default, `chantheap`, `settleoff`, `loadingoff`).

**Still tight:** a10 itself (b138rc [E]: 19 KB free inside the heap, nine level-copy requests waiting a frame, 2,551 draws skipped, as in b137's 2,551). For b139's memory work.

## §VRAM The rooms

- **glyphram:** b137 put the 32 KB glyph sheet in VRAM, which cut the models' room to 592 KB. It now sits at the start of the texture RAM block, which goes back to the system while the PSP sleeps; `psp_tex` counts each acquisition and `psp_ge.c` unpacks the sheet again when the count changes.
- **vramroom:** the check allowed the models at most half the room. Now the textures keep at least 256 KB (a 512 × 512 indexed map with its levels) and the models may take the rest.
```c
if (kb >= 64 && (unsigned int)kb * 1024 <= VRAM_CACHE_END_FULL - VRAM_CACHE_START - VRAM_TEXTURES_LEAST)
```
- **784 KB:** the renderer's PSP-1000 pairs (lab 39048150710 a30, 39048171900 b30; two runs a side) [M]: a30 frame −0.6 to −1.5%, drawing −1.2 to −1.5%; b30 frame −0.5 to −2.6%, the beach's tick −7%; nothing worse beyond the repeats' spread. Each side's log line: `the top 600 KB … the texture cache 616 KB` against `the top 784 KB … 432 KB`. b138's ladder shows the smaller texture cache on c20 and d20.

## §Speed The exact speed-ups

- **TP:** `objects_update` timed every object with two clock reads (~0.56 µs each), sleepers included. Each object is now timed on one tick in eight and scaled by eight; every visit is still counted. Sized by tp136 (PSP-1000): the timers cost ~1 ms a tick.
- **LD1:** `object_connect_lights` re-linked each updated object's lights every tick, 2.57 ms a tick on b30 [M]. Only drawing reads the links; the one simulation reader writes fields only a debug line reads [D]. The links are made on the last tick before a frame, in walk order, and an object deactivated with its link pending is linked then (a 128-byte bitset).
- **DRAWCOLD:** `rasterizer_debug_model_vertices` was called after every model part and returned at once unless its display was on; the call is now made only when it's on.
- **hotwords:** `rigid_runs_full`, `stream_frame` and `level_copies_left` in one aligned struct, one cache line instead of three.

b138rc2 (lab 39052259922, cache model, b30's flight) [E], LD1 and DRAWCOLD on against `debugvertexcall nolightsframe`: the tick −1,617 D-misses, −4,174 I-misses and −171 k instructions a tick; drawing −317 I-misses a tick (recon predicted 318 for DRAWCOLD). About 1.5 ms a tick on b30 [U, from the counts]; LD1's PSP A/B is in this build's tests.

**BONUS** is a fix that makes runs comparable: `location.bonus` carried stack garbage, which play never reads [D] but the sleep checksum, the state hashes and the perception memo's key did. With it, b30 is deterministic in every pool (ld1why3) [E].

## §Menu The ring's sparkle (Sheepdog01's sighting)

The hull's full-size maps sparkled: the GE picks a model part's level by depth, not by the map's size. `showcasebiasN` makes the showcase's maps N quarter levels coarser while ui.map is open (`showcase_levels()`, once a frame; the level mode set only when the map changes). ringflicker1–6 [E]: alternating pixels 5,124 → 89 (noshowcase 19), sharpness 16.29 → 14.29 (noshowcase 13.85). **Your look decides 20**: 16 is sharper with a little sparkle, 24 softer.

## §MP The multiplayer build as a client

Sheepdog01 (09:48): hosting and ad hoc wait until the campaign reaches its targets. `docs/mp_client_only.md` has the change list.
- Co-op's units out (their answers move from `psp_campaign.c` to `psp_nocoop.c`, which both builds compile); actors 8, props 32; no server ever created; the host's records one slot each; ad hoc off.
- The heap's ceiling 3,668 → 5,220 KB [E]. PSP-1000 (lab 39046598878) [M]: joined `[C] Slayer` on Prisoner with 7 players, played to the wall, heap peak 4,588 KB (b137's ceiling would have run out).
- **The profile and code order now apply to it** (the audit's item): campaign-b138 without STRICT (2 functions without counts), b132's order. Blood Gulch plays (b138rc2).
- Risk: a custom map with more than 8 actors loses the rest (stock multiplayer maps have none).

## §Sound Instruments

For the sound session's dry gaps and the remake's ME budget: sound_render's parts as steps in `profile.txt`; each heartbeat's first six dry gaps (channel, class, the silence, the frame before it); the sounds cut off, by reason. Streams queue deeper (`streampacketsN`).

## Release check

Lab 39052254838 and 39052259922, strict with b138's own profile (`campaign-b138`, 13 runs at `a9bf5ed`), against b137 as built (`7e52cd8`) [E]:
- **Play and both seeds identical to b137 at every one of 12,000 ticks** on d20, a50 and c20; the pools differ where BONUS and LD1 change bookkeeping (objects, actor, prop, lights).
- Strict clean on every run; b30, c40, the menu and a10's prompts drawn.
- The menu into a10: the saved-game check 115 ms, the load 1,031 ms, the settle 4 frames.
- The client-only multiplayer build plays Blood Gulch (heap top 3,068 of 5,220 KB).

`d32c48e` adds only the 784 default to the checked code, on the same line, so the profile still matches every function (strict: 0 without counts).
