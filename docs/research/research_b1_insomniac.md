# B1 findings: Insomniac R&D archive (2007–2010)

Source: the archived Insomniac R&D index (Wayback snapshot, Dec 2010) plus 14 talks Sheepdog01 fetched on Oct 13, 2026. Everything here maps to the **Halo CE PSP: performance review checklist** (check numbers 1–53, backlog R1–R12). This file is the single place for B1 results; the dashboard session can fold it in.

**Context for the reader:** Insomniac wrote these for the PS3's Cell. Its SPUs (worker cores) had 256 KB of private memory fed by DMA (direct memory access). That is a harder version of the PSP's problem: a CPU with a 16 KB cache and no L2, plus a Media Engine (ME) with no coherency. The lessons transfer; the SPU instruction tricks mostly don't.

**Status:** B1 done (14 talks plus 2 index posts). Next: B2.

## Talks read

| Talk | Author, year | Value for this port |
|---|---|---|
| A Gameplay Architecture for Performance | Terrance Cohen, 2007 | High: AI "drones" (actor LOD) |
| Insomniac's SPU Best Practices (GDC08) | Acton & Christensen, 2008 | High: batch-update recipe, sync design |
| AsyncCharacterX | Daniel Gonzalez, 2008 | High: how to carve ME jobs out of gameplay code |
| igCollision | Jonathan Garrett, 2007 | High: compact collision formats, deferred queries |
| A Dynamic Component Architecture for High Performance Gameplay | Terrance Cohen, 2010 | Medium: dense per-type pools |
| igMobyBspheres | Jonathan Garrett, 2008 | Medium: cook-time bounds for culling without posing |
| SPU Wrangling (GDC09) | Jonathan Garrett, 2009 | Medium: job-manager and debug infrastructure for the ME |
| Optimization 101 | Mike Acton, 2007 | Medium: lookup table vs arithmetic, exhaustive testing |
| Introduction to B-Trees | Dave Schuyler | Low: confirms node-sized-to-block layout (check 16) |
| Loading Code And You (tier system) | Giacomino Veltri, 2007 | Low: load-state structure only |

Plus two short posts readable in the index itself: **Beware of Statics** and **Remez Exchange Algorithm**.

## Findings by checklist item

### Check 18, 20, 46 — AI cost: "drones" (actor LOD)

*Resistance: Fall of Man* had AI and movement over budget with a **flat profile** (no single hot spot). Their fix was a cheaper actor mode, not a faster AI:

- **Drone mode:** an NPC (non-player character) that can only pick a target, shoot, take cover in place, die, or be promoted. **No AI update and no movement update.** Drones cannot relocate.
- **Cost:** about **10%** of a full actor.
- **Who becomes a drone:** "affinity" from distance, set by two distance thresholds, plus a target percentage of the population. Fill from high-affinity (distant) actors first; never demote no-affinity (close) ones.
- **Promotion back to full AI:** damaged by the player, **zoomed in on** (scope), or a timed random interval.
- **Result:** "barely noticeable" in play; AI and movement fit the budget; a few scenes still needed trimming.

**For Halo:** a direct candidate for check 46, and the first answer if R3 shows AI-heavy worst frames. Promotion on scope zoom fits Halo's pistol and sniper rifle. **Caveat:** Halo's AI moves far more than Resistance's (Grunts flee, Elites dodge, flanking), so a drone that can't relocate is more visible. It also changes gameplay, so under Sheepdog01's rule it gets **tested and evaluated case by case** before committing. Suggested first test: drones only beyond the distance where movement is hard to read, on a scripted heavy encounter.

### Check 10, 12, 45 — the batch-update recipe ("where there's one, there's more than one")

GDC08 walks through rescuing a slow per-object class (`FastPathFollowers`, a virtual `Update()` per object, problems past a few hundred instances). Five steps, each one shippable on its own:

