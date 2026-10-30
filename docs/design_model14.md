# The 14-byte model vertex (keep word `model14`, bake flag `--model14`)

Labels: [M] measured on the PSP, [E] emulator, [D] read in code or data, [U] inference. Background: `memory_ledger.md` §1.0 and candidate (0); the VRAM room: `plan_ge_vram.md`.

## What the GE reads today [D psp_ge.c, model_convert]

The pack holds the Xbox's 32-byte `model_vertex_compressed` (float position, three packed vectors, s16 uv, two node indices and a weight). The first draw of a block rewrites it in place:

| Part | Record | Bytes |
|---|---|---|
| rigid | float uv (8), s8 normal x3 + the first node in the pad byte (4), float position (12) | 24 |
| skinned | 8-bit weights by slot (4 or 8; 128 = 1.0), then the rigid record | 28 / 32 |
| see-through | the rigid record (read on the CPU for sorting, cuts and vertex colours) | 24 |

GE types: `GU_TEXTURE_32BITF | GU_NORMAL_8BIT | GU_VERTEX_32BITF` (+ `GU_WEIGHTS(n) | GU_WEIGHT_8BIT`). The weights are 8-bit already, so nothing about skinning's precision changes.

## The 14-byte record

GE type `GU_TEXTURE_16BIT | GU_NORMAL_8BIT | GU_VERTEX_16BIT` (each attribute aligned to its own size, the vertex to 2):

| Offset | Attribute | Encoding | Exact? |
|---|---|---|---|
| 0 | uv, u16 x2 | the source's s16 + 32768; the GE reads q / 32768, so `sceGuTexScale(s * 32768/32767)` and `sceGuTexOffset(-s * 32768/32767)` give s16 / 32767 x the model's scale, as today | the integers are the source's; the GE's float scale and offset round in the last bit [U] |
| 4 | normal, s8 x3 | as today (normal x 127) | yes |
| 7 | the first node x 3 (the GE skips the pad byte) | as today at offset 11 | yes |
| 8 | position, s16 x3 | round(p / k), k the smallest power of two with max abs(p) <= 32767 k over the part; the GE reads q / 32768, so the part's model matrix (rigid) or each bone matrix (skinned) has its 3x3 multiplied by 32768 k | no: rounded to k / 2 (0.03-0.12 mm on a 1-4 wu part) |

Skinned: the weights (4 or 8 bytes) in front, as today: 18 or 22 bytes. Rigid 24 -> 14 (-42%), skinned 28/32 -> 18/22 (-36/-31%).

- **Origin 0, power-of-two step.** No offset: the fold into the matrices is a uniform scale, so the GE's normals (transformed by the same 3x3, then normalised for lighting) keep their direction, and two parts with the same step put a shared vertex on the same lattice point (no seam between them). Parts with different steps can part by at most the coarser k / 2, sub-pixel beyond ~10 cm from the eye [U].
- **What stays float:** first-person parts (the ledger: ~0.6 px at the near plane), see-through parts (their CPU readers below stay as they are), the 68-byte uncompressed blocks (the sky and parts too big for 16-bit source positions).
- **Where k lives:** a byte beside the converted block's slot (the format and the step's exponent; 2 KB for the 2,048 slots); `model_convert` returns the format to its caller, and the draw carries it.

## What reads model records on the CPU [D]

Opaque parts only (see-through stay float):
- `skin_corner`, `batch_sphere`, `near_clusters_make` (skinned spheres and near clusters, made once a plan);
- `model_geometry`'s near-plane cut (positions of cut triangles; the cut pieces it writes stay 24-byte float records drawn without the view matrix);
- `rigid_corner_bits`, `rigid_runs_for` (rigid runs, once a part);
- `psp_ge_draw_model_body`'s near cut (positions, uv and normal into the 24-byte pieces);
- `node_matrix` (the node byte: offset 7 instead of 11).
Each reads through one decoder, `mv_position(record, format, k, out)`; the draws pick the GE type from the format. The Media Engine never converts (it lists the block for the main CPU, `convert_want`), so the want record also carries "first person". A block converted 14-byte and later asked for by the see-through path is expanded into the list copy that path makes anyway (`psp_ge_queue_transparent_body`), and counted.

## The bake flag (`--model14`, second step)

The runtime keep word buys the GE's bytes and the VRAM room; the cache and the pack need the bake: each opaque, non-first-person block written as its 14-byte records (k from the model's own bounding radius, so every part of a model shares one step: no seams), the block's pointers moved by `levelslim.py`'s relocation, the pack header flagging it. Then `model_convert` only marks the slot. The pack's models shrink 32 -> 14 B a vertex (ledger: -1.3 to -2.8 MB a map, -56% of the models' share of the cache's working set). Built after the runtime side is proved.

## Price, against CTNG [U on the measured slopes]

