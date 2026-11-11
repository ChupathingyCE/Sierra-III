# halo_box: the box harness

Prices one box of the rebuild alone, before it is cut into the engine: the box's own code over a **capture** (the box's inputs and the old code's outputs for sampled ticks of a pad bench), in three modes, alone and beside the GE, with every output checked against the capture's. What it can't give is the in-frame price: alone, a box has both caches and the bus to itself. The modes bracket the frame from both sides; the pair of builds on a pad bench still decides.

State: version 1. Proven on a desktop (`host/test.sh`) and in the emulator [E]: the null box; the self-test box, whose capture it writes, reads back and checks (1,280 outputs, 0 outside); the spoiled capture caught and named; the strict interpreter, no bad access; and the GE's lists built, stopped and waited for without a hang (the emulator's GE takes no time, so every pass there is counted "GE short", as it should be). The counters' module loads and its system call answers in the emulator, where the registers don't count (the bench notices and leaves the pass out); **the counted pass has only run with made-up counters, on a desktop.** **Not yet run on a PSP: no figure of time exists yet.** Main-CPU boxes only.

## Running it

One program holds every adapter in `adapters/` and every box unit of the port (`psp/src/box_*.c`); a run names its box with a keep word.

| Keep word | What it does |
|---|---|
| `box<name>` | the box to run (`boxnull`, `boxselftest`, ...); the null box if none |
| `ge<N>` | also run every mode with the GE reading main RAM N% of the time (`ge35`); up to three |
| `ticks<N>` | stop after N ticks |
| `counters` | one more pass in each mode, alone, with the CPU's own counters read either side of each entity: cycles, cycles stalled on memory and in the pipeline, instruction and data misses, write-backs, cached loads and stores, FPU and VFPU instructions, net of the null box's. The emptied mode's misses are the entity's **footprint in lines**. Loads the bench's kernel module (`kernel/main.c`: one system call, nothing else), which the EBOOT carries and writes to `ms0:/halo/halo_boxk.prx` |
| `spoil` | `boxselftest` only: one output of its capture is written wrong, and the run must name it |

Captures are every `ms0:/halo/capture/*.cap` (in either case: the emulator's stick gives short names back in capitals) whose header names the box, in name order. The report is `ms0:/halo/log/box_<name>.txt`; each step is named on the stick before it starts, so after a hang the last line says where.

- **The null box** (an entity that does nothing) needs no capture: 256 ticks of 91 entities. Its figure in each mode is the harness's own cost there. It runs beside every other box, and is printed and subtracted.
- **The self-test box** writes its own capture (numbers only) and reads it back: the proof of the reader, the comparison and the stick's file calls. With `spoil`, the run must end "not clean" and name tick 210, entity 1013, element 3.

## The adapter

A box is harnessed through one `struct box_adapter` (`box.h`) in `adapters/<box>.c`, listed in `adapters.c`. Its owner writes it; it is the only code that knows both the box's own data and the capture's records.

| Function | When | Timed |
|---|---|---|
| `setup(statics, count)` | once a capture file: build the box's own tables from the static records; returns the bytes the box owns | reported as set-up time |
| `tick(tick)` | before every pass over a tick: put the tick's inputs where the box reads them (and undo whatever the last pass changed) | no |
| `run(i)` | the box's work for entity `i`; `run(BOX_ALL)` once a tick for a box that sets `batch` | **yes** |
| `output(i, &bytes, &length)` | where entity `i`'s output is | no |

The adapter also gives the kinds' names, the tolerance (`BOX_EXACT`, `BOX_ABSOLUTE`, `BOX_RELATIVE` over 32-bit floats, or a `compare` of its own where the terms differ, as a matrix's rotation and translation do) and the box page's budgets.

A capture's inputs are 4-byte aligned in memory and no more: a box that wants 16-byte alignment (the VFPU's loads) copies them in `tick`.

## The modes

| Mode | Between entities | What it stands for |
|---|---|---|
| warm | nothing: the tick three times, the first discarded, the second timed whole, the third with a clock read either side of each entity | the floor |
| emptied | both caches emptied | every line the entity touches fetched once |
| crowded | 32 KB of other data written and 32 KB of other code run, so every set of both caches holds someone else's lines, the data lines dirty | play's worst case: a miss's victim has to be written back first |

Beside the GE: the same three with a display list running that keeps the GE busy and reading a main-RAM texture N% of the time (blocks of two sprites, one textured from main RAM and one from VRAM, sized from the two rates the bench measures first). Nothing wakes the CPU while the list runs. The duty set is the list's own, with the bus to itself. Under the CPU's misses the GE's main-RAM slices run longer, so the report prints **the duty met** beside it: each block carries its number in a state word no sprite reads, a stop asks how far the list got, and whatever of the span the VRAM sprites (which don't stretch) don't account for was spent on main RAM.

## The report

Where the code sat (the box's `run`, the null box's and the harness's loop, with their instruction-cache sets: one program holds every box, so adding one moves the others; the build's log lists each unit's size). µs an entity for each mode and duty: the mean net of the null box, the raw mean and the harness's own beside it, the median, the 95th percentile and the worst; ms a tick at the capture's loads, against the box's budget; by kind (warm). The check: outputs compared, how many outside the tolerance, the largest differences, the first offender. A run whose check fails says so in its last line: its times mean nothing.

The clock is `sceKernelGetSystemTimeLow`: 1 µs steps, 0.57 µs a read [M]. A single entity's time is therefore coarse; the means over a capture's thousands are not.

## The capture: CAP1

`psp/src/psp_cap1.h` is the format; `selftest_capture.c` writes one in C and `tools/cap1.py` reads and writes them on a desktop (`cap1.py info FILE`). A capture of the game is game data: it never comes into this repository.

## Not in version 1

- **Boxes that run on the Media Engine.**
- **The emulator's exact misses an entity** (the cache model beside the same EBOOT).

## The desktop test

`bash bench/halo_box/host/test.sh` builds the portable part natively (address and undefined-behaviour checks on) and runs: a good capture (passes), a spoiled one (caught and named), the capture the bench writes for itself and its spoiled twin, the null box, a cut file (refused). Its times are the desktop's and mean nothing.