1. **Group:** pull every instance of the type out of the generic update list into its own array; call one `UpdateAll()`.
2. **Define inputs and outputs:** list exactly what's read (position, time, state, query results, paths) and written (position, state, queries, animation).
3. **Cut sync points:** collect calls to other systems into a **command queue** (query and query-result separated). Many waits per object become one per system.
4. **Move to another thread:** now possible, because the update has no outside dependencies.
5. **Move to the co-processor** (for us, the ME).

Pattern before: per object, read → update → call out → block. After: loop all objects writing outputs and queued calls → one sync → run the queue.

**For Halo:** steps 1–3 pay off **on the main CPU alone** (fewer scattered lines touched, check 10/12), and they are the prerequisite for any ME job (check 45). Recommended order: do 1–3 for one subsystem, measure counters, *then* decide on the ME. This is lower risk than designing ME jobs first.

### Check 45 — carving ME jobs out of gameplay code (AsyncCharacterX)

- A job ("code fragment") is a **function over a packed array**: only the fields it needs, copied into fixed-size records (they used 32 bytes). **Packing is copying, not compressing:** no clever prep on the main CPU.
- **Results arrive a frame late.** Insomniac's usual answer: use last frame's result. They called this acceptable for aim prediction and movement physics.
- **Pick leaves, not systems.** The author lost time chasing call chains ("that function calls three others, including a virtual call…"). The rule they arrived at: small, well-defined transforms with **all inputs known at start**, no calls out. Examples: idle-motion phase update, homing-projectile steering, path-node advance.
- A per-job "common block" holds tuning constants (like a shader constant table).

**For Halo:** good first ME candidates are leaf transforms in posing (blend math per node batch) and projectile/movement integration. Anything "a frame late" is a gameplay-visible change → **case-by-case under Sheepdog01's rule.**

### Check 45 — ME job infrastructure (SPU Wrangling, GDC08)

- **Job manager is tiny** (under 2 KB): one **ring buffer per core**, jobs run in submission order, 3 priority levels, **coarse jobs** (a whole system per job, sub-batching inside the system).
- **Single-reader, single-writer queue** is the default sync: writer writes data → barrier → bumps the end index; reader bumps the start index. No locks.
- **Don't spin-poll shared memory.** Insomniac used a hardware reservation-lost wait so the waiting core sleeps instead of hammering the bus. **PSP translation:** an ME polling main RAM steals bus cycles from the CPU and GE (graphics engine). This matches the measured 10–20% texture-fetch slowdown from ME audio. Poll from ME-local eDRAM, or poll rarely, or use an interrupt.
- **No coherency translation:** the PSP needs "write data → `sceKernelDcacheWritebackRange` (64-byte aligned) → write the index through the uncached alias" so the ME never sees the index before the data.
- **Never run a general allocator inside a job.** Give each job a fixed buffer split into input, output and scratch; fixed number of transfer slots; fixed maximum code size; fixed parameter list per system; as many instances per call as possible.
- **Debug kit worth copying:** transfer wrappers that assert alignment and size (compiled out in release; catches the NJEMU 64-byte stale-data bug at the source); a sentinel at the stack end checked on job exit; a main-CPU **watchdog** that fires if a job hasn't finished by end of frame; a small **debug stash** (128 bytes) the job writes progress into, dumped on timeout.
- **Design back to front:** start from the data the final consumer needs and work backward. Their glass-shatter system was rewritten because it was designed front to back.
- **Store canonical data in the format the most expensive consumer wants.**

### Check 16, 17, 18, 21 — collision (igCollision)

- **All queries asynchronous:** issue → co-processor services it → caller polls or syncs. **Deferred priorities:** standard (sync by end of frame) and **low (sync by end of next frame)**. Insomniac's own improvement list: "wanted more deferred queries."
- **Broad phase:** a 4×4×4 hierarchical voxel grid, depth 3–4, **one bit per child** (very compressible, few branches). An AABB (axis-aligned bounding box) test is 30 instructions with no branches. Dynamic objects (up to 1024) use bounding spheres and implicit grids: a bitmask per row in X, a bitmask per row in Z, AND them for overlap.
- **Compact triangle-mesh packets** (cook-time format):
  - Vertices in a **1.5.10 fixed-point format: 6 bytes** per vertex. Max 512 vertices and max 64 m span per packet.
  - Triangle indices plus flags in **9.9.9.5 bits: 4 bytes** per triangle.
