# Research brief for the master session: what the blocked-list research changes for the ME work

Oct 13, 2026. For the session that takes on **the Media Engine as a second core** after build 119 (`handoff_2026-10-13b.md` §2.2). Written after reading `handoff_2026-10-13b.md`, `big_levers.md` and `media_engine.md`, so it only claims what's new against them. The detail is in `research_blocked_list_index.md` and the per-source files it links.

**Labels:** [new] not in the project docs; [confirms] an independent source for something already decided; [covered] already done or measured, nothing to do; [U] inference, needs a test.

**Effort:** reading this and folding it in is a light task (sonnet-level for the `levers.tsv` rows). The ME design changes in §1 are judgment work for the main session.

## 0. The short version

- The research **confirms the ME plan's shape** [confirms]: few, coarse, asynchronous jobs; double-buffered records; no system calls in job code; one writer per shared word. Insomniac (SPU Wrangling, GDC08, AsyncCharacterX) and snes9xTYL all landed on the same rules. Nothing in it argues for a different first target.
- **Five things are new for the ME work** (§1): an interrupt path between the CPUs, a check on our whole-cache loops, an ownership rule that could drop the main CPU's whole-cache invalidate after each hand-over, a job ring instead of one slot, and the single-writer queue's exact rules.
- **One emphasis correction** [confirms, from our own data]: two thirds of the main CPU's misses are **instruction** fetches (59 KB hot tick code, 16 KB I-cache). Moving the GE layer's 188 KB of code (mewalk) onto the ME also takes it out of the main CPU's I-cache. That's an argument for L1 beyond its cycles: `gedefer`'s reorder alone was neutral on the PSP, but on the ME that code runs through a *different* I-cache entirely [U].

## 1. What's new for the ME design

### 1.1 A hardware interrupt between the CPUs [new; rig test]
- **YAPSPD §8.2:** writing 1 to **`0xBC100044`** posts an interrupt to the *other* CPU (`syncn; sw 1 → 0xBC100044; syncn`). Main-CPU side is wrapped by `sceSysregInterruptToOther`; the main core receives the ME's as **interrupt 31**. There's also a shared **hardware lock register at `0xBC100048`**.
- **Allegrex `halt`** (opcode `0x70000000`): "halt execution until next interrupt."
- **What it could change:**
  - **Collecting results without polling:** the ME raises interrupt 31 when a job finishes; the main CPU's handler signals a semaphore. The game thread can then do other work (L3/L5) and block only when it truly needs the answer, without the 50/100 µs sleep granularity that cost b87's round trips.
  - **ME idle:** today the ME polls an uncached word every ~25 µs with 2,000 `nop`s between (`media_engine.md`), so its idle bus cost is already small. `halt` + interrupt would cut wake latency to near zero and the idle bus reads to none. A modest gain; mainly latency.
- **Correction to the research docs:** B1/B4 suggested the ME's spinning explains the 10–20% texture slowdown under ME audio. Given our 25 µs idle poll, that's unlikely to be the main cause; the ME's *job* traffic (reading inputs from main RAM) is the better suspect [U].
- **Update from the code review (`code_review_b119_me.md` §3):** our own library, me-core, already wraps the main→ME direction: `meLibSendExternalSoftInterrupt()` writes `0xBC100044`, `meLibHalt()` is the `halt`, and `meLibExceptionHandlerInit()` installs the ME's handler (`meLibOnExternalInterrupt`). me-core's `samples/exceptions` demonstrates it. Daedalus and snes9xTYL don't use it, and the port doesn't yet. The ME→main direction (interrupt 31) isn't wrapped.
- **Rig test (small, filler):** ME `halt`s; main CPU posts the interrupt; measure wake latency with the ME's cycle counter, and confirm interrupt 31 reaches a main-CPU handler. Plus a sleep/resume pass, since the kernel part already replaced the ME sysevent handler.

### 1.2 Check our whole-cache loops cover both ways [resolved by the code review: fine]

me-core's `meLibDcacheWritebackInvalidateAll()` uses the same two-ops-per-index loop as Daedalus (`code_review_b119_me.md` §6). No action. Original note kept below for the reasoning.

- Daedalus's ME-side write-back-all issues **cache op `0x14` twice per index over 0–8 KB** (`research_b4_me_cache.md`). The reading [U]: the 16 KB D-cache is **2-way, 8 KB per way**, and an index op touches one way per call, so it takes two.
- **Check `psp_me.c` / me-core's whole-cache write-back and invalidate on the ME side.** A loop over 16 KB once, or 8 KB once, may cover only one way per set on some passes. That would leave stale lines that show up as rare wrong answers, the kind b87's 0-differing run may not have hit.
- Cache op codes as used in the field: `0x1b` hit write-back+invalidate (by address; safe), `0x19` hit invalidate (discards dirty data: only for whole, owned lines), `0x14` index write-back+invalidate, `0x11`/`0x13` index store tag (hard reset of the cache).

