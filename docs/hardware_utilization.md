# Hardware utilization: does the port use the PSP well?

Draft: phase 1 (the survey) done; phase 2's ranked list in section 7, for Sheepdog01 before any rebuild. The hardware session's brief is in the lab (`projects/sierra/coord/handoff/hardware_2026-10-28.md`). Labels: [M] PSP, [E] emulator or estimate, [D] code or data, [U] inference.

## 1. Which lane limits each map

The main CPU binds all seven campaign intros [M] (w142, PSP-3000, tm-plain 40566969386; lab findings `timemap_lanes`). The GE is busy 22-68% of the frame and the ME 34-69%.

- **Record dominates** (the main CPU's drawing) on a10 (30.1 vs 2 x 14.0 ms of ticks) and a30 (38.6 vs 2 x 11.5). Moving the draw walk off the main CPU (Plan C) pays most here.
- **The tick dominates** on b30 (2 x 44.4 vs 43.2), c10, c20 and d20. Faster tick maths pays most here.

## 2. Where the main CPU's cycles go

Medians over the hardware profiler's ~1 s windows, with loading windows dropped [M] (lab findings `cpu_cycles_by_kind`).

| Map | Waiting on memory | Internal stalls | VFPU | Coprocessors | Issuing |
|---|---|---|---|---|---|
| a10 (intro) | 57.8% | 6.8% | 0.4% | 3.1% | 31.8% |
| a30 (intro) | 57.2% | 7.1% | 0.3% | 3.8% | 31.6% |
| a50 (intro) | 61.8% | 6.4% | 0.4% | 3.3% | 28.1% |
| b30 (intro) | 62.8% | 6.1% | 0.5% | 4.2% | 26.4% |
| c10 (intro) | 56.2% | 7.2% | 0.6% | 4.9% | 31.1% |
| c20 (intro) | 62.6% | 6.4% | 0.5% | 3.0% | 27.5% |
| d20 (intro) | 53.7% | 7.6% | 0.4% | 5.6% | 32.7% |
| b30 (route, 40089996527) | 57.2% | 7.2% | 0.4% | 4.9% | 30.3% |
| a30 (route, 40096315033) | 53.3% | 8.1% | 0.4% | 4.4% | 33.8% |
| d20 (opening, PSP-1000, 40096390859) | 52.6% | 7.9% | 0.3% | 3.0% | 36.1% |

**What this means:**
- The VFPU does one job in the whole port (`matrix4x3_multiply`).
- Only about 30% of cycles do work at all. The VFPU can shrink only that ~30%; the memory wait (53-63%) is untouched by it.
- So a VFPU rewrite's ceiling is a fraction of its function's share, and the memory side (item 4) is the larger half.

## 3. The float inventory (api-job hwfloat1 40731074790, read and spot-checked)

Emulator instruction-fetch shares from the cache maps of b30, d20 (40640730484) and a30, a50, c10 (40599003550), crossed with the code at w142 [D]. Worth is the fetch share times the estimated float fraction of the body [U]. It is an upper bound on the compute half only: the PSP's memory wait isn't in it.

**Alignment:** nothing in the engine is 16-byte aligned by type [D]. Every float struct is 4-aligned, so on the PSP-1000 VFPU code needs `lv.s` loads or aligned temporaries (`ulv.q` corrupts a register, `halo-psp-feasibility.md`).

VFPU forms:
- **E:** element-wise add, mul and div in the FPU's order. Exact, measured (vfpuround).
- **D:** needs `vdot` or `vmmul` accumulation. Exactness not measured.
- **S:** needs `vsqrt` or `vrsq`. Not exact.
- **U:** unaligned data.
- **N:** no fit (branchy, mostly compares, one dot a step).

### The tick

| Function | Share of the tick's fetches | Form | Worth [U] |
|---|---|---|---|
| `psp_bsp3d_test_sphere`, `psp_bsp2d_test_sphere`, `collision_bsp_test_vector_recursive`, `bsp3d_test_point` | 1-12% each | N (one plane dot and compares a node: a memory-bound walk) | ≤ 0.5-2.8 each, but not VFPU material |
| Posing chain: `matrix4x3_rotation_from_quaternion`, `quaternions_multiply`, `quaternions_interpolate_and_normalize`, `quaternion_normalize`, aim blend | 0.6-1.6% each | E (the normalizes need sqrt on the FPU: VFPUQ); output matrices unaligned (U) | ≤ ~3.8 together |
| `object_compute_node_matrices_with`, `matrix4x3_transform_point`, `valid_real_vector3d_axes3` | 1.0-4.4% | D | ≤ ~3.3 together |
| `fast_vector_intersects_sphere`, `sphere_intersects_cluster_portal`, `path_state_traverse` | 0.6-2.5% | N, part S | ≤ ~2.5 together, early exits limit it |

### Drawing

| Function | Share of drawing's fetches | Form | Worth [U] |
|---|---|---|---|
| `render_frustum_cube_visible` | 1.7-5.2% | D, 4-wide bounds tests (lever RM). The frustum's planes sit at 8 mod 16 (`render_cameras.h`, checked), so it needs one aligned copy a frame, which is cheap | ≤ 4.4 |
| `planes_intersect_rectangle` (a50), `weather_particle_system_render` (c10) | 2.0-3.2% on one map each | D | ≤ 1.8, 1.6 |
| `build_sprite` (vertex loop), `convex_polygon2d/3d_clip_to_plane` | 1.3-4.9% | E in the loop, otherwise N | ≤ 1.3; the clips are N |
| `structure_visibility_*` | 1.0-5.3% | Not settled (inlined float work unread) | CAN'T TELL |

### What it says so far

1. **The tick's hottest float work is the collision BSP walks.** They are one dot product and two compares per node, chasing tag data through memory: a placement or memory question, not a VFPU one.
2. **The VFPU's clean candidates are posing and the frustum tests.**
   - Posing: the quaternion and matrix chain, ≤ ~7% of the tick's fetches together. Coordinate with the budget session's LBM, which removes ~42% of b30's biped matrices outright.
   - The frustum tests: RM, ≤ ~4% of drawing on the maps that show it.
   - Both are upper bounds on the compute half. On the PSP the gain is those shares times roughly the 2-2.6x VFPU speed-up, minus the memory wait those functions keep.
3. **The one VFPU job isn't bit-exact today.** `matrix4x3_multiply` runs on `vmmul` and is switched on by a tolerance check (`psp/src/psp_vfpu.c`: `worst < 1e-5f`; lab levers.tsv calls it "last-bit different"). So `vmmul`'s accumulation order already differs from the FPU's somewhere. Measuring `vdot`/`vmmul` against the FPU (the brief's first open question) decides the D rows. Whether node matrices reach multiplayer's shared state is item 6 (api-job hwmpbound1).
4. **Not settled:** the alignment of tag data (collision planes, portals, keyframes; a map-level check of block offsets mod 16 would settle it); `objects_update`, `effect_update` and `structure_visibility_*`'s inlined float work.