- Broad and narrow phase overlapped: narrow starts as soon as broad emits its first candidate.

**For Halo:**
- **Check 18 (AI line-of-sight):** the low-priority "next frame" query is the mechanism for time-slicing perception. Gameplay-visible → case by case.
- **Check 17:** the 6-byte vertex / 4-byte triangle packet is a concrete target format for collision BSP leaves. Scale caveat: Halo world units ≈ 3.05 m (10 ft); cook would choose a packet span and fixed-point scale per packet.
- **Check 16:** dense bit-per-child grids are an alternative to deep binary BSP walks for the broad phase.

### Check 10, 11, 22 — dense per-type pools (Dynamic Component System)

- **One pool per concrete type**, all instances the same size. A **roster** (array of pool indices) with a **partition**: entries before it are live, after it free.
- **Free:** swap the entry with the last live one, decrement the partition, bump the generation. Instance data is never touched.
- **Handle = index + generation**, constant-time resolve.
- **Update per type, walking only the live prefix.** The live prefix doubles as a transfer list for the co-processor.
- **Recommended idiom:** a component exists only while in use, so pools only need the max concurrent count.

**For Halo:** the datum tables already have index-plus-salt handles (salt = generation), so half of this exists. The likely gap: per-tick loops that **scan the whole table and skip empty slots**, which touches lines for dead entries. A live-index roster per hot table is a small, local change. Worth checking against counters.

### Check 20, 33, 36 — culling without posing (igMobyBspheres)

- Computing bounds from the posed skeleton every frame was **slow and cache-missy** (sparse data on the main CPU).
- Fix: **separate render and collision bounds**, computed in batch, using **cook-time "helper" spheres bound to joints** (about 4 per character), chosen offline from the animation's extreme vertices.
- Using **last frame's** bounds turned out not to be a problem in play.
- Side lesson: they dumped model/skeleton/animation to plain text and iterated in small command-line tools on Linux. Fast iteration beats working inside the full toolchain.

**For Halo:** posing LOD (check 20) needs a way to cull an off-screen biped **without posing it first**. A few cook-time spheres per model, transformed by a handful of node matrices, does that. Pairs with check 33.

### Check 44 and lookup tables (Optimization 101)

- A 135×12 table of 16-bit values (about 3.2 KB) was replaced by **a 16-byte table plus arithmetic**: about 16 instructions, no branches, no large table.
- Method: look at the actual data (not what it "means"), find the pattern, and **test every possible input exhaustively on PC** (e.g. divide-by-15 as `(17x + 16) >> 8`, valid for 0–239, found by brute-force search).
- **For Halo:** a hot-path table larger than a few cache lines is a latency risk (39–72 cycles per miss vs. about 1 per ALU instruction). Candidate new check: "large lookup tables in hot paths, replace with arithmetic where the input range is small and exhaustively testable." Integer division by constants should also compile to multiply/shift; verify in disassembly.

### Check 44 — constants (Beware of Statics, index post)

Moving constants from memory to values built from immediate instructions shrank a function by almost half. On MIPS a 32-bit constant takes two instructions (`lui`/`ori`) with no memory access. Check that hot-loop constants aren't compiled as loads from `.rodata` or static data.

### Check 38, R4, R5 — math (Remez Exchange Algorithm, index post)

Fit minimax polynomials (Remez exchange) instead of Taylor series for sin, exp and similar; Taylor's error at the range edge was about 1000× worse. Bonus for this port: a hand-written polynomial with fixed operation order gives the **same result on PC and PSP**, unlike each platform's libm. That makes R4 differential testing cleaner.

### Check 16 — B-trees

Confirms the principle: size each node to the transfer block (here a 64-byte line), align nodes to that size, and read-only trees can pack nodes completely full. Also the counter-case: if the whole set fits in fast memory, a B-tree buys nothing. Nothing new beyond check 16 as written.

