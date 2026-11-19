# halo_box: the box harness

Prices one box of the rebuild alone, before it is cut into the engine: the box's own code over a **capture** (the box's inputs and the old code's outputs for sampled ticks of a pad bench), in three modes, alone and beside the GE, with every output checked against the capture's. What it can't give is the in-frame price: alone, a box has both caches and the bus to itself. The modes bracket the frame from both sides; the pair of builds on a pad bench still decides.

State: version 1, with the Media Engine's places (below). Proven on a desktop (`host/test.sh`) and in the emulator [E]: the null box; the self-test box, whose capture it writes, reads back and checks (1,280 outputs, 0 outside); the spoiled capture caught and named; the strict interpreter, no bad access; and the GE's lists built, stopped and waited for without a hang (the emulator's GE takes no time, so every pass there is counted "GE short", as it should be). The counters' module loads and its system call answers in the emulator, where the registers don't count (the bench notices and leaves the pass out); **the counted pass has only run with made-up counters, on a desktop.** Since run on both PSPs (the lab's findings, `box_*` and `framepic_price_*` rows). Boxes on the Media Engine: below.

## Running it

One program holds every adapter in `adapters/` and every box unit of the port (`psp/src/box_*.c`); a run names its box with a keep word.

| Keep word | What it does |
|---|---|
| `box<name>` | the box to run (`boxnull`, `boxselftest`, ...); the null box if none |
| `ge<N>` | also run every mode with the GE reading main RAM N% of the time (`ge35`); up to three |
| `gefake` | the emulator only: the GE's three measured figures replaced by a PSP's, so the list's building and stopping are exercised where the GE takes no time. Nothing beside that GE is a measurement |
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

