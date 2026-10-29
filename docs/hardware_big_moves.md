# The rebuild's big moves, ranked (hardware session)

2026-10-29, for the master and Sheepdog01 (the master's ask 17:20Z: the heavy program, not crumbs). Nothing here is built. Figures are ms a frame on the PSP, on the heavy stretches:
- **b30 flight:** 155-158 ms (PSP-3000, tock w149c);
- **c20 swarm:** ~116 (PSP-1000);
- **d20 fly-through:** 108 (PSP-1000, w140 framesplit);
- **c10 hands off:** 53-91 (PSP-1000).

Labels: [M] measured on a PSP, [E] emulator, [D] code, [U] inference.

## Where the frame is, and what's idle

**Where the frame goes** (the lab's `rank_pipelined.md` §9 [M]). On every heavy stretch the main CPU runs two ticks and then records the frame, one after the other:
- **b30 flight:** ticks 121, record 45 (of 172 on that build);
- **c20 swarm:** 93 + 25;
- **d20 fly-through:** 74 + 24;
- **c10:** the record is a third to a half of the frame.

**What's idle:**
- **The ME** replays the records into the GE's lists and is 41-68% busy (b30 flight 75 ms a frame, idle 79);
- **The GE** is busy 16-80 ms a frame;
- **The VFPU** is 0.3-0.6% of the main CPU's cycles.

**Why the VFPU isn't a lever:**
- The main CPU's cycles are 53-63% memory wait and ~30% issuing [M, hwkinds1]. Its float work is posing (done: VFPUP, VFPUQD) and frustum tests; the collision walks are memory-bound.
- The instruction cache is ~37% of the tick's cycles, and ~86% of that is capacity (tg141).
- So the VFPU can't buy milliseconds. The big moves are the ones that take whole blocks of work, and their code, off the main CPU.

**Why the ME's past moves lost** (NW2 c20 +3.2% [M]; PREDEC dropped [M]; NW1 and BW sized small), so the plan avoids the same pattern:
- **Slower floats on the ME:** a matrix product is ~3x slower on the ME's FPU than on the main CPU's VFPU, so posing work is cheaper where it is.
- **Hand-over costs:** copy-backs, settles and whole-cache collects ate the gains.
- **Readers inside the same update:** most of what a tick computes is read later in the same tick (bipeds' nodes, physics' sweeps), so it can't be handed off without a wait.

**What pays on the ME:** integer- and branch-heavy work, with no reader until much later, reading state that holds still while it runs.

## Ranked

| # | Move | b30 flight | c20 swarm | d20 fly-through | c10 | Label | State |
|---|---|---|---|---|---|---|---|
| 1 | **L15**: the campaign at 15 ticks a second, frames interpolated | **-48 to -53** | **-46** | **-26 to -37** | -13 to -25 | U on M | building (helper l15, step 1) |
| 2 | **AIME**: the AI's thinking for the next tick run on the ME while the main CPU records this frame | ~-8 to -12 (the ME's room caps it; up to ~-20 with room) | ~-8 to -10 | ~-12 to -15 | ~-1 | U on M x E | to prototype after L15 step 1 |
| 3 | **The record on the ME** (option C's next step: the objects pass, 20 ms of b30's 45 ms record) | up to -20, if the ME has room | ~-10 | ~-10 | **the c10 lever**: up to ~-15 | U on M | research's thread (C step 3, held); here only the ME's room under L15 |
| 4 | **Particles and effects' local motion on the ME during the record** (after FX15 halves them) | ~-1.5 | ~-2 | ~-4 to -5 | small | U on M | after AIME |

### 1. L15
The only multi-x lever. It takes one of the two ticks out of every heavy frame. Design: docs/l15_design.md (merged). Step 1 is building: the PSP_L15 switch, the `l15` keep word, the hybrid's physics substeps, two animation advances, a 30 Hz script clock.

**After L15 the pipeline is nearly balanced on b30's flight:** main CPU ~100 ms, GE ~80, ME ~75. Every later gain there needs room on all three, which is why #2 and #3 are priced against the ME's room.

### 2. AIME: AI thinking on the ME, during the record
- **What moves:** each actor's thinking for the next tick (perception's servicing, situation, emotion, the decision loop and the cover search) moves to the ME while the main CPU records this frame. That's ~78% of ai_update [E aitick2], 20-24 ms a frame on b30's flight, about the same under L15: today each actor thinks once a frame across two ticks (aicad3), and under L15 once in one tick.
- **The window:** the tick runs `ai_update` early, before effects, scripts and objects_update (game.c). So the next tick's thinking reads the state the last tick left, which holds still while the main CPU records (the record only reads it).
- **Why it pays where NW2 didn't:** the AI is branchy integer code with its own rays, which run as fast on the ME as on the main CPU. Its output (orders, actions, the actor's own memory) is read only at the next tick's control steps (move, look, fire, unit_control), which stay on the main CPU. And its code (ai/ is among the largest bodies of the tick) leaves the main CPU's instruction cache, which also relieves objects_update [U, as NW's model showed I-misses -16% with work moved].
- **Why it needs L15:** a frame of two 30 Hz ticks has no record between its ticks, so there's no window for the second tick's thinking. Under L15 every tick follows a record.
- **What it costs in behaviour:** the thinking reads the world before the next tick's units_update and player input (a few ms earlier in game terms; Halo's own perception delays are hundreds of ms). It's an AI-timing change, the class Sheepdog01 opened to experiment, proven by the outcome census as aicad3 was. Campaign only.
- **Hazards, all known from mevisland and pose-ahead:**
  - the main CPU writes the world back before the hand-over;
  - the ME writes only actor and prop datums, which the main CPU drops before the next tick;
  - no allocation on the ME;
  - the AI's random numbers from a seed of its own, so the main CPU's draws during the record can't race it;
  - the cover search's 82 KB path state in an arena;
  - rays against a world that holds still.