### Check 50 — loading (tier system)

Load states as stacked "stages" (load / initialize / per-frame / per-frame-during-load). Lessons: allocation inside the file reader caused memory stomps; they used co-processor decompression to relieve load-time pressure. Minor relevance; supports ME decompression during loads.

## Proposed additions to the checklist

| Proposed | Tier | Check | Source |
|---|---|---|---|
| A | 5 | Actor LOD ("drones"): distant actors skip AI and movement, promoted on damage, scope zoom or timer. Gameplay-visible, case by case | Gameplay Architecture for Performance |
| B | 2 | Dense live-index roster for hot datum tables, so per-tick loops never touch empty slots | Dynamic Component System |
| C | 2 | Batch-update recipe steps 1–3 (group, define I/O, queue external calls) per hot subsystem, before any ME work | GDC08 |
| D | 3 | Cook-time bounding spheres per model for culling and posing LOD without a full pose | igMobyBspheres |
| E | 4 | Hot-path lookup tables larger than a few lines replaced with arithmetic, verified exhaustively on PC | Optimization 101 |
| F | 4 | ME job infrastructure: per-core single-reader/single-writer queue, no bus spin-polling, alignment-asserting transfer wrappers, watchdog and debug stash | SPU Wrangling, GDC08 |

Fold into existing rows: **18** (low-priority next-frame queries for line of sight), **17** (6-byte vertex / 4-byte triangle packet format), **44** (immediate constants).

Anything "a frame late" (A, deferred queries, ME results) is gameplay-visible and goes through Sheepdog01's case-by-case test-and-evaluate rule.

## Second batch (fetched Oct 13, 2026)

| Talk | Author, year | Value |
|---|---|---|
| Multithreaded Optimization by Example (Multithreading Optimization Basics) | Mike Acton | High: the exact lock-free queue for CPU↔ME |
| SPU Gameplay (GDC09) | Joe Valenzuela, 2009 | High: per-type and per-state batch updates in practice |
| A Pipeline for Lockless Processing of Sound Data | David Thall, 2009 | Medium: staged counters, message-only API for audio |
| Online Co-Op Bot Synchronization | Heather Barclay, 2008 | Multiplayer session, not performance |

### Check 45, proposal F — the single-reader, single-writer queue

Acton's `SrSwFixedLookasideDequeue`. The whole queue is two counters and a fixed array:

- `pushed_count` is written **only** by the producer; `popped_count` **only** by the consumer. Neither increment needs an atomic read-modify-write, only an indivisible aligned 32-bit store.
- Counters only ever go up. Size is a power of two; slot = `count & (size - 1)`; items in queue = `pushed - popped` (unsigned wraparound is harmless).
- **Producer:** check not full → write the item into the slot → **store fence** → bump `pushed_count`.
- **Consumer:** read the item → **load fence** → bump `popped_count`. Read the count **once** and process only that many; anything added meanwhile waits for next time.
- Use one queue per direction (separate read data from written data).
- In a game, the main loop and vsync are the natural "check again" points; no sleeping or spinning.

**PSP translation (derived from the PSP facts in the checklist; verify on hardware):**
- The fence on Allegrex is `sync`. But the bigger issue is **no cache coherency** between the CPU and ME, so the fence alone is not enough.
- Producer on the CPU: write item → `sceKernelDcacheWritebackRange` over the item (64-byte aligned) → `sync` → store `pushed_count` **through the uncached alias**.
- Consumer: read the count through the uncached alias, then make sure its cached copy of the item lines is invalidated before reading them.
- **Put each counter on its own 64-byte line, and only ever touch counters uncached.** If both counters share a cached line, a writeback from one core can overwrite the other core's counter with a stale value. That is the same failure NJEMU hit with unaligned buffers.

### Check 10, 12, 18, 48 — batch updates in practice (SPU Gameplay)

