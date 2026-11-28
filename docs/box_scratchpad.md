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

**Ruled 2026-11-27 08:0x (the lead, decisions.md): form (D).** `timerson` runs take the table from the heap too. Task 3's pair is skipped if `placedin` on the layout the default build of record links shows no placed block moving and the start-up's piece list shows every other scratchpad piece at its parent's address. Done-when 2's off EBOOT is amended to one EBOOT: under `profscratch` the piece list is the parent's.

### 1.1 Task 1: form (D) built [D at this branch, on master/tock162 1c7ad138]

_The builder, 2026-11-27 08:12. The branch is `builder/spprofout2`, off the tock (master/tock162 1c7ad138), the tree the build of record links. `builder/spprofout` (18bb7848) sits on main (293bc4c5), which lacks the recipe's text layout `w_tgme2m1_tgc90` and most of the tock's code, so it can't build the recipe of record. This page came over from it unchanged. §1's line numbers are main's; the ones below are this branch's._

**The change.**
- `psp_platform.c:679-716` `psp_profile_tables_take`: with `profscratch` it takes the two tables with `psp_scratch_take`, as the parent does. Otherwise it logs the held room (`psp_scratch_held_log`) and, only when `!psp_timers_off`, takes them from the heap (`psp_heap_take`). `profile_tables_ready` marks that the take has run.
- `psp_platform.c:2438-2451` `profile_report`: it returns only before the take has run (`!profile_tables_ready`, which is when the parent's `!profile_slots` return fired). With no table, only the memcpy and the clearing are skipped. The static `copy` stays all zeros, so `used` is 0 and no step line is written. The window's head line, `object_report`, `recon_counters_report` and `objsteps_report` are written as before. In a `notimers` run the parent's table had no names in it, so `profile.txt` keeps its form.
- `psp_scratch.c:365` `SCRATCH_PROFILER_BYTES` is renamed `SCRATCH_HELD_BYTES` (still 6656u). Its two uses in `psp_scratch_bsp_take` (`:387, :392`) are unchanged in value, so SCRATCHBSP's share is the parent's.
- `psp_scratch.c` adds `psp_scratch_held_log`, the "scratchpad: N bytes held at …" line, and `psp_heap_take`, a heap piece logged with its `scratch span:` line for `tools/cachemap.py`.

**Constraint (i): `psp_timers_off` is set before the take.** Its only write is `psp_platform.c:1504` (`keep_load`), and its definition `:758` starts it at 0. A search of `psp/`, `port/` and the `*.py` fixups finds no other assignment. `keep_load` has one caller, `main` at `:5023`. `main` reaches the game at `:5097` (`return halo_main()`). The take runs from `ge_initialize` (`psp_ge.c:5138`; `:4974` in the brief is main's line), called only from `window_begin_main` (`psp_ge.c:6559`) at the first frame drawn, inside `halo_main`. So the word is set before the take, and nothing clears it after start-up. The heartbeat thread, started at `:5075`, only reads it.

**Constraint (ii): every reach of the table is guarded.** `profile_slots` and `profile_front` are reached at:
- `:720-723` `profile_forget`: under `if (!profile_slots) return`.
- `:733-750` `profile_slot_for`: called only from `:807`, `:845` and `:2087`.
- `:807` `steps_frame_edge`: under `psp_timers_off` (`:804`) and `&& profile_slots`.
- `:845` `psp_step`: under `if (psp_timers_off) return` (`:829`) and `&& profile_slots`.
- `:2087` `objsteps_split`: under `psp_timers_off || … || !profile_slots`.
- `:2443-2449` `profile_report`: under `if (profile_slots)`.

`profile_front` is reached only in `profile_forget` and `profile_slot_for`. It is non-NULL whenever `profile_slots` is (`:713-714`).

**`placedin` on the layout of record** (`psp/layout/w_tgme2m1_tgc90.txt`, `config/recipe.env`'s `PSP_TEXT_LAYOUT`), over `psp_platform.c` and `psp_scratch.c`: 7 placed lines are defined in these files. They are `main` (layout line 27), `psp_tick_account` (30), `psp_object_account` (223), `profile_slot_for` (259), `psp_step` (292), `psp_scratch_census_bsp` (837) and `psp_object_count` (840). **None of them is edited.** The edited functions (`psp_profile_tables_take`, `profile_report` and its caller `heartbeat_thread`, `psp_scratch_bsp_take`) and the two new ones aren't placed, so they link after the placed blocks: **no placed block moves** [D from the layout; the link map of task 2's build confirms it]. `psp_ge.c`'s one edit is a comment (two lines longer, and none of the three files uses `__LINE__`).

**What does move.** One 4-byte static (`profile_tables_ready`) and four new log strings shift the unplaced data and read-only data after them in `psp_platform.o` and `psp_scratch.o`. Unplaced code after the two objects' grown functions shifts too.

**The piece list** (done-when 1) comes from the start-up log of task 2's emulator batch, both sides. Default: the BSP's nodes at 00010000 as in the parent, the cut corners from the heap as in the parent, then "scratchpad: N bytes held at …", and no profiler line. `profscratch`: the parent's lines, the profiler's two pieces at the parent's addresses.

## 2. The frame at one tick a frame, slot by slot (sp-plan (2), paper) [D at cc828579]

_The builder, 2026-11-28 04:2x. The slots are the five pages' "Scratchpad slot" sections (posing, biped motion, collision, draw list, world store) and the tick schedule's table. The tick's passes are tc-tick's parked design, not code. Slot sizes are worked from struct fields, not a measured sizeof. Nothing is built or claimed as a gain._

**What stays resident, default build** [D]:

- **Nothing is taken before the level BSP's nodes.** `ge_initialize` calls `psp_scratch_bsp_take`, then `cut_take`, then `psp_profile_tables_take` (`psp/src/psp_ge.c:5135-5138`). The only earlier askers are the two execution probes (`psp_scratch.c:266,330`, from `psp_vfpu.c:675-676`), which return unless "spadexec" or "spadexec2" is set.
- **Nodes: 9,728 B** (304 x 32): (16,384 − 6,656 − 0), rounded down to whole 32 B slots, with nothing lost (`psp_scratch.c:377,392`).
- **Held: 6,656 B**, untaken (`psp_scratch.c:365,411-423`). The cut corners' 8,448 B don't fit and go to the heap (`psp_scratch.c:56,65`; `psp_ge.c:14357`).
- **Spare: 0 B.** 9,728 + 6,656 = 16,384. The emulator's start-up line agrees [E]: "6656 bytes held at 00012600 … 9728 of 16384 used" (run 46015136001). The "~0.9 KB spare" in QMARK and the brief is an older layout's; it reads like 16 − the "15.1 of 16 KB" of `psp_scratch.c:18` [U], and that layout's own sizes (8,448 + 6,656) left 1,280 B.

**The frame, in order.** One main-CPU pass runs at a time. Each row is nodes + slot + what's left of the held room = 16,384. The ME has no scratchpad: 0 B in every row, its sweep beside P1 to J and its replay beside the drawing included (`psp_scratch.c:3-4`).

| # | Main-CPU instant | Slot's page | Nodes | Slot | Held, free | **In use, of 16,384** |
|---|---|---|---:|---:|---:|---:|
| 1 | tick: H0 to H4 (fence, AI, players, activation, copy and post) | tick; world store (hand-over: none) | 9,728 | 0 | 6,656 | **9,728** |
| 2 | P1 the walk, P2 the units part | tick | 9,728 | 0 | 6,656 | **9,728** |
| 3 | P2 I, the bipeds' intent | biped motion | 9,728 | ~800 | ~5,856 | **~10,528** |
| 4 | posing's pass, parallel phase [U: its place; §12.3's plan names no such pass] | posing, at 64 nodes | 9,728 | 2,292 | 4,364 | **12,020** |
| 5 | J the join | tick | 9,728 | 0 | 6,656 | **9,728** |
| 6 | G's fallback, only for late or not-held bipeds | collision | 9,728 | 5,400 | 1,256 | **15,128** |
| 7 | S1 M, the bipeds' solve | biped motion | 9,728 | ~800 | ~5,856 | **~10,528** |
| 8 | S2 after M | tick | 9,728 | 0 | 6,656 | **9,728** |
| 9 | drawing: the frame-list build | draw list | 9,728 | 4,448 | 2,208 | **14,176** |
| 10 | the rest of the drawing | none | 9,728 | 0 | 6,656 | **9,728** |

**No instant is over 16,384 B.** The most is row 6, 15,128. This holds only while:

- one slot is loaded at a time. Row 6's slot with any other would be over the held room by itself (5,400 + 2,292 > 6,656);
- "profscratch" is off. With it the profiler takes the held 6,656 B and no slot fits;
- the allocator learns to share the held room in time. Today it hands out fixed pieces at start-up (`psp_scratch.c:15-17`).

Queries from inside object updates have no pass and no slot (box_collision.md).

## 3. Each slot's candidates, by misses saved per byte (sp-plan (3)-(4), paper)

_The builder, 2026-11-28 04:3x. Counts are emulator counts [E]: leads, never prices. Nothing is called a gain. Spare is 0 B (§2), so nothing is sized into a spare._

**The one count per byte that is measured on the scratchpad itself** (resident, not a slot): SCRATCHBSP's nodes, ~1,346 tick D-misses a tick on b30 for 9,728 B = **0.14 a tick per byte** [E: the cache pair, run 40770037236].

**Ranking.** "No count": no emulator count of that structure's misses was found on the box pages, in `cache_work.md` or in the lever rows.

| Slot (§2 rows) | Rank, candidate, bytes | Misses saved per byte | Earlier try (below) |
|---|---|---|---|
| collision, G's fallback (6) | 1. `psp_surfaces_seen`, today's walk's dedup marks, 3,072 [D, `psp_fixups.py:17782,17805-17812`] | **≤ 0.085 a tick** [E: ~260 misses a tick, b30 to 170 s, the cache model on w140's build, `cache_work.md` §3; no run id there]. Whole tick, every asker, so a ceiling for one pass | SPX, QMARK |
| | unranked: the box's sphere result, 5,400 (§2's slot) | no count | SPX, QMARK |
| biped motion (3, 7) | unranked: the store sections, 540; the intent-to-solve record, ~260 | no count | SPX |
| | unranked: one biped's gathered list, solve only; average 237 on b30, 163 on c20 [E: run 44217799752], largest 14,200 | no count | SPX |
| posing (4) | unranked: decoded orientations, 832-2,048; the plan, 192; the parent's matrix, 52; §9's decode plan, median 2.8-3.2 KB | no count (`box_posing_pose_ref`'s 1,036-1,289 D-misses a tick [E: `fpass2_own_pair_by_subsystem`] aren't split by structure) | SPX |
| draw list (9) | unranked: RSO's slot tables, 4,096; the faded reflections, 96; the parts' buffers, 256 | no count | SPX |
| tick H0-H4, P1, P2, J, S2; the rest of the drawing (1, 2, 5, 8, 10) | no candidate chosen | no count | |

- `psp_surfaces_seen` doesn't fit beside collision's 5,400 B slot (8,472 > 6,656 held), and it is today's walk's table, not the box design's.
- Other per-structure counts from the same base (`psp_sphere_memo` ~290, `ai_profile` ~260 a tick [E]) belong to no slot here; the sphere memo is off by default (`review_smemooff.md`).

**Earlier tries** (`toolbox/levercheck.py`, the lab, 2026-11-28; "none" = no revive_if on the row):

- **SCRATCHBSP:** "on by default"; PSP-3000 pair 40775509445: exact, small win (b30 tick -0.3 ms on the cinematic and flight [M]). revive_if: none.
- **CSTPOOL:** "closed: null on the PSP-1000". revive_if: "the pool's 2.6 KB table going cold on the PSP; the PSP-1000 pair was in the noise (tick 46.2 vs 46.4 cinematic)".
- **CODESPAD:** "REJECTED [M]" (code run from the scratchpad). revive_if: "the scratchpad's uncached view running near cached speed; it costs 14.3 cycles an instruction against ~4.5 for a missing line".
- **SPX** (tables shared in time): "sized: not free": psp_surfaces_seen 3 KB, psp_sphere_memo 7 KB, ai_profile 3.8 KB, ~3.6M of 200M tick D-misses; "swapping 15 KB in and out each tick costs about as much [U]". revive_if: none. Every slot here is this form, so each owes its copy-in and write-back against what it saves.
- **SPADG** (hot globals): "HARDER THAN ITS COUNT". revive_if: "a count showing most of the small-data area's hot lines are read by the main CPU only, or the ME's jobs given a small-data copy of their own (under S)".
- **SPSTACK** (hot stack): "BEHIND STK'S ROUTE". revive_if: "the fat frames gone (STK) and a hot top that fits 4 KB left; scratchpad room freed (the profiler's 6.6 KB in play builds)". That room is now freed (§1.1); the slots above claim it.
- **QMARK** (query-local object marks, 256 B): "PROPOSED 2026-11-16 ... Unbuilt, unsized on the PSP"; its "spare ~0.9 KB" no longer exists (§2). revive_if: none.

**What would rank the rest:** a count of D-misses by structure inside each pass, over the box's own structures. Today's counts are by function or by whole-tick structure, and the boxes' passes aren't all built.
