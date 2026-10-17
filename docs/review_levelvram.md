# Review: the level's streams in vrammodels' VRAM room (branch `levelvram`, on `glowmaps`)

STATE 6b item 2 ("the level's geometry in VRAM"), for the drawing-bound maps (the master's order: a30, d20, c20). An exact speed-up: the same bytes, the same pixels; on by default, keep word **`nolevelvram`** off. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## Why

- `gereads` per place (this branch's first commit) [E, lab levelvram1 39021800898]: the level's copies are the largest geometry the GE still reads from main RAM, the bus the CPU waits on: a30 759, b30 602, c20 669, d20 746 KB a frame. Their best 256 KB would take 64-98% of those reads.
- vrammodels' 600 KB room is full on a30 and b30 but holds only 374 KB on c20 and 235 on d20: up to 365 KB of VRAM idle.
- So no new carve from the textures: the level's streams join the room the models already have, and the same ranking (reads a byte) decides who holds it.

## The changes (`psp/src/psp_ge.c`)

- `vm_level_note`: a level stream just written (built, or its origin moved) is registered with its size; any copy it had is released, since its bytes may have changed. Called at both places the streams are written back.
- `vm_level_forget` (in `stream_free`) and `vm_level_move` (arena compaction, and a piece migrating from the heap into the arena): a freed stream's copy goes; a moved one is known again at its new place and copied afresh.
- `struct vm_entry` gains `level` (no geometry-cache generation: level entries are valid until released) and `held`, the copy's own size. `vm_place` now counts the room by `held`: before, a copy in quarantine kept its place while its entry's `bytes` could already name a new buffer, so a new copy could be placed over it (levelvram2 drew a fixed wrong region on c20 and d20 until this; the same latent flaw existed for model buffers at a reused address).
- Unchanged: the frame-end ranking and copying (at most 128 KB a frame), the two-frame quarantine, the draw gate's redirect, the Media Engine's draws left alone.
- `gereads` gains a fit curve per place (level copies, models alone); its report and the draw-kinds report out of line and cold: `psp_ge_frame_end` 21,600 -> 19,492 bytes.

## Checks

| Check | Result |
|---|---|
| The level's reads, shared against `nolevelvram` (levelvram3, 39023725376) [E] | main RAM to VRAM: a30 759 -> 72, c20 669 -> 111, d20 746 -> 246 KB a frame; b30 602 -> 569 (the room contested by models) |
| Frames, same frame number and camera [E] | early frames pixel-identical on every map; later ones part as the faster GE moves streaming |
| Play, `statecrc` (levelvram4, 39024974274) [E] | d20: identical at all 6,000 ticks, both seeds; b30 parts at tick 1,474, its known sensitivity to any change in the emulator's speed (STATE 5c) |
| Strict interpreter, d20 [E] | clean |
| PSP-3000, c20 (39025354526) [M] | frame 62.6 against 67.2 ms in the steady stretch (-7%), every heartbeat; drawing -6.7 to -8.4%; tick unchanged |
| PSP-3000, d20 (39025400180) [M] | frame -13% in the opening, -7.6% (game time 1100-2300), -5.0% after (85 heartbeats) |

## Cost and risk

- Code [D, psp-gcc 15.2.0 -O2]: `vm_redirect` (every draw while vrammodels is on) +20 bytes; the notes and moves in stream building and the arena (warm or cold paths); `psp_ge_frame_end` 2.1 KB smaller.
- The entries are keyed by address and the table holds 1,024; arena compaction gives streams new addresses, so over a long level it fills faster (d20: 299 known after 200 s). When full, new buffers get no copy: safe, the gain fades. Watch `vrammodels: … buffers known`.
- One run a side on the PSP-3000 so far; the PSP-1000's numbers decide.