### 1.3 Ownership instead of the main CPU's whole-cache drop [new; design]
- L1 step 4's plan: after a hand-over, "before touching the GE layer's state again [the main CPU] writes back and drops its own cache." Dropping the whole data cache throws away the game thread's working set every hand-over.
- The research's rule (Acton's queue, Insomniac's job rules, YAPSPD's uncached-alias rules): **each line has exactly one writer**, and a reader either reads it **uncached** or **invalidates only those lines**.
- Apply: list what the main CPU reads back from the ME's side (the done number and the stall address are already uncached). If the main CPU never *reads* GE-layer state the ME writes, it needs no invalidate at all, only the write-back of its own records. Where it must read something back, invalidate just that range.
- **YAPSPD's rule of thumb for write-backs:** over ~16 KB, write back all rather than a range (the D-cache is 16 KB). So the **main CPU's write-back of a 32–64 KB record buffer as "all" is right**; it's the **invalidate** that's worth narrowing.

### 1.4 A ring of jobs, not one slot [confirms → design]
- Insomniac's job manager: a ring per core, the producer bumps `end`, the consumer bumps `start`, coarse jobs, three priorities. Our slots hold one request at a time per client (audio, render), which is why b112's audio waited up to 38 ms behind render jobs.
- A small ring per slot (power-of-two entries), with audio served between render runs (b113 candidates' "audio mixed in between runs"), gives the "hand it a frame's worth early, collect late" pattern `media_engine.md` concluded on.

### 1.5 The single-writer queue's rules, for the ring [confirms; details]
From Acton's `SrSwFixedLookasideDequeue` (`research_b1_insomniac.md`, second batch), adapted to no coherency:
- Two counters, each written by one side only; they only increase; size is a power of two; count = `pushed - popped` (unsigned wrap is fine).
- Producer: write the entry → write back its lines (64-byte aligned) → `sync` → store `pushed` **uncached**. Consumer: read `pushed` uncached → invalidate the entry's lines → read it → store `popped` uncached. Read the count **once** per pass.
- **Each counter on its own 64-byte line**, touched only through the uncached alias.

## 2. Supporting patterns for the ME's jobs [confirms]

- **Pick leaf work with all inputs known at start** (AsyncCharacterX). Matches mewalk's "0 flagged" gate and L3's split of the steady path from the rare paths (placement, seats, dialogue, erasure as main-CPU requests).
- **Group, define inputs and outputs, queue outside calls, then move** (GDC08's five steps; proposal C, already `RC`/levers row). `gedefer` *is* steps 1–3 for the GE layer. For L3 (AI), the same steps on the main CPU first.
- **Sort work by type and state before running it** (SPU Gameplay: one code fragment per AI state). For L3, batching actors by AI state cuts branching and keeps one state's code in the ME's I-cache [new, cheap, also helps on the main CPU].
- **Debug kit** (SPU Wrangling): b113's timeout and progress word already cover the watchdog. Worth adding: transfer wrappers that assert 64-byte alignment and size (compiled out in release), and a stack sentinel checked at job end.
- **Data the ME alone reads goes in eDRAM** (`big_levers.md` §0) [covered]; matches the SPU local-store model.
- **Open question, no source answered it:** can the ME drive the DMAC into its eDRAM? If yes, the ME could double-buffer inputs (process batch N while N+1 arrives) the way SPU jobs did. If not, the ME copies through its own cache.

## 3. Rig tests from the research (all small; good filler)

| # | Test | Decides | Priority for ME work |
|---|---|---|---|
| 1 | ME `halt` + `0xBC100044` interrupt; wake latency; interrupt 31 on the main core | §1.1 | **High** |
| 2 | D-cache associativity (§1.2 is resolved in code; this now only sets R1's cachegrind associativity) | R1's settings | Low |
| 3 | Stall on miss vs. stall on use: profiler `stall (memory)` ÷ (D miss + uncached loads), missing loads used at once vs. hoisted | Whether load-scheduling levers (J, L, preloading) pay on the main CPU | Medium |
| 4 | VFPU `sv.q` write-through for GE-bound data | Proposal R | Low: L1 moves list building to the ME, which has no VFPU |

Profiler counters for all of these are listed in `research_b8_yapspd.md` (incl. cached loads, VFPU stalls and instruction counts, beyond what check 2 used).

## 4. Proposals A–U against the existing levers

Assumption to check: the handoff's `RA`–`RF` are the research's A–F in order (A drones = RA, deferred with B1).

| Research | What | State | Suggested action |
|---|---|---|---|
| A | Drones (actor LOD) | `RA`, deferred | none |
| B | Live-index roster for datum tables | `RB` [U] | keep proposed; cheap cachemap check of table scans |
| C | Batch-update recipe | `RC`; `gedefer` is it for the GE layer | apply to L3 |
| D | Cook-time bounding spheres for culling without posing | `RD` [U]; relates to #25 (skip unseen objects) | keep proposed |
| E | Lookup tables → arithmetic | `RE` [U] | low |
| F | ME plumbing | `RF` | **extend with §1.1–1.5** |
| G | Single-writer queue rules | new | fold into RF |
| H | Sort actors by AI state | new | add with L3 |
| I | Lightweight cosmetic objects | new, gameplay-visible | defer |
| J, L | Load→compute→store; branch-free selects | new | wait for rig test 3; weigh by off words in one build (PGO note) |
| K | `restrict` on hot leaves | new | low; per function, verified in disassembly |
| M | Branch-free 4-wide VFPU bounds tests | new | main-CPU tick only (sphere tests, N4's cluster re-links) |
| N | `pahole` padding audit of hot structs | new, cheap | one-off check on the PC build |
| O | Copy members/counts to locals before loops | new | fold into code reviews of hot loops |
| P | PC reference float settings | not needed: exactness is judged PSP-emulator vs. PSP, not PC vs. PSP | drop |
| Q | Collision compaction keeps shared edges identical | caution for X9 | attach to X9 |
| R | VFPU write-through stores | new | low (see test 4) |
| S | Big-polygon clipping recipe | near-plane cuts already (skincut, NC, NR) | compare dQuakePlus's wide-frustum reject against our cut path when it next comes up |
| T | FPU control setup | new, small | one log line at start: FCR31 |
| U | `-march=allegrex` | [covered]: the build already uses it | none |

## 5. The code review's findings (added after review)

`code_review_b119_me.md`: the main CPU's spin in `psp_me_finish` reads uncached words and the clock every ~64 cycles (bus traffic during ME jobs; space the reads out); the ME's idle loop does ~10 uncached accesses a pass; the main CPU drops its whole cache after overlapped jobs; audio served mid-GE-job flushes the ME's whole cache twice per block; upstream breaks 2 fixup anchors (errors.c) and moves the network version 25→28.

## 5b. Round 2 lever: resting weapons and equipment sleep (plan)

Source: `research_halo_activation.md` (opportunity 1). Ground weapons cost **1.5–4.7 ms a tick** on the PSP (hardware_b114). The port's byte-identical sleep covers scenery, parked vehicles, corpses and (off) machines; **items never sleep** (`psp_fixups.py` ~2870, `psp_sleepy_update(..., header->type == _object_type_scenery)`). Exact by construction if the wake list is right.

**Plan: a short read, then a check mode, then `statecrc`. Not an exhaustive audit.**
1. **Short read of `item_update` / weapon and equipment updates** for the obvious wake triggers: pickup or parenting, damage and impulses (explosions, collisions), the surface underneath (`_item_on_structure_bit`; breakable surfaces, `items.c:768-795`), BSP switches (wake all sleepers, as corpses do), scripts moving the object, garbage collection (sleepers must still count, as the corpse path does). Self-changing state (plasma heat cooling, timers) needs no list: it changes the bytes, so such an item never falls asleep.
2. **Build behind keep words, following the existing pattern:** `itemsawake` (off switch) and `itemsleepcheck` (sleeping items still run the real update; any tick where the result would differ is logged with the object and field).
3. **Verify:** the check mode on combat-heavy routes (b30's beach, c40, a Flood level), then `fixedseed statecrc` whole-run comparisons, within one build by off words.
4. **Read further only for rare events the routes don't exercise** (breakable surfaces, mid-level BSP switches, scripted item moves).
5. **Measure on the rig:** per-type item update time with and without, by off words in one build.

**Related, measure before building:** the checksum's own cost (~6% of the tick's data misses in the b112 cache model) is paid by every sleeper; a cheaper change test would speed all sleeping types. Device controls and light fixtures also never sleep; audit them only if their per-type time on b30/d20 exceeds ~0.5 ms a tick.

## 6. Not in this brief

- Legal (R12): dropped by Sheepdog01.
- The GDC 2004 "Programming the PSP" transcript: dropped (the GAME Watch write-up covers it).