- **The cap on b30:** under L15 the ME is ~75 ms busy of a ~100 ms frame, and the AI at the ME's ~1.3-1.5x would want 26-36 ms. So b30 gets what fits (~8-12) unless the replay's own load falls (LMC's static lights, #3's split). c20 and d20 have the room (ME 35 and 22 ms a frame).
- **Prototype path:**
  1. On the main CPU alone: the thinking moved to the end of the record, one tick ahead, with its own seed. This proves the behaviour (outcome census) and the readers, with no ME risk.
  2. The ME job, with the hand-over, the drops and a check word.
  3. One PSP pair on c20's swarm, then b30.

### 3. The record on the ME
- **What's left on the main CPU:** the record lane is 24-45 ms a frame on every heavy stretch [M] and a third to a half of c10's frame. mevisland already moved the visibility walk to the ME. The objects pass (b30 20 ms) is option C's step 3, research's thread, held on the bus snapshot's price.
- **What this plan adds:** under L15 the ME's room is what limits both #2 and #3 on b30, so they're priced together. On c10 (drawing-bound, little AI) #3 is the only big move.
- Research owns it; hardware gives it the ME-room figures under L15 from #1's runs.

### 4. Particles and effects on the ME
- **What moves:** particles_update and particle_systems_update move nothing the simulation reads (local, drawn only), and mepoints already put their point physics on the ME [M: exact, safe, small].
- **The rest:** the systems' own motion and ageing during the record. d20's effects and particles are 8-11 ms a frame [M], halved by FX15 first.
- **Effects that spawn, damage or play sounds** stay on the main CPU.

## Two enablers: the DMA controller and the ME's eDRAM (the master's ask 18:45Z, Sheepdog01 13:37)

Sized, not built.

### The DMA controller
What's measured (`docs/big_levers.md` "The DMA controller" [M], halo_bench v6-v7, v10):
- It copies RAM to RAM at 181 MB/s against a CPU memcpy's 76, and into VRAM at 323.
- The call blocks its caller.
- While it runs, a CPU miss takes 499 ns instead of 218, and the DMA drops to 64 MB/s when the CPU is busy.
- It can't write the scratchpad.