## 4. The memory side (from the lab's findings; no new runs)

**Instruction misses outnumber data misses** [M]. Per million cycles, the intros show 3,456-4,916 I-misses against 2,470-3,121 D-misses (`cpu_cycles_by_kind`), about 1.2-1.8x. On a10 the tick alone has 5,035 against 2,814 (w139). Code size and layout is the largest single memory lever. It is already being worked (the `tgc` layouts, `tick_imiss_by_layout`). Denser code helps it, the VFPU's included.

**Where the data misses come from** [E] (cache model, lm1 39895433740; b30, a30, c20, d20):

| Source | Share of data misses |
|---|---|
| The tag window (tag data, mostly the collision BSPs and their planes) | 28-37% |
| The collision walks' own state | 15-30% |
| The object pool | 12-14% |
| Thread stacks | 9-10% |
| Node matrices and animation | 5-6% |
| Spheres and cluster references | 3-4% |

Collision is about half of it, which matches section 3: the tick's hottest float work is a memory walk, not maths.

**The scratchpad** (16 KB, as fast as a cache hit, off the bus) [M, w142 tm-plain log]: 15,104 of 16,384 bytes used:
- the models' near-cut corner table (8,448 B, drawing);
- the profiler's slots (6,144 B) and front table (512 B).

The split was set at build 108 by the cache model's misses per byte (`psp_scratch.c`, `overnight_b108.md`). The tick's top miss sources today (tag window, collision) get none of it. The feasibility notes' lever (the top of each BSP in the scratchpad) was built since as SCRATCHBSP, on in the tock (corrected 2026-11-16). **Lead:**
- re-rank the 16 KB by misses per byte on w142's cache map;
- price the winner with a PSP pair, never the cache model (the lab's cache rule; `noscratch` is the off switch);
- ask whether the profiler's 6.6 KB belongs there in play builds.

**The ME's eDRAM** (lab `projects/sierra/findings/edram.md`):
- About 1,664 KB is claimable.
- The main CPU can't load from it; only the ME or DMA can. So it is the ME's memory, and it matters to placement: an ME job's tables can live there.
- The two big claims exclude each other: CPA, the checkpoint (1,089 KB), and R2, the sound cache (1,664 KB; rejected on the PSP as built).

**The bus: the GE's traffic is what costs the tick** [M]:
- When the ME and the main CPU both walked RAM, neither slowed (mewalk).
- The ME's loads cost the tick 1.4-6.7% (`tick_change_me_busload`, PSP-1000).
- When the GE pulled from main RAM under M6 #2, 77-94% of the tick's rise was contention: each miss stalled longer (`contention_tick_ms`, PSP-3000). With the models' vertices read from VRAM (mevram) it fell to 55-76% of a smaller rise.
- That answers the brief's open question: the ME's traffic is cheap; the GE's main-RAM reads are what the tick pays for. So data the GE reads belongs in VRAM first.
- DMA's own share of the bus budget is about 1 [D] (`bus_budget`).

**VRAM today** [M, w142 tm-plain 40566969386, a10's intro, PSP-3000 log]: of the 2 MB, the display takes 832 KB, the texture cache 616 KB and the model buffers (`vrammodels`) 616 KB.
- **Textures:** the cache is filled by binds per byte; its best third holds 189-270 KB. Several of the most-bound textures still live in RAM (a 128x128 at 4,896 binds in 900 frames).
- **Model buffers:** they run full. In different 300-frame windows they were full 0-274 times and copied up to 6 MB into VRAM.
- **The room is spoken for** (the master's 12:55 note; levers.tsv M6B, CTNG, VRSP, VRBTEX). The dashboard's `vram_plan` is b115's and stale.
- **What's left is choosing what earns the room,** not finding more of it. The copies into the model buffers are bus traffic of their own: a lead for phase 2 is to price their churn against what they save the GE.

### The bus census: no cheap room left

The emulator's bus census (`buscensus`, the halo-local patch's item 23; read with the lab's `bus.py`) counts the bytes each frame pulls from main RAM, by source and by who reads them: the main CPU's and the ME's cache fills and write-backs, and the GE's lists, vertices and textures [E].

**b30, 90 s on its route** (lab bus7 40884427736, bus8 40894428250): about 12.9 MB a frame.
- By reader: the main CPU fills 5.6 MB and writes back 1.2; the ME fills 2.0 and writes back 0.75; the GE reads 1.4 of vertices, 1.4 of texels and 0.25 of lists.
- By source:
  - the program's `.bss`, 2.5 MB: a long tail; its largest symbol is the geometry cache's entry table, which the ME's scans read at 148 KB a frame;
  - the heap, 2.2 MB: mostly the GE's level copies (their arena ~0.9 MB, with the level carve ~1.4 MB, ~1.0 of it the GE's vertex reads) and gedefer's buffer (~0.34);
  - the texture cache, 1.4 MB;
  - the objects' game state, 1.05 MB;
  - tag data, 1.0 MB, the level collision BSP 0.42 of it;
  - the display list, 0.9 MB.
- c10 is the same kinds at about half the size (6.5 MB a frame).

**Three cuts, each priced on the PSP:**

| Cut | Bytes saved [E] | PSP |
|---|---|---|
| BSPDFS: the collision BSP renumbered depth-first | 59 KB a frame on b30 (420 to 361) | even on b30 (40862518281) and a10 (40875906608): shelved |
| MEPLACE: the ME's geometry lookups through the place keys | ~260 KB a frame on b30 (2.1%; the ME's entry-table fills 148 to 33) | null on b30, PSP-3000 (40894645109: flight 158.2 to 158.0 ms, tick 53.1 to 53.0): rejected |
| levelcompact (b87): the level's vertices 20 to 10 bytes | about half of the GE's ~1 MB a frame of level vertex reads | no gain three times (b87, b93, b102) and ground cracks: settled |

**What it says** [M for the prices, U for the reason]:
- Cuts of 0.5-4% of the bus, on any reader, don't move the frame or the tick.
- What the tick pays for is contention: the GE reading main RAM while the tick misses (the bus subsection above: 55-94% of the tick's rise under overlap). That depends on when the GE reads, not only on how many bytes it reads [U]; levelcompact halved the bytes and gained nothing.
- What's left of the census are things already priced or spread thin:
  - the GE's texels and vertices (mevram and the texture cache already hold what VRAM has room for);
  - the main CPU's own misses (no symbol over ~0.25 MB a frame);
  - the ME's traffic (cheap to the tick: mewalk, `tick_change_me_busload`).
- So "after the bus has room" can't be C step 3's gate: no cheap cut makes room.

**C step 3's gate, proposed instead:** price the snapshot itself, since its copy is the bus traffic C adds.
1. **[E] Count it:** the snapshot's bytes a frame on b30, a10 and c10: the objects' render state, node matrices, lights, effects and decals (walkwrites lists what the walk writes). One emulator run with a counting keep word.
2. **[M] Price it:** one PSP pair on c10 (`keepsets "- | snapdry"`, repeat=2). `snapdry` writes that snapshot at each frame's end, and nothing reads it. The pair prices the copy's own cost, the tick's contention included.
3. **Go** if snapdry costs the frame less than a third of what C takes off the main CPU on the same map. The engine's whole walk is 12-56 ms a frame (b30 55.8 at b138, levers_next_b138.md); its visibility part alone is 8.4 on b30 (visspan, PSP-1000) [M]. Otherwise the copy eats C's gain, and C stays parked.

**The gate's outcome (2026-10-29): C is parked** [E]:
- **Step 1, the count** (lab snap1 40903127317, `tickwrites`: the 64-byte blocks of the game state the ticks change between two frames' drawing, at the PSP's 2 ticks a frame):
  - b30 ~170 KB a frame (objects 88, AI props 32, actors 18, lights 12, particles 7; the drawing reads ~110 of it);
  - c10 ~50 KB, a10 ~20 KB.
- **Step 2's dry run in the emulator** (snapdry2 40905044915): the per-object copy needs 448 KB (a10) to 672 KB (b30, c10) a frame: each live object's memory with its nodes, plus the live lights and particles. That is 2.5-20x what the ticks change.
- **It doesn't fit.** Once the level's buffers are in (display list 768 KB, copies' arena 800, gedefer 224), the heap refused it on every map: 450-670 KB were free inside, but only in pieces. While a 512 KB buffer held on b30, the level carve took 128 KB more from the texture cache. A real snapshot is double-buffered, so twice that.
- So a PSP pair would price a copy that fits only by squeezing the level copies and the textures. It wasn't asked for.
- **The ways back, not taken now:**
  - (a) a field-level snapshot: only what the drawing reads and the ticks change (~110-170 KB on b30). It needs the walk's read set first, a "walkreads" census in the cache model.
  - (b) the snapshot in the ME's eDRAM (~1.6 MB claimable), filled by DMA and read by the ME's walk. It conflicts with CPA's claim (1,089 KB).

### C's field-level snapshot: the design (2026-10-29; not built)

**The read set, measured** [E] (lab walkr1 40926317411, walkr2 40931322249; the halo-local patch's item 24, `walkreads`). It counts the 64-byte lines the drawing loads that the ticks stored since the last drawing, 120 s a map, strict:

| Map | Drawings | KB a drawing, average | Drawings by KB | The most | Heap's largest free block, lowest |
|---|---|---|---|---|---|
| b30 | 2,444 | 88 (objects 38, lights 12, program globals 11, particles 11, heap 8, contrails 2) | under 128: 2,147; 128-192: 296; over 192: 1 | 990 KB, drawing 1 | 315 KB |
| c10 | 3,281 | 38 (objects 15, program globals 12, heap 4, lights 3) | under 128: 3,280; over: 1 | 701 KB, drawing 1 | 346 KB |
| a10 | 3,488 | 25 (objects 10, program globals 9, heap 3) | under 128: 3,487; over: 1 | 495 KB, drawing 1 | 247 KB |

- Every drawing but the first after a load reads under 192 KB of what the ticks changed. The first reads everything, because the load wrote everything. Whether a cutscene cut is a second over-size case these routes don't settle [U].
- **It's a gather, not a block** [E]: the lines changed in at least 10% of b30's drawings come to 166 KB in 686 runs, with a median run of one line (64 B); runs of 1 KB or more hold only 60 KB. c10 and a10 are the same (65 KB in 351-417 runs). So a DMA block copy doesn't fit it: the copy walks a field list.

**The shape:**
- **Copied by structure, not by line.** The ticks don't mark what they change, so the copy takes, from each object the drawing will walk, the fields the drawing reads and a tick may write (render state, node matrices, the lights' and particles' live fields), whether they changed or not. That's more than walkreads' average and less than snapdry's per-object copy (448-672 KB). Its size waits on the field list (below).
- **Its home: one buffer of ~192 KB, in the ME's eDRAM** (`edram.md`'s row; the big-moves budget already holds 192 KB for it beside CPA, AIME, L1 and CSTPOOL, and that budget fits only just). Two buffers (384 KB) don't fit beside the others, so it's single: the frame's copy waits for the ME's walk of the frame before to end. That wait is bounded by the walk's own deadline (on b30 the walk is ~20 ms, the frame's two ticks ~100), so it should almost never happen [U]. The main CPU can't store into eDRAM directly, so the fill is the ME's own gather, from main RAM, while the main CPU waits at the frame's end. The heap is the fallback home: 192 KB fits its largest block on all three maps, but takes most of a10's (247 KB).
- **The copy's price** [U]: 100-200 KB a frame on b30 at the main CPU's memcpy rate (76 MB/s) is 1.3-2.6 ms; the ME's gather beside an idle main CPU should be near that. Against the objects pass it moves (b30 ~20 ms), that's well under the gate's third. Only a PSP pair says.

**The over-size frame** (the first after a load; any frame whose structure outgrows the buffer): that frame isn't snapshotted. The ME walks the live state while the main CPU waits for it, so the frame is drawn exactly, on the ME, without overlapping the ticks. That loses one frame's overlap per load, and isn't a band-aid: the job stays on the ME, and the wait is bounded by the walk. Each such frame is counted and logged.

**The ME replay's gap, closed** [D, E]. gedefer's records keep four kinds of pointer instead of copies (its comment in `psp_ge.c`). Under C, the replay reads them beside the next ticks, so each must be something the ticks don't write:
- the geometry cache's blocks: written by the drawing, kept for RECENT_FRAMES; not the ticks;
- the level's vertices: written at the load only;
- the frame's dynamic triangle lists: written by the drawing;
- **the decals' vertices: written in the tick.** These are the gap. walkreads measures them at ~0.06 KB a drawing on b30 (new decals only), so the snapshot carries them like any other field, and the gap closes.

**Next, in order:**
1. **[E] The field list:** map walkreads' lines to (object type, field offset), with the object table's addresses dumped at the window's opening, to give the copy's fields and its bytes a frame per map.
2. **[E] snapdry with that copy:** the field copy made at each frame's end, nothing reading it; exactness and the copy's count.
3. **[M] A c10 PSP pair** `"- | snapdry"` x2: the copy's own cost. Go by the gate above (under a third of what C takes off the main CPU).

**Priced (2026-10-29): park the build, keep the design.**
- **The copy on the main CPU** [M] (`snapfield snapfieldactive`: copy-on-wake, the active objects and their children; c10 PSP-1000 pair, lab sierra-hw 40955133252): the frame +3.6 to +6.1 ms on every stretch, all of it in the drawing. The copy was 5.3 ms a frame on average, 8.6 at most, for 144 KB. That is 27 MB/s, a third of memcpy's rate, because its two passes walk every object's header and copy many small ranges. The bar is ~4.8 ms (a third of c10's ~14.5 ms model pass), so it sits on the line.
- **The one-pass copy clears the gate** [M] (`snapfieldfast`: only the active objects and children, the small ranges word by word; c10 PSP-1000 pair, lab sierra-hw 40958783351): the copy was 3.5 ms a frame on average for the same 144 KB, against 5.3 for the two-pass copy, and the frame rose +2.3 to +3.0 ms on every stretch. That is under the ~4.8 ms bar on the main CPU alone. The emulator had it at 1.02 ms against 1.79, so it times this copy about 3.5x short.
- **The design's form** [U]: the ME gathers into eDRAM.
  - The main CPU's cache write-back is already paid: gemeon's handoff writes the whole cache back.
  - So the main CPU pays the wait for the gather, unless it has other work then, and the bus while the ME reads.
  - The gather keeps the copy's read misses but sends nothing back to main RAM: about 2-3.5 ms a frame on c10, if the one-pass copy lands near its emulator ratio. That is under the bar.
- **But c10 is bound by the ME** [M, U], and placement goes by the overall balance.
  - The ME is 30-37 ms busy in a 53-55 ms frame.
  - C moves the walk (~14.5 ms) onto the ME, with the gather: ~47-55 ms on the ME, against ~40 left on the main CPU. c10's frame would fall only to the ME's figure.
  - PDC's c10 saving (~8-9 ms, stage A) brings the ME to ~39-46.
  - So on c10, C pays only after PDC, and its copy belongs on the main CPU (which then has room), not the ME.
  - b30 is the other way round: the main CPU binds (the ME 70-78 ms of a 115-170 ms frame), so C would pay there first.
- **Cutting the copy** [E, U]: the node matrices are 53% of it (c10 76 of 144 KB, b30 49-68 of 93-129).
  - c10's are mostly its animated scenery, which moves every tick, so copying only what changed wouldn't cut them.
  - PSC would: a looping scenery's matrices are its table's frame times its transform. The snapshot could carry those (~60 B) instead of the node block, and the ME's walk rebuild the matrices (a scalar multiply a node: the ME has no VFPU, `psp_me.c` "no system calls, no library calls, no VFPU").
  - Counted (lab snapf6 40959834148 [E]; objects that animate by themselves with no parent, an upper bound on PSC's): c10 230 objects, 35 of the 76 KB; b30 4-8 KB of 50-68; a10 none. With ~60 B each in their place, the copy falls ~15% on c10 (144 to ~122 KB) and ~3% on b30. PSC is worth having, but it isn't what C waits on.
- **It waits on one thing:** PDC stage B's measured ME saving, since C adds the walk to the lane c10 is bound by. The copy's place is settled: it stays on the main CPU, under the bar, with its buffer in the heap.

**Read against the code (2026-10-29; lab api-job cplan 40973577774, Opus, quotes spot-checked): the copy is priced, but C isn't ready to build** [D].
- **Nothing reads the snapshot yet.** The walk reaches objects through the live header array, which the ticks change (`objects.c` `object_header->datum = NULL;`, and `memory_pool_compact` moves the datums). The snapshot needs a reader: its own header map by absolute index, and an accessor the walk goes through.
- **The field list has holes for a walk that runs beside the ticks.** walkf1 listed what the ticks *changed* between drawings. A walk that runs beside the ticks needs everything it *reads* that a tick may write. Missing:
  - the object links (0xc4-0xd0), region permutations, first widget, and velocity (lighting reads it);
  - the flags word for weapons, equipment and scenery, and all four outgoing function values;
  - both camouflage words;
  - game time and the program globals walkreads counted (9-12 KB a drawing);
  - the first-person weapon, antennas, particle systems, widgets, effects and weather.
- **Copy-on-wake is half built.** Active objects and children are copied, but nothing copies an object when a tick wakes it.
- **Visibility has to be placed.** It reads the clusters the ticks write. Collecting it before the next tick starts keeps it exact (`mevis_landing.md`).
- **walkapart tells the walk from the tick by one global** (`psp_walk_side`). That only works while they take turns, so it must become a property of the CPU.
- **The ME's limits:**
  - no system calls, library calls or VFPU, so `error()` and lazy keep-word reads must go;
  - the ring's 10 ms deadline, against a 14.5-20 ms walk;
  - its late fallback (the main CPU redraws from live state) would be wrong once the next ticks have run. The fix-where-it-runs form is a bounded wait for the ME, with the main CPU walking the snapshot only if the ME is dead.

**The build, in steps, each with its proof** (all in the emulator before any PSP time):
0. **The reader:** the walk on the main CPU, reading a snapshot taken just before it, gives the same gestream, framecrc and statecrc on five maps.
1. **Completeness, serially:** snapshot, then ticks N+1, then walk N from the snapshot, all on the main CPU. Any live read shows as a difference; fill the field list until none does.
2. **Visibility collected before the hand-off.**
3. **walkapart made safe beside the ticks:** statecrc every tick, no MEWRITES lines both sides wrote in game state.
4. **The ME port:** no MERACE, MECOHERE lost or stale lines; a symbol audit of the ME's code.
5. **A b30 PSP pair** `"- | C"`, once PDC's ME saving is measured.

## 5. The placement map

Each unit's strengths, measured:
- **The main CPU** has the VFPU: 1.9-2.6x its own FPU on the node product [M].
- **The ME** has an FPU exactly as fast as the main CPU's and bit-exact with it. It has no VFPU. Its traffic costs the tick 1.4-6.7% [M], and it runs branchy scalar work at 0.88-0.96x the main CPU's time (mevis [M]).
- **The GE** has spare fill on the main-CPU-bound maps [M]. Its main-RAM reads are what slow the tick (section 4).

So:
- matrix and vector work goes where the VFPU is;
- branchy scalar work goes to the ME;
- bytes the GE reads go to VRAM.

Per-pass figures are research's (lab `projects/sierra/coord/handoff/render_ownership_2026-10-27.md` 6b, b30) unless marked.

| Job | Runs on today | Best unit | Why |
|---|---|---|---|
| Visibility (find_clusters, subclusters, find_rendered_objects): 8.0-8.8 ms a frame | main CPU | **ME** | Scalar and branchy, no VFPU, static inputs; measured 0.88-0.93x on the ME, exact, never late (mevis [M]); its one tie to the tick (the marker stamp) is cut by walkapart |
| Level pass: 2.1-3.5 ms | main CPU walk, ME replay | **ME** (B's first job) | No VFPU; self-contained |
| Model walk and node matrices: ~20 ms main, 27.9 ME replay | main CPU walk (VFPU product), ME replay | **Main CPU, more of it on the VFPU** | Holds every VFPU use in the walk; on the ME the node products would take ~2.5-5 ms instead of 1.3-1.9 [M]. Part draws can balance either way (B) |
| Decals, HUD, screen quads: ~0.6 ms | main CPU | ME jobs | Cheap, no VFPU |
| Particles, contrails: 1.3-2.9 ms | main CPU | Main CPU for now | Reach firmware paths that need stand-ins |
| Tick: posing (quaternions, rotation matrices, node walk; ~10% of b30's tick [E]) | main CPU's FPU | **Main CPU's VFPU** (E forms; section 3) | Element-wise forms are exact; LBM first removes the unneeded matrices |
| Tick: collision BSP walks (about half the data misses) | main CPU | **Main CPU, with the scratchpad**; batched queries to the ME later (L3, BW) | One dot a node: memory, not maths. The ME's own cache and cheap bus traffic make batched line-of-sight a fit, once AI's share of the ME is settled against drawing's |
| AI rays | ME ring jobs | ME (as now) | 1-3% of the ME [M] |
| Sound | ME | ME (as now) | |
| Geometry and textures the GE reads | VRAM where they fit | VRAM | ~~9.6 µs of tick a KB of geometry from main RAM [M]~~ (retired 2026-11-10: the toll follows fetches, not bytes; the measured toll: b30 flight's tick 5.8 ms, `hardware_facts.md`); the VRAM route is nearly used up (the master's second opinion) |

**Plan C re-weighed with the VFPU:**
- Full C, where the ME owns all drawing, gives up the VFPU on exactly the pass that uses it (models). It also keeps the GE's main-RAM reads beside the tick.
- What the hardware argues for is a split: the ME takes visibility, the level pass and the small passes; the main CPU keeps the model walk and does more of its maths on the VFPU.
- That is B plus mevis, the order research and the master's second opinion already reached.
- C's snapshot doesn't fit the heap as a per-object copy (section 4, the gate's outcome): C is parked, and B is next. Its way back is the field-level snapshot (section 4, its design). The VFPU findings add a reason to keep models on the main CPU, rather than a reason to revisit C.

**Posing on the ME (MEPOSE, PNC) against the VFPU** (the master's 13:35 list):
- **The VFPU took the main CPU's share first.** VFPUP and VFPUQD, now in the tock, take ~1 ms off b30's flight tick [M].
- **MEPOSE** would move the posing of objects nobody reads early (no parent, no children, nothing held) to the ME. Movable: ~4.8-5.4% of c20's tick, ~0.7% of b30's [U on E]. On the ME that work runs the FPU's way, ~2x slower than the VFPU's, but off the main CPU entirely.
- **It pays where the ME has room.** It then competes for the ME's time with mevis and B's replay, and placement is judged by the overall balance (the 12:27 rule). So it comes after mevis lands, priced by a c20 PSP pair, and its movable share is re-counted on the tock with the VFPU forms in.
- **PNC** stays held: it needs B's records and jobs.

**A conflict to settle:** RM (VFPU frustum tests, `render_frustum_cube_visible`, ≤ 4.4% of drawing's fetches) runs inside visibility. If visibility moves to the ME, RM has no VFPU to use. Visibility on the ME (8 ms off the main CPU) outranks RM.

## 6. The multiplayer boundary (api-job hwmpbound1 40731381973, read and spot-checked)

The PSP is only ever a client of ChupathingyCE's desktop host (network version 29; `FIXES_MPCLIENT`). **Halo's netcode isn't lockstep: nothing a client computes has to stay bit-exact with the host** [D]. What crosses the wire is either the client's own word (input, aim: taken as given, clamped) or the host's word, which the client accepts and snaps or glides to.

| System | Verdict | Why [D] (engine at the pin d98982b) |
|---|---|---|
| Input, aim | LOCAL (the client's word) | sent as floats, the host clamps them (`network_distributed.c`), nothing recomputed to compare |
| Own biped movement | PREDICTED, bounds-checked | the host takes the client's position within 3.5 units (`HOST_ACCEPT_TOLERANCE`); the client snaps to the host's past 3.0 (`LOCAL_CORRECTION_TOLERANCE`) |
| Other players, vehicles | PREDICTED | host states in 16-bit vectors; under 1.0 unit the copy glides half the gap a tick, past it jumps (`network_objects.c`) |
| Own vehicle | PREDICTED, bounds-checked | 4.0 / 5.0 units (`LOCAL_VEHICLE_TOLERANCE`, `HOST_VEHICLE_ACCEPT_TOLERANCE`) |
| Damage, death | host's | the client deals no damage; its hit reports (floats, node, region) pass the host's bounds (3-6 units, finite), not a bit match (`network_damage.c`) |
| Node matrices, animation, collision | LOCAL | never on the wire; reach the host only through movement and hit reports, both bounds-checked |
| Projectiles, particles, decals, contrails, lights, sound, HUD, drawing | LOCAL | no message types for them (`network_distributed.h`'s enum); projectiles are client-only objects |

**Checks across machines:** none per tick. The engine's checksums (FNV-1a in `network_distributed.c`) are the host deciding whether a state changed since its last send, not a desync test (checked). At join: a token mixing the build time and `tag_groups_checksum()` in non-`DEBUG` builds, and the map version.

**What does have to match** is discrete, not float: objects placed at map load must take the same datum indices as the host's (`network_objects.c`). Whether a Sierra-III bake keeps that order is unchecked.

**So for the bit-exact rule** (the brief's rewording 2): VFPU maths that differs in the last bits is safe for multiplayer, on the tick as well as in drawing. What it could change is behaviour well inside the tolerances: a biped drifting past 3.0 units would snap visibly, and hit reports choose region and material on the client. Neither needs bit equality; both need the play to stay the same, which the campaign's side-by-sides already test. Two things keep statecrc useful in the lab anyway: exactness proofs for A/Bs, and the repeatability of fixed-seed runs. A lever that changes the last bits needs its new maths on both sides of its A/B.

**Riskiest calls** [U]:
1. Own biped prediction: the PSP's collision differs from the PC's (the packed collision tree), so a large drift shows as snaps, independently of any VFPU work.
2. Hit reports: a systematic PSP bias in node matrices would go unnoticed (by design: what the shooter saw hit, hits).
3. Map-load object indices against the bake: a hard failure if wrong; not float.

**Side notes for the multiplayer session:**
- The engine's `port/linux/NETCODE.md` still says version 10.
- The join token's size comes from `sizeof` on an array parameter, so it is pointer-sized: 8 bytes on PC, 4 on the PSP, in non-`DEBUG` builds. Unchecked which the PSP's multiplayer build is.

## 7. Phase 2: the ranked list (draft, for Sheepdog01 before any rebuild)

| # | Lever | Worth | Risk | Exactness | Priced by |
|---|---|---|---|---|---|
| 1 | **Visibility on the ME** (mevis; research's) | 8.0-8.8 ms of the main CPU's frame on b30 [M] | Low: measured exact, never late | Exact (statecrc [E]) | Research's PSP pair; this session endorses its placement |
| 2 | **Posing on the VFPU** (merged with 4 after the count below): the quaternion product, blends and normalizes, and the rotation matrices, in the VFPU's own instructions (`vqmul`, `vdot`, `vrsq`) where they pay | Exact forms alone: ~0.7% of b30's tick fetches [E count], under the 1% gate. The VFPU's own forms: per-call times from `vfpubench` [M, pending] | Medium: unaligned data, so `lv.s` loads | Not bit-exact; posing is LOCAL (section 6), so the A/B carries the new maths on both sides and side-by-sides judge it | `vfpubench`'s PSP run first (times and rounding), then one A/B pair on b30's flight |
| 3 | **The scratchpad re-ranked** by misses per byte on w142 (candidates: the top of each map's collision BSP, the planes it touches first) | Unsized: the tick's top miss sources (tag window, collision) hold none of it today | Low: `noscratch` is the off switch; one owner thread | Exact (only placement) | Cache rule: a PSP pair, never the cache model |
| 4 | (merged into 2) | | | | |
| 5 | **Small passes as ME jobs** (decals, HUD) | ~0.6 ms main [M] | Low | Exact | Part of B |
| 6 | **Collision queries batched to the ME** (L3, BW) | Large but unsized; needs AI's and drawing's split of the ME first | High (XL) | Exact if the ME's FPU is (it is [M]) | Later |
| - | RM (VFPU frustum tests) | ≤ 4.4% of drawing's fetches | | | **Dropped** if 1 lands |

**Sheepdog01's 12:27 ruling:** both rule rewordings are in force; the list goes ahead in this order.

**#2's removal count** (b30's flight, cache map 40640730484 [E]): the five targets are 787 M of 20.0 G tick fetches (3.9%).
- Element-wise forms can only pack the products. `quaternions_multiply` packs well (16 multiplies and 12 adds into 4 swizzled `vmul.q` and 3 `vadd.q`).
- `quaternion_normalize` gains nothing: its sum stays sequential, and the sqrt and divide stay on the FPU.
- The rotation matrix's nine sums each need lanes from different registers, so they stay scalar (~15% of the function).
- That totals ~133 M, about 0.7%: under the gate. So the exact forms alone don't pay.
- The VFPU's own instructions do much more: `vqmul.q` is the whole product in one instruction, and `vdot.q` a whole dot. Since posing is local to the client (section 6), #2 and #4 become one lever, priced by `vfpubench`.

## 8. Results so far (PSP pairs, 2 runs a side)

| Lever | Status | b30 tick | Notes |
|---|---|---|---|
| **VFPUP** (the quaternion product, element-wise, exact) | in the tock, on | -0.2 ms every stretch; cinematic frame -1.6% | 573 products a frame; pair 40741608139 |
| **VFPUQD** (interpolate + normalize with vdot/vrsq, ≤12 ulp) | in the tock, on (Sheepdog01 16:13) | -0.8 ms flight, -0.3 ms cinematic | Fixed-seed fights re-roll; the free-play census found nothing broken; landing/beach gaps not credited (different fights); pair 40775475465 |
| **SCRATCHBSP** (the level collision BSP's top 304 nodes in the scratchpad) | in the tock, on | -0.3 ms cinematic and flight | Exact; the near-cut table moved to the heap; pair 40775509445 |
| LBM v2 (budget's lazy biped matrices) | rejected as built | +0.5 to +1.1 ms | Exact but slower everywhere; pair 40747441996 |
| VFPUNODE (node products) | parked at the gate | ≤ ~0.5% | Already on vmmul |
| VFPUQ exact (interpolate) | parked at the gate | ~0.5% | Superseded by VFPUQD; normalize and rotation are slower exact |
| **NRCHK** (the root node's validity test left out; it only wrote debug.txt) | in the tock, on | -0.3 to -0.7 ms; c10 -1.5 to -1.8 ms (with PGO) | Exact; a code-size effect, like X10; pairs 40793130396 (c10), 40795042670 |
| WM (warm-vs-cold for mevis) | parked with numbers | | No gain: mevis's working set exceeds the ME's caches (wmbench) |
| **GSCHECK** (the tick's two game-state checks off; `gscheck` puts them back) | in the tock (Sheepdog01 21:14) | c10 -0.2 to -0.3 ms early | One build, exact; pair 40809345943 |
| **SLEEPDBG + COLLUSAGE** (the sleep's debug reports cold, the collision usage calls gone) | in the tock | c10 -0.3 to -0.5 ms (no PGO) | Exact; a two-build pair, so within layout's reach (below); 40812138009 vs 40812168360 |
| **ACCLEAF** (the hottest lookups' keep tests out) | in the tock | c10 -1.0 ms (no PGO) | Exact; drawing +1.2 ms in the same two-build pair: layout; 40833635029 vs 40833694056/40835744413 |
| **MECTX** (VFPU posing refuses the ME's work run on the main CPU) | in the tock | (a fidelity fix) | The emulator's ME posing now uses the real ME's FPU maths; MESTRICT sites 8 -> 1 (mectx2) |
| **OBJPLACE** (sleepers' no-child/not-garbage bit beside the sleep tables) | merge asked | c10 -0.2 to -0.3 ms (-1.0 to -1.3%) | One build, exact; the object pool's traffic -5.7% b30, -11.2% c10 [E]; pair 40839486853 |
| LW2 (the log written every 30 s) | shelved | none | The log's stick time -2/3, but nothing waited behind it; a hang would lose more log; pair 40834903762 |
| PRECONVERT (model buffers baked in the GE's form) | not merged: Sheepdog01's call | none | Round 1 lost (+36% frame: the baked buffers skipped the VRAM copies); round 2 even; worth B8 and ~200 KB of heap for packs +2.5-5 MB a map; 40841204853, 40845838294 |
| NW1, NW2 (node matrices on the ME) | can't pay / rejected | NW2: c20 tick +0.8 ms | NW1 sized at <= 3% of c20 with no cache relief; NW2 built and exact, but the walk is cheap on the VFPU and the copy-back costs more; pair 40843585564 |
| SHADOWWALK, CSTPOOL, PROFSEC, MACHLINE, SKIDX8 | sized | | SHADOWWALK <= 0.15% of b30's frame; CSTPOOL ~0.5-0.95 ms a frame but needs an eDRAM claim (a row in the lab's edram.md; no longer Sheepdog01's call, 2026-11-11 17:21); PROFSEC dormant; MACHLINE and SKIDX8 under the bar |

**The tock against b148, each built as it ships** (its own profile, strict, layout w142a_tgc90; 2026-10-29 [M]): c10 (PSP-1000) tick -1.0 to -1.5 ms (-6 to -8%), frame -1.6 to -2.5%; b30's flight (PSP-3000) tick -2.2 ms (-4.5%), frame -2.0%; drawing +2.6 to +3.6% on both maps, which the cache model doesn't show (drawhunt1: equal) and link layout alone can do (below). The next test is a layout from the tock's own profile.

**Method: link layout alone moves the frame 2-5% on the PSP** [M]. The same code (w142 bd6731a4) linked in default order and with w142a_tgc90, without PGO, on b30's flight: drawing 0.6-1.3 ms, the tick 1.3-2.0 ms, all real (40837811468 vs 40837873313). So a pair of two builds carries about a millisecond of layout either way: a change that moves code (a build switch, removed code) is priced in the build that ships, with a fresh profile and layout; keep-word A/Bs in one build are unaffected. The emulator also misses the GE's contention for the bus (PRECONVERT round 1) and the VFPU's speed on the main CPU (NW2): placement is settled on the PSP.