- **Object-driven update** (`for each entity: collect, update, move, animate`) "can't amortize setup costs, can't hide deferred work." Split it into passes: issue all animation requests, then finalize all, then post-update.
- **Aggregate updater per type**, with instances further **sorted by state**. They wrote one code fragment **per AI state** (`jet_follow_path`, `jet_circle`, `jet_crash`), so each loop runs one state's code with no per-object branching.
- **Trivial win:** an early-out like "skip if waiting until frame N" moves from inside each object's virtual `Update()` to a test in the batch loop. No call, no full object fetched.
- **Hot fields as an embedded "info" struct** inside the class (aligned to 16 there; a cache line here). This is check 10's hot/cold split done in place, without moving data to a new array.
- **Instance streams:** describe data as base + stride + offset (a slice of an existing struct) with direct, indexed, or indirect-indexed addressing. You don't have to copy into a packed array, and a job can fetch an instance and its linked object together instead of one-then-the-other.
- **Pointer chasing is the real stall:** "we almost always need to fetch an associated data member out of our instances immediately." Gather the second-level addresses for the batch first, then touch them.
- **No virtual functions in the update phase.** One code path per leaf class, chosen at compile time.
- **Deferred calls back to the main CPU:** a small command buffer of fixed-format requests (e.g. a swept-sphere collision query). Results come back next frame through a handle built from (stage, job, request number).
- **"Guppys":** a 128-byte lightweight object for cosmetic things (severed limbs), updated entirely on the co-processor with simplified rendering. Spawning one replaced spawning a full object.
- **Design advice for new systems:** separate perception, cognition and action (check 18); "make it difficult to write unoptimizable code."

**For Halo:**
- **Sort actors by AI state** before the update, so each state's code runs as one batch. Cheap, local, no gameplay change.
- A **lightweight object path** for cosmetic objects (dead-body debris, Flood pieces, shell casings, dropped clutter) is worth a look under check 48. It must be visually identical.

### Check 49, 50 — audio and loading (Lockless Processing of Sound Data)

- **Staged pipeline of counters:** each stage has a "requested" and "completed" counter; a stage works while `completed < requested` and **increments completed last**. Stages can run on different threads with no locks. It's the queue above, generalized to a chain.
- **Message-only API** to the sound system: no state queries, no handles into its memory, pass by value, one direction, errors reported later. That's what makes the lock-free design possible.
- Loads go to a **staging buffer**, then copy into the main buffer; unloads trigger a defragment; sounds can be relocated during playback.
- Load by **runtime statistics**, not by the asset dependency graph: precache sounds that need low latency and are likely soon, stream the rest on demand.

**For Halo:** this is the shape for ME audio. The main CPU sends play/stop/load messages one way; the ME owns the mixer's memory; nothing on the CPU reads mixer state directly (which would also be a coherency hazard).

### Multiplayer note — co-op AI sync (for the multiplayer session)

*Resistance 2* co-op kept enemies "loosely" synced:
- One **authority** machine per bot runs the full AI and makes every targeting and health decision. Others run a **"remote" AI state** that only plays back received decisions (position and goals, dodge, cover, melee, firing).
- **Damage to a bot is decided on the shooter's machine.** Their reasoning: "bots don't get to complain about lag."
- Authority **migrates** when a client disconnects.
- Only important decisions are synced; some behaviors come free from synced movement.
- Placed bots share handles on every client; spawned bots are matched by (spawner handle, spawn ID).

**Performance side-effect for the PSP:** a PSP running as a non-authority co-op client would skip AI decision-making for every bot, so a PC host could carry the AI.

## Proposed additions, second batch

| Proposed | Tier | Check | Source |
|---|---|---|---|
| G | 4 | CPU↔ME queue: single writer per counter, counters on separate 64-byte lines accessed uncached, payload written back before the count is published | Multithreading Optimization Basics |
| H | 2 | Sort actors by AI state (and objects by type) before update; one loop per state | SPU Gameplay |
| I | 5 | Lightweight object path for purely cosmetic objects; must look identical | SPU Gameplay |

## Status

B1 is done. Remaining Insomniac posts are low value for performance (Three Big Lies is covered by GDC08; Curiously Small Code is about runtime texture swizzle, which ours does at cook time). Next: B2, cellperformance.com.
