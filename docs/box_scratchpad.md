# The scratchpad's box

The 16 KB of SRAM beside the PSP's CPU (`psp/src/psp_scratch.c`'s header): who holds it, and what each piece costs where it sits. Its owner is the lead (sp-plan); its pieces are the builders'.

## 1. The profiler's table out (sp-plan (3), task 0: the form, on paper) [D at 293bc4c5]

_The builder, 2026-11-27 07:47, on `briefs/sp_profout.md` task 0. No code yet: the lead rules the form first._

Ruling (Sheepdog01 2026-11-27 02:1x (3)): "The profiler's 6.6 KB leaves shipped builds, and its room is held for the passes' budgets, not given to more BSP nodes." Lines below are at 293bc4c5 (main; its one commit past the brief's 75f22dca touches only `tools/abreport.py` and `tools/benchreport.py`).

### The finding that sets the form

**In every campaign build the table is taken and never used in play.** `notimers` has been a keep default since a8e15a90 (2026-11-03, TIMERCALLS): `psp_platform.c:1241` (`{ "notimers", "timerson" }` in `keep_defaults`), set at `:1413` (`psp_timers_off = keep_search("notimers") || keep_search("lean")`). With it:

- `psp_step` (`:772-846`) returns at `:781-793`: it stores `last_step` and marks the crumb; it never reads the clock or the table.
- `steps_frame_edge` (`:743-770`) returns at `:756` before its table reach at `:759-766`.
- `objsteps_split` (`:1965-1976`, recon builds) returns at `:1969`.
- The only reader left is the heartbeat thread's `profile_report` (`:2307-2373`) every 10 s: a 6,144 B `memcpy` of the slots into its static copy (`:2322`) and a zeroing pass (`:2323-2327`), then a report of nothing (no slot has microseconds, `:2333`).

The table is reached in play only under `timerson`, and in the multiplayer build (`#ifndef PSP_MULTIPLAYER` at `:1407`: it has no defaults, so its timers are on).

The ~2,800 reaches a second and the 2.5% of b30 beach's drawing misses (`:640-645`, b108l) were counted with the timers on, before 11-03. So was PROFHEAP's +2.6% landing drawing (the archive's pair 41210794029, 2026-10-31). Neither describes today's default build. [archive, as_of 2026-10-31 / b108; the dates from a8e15a90's log]

### (a) What the step costs, a call and a frame

- **Default (`notimers`): no table cost.** A step is the `PSP_ME_WORK()` test, a load of `psp_timers_off`, a store to `last_step` and the crumb test (`:779-792`). It is the same wherever the table sits, or with no table at all.
- **`timerson`:** one frame in four is timed (`STEPS_TIMED_EVERY 4`, `:670`; `stepsall` makes it every frame, `:1422`). A timed step is a `sceKernelGetSystemTimeLow` system call and `profile_slot_for` (`:697-720`): one 8 B front-table load and compare, then on a miss a linear scan of up to `profile_used` 12 B slots. Its count comes from `:666`: "a busy frame passes 2,000 steps or more (b30's beach)". At b30's ~6.5 fps a quarter of the frames timed, that's ~3,250 reaches a second, which agrees with the ~2,800/s at `:641` [D from the code's own counts; no µs figure: none was measured, and an emulator time would be a lead only].

### (b) Who reads the table, and which build is "shipped"

What the lab prices does **not** come from the table:

- `ops/benchread.py` runs `tools/benchparse.py` (the `bench: RESULT` lines) or `tools/benchreport.py`. benchreport's tick, frame and drawing are the **heartbeat line's** (`benchreport.py:111`, `heartbeat: game time … | tick … | frame …`). The heartbeat takes them from `tick_total` and `frame_total` (`psp_platform.c:4385-4388, 4428`), not from the slots. `notimers` keeps them (`:722-726`, "the heartbeat's frame and tick times stay").
- No other reader in `tools/` opens `profile.txt`, `abreport.py` and `frametrace.py` included (a search of `tools/*.py` for `profile.txt` and `ms/frame`).
- Only **`tools/hwreport.py:245`** and **`tools/passtable.py:113`** read `profile.txt`'s step table. Both are measuring tools, and their tables already read 0 without `timerson`.
- **`profile_report` writes more than the table:** after the step lines it calls `object_report` (`:2364`, the "objects_update by type" line that `passtable.py` matches windows by), `recon_counters_report` (`:2366`) and `objsteps_report` (`:2368`). It returns at `:2320` when `profile_slots` is NULL. So a form that doesn't take the table must move that return to guard only the table's part, or those reports are lost with it.

**"Shipped"**: `psp_build.py:199-204` says "a final release is built with `PSP_METRICS=0`". Every lab build (PSP pairs, emulator batches) is `PSP_METRICS=1`, the default. So the ruling's shipped build is the `PSP_METRICS=0` release. Because of the finding above, though, the room can be freed in the build of record too, at no cost to any figure the lab prices. I bring that choice to you as form (D).

### (c) The forms

| Form | What it is | Scratchpad free (default build) | Cost where the table is used | Reports |
|---|---|---|---|---|
| (A) PROFHEAP's route | the table from the heap in every build | +6,656 B everywhere | its reaches miss the D-cache in `timerson` runs; the archive's +2.6% landing drawing was with the timers on (old build); default runs don't reach it | all kept |
| (B) compiled out of `PSP_METRICS=0` | `#if PSP_METRICS` around the take, the step's timed half and the table | +6,656 B in the release only; the lab's builds keep it in the scratchpad, so the passes' budgets measured on lab builds would not see the room | none in lab builds; the release has no profile (none reads it there) | lab builds: all kept |
| (C) a smaller table / only the front table in the scratchpad | e.g. the 512 B front table stays and the 6,144 B slots go to the heap | +6,144 B | every timed step still reaches the slots for its add, so most of (A)'s misses stay in `timerson` runs | all kept |
| **(D) taken only with `timerson`, from the heap** | `psp_profile_tables_take` takes the table only when `!psp_timers_off`, with `memalign` from the heap and never from the scratchpad. `profile_report`'s return at `:2320` guards only the table's lines | **+6,656 B in every default build: the release and the lab's build of record alike** | default runs: none (the table is never reached, `:781`). `timerson` and the multiplayer build: (A)'s misses, in runs whose step timing already costs ~3,250 system calls a second | all kept: the step table under `timerson` as now; under `notimers` it is empty now and stays empty; the object, recon and objsteps lines unchanged |

**I recommend (D).**

- The room is free in the build the passes are measured and priced in, not only in a release the lab never runs. That's what the passes' 16 KB budgets need (sp-plan (1)-(2)).
- It costs nothing that the default build reaches.
- It needs no `PSP_METRICS` split, so the release and the build of record keep one code path.

The keep word for task 1 is its off side: `profscratch` puts the table back in the scratchpad as now, so the off EBOOT byte-equal check is the parent's behaviour under that word. One sub-choice is yours: whether `timerson` runs keep the scratchpad table. I'd say no. Holding the room in every build means a pass's budget doesn't change with an instrument word, and a `timerson` run is a measuring run whose step figures carry their own system calls already.

**SCRATCHBSP's share is unchanged under (D).** `psp_scratch_bsp_take` (`psp_scratch.c:368-393`) runs before the profiler's take (`psp_ge.c:4971-4974`) and leaves `SCRATCH_PROFILER_BYTES` (`:358, :380, :385`) whatever follows. With no take after it, those 6,656 B stay unassigned: the room is held, not given (ruling (3)). The constant then names held room, not the profiler's. Task 1 renames it (`SCRATCH_HELD_BYTES`) with its comment, and the start-up's piece list logs it as held.

**A PSP pair for (D)?** I don't think the form needs one [D]: no reach the default build makes changes, and the scratchpad's other pieces keep their addresses. What does change is code: `psp_profile_tables_take` is placed (`psp/layout/b132.txt:11804-11805`), and it and `profile_report` grow by a few instructions. Task 1 runs `toolbox/placedin.py` on the layout of record before the commit and names any placed block that moves. If one does, a one-EBOOT pair on b30's fight (task 3) prices the drift. If none does, I propose to skip task 3. That call is yours.
