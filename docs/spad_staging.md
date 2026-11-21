# Scratchpad staging: which data each part of the frame reads heavily

**Status (2026-11-21):** the count's instrument is built (`PSP_SPADPHASE=1`, below); the table waits on its batch (the lab's `spadstage1`). Step (1) of the ask; step (2) is one PSP bench, after the count names a set worth staging. [enables:spad-stage]

## Why now

The main CPU's scratchpad (16 KB at 0x00010000) takes DMA writes [M, PSP-1000, 2026-11-21; the lab's findings `spad_dmac_into_v2_1000`, `spad_dmacplus_into_v2_1000`, bench-hw 44917686326; REBUILD.md §8b. It supersedes `hardware_facts.md`'s "DMA cannot write it", whose update goes with spadprobe2's filing]: `sceDmacMemcpy` and DMACplus channel 0x1C0 at the 0x0001xxxx and 0x4001xxxx forms, DMACplus at 3-5 µs a KB. The kernel-segment forms hang the PSP and are never used (`psp_dma_address_ok` refuses them). So the scratchpad no longer has to hold one set for the whole frame: it can be refilled between parts. The question changes from "what fits once" to "which data does one part read heavily that another part doesn't".

**The ME is a source too** [M, PSP-3000, 2026-11-21; the lab's findings `spad_dmacplus_me_v21_3000`]: DMACplus's ME-side channels move eDRAM to the scratchpad and back at the user forms, 3-5 µs a KB. So a part's data can be staged by the ME from eDRAM (results the ME made there), not only fetched from main RAM by the main CPU's own DMA starts.

## What has been tried and counted already

From the lab's `levercheck.py` (SCRATCHBSP, PROFHEAP, CSTPOOL, CODESPAD, SPADG, SPSTACK, SPX) and findings:

- **SCRATCHBSP** (in, on by default): the level collision BSP's top 304 nodes and their planes in the scratchpad, ~9.7 KB: b30 tick −0.3 ms on the cinematic and flight [M, PSP-3000 pair]. The scratchpad is 15.1 KB in use today.
- **PROFHEAP** (stopped): the profiler's 6.6 KB out of the scratchpad, giving SCRATCHBSP 208 more nodes: b30 flight tick −0.1% (noise) [M, PSP-1000]. More of the same static set bought nothing.
- **CSTPOOL** (stopped): the 2.6 KB float constant pool from the scratchpad: null on the PSP-1000 [M].
- **CODESPAD** (rejected): code runs from the uncached view at 14.3 cycles an instruction [M].
- **SPADG** (harder than its count): the hot globals are small data read off `$gp` (14.8 KB) and the ME reads them too.
- **SPSTACK** (behind STK): the hot stack needs ~8 KB, not 4.
- **SPX** (sized, not free; M6 dropped it): the scratchpad shared in time, the tick's tables while the drawing's are out: best fits ~0.15 ms a tick [E], and "swapping 15 KB in and out each tick costs about as much [U]". That price was a CPU copy; DMACplus beside the CPU is the new term, and it is what this count re-asks.
- **Counted before, by structure, whole tick** [E, the lab's dcanat1/2/3, tock162 f99b964b, 2026-11-11]: 32,053 tick data misses a tick on b30's cutscene and flight, 13,039 on c20; 9,428 distinct data lines a tick on b30 (603 KB) and 5,590 on c20; the hottest 8 KB of lines hold 19.6% (b30) and 25.4% (c20) of the tick's data misses, 16 KB 29.7% and 35.1%. By the missing function's system on b30: collision 28%, AI 13%, other objects 13%, posing 12%. **bspread1** [E, 2026-11-07]: the collision BSP's arrays are 8.8% of b30's tick data misses (7.8% c20). The object pool is 16% of the tick's data misses [E].

What none of these split: the data **by part of the tick**, with how often each part reads each byte and whether a part's set is known before it starts. They are ten days and several boxes old (tock162 f99b964b against today's tock 49e336a5 with the sweep's placement, bipedstage, posing box), and a different stretch (b30's b139t2 route, not the leg2 pads). So this is a new count, on the current tock, on the pads.

## The instrument: one part's tick at a time (`PSP_SPADPHASE=1`)

The emulator's cache model splits every count by the game's phase word (`psp_current_phase`: 0 other, 1 tick, 2 drawing, 3 frame end), and its per-line files (`dcache.bin`; the anatomy's `.lines`, ANATOMY=1) and `tools/cachemap.py` already name each line's data set (program variables from the ELF, game-state arrays, the level BSP's arrays with `scratchcensus`, tags by group, regions). So instead of a new tracer, a recon build relabels the phase word: with one of the keep words below, the word says 1 only while that part runs, and 0 for the rest of the tick; drawing and the frame end keep theirs. Every existing per-phase count then becomes that part's. No emulator change.

| Keep word | The part | How it's told apart |
|---|---|---|
| `spadcoll` | collision | call scope: the eleven entries of `collisions.c` (`collision_test_*`, `collision_get_features_in_sphere`, `collision_fix_pill`, `collision_move_*`) and everything beneath them |
| `spadpose` | posing | call scope: `object_compute_node_matrices_with` (every pose, the update's and one on demand; the posing box runs inside it) and the step labels `object_postprocess_node_matrices`, `psp_pose_ahead_make` |
| `spadai` | the AI (its main-CPU part) | step labels: the lab's `stepgroups.txt` ai-think and ai-serial (`actor_*`, `prop_*`, `psp_perception_*`, `psp_aime*`, `ai_update`, `encounter_*`) |
| `spadworld` | the world store (today's walk) | step labels: stepgroups.txt's walk (`objects_update`, `object_update`, `psp_sleepy_update`, `psp_object_count`, `psp_nms_skip`, `object_delete_recursive`, `cluster_partition_*`) |
| `spaddraw` | the drawing's main-CPU part | the drawing is the 1, the tick the 0 (so the anatomy's per-line file, which reads the 1 only, covers it) |
| (none) | the whole tick and the drawing | the real phases: the base run |

- **Precedence:** a collision call inside an AI step or a pose counts as collision; a pose inside a step counts as posing. The parts are disjoint.
- **The ME's work:** left out twice: the game's hooks return at once on the ME (`PSP_ME_WORK()`), and the batch's `meside` sends the ME's simulated loads and stores to a cache of their own. On the PSP that work doesn't touch the main CPU's scratchpad, so it isn't counted.
- **Code:** `psp/src/psp_platform.c` (`spad_*`, in `psp_phase` and `psp_step`), `psp_fixups.py` (`spad_scopes`: a cleanup variable as the first line of each scoped function's body, so every return leaves the scope), `psp_build.py` (`PSP_SPADPHASE=1` needs `PSP_RECON=1 PSP_TICK_STEPS=1`). No other build carries any of it.
- **The reading:** `tools/cachemap.py` adds a `spadsets` section at the end of `cachemap.txt` in any build with the symbol `psp_spad_class`: for the tick and drawing phases, every data set by name (lines touched, accesses, misses, size, what its hottest 1/2/4/8 KB hold by accesses and by misses, and from the anatomy file its line-ticks, fills and words used), then the phase's 512 hottest lines by misses and by accesses. Tags are named by group only. The lab's `spadstage.py` reads a batch's runs into the tables below.
- **How "known before the part starts" is judged:** two ways, both labelled. (a) **Steadiness** [E]: the anatomy counts, per line, the runs of the part it was touched in; steadiness = line-runs ÷ (lines × the part's runs). 1 means every line of the set is read in every run of the part: the set is the same each time, so a fetch ahead needs no knowledge of the run. Low means each run reads a different slice. (b) **What the data is** [U, a rule by name, to be read in code before any claim]: a program variable or a fixed array is known; the BSP's arrays are fixed but the nodes a query walks follow its position (the top levels known at map load); object records and tags follow the objects a part visits, listed at the tick's start (their neighbours are found during it).
- **What it can't say:** time. The counts are the cache model's [E]: a lead for choosing a set, never its price (the cache rule). And the parts interleave in today's tick: collision runs inside each biped's update, hundreds of times a tick (the table's "runs a tick" says how many). A staged window pays per fill, so a part that runs as one pass (the rebuild's boxes) is where staging fits; in today's interleaved tick a part's set would have to stay resident across the others.

**The noise it adds:** a recon build with `PSP_TICK_STEPS` (a step before each call in the tick), one pointer-hashed 2 KB table of labels and a few stores to the phase word a step. Play is the same with or without the keep words (fixedseed, fixedticks); the batch checks it by the drawing's totals, which every run of a map counts alike.

## The break-even of a fill [U on M]

An 8 KB fill by DMACplus takes 24-40 µs at 3-5 µs a KB [M]; a data miss costs ~157-196 ns [M, `hardware_facts.md`, halo_fwbench round 2]. So a fill pays only past **~120-250 misses it takes away**, before the bus time it takes from the CPU's own misses (×1.20 at 25% duty, ×1.38 at 50% [M]) and the channel's start (a kernel call). A set the part also writes costs its write-back by DMA too. A part's set is worth the PSP bench only well past that line.

## The table (spadstage1, lab sierra-batch 44924540134, 2026-11-21) [E: the cache model]

**The answer:** in today's interleaved tick, staging a part's data takes away no more misses than a fixed 16 KB does. On b30, the whole tick's best 16 KB of lines, held statically, removes 7,204 misses a tick, against 7,362 for each part's best 8 KB filled once a run of it (+2%). On c20 the static set does better: 10,409 against 7,209. The parts are entered 29-186 times a tick, and most of their sets aren't steady (0.00-0.13: different lines each entry), so a fill per entry can't pay (a fill pays past ~120-250 misses [U on M]) and the sets aren't known ahead. A staged window pays only where a box runs a part as one pass over a known set, made ahead: the sweep records' hand-off is the candidate (the lead 2026-11-21 12:1x).

### b30: 3900 ticks (base anatomy entries)

| part | entries a tick | accesses a tick | misses a tick [E] | share of the tick's misses | scratchpad accesses a tick | best 8 KB: misses a tick | its lines shared with the other parts' best 8 KB |
|---|---|---|---|---|---|---|---|
| the whole tick | 1.0 | 656615 | 21731 | 100.0% | 4422 | 5116 | - |
| the drawing (main CPU) | 1.0 | 423905 | 14771 | - | 1079 | 2773 | coll 19, pose 16, ai 31, world 4 |
| collision | 103.8 | 132150 | 3972 | 18.3% | 2881 | 1614 | drawing 19, pose 17, ai 17, world 3 |
| posing | 114.1 | 148992 | 3694 | 17.0% | 0 | 1640 | drawing 16, coll 17, ai 22, world 6 |
| the AI | 28.9 | 104214 | 3812 | 17.5% | 9 | 1004 | drawing 31, coll 17, pose 22, world 14 |
| the world store | 85.9 | 22088 | 648 | 3.0% | 0 | 331 | drawing 4, coll 3, pose 6, ai 14 |

ceilings, misses a tick [E]: static (the whole tick's best 16 KB of lines, fixed) 7204; staged (each part's best 8 KB, filled once a run of it; the drawing's too) 7362; a fill pays past ~120-250 misses [U on M]

### b30, the whole tick: data sets by misses

| set | KB touched | accesses a tick | reads a byte a tick | misses a tick [E] | hottest 8 KB holds (accesses / misses) | steady | known before the part |
|---|---|---|---|---|---|---|---|
| game state: objects | 828.4 | 48245 | 0.06 | 3093.7 | 20% / 25% | 0.13 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| (thread stacks, unnamed: 9fb0000) | 32.0 | 262305 | 8.00 | 1856.4 | 94% / 92% | 0.43 | n/a: the stack |
| game state: object | 11.4 | 23566 | 2.02 | 887.2 | 94% / 87% | 1.00 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| game state: object spheres | 15.2 | 7227 | 0.46 | 680.4 | 100% / 100% | 0.24 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| game state: actor | 37.6 | 14441 | 0.37 | 629.0 | 64% / 42% | 0.52 | yes: a fixed array (its live entries listed at the tick start) |
| tag window: BSP and free space | 348.4 | 19070 | 0.05 | 616.9 | 76% / 50% | 0.06 | yes: a program variable |

### b30, collision: data sets by misses

| set | KB touched | accesses a tick | reads a byte a tick | misses a tick [E] | hottest 8 KB holds (accesses / misses) | steady | known before the part |
|---|---|---|---|---|---|---|---|
| game state: object spheres | 8.1 | 6931 | 0.83 | 625.5 | 100% / 100% | 0.08 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| tags: coll | 151.6 | 7255 | 0.05 | 476.6 | 67% / 58% | 0.00 | part: the present objects' tags, known at the tick start |
| game state: cluster collideable object reference | 7.3 | 10400 | 1.39 | 407.2 | 100% / 100% | 0.07 | yes: a fixed array (its live entries listed at the tick start) |
| span: level bsp: bsp3d planes | 90.2 | 3564 | 0.04 | 381.4 | 66% / 63% | 0.01 | part: fixed arrays; the nodes a query walks follow its position (top levels: map load) |
| (thread stacks, unnamed: 9fb0000) | 21.7 | 55199 | 2.49 | 373.9 | 92% / 91% | 0.03 | n/a: the stack |
| game state: objects | 154.9 | 3630 | 0.02 | 241.2 | 43% / 58% | 0.00 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |


### c20: 2700 ticks (base anatomy entries)

| part | entries a tick | accesses a tick | misses a tick [E] | share of the tick's misses | scratchpad accesses a tick | best 8 KB: misses a tick | its lines shared with the other parts' best 8 KB |
|---|---|---|---|---|---|---|---|
| the whole tick | 1.0 | 750604 | 25579 | 100.0% | 1927 | 8106 | - |
| the drawing (main CPU) | 1.0 | 344667 | 10521 | - | 1356 | 1811 | coll 13, pose 22, ai 24, world 2 |
| collision | 154.7 | 105797 | 2643 | 10.3% | 1642 | 1310 | drawing 13, pose 16, ai 15, world 7 |
| posing | 186.1 | 210345 | 5229 | 20.4% | 2 | 2822 | drawing 22, coll 16, ai 25, world 4 |
| the AI | 29.2 | 88266 | 3673 | 14.4% | 6 | 1020 | drawing 24, coll 15, pose 25, world 13 |
| the world store | 85.1 | 9461 | 434 | 1.7% | 0 | 246 | drawing 2, coll 7, pose 4, ai 13 |

ceilings, misses a tick [E]: static (the whole tick's best 16 KB of lines, fixed) 10409; staged (each part's best 8 KB, filled once a run of it; the drawing's too) 7209; a fill pays past ~120-250 misses [U on M]

### c20, the whole tick: data sets by misses

| set | KB touched | accesses a tick | reads a byte a tick | misses a tick [E] | hottest 8 KB holds (accesses / misses) | steady | known before the part |
|---|---|---|---|---|---|---|---|
| game state: objects | 868.7 | 67548 | 0.08 | 4677.2 | 15% / 17% | 0.17 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| (thread stacks, unnamed: 9fb0000) | 20.4 | 309661 | 14.84 | 2234.5 | 100% / 99% | 0.34 | n/a: the stack |
| tag window: BSP and free space | 379.2 | 24093 | 0.06 | 1202.8 | 78% / 64% | 0.07 | yes: a program variable |
| (program, unnamed: 8b70000) | 1.8 | 12328 | 6.64 | 766.8 | 100% / 100% | 0.78 | ?: unnamed (heap or caches) |
| game state: object | 7.5 | 28015 | 3.65 | 758.8 | 100% / 100% | 0.95 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| psp_bs3_rec | 14.3 | 7248 | 0.49 | 586.3 | 73% / 77% | 0.78 | yes: a program variable |

### c20, collision: data sets by misses

| set | KB touched | accesses a tick | reads a byte a tick | misses a tick [E] | hottest 8 KB holds (accesses / misses) | steady | known before the part |
|---|---|---|---|---|---|---|---|
| (thread stacks, unnamed: 9fb0000) | 13.1 | 42992 | 3.20 | 315.0 | 99% / 99% | 0.04 | n/a: the stack |
| sweepme_records | 55.6 | 2594 | 0.05 | 284.7 | 54% / 44% | 0.00 | yes: a program variable |
| game state: object spheres | 2.2 | 6852 | 2.97 | 274.5 | 100% / 100% | 0.15 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| game state: objects | 221.3 | 2243 | 0.01 | 177.3 | 42% / 64% | 0.00 | part: the objects a part visits are listed at the tick start; their neighbours are found during it |
| game state: cluster collideable object reference | 1.4 | 10301 | 7.32 | 159.3 | 100% / 100% | 0.19 | yes: a fixed array (its live entries listed at the tick start) |
| psp_bs2_arena | 12.2 | 2818 | 0.23 | 155.5 | 98% / 99% | 0.01 | yes: a program variable |


The full per-part set tables are the lab's findings rows `spad_<part>_set` and the reader's output (`projects/sierra/scripts/spadstage.py`).

## Running it

The lab's batch `spadstage1` (ten runs, one build): `<map>-base`, `-coll`, `-pose`, `-ai`, `-world` for b30_leg2 (exitat130) and c20_leg2 (exitat90), with `cache`, `meside`, `medcache`, `anatomy`, `taggroups`, the keep words `padplay fixedseed fixedticks padend lw2 padhold mexec seed1 scratchcensus` and the part's word. Read: `spadstage.py <results> --rows <tsv>` into the lab's findings (`spad_*` rows) and the tables above.