- **VRAM room.** On b30's flight the room holds 499-536 KB of copies, 14-15 KB of them level, so ~485-520 KB of models; at ~0.6 of their size (rigid and skinned mixed) they free ~185-210 KB, which the waiting copies (535-547 KB, mostly level streams) refill. The room's slope on the PSP is 15.9-17 us of tick a frame per KB resident [M, +177 KB]: **~3.0-3.5 ms a frame on the flight and landing**; beach ~0.5 (3.4 us/KB), c20 ~0.6 (models ~240 KB of 566, 7.3 us/KB [E]), a30 ~0.8 (~290 KB, 7.5 us/KB [E]). Unlike a bigger room, no texture room is taken: no untextured frames.
- **Main RAM** (models outside the room, ledger): b30 ~0.55, a30 ~0.9, c20 ~0.2 ms.
- **CTNG** (fill, pack, split, index lists in the room) is 1.0-2.2 ms on b30's flight and landing, 1.6-1.8 c20, 1.0-1.4 a30. The 14-byte vertex is the larger lever on b30 and a30, and they compound: smaller copies fill the room's slivers better, and the room's waiting curve is what both buy.
- **Gates** (1% of the frame): flight 1.60, beach 0.84, c20 0.82, a30 0.63-1.18: b30's flight and landing and a30 pass; c20 is at the gate.
- **CPU:** the conversion reads each part twice (its range, then the records) on its first draw only; the decoders run where the float reads ran.

## How it is proved

1. Play: statecrc identical (`nomouth`) against `model14` off on the same build: model records are the renderer's only [D].
2. Pictures: side-by-sides (framecrc will differ): pixels differing and the largest difference, against a run-to-run control of the plain build; then a PSP pair (`- | model14`) with shots. Default-on is Sheepdog01's call after the side-by-sides.

## State

- **Step 3a built** (keep word `model14`, off by default): opaque rigid parts only, at run time. `model_convert` picks the part's step, writes the 14-byte records in place, and keeps the step's code per slot (`converted_step`); the draw folds 32768 x the step into the part's model matrix, sets the texture offset (`model_tex_scale`) and takes it off at the part's end; `record_point` decodes positions for `rigid_corner_bits`, `rigid_runs_for` and the near cut (whose pieces keep the GE's units, as they are drawn through the same matrix); `node_matrix` reads the node at offset 7. The see-through path refuses a 14-byte block and counts it (`GE: model14:` line, after each frame report), so a non-zero count is a part that stopped drawing. Skinned parts stay 28/32 bytes until step 3b folds the step into each bone matrix (`bone_commands` and `sceGuBoneMatrix`).
- Next: compile (campaign and multiplayer), then the emulator pair (`- | model14`, nomouth): statecrc identical, shots' pixel differences against a plain-twice control; then 3b (skinned), then the bake flag.
- **Step 3a measured [E]** (the emulator, b30/a30/c20 openings, nomouth): a30 and c20 play the same with the word as without (play hash and both seeds at every checkpoint); b30's own plain-twice control parts with shots on, so its pair is being rerun without them. Pictures: 0.008-0.074% of pixels differ over 23-24 shots (largest 255 at a few edge pixels; part of it is shots landing a tick apart), against 0 for the control on a30/c20. No see-through part was refused. The room: 3a converts 38-97 KB (c20 9 parts, a30 42, b30 58), not the ~200 KB priced above, which counted skinned parts too; b30 holds 495 KB of copies instead of 481 and draws 3% more model vertices from VRAM, and c20 leaves 29 KB of room idle with nothing waiting. Rigid alone is a small lever: the price stands or falls with 3b.
- **Side-by-sides (emulator shots, b30/a30/c20 openings, plain vs model14 at the same game times) [E]:** the worst frames (b30 26 s, 44 s, 67 s; a30 14 s) differ at 190-323 of 130,560 pixels, almost all by 4-15 levels of 255 (one step of the GE's 16-bit colour and dither), scattered over model surfaces; up to ~16 edge pixels a frame by more. The 255-level differences in a30 and c20 sit in the debug overlay's game-time text at the bottom (shots taken a frame apart), not in the scene. Plain against plain again: 0-18 pixels. A contact sheet (plain | model14 | difference) goes to Sheepdog01 with the PSP pair.
- **Skinned census (step 3b's removal estimate):** the model14 line also counts the skinned parts still at 28/32 bytes and the KB 10 bytes less a vertex would free; 3b is built only if that is worth it against the 3a figures above.
- **Skinned census measured [E]** (b30/a30/c20 openings to 150 s, nomouth): the skinned parts left at 28/32 bytes are b30 125 parts, 30,610 vertices, 298 KB that 3b would free (3a: 97 KB, so 3.1x); a30 50 parts, 23,993 vertices, 234 KB (3a 80, 2.9x); c20 19 parts, 6,335 vertices, 61 KB (3a 38, 1.6x). Skinned parts hold about three quarters of the convertible bytes on b30 and a30, so the room's price above stands or falls with 3b, as expected. These are loaded parts, not the room's residents: c20's room already holds every buffer, so there 3b buys GE bytes read, not room. b30 plays the same with the word as without on this base (play and both seeds at all 4,500 ticks).
- **3b: go, sequenced after the b30 hardware pair of 3a** (`- | model14`, the flight route): the emulator's cache model has no bus, so 3a's fewer main-RAM bytes (-17% of the GE's main-RAM reads on b30's opening [E]) need one hardware point before 3b's larger share is built on the same assumption. If that pair is flat, 3b is no-go.
