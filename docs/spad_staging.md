# Scratchpad staging: which data each part of the frame reads heavily

**Status (2026-11-21):** the count's instrument is built (`PSP_SPADPHASE=1`, below); the table waits on its batch (the lab's `spadstage1`). Step (1) of the ask; step (2) is one PSP bench, after the count names a set worth staging. [enables:spad-stage]

## Why now

The main CPU's scratchpad (16 KB at 0x00010000) takes DMA writes [M, PSP-1000, 2026-11-21; the lab's findings `spad_dmac_into_v2_1000`, `spad_dmacplus_into_v2_1000`, bench-hw 44917686326; REBUILD.md §8b. It supersedes `hardware_facts.md`'s "DMA cannot write it", whose update goes with spadprobe2's filing]: `sceDmacMemcpy` and DMACplus channel 0x1C0 at the 0x0001xxxx and 0x4001xxxx forms, DMACplus at 3-5 µs a KB. The kernel-segment forms hang the PSP and are never used (`psp_dma_address_ok` refuses them). So the scratchpad no longer has to hold one set for the whole frame: it can be refilled between parts. The question changes from "what fits once" to "which data does one part read heavily that another part doesn't".

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

## The table (to fill from spadstage1)

Per map: b30_leg2 to 130 s, c20_leg2 to 90 s; the tock 49e336a5 + this branch, PSP_RECON PSP_TICK_STEPS PSP_SPADPHASE, the tock's profile and layouts; the ME's side apart. Counts a tick [E].

### The parts

| Map | Part | Runs a tick | Accesses a tick | Data misses a tick [E] | Share of the tick's misses | Best 8 KB of lines: misses a tick | Lines shared with other parts' best 8 KB |
|---|---|---|---|---|---|---|---|
| b30 | whole tick | 1 | to fill from spadstage1 | to fill | 100% | to fill | - |
| b30 | collision | to fill | to fill | to fill | to fill | to fill | to fill |
| b30 | posing | to fill | to fill | to fill | to fill | to fill | to fill |
| b30 | the AI (main CPU) | to fill | to fill | to fill | to fill | to fill | to fill |
| b30 | the world store | to fill | to fill | to fill | to fill | to fill | to fill |
| b30 | drawing (main CPU) | 1 | to fill | to fill | - | to fill | to fill |
| c20 | (the same six) | to fill from spadstage1 | | | | | |

The ceilings, misses a tick [E]: static (the whole tick's best 16 KB of lines, fixed) against staged (each part's best 8 KB, filled once a run): to fill from spadstage1.

### Each part's data sets

One table a part a map (`spadstage.py`'s output), the sets by misses:

| Set | KB touched | Accesses a tick | Reads a byte a tick | Data misses a tick [E] | Its hottest 8 KB hold (accesses / misses) | Steady [E] | Known before the part [U] |
|---|---|---|---|---|---|---|---|
| to fill from spadstage1 | | | | | | | |

### The candidates

A candidate is a set (or a set's hottest slice) of ≤8 KB that one part reads many times a byte, steady or known ahead, whose misses a run clear the fill's break-even, and that the other parts' best 8 KB don't share (so a static placement can't hold it beside theirs). To fill from spadstage1; each goes to the lead with its scenario, then one PSP bench (step 2).

## Running it

The lab's batch `spadstage1` (ten runs, one build): `<map>-base`, `-coll`, `-pose`, `-ai`, `-world` for b30_leg2 (exitat130) and c20_leg2 (exitat90), with `cache`, `meside`, `medcache`, `anatomy`, `taggroups`, the keep words `padplay fixedseed fixedticks padend lw2 padhold mexec seed1 scratchcensus` and the part's word. Read: `spadstage.py <results> --rows <tsv>` into the lab's findings (`spad_*` rows) and the tables above.