So it's worth using for a copy the main CPU would otherwise make itself, in one block, where the 2-2.5x saving lands on the binding lane. A background copy beside the main CPU's work costs it about what the copy would have. Every byte crosses the bus either way, so the GE and the ME see the same traffic; the DMA just takes it in a shorter burst.

| Copy in the plan | Size | CPU memcpy | By DMA | Fit |
|---|---|---|---|---|
| C's field-level snapshot (walkr1, walkr2) | b30 166 KB a drawing in 686 runs, the median run one 64-byte line (walkr2); c10, a10 less | b30 ~2 ms | many single lines: each call's own cost eats the gain | **poor: a gather, not a block** (walkr2 corrected walkr1's reading as one block) |
| L15's interpolation snapshot (drawn objects' nodes, the camera) | ~40 KB a tick on b30 (784 nodes x 52 B), gathered from scattered datums | ~0.5 ms | many small blocks: the call's own cost eats it | poor: a gather, not a block |
| AIME's hand-over | none: the actors' and props' arrays stay where they are; the main CPU writes the world back (one cache write-back, ~12-24 us) and drops the lines the ME wrote | - | - | not needed |
| The record's copies | none: records are written in place in main RAM (the in-flight 224 KB) | - | - | not needed |

**The DMA controller in the plan:** no good fit is left. Every copy the big moves make is a gather of single lines (C's snapshot, L15's interpolation) or isn't needed (AIME, the record). It keeps the bulk moves it already does or could do (texture uploads, the level arena's compactions). halo_bench12's re-measure of its contention (Sheepdog01 13:49) may change what a background copy costs, not what fits.

### The ME's eDRAM
- **The space:** ~1,664 KB claimable (256-1,920 KB; findings `edram.md`). Only the ME and DMA reach it; the main CPU can't load from it.
- **What it buys:** what the ME reads from there never touches the bus, so it neither slows the main CPU's misses nor the GE.
- **The rule:** each claim is Sheepdog01's (`edram.md`); this is the proposed budget for the plan's claims, against today's register:

| Claim | Size | What it buys | Status |
|---|---|---|---|
| CPA: the checkpoint built in eDRAM | up to 1,089 KB (b30's largest) | the save hitch off the main thread (~130 of ~190 ms median) | built, off; not exact yet; in the master's queue |
| AIME: the cover search's path state and the ME's AI scratch | ~82 + ~32 = **~114 KB** | the search's 82 KB working set read and written off the bus, for the largest single piece of the AI's thinking (`perform: uncover` was 75% of the decision loop [E aitick2]) | with AIME step 2 |
| C's snapshot | **~192 KB**, with overflow to main RAM (b30 89 KB a drawing typical, 990 worst) | the ME's draw walk reads the frame's state off the bus | with C step 3 (research) |
| The level pass's positions (L1) | ~190 KB | the ME's near-plane cuts from eDRAM; ~1 MB of heap freed with the level copies in VRAM | plan |
| CSTPOOL: a copy of the float constant pool for the ME's code | 64-80 KB | ~0.5-0.95 ms a frame on b30 (the main CPU's constants in the scratchpad) | waiting on Sheepdog01 |
| **Total** | **~1,640-1,665 KB** | | **fits the 1,664 KB only just**, with the ME's stack held to 128 KB |

The sound cache in eDRAM (R2) takes all of it and was rejected on the PSP (frame +7-14%), so it's out of this budget. If CPA's checkpoint grows past b30's 1,089 KB on a later map, C's overflow takes the difference: C is the claim that can spill to main RAM at the least cost.

## Not on the list, and why
- **BW (biped_update on the ME), NW, PNC, MEPOSE:** the readers sit inside the same update, the ME's FPU is slower than the VFPU, and the copy-backs cost more than the walk. They're sized or lost on the PSP (NW2 c20 +3.2% [M]).
- **MP (next tick's base poses on the ME):** that's pose-ahead, already on.
- **The VFPU's float inventory:** under 1% of the cycles. Its work is done (VFPUP, VFPUQD).

## Order
1. L15 step 1 (building).
2. AIME prototype step 1 (main CPU, behaviour only) beside L15's step 2.
3. AIME on the ME, on L15's build.
4. #3's ME-room figures to research.
5. #4 after FX15.
