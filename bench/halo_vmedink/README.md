# halo_vmedink: Dink's mat4x4 sample and the near-vme kernel's context load

The near-vme kernel (`docs/box_near_vme.md` §9) matched Th6uD1nk's emulator vmee word for word, but on the PSP-1000 its words came out wrong (the lab's nearvme1 read, 2026-11-21). This bench asks whether vmee or our setup is wrong. It runs his own batched mat4x4 sample the way his toolkit ships it, and our kernel's tile once his way and three times the box's way, all in one program on the PSP.

The bench needs a PSP. The emulator can't run the ME's kernel call (PPSSPP faults in kcall at `meLibDefaultInit`; the lab's lessons), so bench-emu or sierra-build is only its first compile.

## The cells

They run in this order, each after his ME loop's wipe. Each writes a `calling` line before it starts.

| Cell | What runs | Passes when |
|---|---|---|
| 1 | his `mat4x4-batch`, vmeh's `vmehRun1`: his tables, kernel, transfers and his result store to `sharedRes` | all 64 results equal M·v worked out on the main CPU, in BASE 3 words 19 + 4j and in `sharedRes` |
| 2a | our one-slot kernel (`near_verdicts_g1`) on one tile, vmeh's `vmehRun2`: his context loads (the ME firmware's preload), his waits | all 480 plane words equal vmee's |
| 2b | the same tile through the box's own steps (`box_near_vme.c`, one slot, builder/nearvme-me 1d5b9434): the hand-written 256-word context load (0x440ff014/18/10/08), the trigger as one store, polled waits with the box's floors and bound, the DMAC's fast mode on | as 2a |
| 2c | 2b with the fast mode off | as 2a |
| 2d | 2c with each context loaded his way (`vmeLibLoadCustomContext`, 112-word images) | as 2a |
| 1 again | cell 1 after our cells | as cell 1 |

How to read it: if cell 1 passes, his toolkit works on this PSP. Then 2a wrong means vmee's model of our kernel is wrong. 2a right with 2b wrong means our setup is wrong, and 2c and 2d narrow it down to the fast mode or the context load. For every cell the log also compares the four base buffers' windows with vmee's. Those comparisons don't decide a pass: intermediate words can differ on the PSP while the results are right (halo_vmekern saw that in this same mat4x4).

The images in 2b to 2d are built by `nearDotsBuild`, the same text as the box's `nearDots1Build` (near_contexts.h; compared when the bench was made), into 256-word arrays that are zero past the context, as `nvme_job_begin` makes them.

## Waits and addresses

The main CPU waits at most 2 s for each cell's answer. If the ME doesn't answer, it says so and starts no more cells. Our ME code bounds its waits in the ME's cycles, as the box does. Dink's generated code waits through the ME firmware's own functions, which have no bound on the ME; only the main CPU's bound covers those, and a hang there can leave the ME stuck until the PSP is power-cycled. Every address given to the VME's DMAC is a 0x4xxxxxxx user form (`useg_mem`), never a kernel-segment one.

His runner's steps are kept as he ships them (Sheepdog01, 2026-11-21 08:09): `scePowerSetClockFrequency(333, 333, 166)`, `meLibDefaultInit`, and his loop's `vmeLibEnable`, `vmeLibWipe`, `vmeLibDisable` before every kernel. The box's own cells use the box's register forms of the two bus-clock enables (psp_vme.c's), as the box did.

## Output

The output goes to `ms0:/halo/log/vmedink.txt`, written a line at a time. Each cell's buffers are written in vmeh's dump format (`Kernel: NNN - name`, then `Buffer: BASE n`). vmeh's own `log.txt` is kept in `ms0:/halo`, not in the log folder, and deleted at the end. When it's done, the bench waits for the run's wall (X exits by hand).

## What's here, and from where

| Path | What | Source and license |
|---|---|---|
| `src/mat4x4.vmeg.cpp` | his `samples/cpp/vmegc-mat4x4-mul-batch`, tables and kernel unchanged; his config and exec played in an `@emulate "mat4x4-batch"` block in place of his display loop, with his result store kept (the mutex around it guarded his display loop, which is gone) | vme2-next-level (MIT, (c) m-c/d, Th6uD1nk) |
| `src/near_tile.vmeg.c` | Sierra-III's `psp/src/vme/near_verdicts_g1.vmeg.c` (builder/nearvme-me 1d5b9434) with `nearVertex` and `nearRow` holding one tile: the lab's check generator (nearvme_check_groups.py), safe32, a realistic one-slot batch of 96 vertices, seed 20261121 | ours; its accumulate form is his sample's (MIT) |
| `gen/vmedink_hw.cpp` | vmeh's program for the two files, compiled by vmegc with `--compat fat`, `#line` markers removed | generated |
| `expected.h` | vmee's answers for the same files (`--compat fat`, window 512): the four base buffers and `sharedRes` for cell 1, the base buffers and the five `nearPlane` arrays for the tile | generated |
| `dink/vme-debug.h`, `dink/LICENSE.md` | the dump of the base buffers that vmeh's runner writes | vme2-next-level `vmeh/vme-debug.h`, unchanged (MIT) |
| `mcidclan/me-core-mapper/` | the ME and VME library, halo_vmekern's copy (0197c99); its sources are the same as vme2-next-level's `me-core-mapper` submodule | mcidclan/psp-media-engine-custom-core (MIT, (c) m-c/d) |
| `embed.cmake`, `CMakeLists.txt` | the build, halo_vmekern's | ours |
| `vmedink.cpp` | the runner: his ME loop with our cells as extra commands, the bounded waits, the checks | ours |

## How the generated files were made

In a copy of vme2-next-level (its `vmeh.py`, `vmegc.py`, `vmeemu.py`, run with `python3 -I` and only the toolkit's folder on `sys.path`, as the lab runs it):

```
python3 vmeh.py mat4x4.vmeg.cpp near_tile.vmeg.c -o hw.vmeg.cpp --buffers-words 512
python3 vmegc.py --compat fat hw.vmeg.cpp -o hw.cpp
grep -v '^#line' hw.cpp > gen/vmedink_hw.cpp
python3 vmeemu.py --run mat4x4-batch mat4x4.vmeg.cpp --compat fat --buffers-words 512
python3 vmeemu.py --run tile near_tile.vmeg.c --compat fat --buffers-words 512
```

The two vmee runs also print the arrays their stores wrote (the lab's runner for vmee does that), and `expected.h` is made from their output. Checks made at the same time:
- vmee's mat4x4 results equal integer M·v, 64 of 64, in BASE 3 and in `sharedRes`;
- vmee's tile planes equal the lab's C reference (nearvme_ref_stream.c), 480 of 480.

None of Dink's Python runs on the lab's runners: they build this folder with pspdev, as any bench.