Beside the GE: the same three with a display list running that keeps the GE busy and reading a main-RAM texture N% of the time (blocks of two sprites, one textured from main RAM and one from VRAM, about 200 µs a block. A sprite's cost isn't its pixels times a rate (small sprites cost less a pixel than big ones), so each duty's block is tuned by timing it: the whole block, and the block with its main-RAM sprite shrunk to one pixel, whose difference is the time on main RAM; the sprites are resized until that is the duty asked for). The blocks are short on purpose: the first PSP run had 10 ms blocks, and its "50%" passes, each shorter than one sprite, ran beside the main-RAM sprite alone. Nothing wakes the CPU while the list runs. The duty set is the list's own, with the bus to itself. Under the CPU's misses the GE's main-RAM slices run longer, so the report prints **the duty met** beside it: each block carries its number in a state word no sprite reads, a stop asks how far the list got, and whatever of a mode's spans the blocks' time off main RAM (as timed alone: it doesn't stretch) doesn't account for was spent on main RAM.

## The report

Where the code sat (the box's `run`, the null box's and the harness's loop, with their instruction-cache sets: one program holds every box, so adding one moves the others; the build's log lists each unit's size). µs an entity for each mode and duty: the mean net of the null box, the raw mean and the harness's own beside it, the median, the 95th percentile and the worst; ms a tick at the capture's loads, against the box's budget; by kind (warm). The check: outputs compared, how many outside the tolerance, the largest differences, the first offender. A run whose check fails says so in its last line: its times mean nothing.

The clock is `sceKernelGetSystemTimeLow`: 1 µs steps, 0.57 µs a read [M]. A single entity's time is therefore coarse; the means over a capture's thousands are not.

## The capture: CAP1

`psp/src/psp_cap1.h` is the format; `selftest_capture.c` writes one in C and `tools/cap1.py` reads and writes them on a desktop (`cap1.py info FILE`). A capture of the game is game data: it never comes into this repository.

## The emulator twin

The same EBOOT under the emulator's cache models gives exact counts an entity, which a PSP's counters are then checked against: `harness.c` names what it is doing in a word (`box_phase`: 0 the harness, then mode + 1 while the box runs an entity in its per-entity pass, alone), and the models split their counts by it. Per entity and mode: instructions run, cached data accesses, data misses, instruction misses, dirty lines written back. The crowded mode's instruction misses are the entity's footprint in lines of code, and the emptied or crowded mode's data misses its footprint in lines of data (the instruction model isn't emptied by the firmware's invalidate, so the emptied mode's instruction misses are not the PSP's: read the code's footprint from the crowded mode).

The self-test box, as the first worked example [E]: 326 instructions and 102 data accesses an entity; data misses 2.3 warm, 12.1 emptied and crowded; instruction misses 0 warm, 5.0 crowded; in the crowded mode every data miss wrote a dirty line back, as that mode intends. Against the PSP-3000's times for the same box [M]: the 15 misses the emptied mode adds cost 2.74 µs, 61 cycles a miss; the crowded mode's cost 3.93 µs, which is ~100 cycles for each data miss behind a dirty line. Both agree with the port's measured figures for a miss, which is the harness's modes doing what they say.

## The capture's writer

`psp/src/psp_capture.c` (calls in `psp_capture.h`) writes CAP1 files from hooks at the old code's entry and exit for a box: `psp_capture_open(plan)` once; in a tick `psp_capture_tick(tick)` (1 if the tick is sampled), then for each entity `psp_capture_static(key, bytes, length)` wherever a static is met (kept once a file), `psp_capture_begin(id, kind, flags)`, `psp_capture_input(...)` in pieces at the entry, `psp_capture_output(...)` in pieces at the exit, `psp_capture_end()`; `psp_capture_tick_end()`; `psp_capture_close()` at the run's end. Entities may begin inside one another (an attached object posed inside its parent's call), up to eight deep. Files are `ms0:/halo/capture/<box>_NNN.cap`, each complete in itself, closed before a size cap (32 MB unless the plan says otherwise); a tick that outgrows its room is dropped whole and counted. Every call does nothing with no capture open, in a tick that isn't sampled, and on the Media Engine. It is not built into the port by this branch: the capture switch and the hooks are the build's side.

Proven on a desktop (`host/test.sh`: the self-test box's records pushed through the calls with entities nested two deep, a 48 KB file cap making ten files, a tick too large for its room; the harness reads the files back and every output checks) and compiled for the PSP. Not yet run in the port.

## On the Media Engine

A box's per-entity pass can also run on the ME, beside the main CPU's own in the same run, every output checked against the capture word for word as the main CPU's are. The ME's side (`me.c`, `psp/src/psp_mexec_copy.h`, the harness's ME passes) was built and proven on the PSP-3000 for biped motion on `builder/mebench` (7df2027: exact on all three places; the lab's findings `boxmotion_me_ram_b30beach`, `boxmotion_me_edram_b30beach`), and is carried here unchanged, with the per-entity job form added for line (b)'s falsifier (`docs/box_collision.md` §10).

| Keep word | Where and how the box's per-entity pass also runs |
|---|---|
| `me` | on the ME, **ram**: code and data in main RAM, as linked |
| `meedram` | **edram**: the code copied into the ME's eDRAM; the data moved there by the adapter's `me_place`, or left in main RAM when it has none (the sweep has none: its BSPs are megabytes) |
| `mebusy` | **edram, CPU busy**: as edram, with the main CPU's loop of main-RAM reads timed beside it |
| `medma` | with `mebusy`: the ME also moves the copy's bytes by DMACplus after each pass (it failed on the PSP-3000: `me_drives_dmacplus_b30beach`) |
| `mejob` | with `me` or `meedram`: each entity also handed to the ME as **a job of its own**, timed from the main CPU |

Ask for each place on its own keep word (B77: three at once, with the null box's busy loop writing through a null pointer, turned a PSP-3000 off). Each of the ME's steps is written to the log before it starts.

**The passes** (`me`, `meedram`, `mebusy`): the main CPU prepares the tick (`adapter->tick`), writes its D-cache back and hands the ME the whole tick through a block of uncached words. The ME empties or crowds its own caches by the mode before each entity, reads its cycle counter either side of `adapter->run(i)`, and writes its cache back at the end; the main CPU then drops its copies of the outputs and checks them. The ME's own µs an entity at 333 MHz, net of the null box's same pass, against the main CPU's net mean in the same mode (`x CPU`).

**The jobs** (`mejob`): what the game would pay to hand one entity over. Per entity: the ME's caches emptied or crowded by the mode (an order of its own, untimed); the main CPU's D-cache written back (timed apart, reported as `write-back`); then the job handed over and the main CPU spinning on the uncached done word, timed by `sceKernelGetSystemTimeLow` over the hand-over and the wait. Net of the null box's same job (a hand-over that runs nothing), so the figure is the entity's work on the ME as the main CPU sees it, the ME's poll included at its average. While jobs run, the idle ME polls every 16 nops instead of every 2,000, and the main CPU spins rather than sleeps. The ME's own cycles for each job are reported beside. Every job's output is checked (`check of the ME's jobs`). The order counter and the jobs' three arrays sit on cache lines of their own: a line the main CPU wrote back while the ME held it would land over the ME's bytes.

**The ME can't make system calls and has no VFPU.** The main CPU waits on the ME a bounded 5 s; a pass or job that doesn't end is named (box, place, mode, entity) and the ME's passes stop. Before that, the build checks: `tools/mexec.py` scans every function it copies for VFPU instructions, syscall instructions and calls into the import stubs, and counts the calls through a register that leave the copy, in the build's log (`mexec: the ME check over ...`).

**Code in eDRAM:** `tools/mexec.py --entry box_me_pass` writes the copy's table after the link (`-Wl,-q`), from `me/copy.txt`. A line `+name` takes the function and everything it reaches by jal, by address, so the sweep's walks are taken whole although `box_sweep.c` and `box_collbsp.c` share static names; `-name` keeps a function (the allocator, `printf`) out. The copy sits at eDRAM 256 KB in 192 KB of room (`psp_me.h`'s map, the bench alone on the PSP), the data room at 512-1,280 KB. A function reached through a pointer runs on from main RAM; an adapter with `me_aim` re-aims its pointers at the copies.

**The emulator** runs the ME's passes and jobs on the main CPU, timed by the clock: the hand-over's set-up, `me_place` and the checks are proven there, not the copy and not any time. The desktop test does the same (`HALO_BOX_ME=1`). Only the PSP proves the copy and gives a time.

**Not here:** the GE beside a pass on the ME, and a batching box on the ME.

## The desktop test

`bash bench/halo_box/host/test.sh` builds the portable part natively (address and undefined-behaviour checks on) and runs: a good capture (passes), a spoiled one (caught and named), the capture the bench writes for itself and its spoiled twin, the null box, a cut file (refused). Its times are the desktop's and mean nothing.
