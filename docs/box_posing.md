# Box: posing, the pilot (bottleneck, 2026-11-11; v0, spec)

The lab's `research/opportunities/REBUILD.md` is the spine (§3.3, §4, §7 step 3: the pilot, whose question is the method's: can a box written forward meet a 2.4x budget alone on the PSP?). This page follows its §4 layout. Its inputs: the lab's `research/opportunities/posing.md` (the cost, the code's shape, the stopped tries; a DeepSeek draft spot-checked), `posing_anatomy_ds.md`, the node census (`census_nodes_fate_*`) and NW2's and NB's failures. Labels: **[M]** measured on a PSP, **[E]** emulator or a count, **[D]** read in code or docs, **[U]** inference.

**State:** spec. Next: the capture and the harness (REBUILD.md §5, research designs, the master builds), then the box's code against the capture, then its standalone price.

## 1. Contract

**Takes, per object to pose this tick** (through the world store, `box_world_store.md`): the root (position, forward, up, scale), the parent's node matrix and attachment node for attached objects, the animation state's decoded node orientations (the ME's pose-ahead output, as today) and the overlays' (aim, look) contributions, and the object's model.

**Gives:** each posed object's node matrices (52 bytes a node, `NODE_FLOATS 13`) in the world store's node block, and its bounding sphere.

**For exactly the objects something reads this tick.** The readers, in tick order: the object's own update (units' preprocess and IK, which read their own nodes first: 4.6 of b40's cliff's 6.7 ms of biped matrices [M, instrumented]), collision and the AI (markers, heads, hands), effects attached to nodes, and the drawing (the draw list copies a drawn object's matrices after the tick). An object no reader needs this tick isn't posed: on b40's cliff 1.35 ms a tick of matrices were made, never drawn and never read [M, instrumented]; at two ticks a frame ~50-63% of all matrices are made over before a draw reads them, which a reader-driven box doesn't pay (the census's 'superseded' share, `census_nodes_fate_b30`, `_b40_cliff`).

**Per-model, made once a map:** the walk plan (node order, each node's parent, its channel flags) and the marker table (name to node), from the model tags.

**Not this box's:** the animation decode (stays the ME's pose-ahead), the overlays' choice of angles (the units' and AI's), and the drawing's skinning.

## 2. Data

- **The plan, per model** [U, the count is open]: ~6 bytes a node (parent, flags, walk order) and ~4 bytes a marker; a map's models × nodes (MAXIMUM_NODES_PER_MODEL 64) is tens of KB. Read linearly by the walk: no tag link reads (today's walk's top data-miss site is the node's tag links, `objects.c` 6073-6088 [D]; 917 misses a tick on b30, 683 on c20 [E]).
- **Per object, in the world store:** the inputs above (root, parent link, decoded orientations' pointer) and its node block (a 26-node biped 1.35 KB, 21 lines; 64 nodes 3.3 KB). The node block stays where the drawing reads it, so the draw list's copy reads lines the walk just wrote.
- **No per-object memo table** (PF's 24 KB of slots measured a wash past the 16 KB data cache [M]).
- **One writer a line:** the node block is written only by this box in the tick; on the ME (§3, later) a family's blocks are written whole by one core.

## 3. Placement

**The main CPU with the VFPU** (the product is already `vmmul`; the walk's lines are warm there). Code: one small walk kernel (~632 bytes: rotation from quaternion 228 + matrix multiply 404 [D, nm sizes]) and stages of ~1-2 KB, resident against a 16 KB instruction cache, where today a biped's posing path touches ~22-24 KB of code and one function is 10,384 bytes [D].

**The ME's share later, as whole families in walk order** (the tick schedule's parallel phase): the ME has room for tick work on the fights only at the 15 fps floor today (the beach ~9 ms a tick, the snowfield ~13 [M], the lab's `tick_core.md`, findings `me_room_fights_*`), none at 30 fps on the beach until the drawing's render job shrinks. Never per node through records: NW2's per-node hand-over cost more than it moved (c20 tick +3.2% [M]).

**In the two-core tick** (Sheepdog01 2026-11-13 11:02, decisions.md; the lab's `research/opportunities/two_core_tick.md`): **phase: the parallel phase.** Posing reads its own object's inputs and its parent's node matrix and writes only its own node block (one writer a line, §2), so it needs nothing from the serial phase; a child is posed after its parent by walking whole families in order (§3). Whether the IK (`unit_postprocess_node_matrices`, a third of the pieces' data misses, §8) reads another object's matrices of this tick (a held weapon's markers) is open [U]: if it does, that read moves to the join. **Core: the main CPU's half**, since its costly part is VFPU work: the decode (`animation_get_node_orientations`, ~83% of the pieces' instructions, §8) and the matrix product; under the step split (two_core_tick.md's first open question) posing is one of the main CPU's steps, under the object split the ME's half takes whole families in posing's scalar form. **Working set:** per biped the node block (a 26-node biped 1.35 KB, §2), its decoded orientations (32 bytes a node), the plan (~6 bytes a node) and the animation's frames it decodes (tag data, read-only: how many bytes a decode reads is open [U]); code, the kernel's ~632 bytes and stages of 1-2 KB against the 16 KB instruction cache (above). No shared state, so nothing for the scratchpad; eDRAM matters only for the ME's half (last tick's inputs DMA'd in).

## 4. Budget

**v0 (REBUILD.md §3.2): 2.3 ms a tick on the beach fight**, against ~5.5 today (the combat steps scaled to the bench's tick [M, PSP-3000]), i.e. ~2.4x cheaper; on the flight and the snowfield the same 2.3 ms asks ~2.5-2.8x.

**Per entity** [U on M, to be replaced by the standalone price]: bipeds are ~75% of node-matrix time (b30 flight 7.8 of 10.6 ms instrumented, b40 cliff 6.7 of 10.0 [M]) and the beach updates ~25-31 bipeds a tick, so **~60 µs a posed biped**, ~2.3 µs a node (a 26-node biped; ~770 cycles at 333 MHz), and ~10-15 µs a weapon or scenery object. Today's per-node cost, with its I-misses, is about 2.5x that [U].

**Memory:** the plans (tens of KB a map, counted at the capture) inside the heap's room (c20 ends Sheepdog01's play with ~89 KB free [M]: the plans must come from what the box replaces, PSC's 112 KB cap and pose-ahead's tables being the candidates).

## 5. Means

- **A (data owned and packed):** the plan replaces the tag-link walk (the top data-miss site), and the small kernel replaces a 22-24 KB code path: the node-matrix stage's own instruction cache drops its I-misses by 4,157 a tick on b30 and 5,834 on c20 [E], ~0.7-1.2 ms a tick net [U on E]; the price is the PSP's (cache work is under-priced 2-3x by the model).
- **B (work on need):** only objects a reader needs this tick are posed; the never-read share (1.35 ms on b40's cliff) and, at one tick a frame, the superseded share go.
- **C (both cores):** whole families on the ME in the parallel phase, once the ME has room (§3).

**The gap, and the pilot's real question:** the box has to take ~3.2 ms of ~5.5 on the beach. A is ~1 ms [U on E] and B's never-read share 0.4-1.35 ms [M], so **~1-1.8 ms of the 3.2 has to come from the kernel's own per-node cycles** (today's per-node work with its misses taken out). That gap is what the pilot's standalone price answers first: if the kernel over the capture can't make a node in ~2.3 µs, the budget moves (REBUILD.md §5) before any integration work.

## 6. What it replaces

The engine's `object_compute_node_matrices_with` (and its postprocess), the units' preprocess where it only reads nodes, and `model_get_marker_by_name`'s per-read search (`model_find_marker` with `csstrcmp`, 1.0-1.5% of the tick [M]). The port's earlier tries on this ground (`tools/levercheck.py --ground posing`): SKELBATCH (its `noinline` kernel shell is this box's; its VFPU-only gain was noise), NW/NW1/NW2/PNC/MEPOSE (per-node work through records: the hand-over ate it), NB and LEAF (computes moved after the loop found their data cold; this box poses inside the reader-driven order instead), LBM/LNM/NMS (skipped nodes made later on reads at ~17 µs each: this box decides by reader before the walk, not after), PF (a per-object memo past the data cache), PH (the decode stays the ME's), L10 and POSEK (merged on the old path; the box replaces their ground). VISLAZY (stopped: made an undrawn object's matrices on demand inside the old walk; exact, b30 0.2-0.37 ms a tick in the emulator [E]) and the reader census behind it (the lab's findings `anim2_undrawn_tick_readers` [D]: 10+ sites read an undrawn object's pose every tick, its head marker through `actor_input_sample_position` and `prop_position_refresh` among them): **reader-driven posing differs** in that the readers are named at the box's contract and each declares the nodes it needs before the walk (a head marker is one chain, not a whole skeleton), where VISLAZY and LBM let a read trigger a whole or partial compute after the walk, at ~17 µs a late compute. UNSEENPOSE (a compromise, shelved: undrawn objects posed at the root only; out of every plan, for the far tier only if Sheepdog01 asks). VFPUTRIGSIM (the simulation's trig on the VFPU, priced flat on posing's ground: tick in the noise on b30's flight, drawing +0.9-2.2% [M, PSP-3000, the lab's sierra-hw 43016382621]).

**Upstream manifest** (`psp/boxes/posing.upstream`, REBUILD.md §5b): `objects.c` object_compute_node_matrices_with and its callers' node reads, `models.c` model_find_marker / model_get_marker_by_name, `units.c`'s node preprocess, the animation interpolation it consumes; taken at the rebased pin (§5c).

## 6b. v0's cut, and the capture

**v0 is the walk** (Sierra-III `bottleneck/box-posing`: `psp/src/box_posing.c`, its adapter `bench/halo_box/adapters/posing.c`): the root's matrix from the object's place (translation, rotation from forward and up, mirrored, the physics' centre of mass and the origin offset, under the parent's node with its scale taken out and its mirror applied) and each node's matrix from its orientation under its parent, in a per-model plan's order (the engine's node-stack order over the tag's links, made once a map) in place of the links' reads; then the bounding sphere. **Upstream of it, still the engine's in v0** and captured as their result: the animation decode (the ME's pose-ahead), the overlays, the object's scale, the type's preprocess (the units' aim and look) and the interpolation. They join the box after the walk is priced alone, each with its own share of the 2.3 ms, so the pilot answers its question (can a node be made in ~2.3 µs alone) first. The units' postprocess (IK) stays the engine's in v0 too.

**The capture** (`PSP_CAPTURE=posing`, a recon build's hook; the master builds it, REBUILD.md §5, the lab's `research/opportunities/harness.md` §7). At `object_compute_node_matrices`' walk, for each object it poses in a sampled tick:
- **in**, taken where the walk starts (after the interpolation): the model's key, the flags (world relative; mirrored; attached, with the parent object mirrored; with physics; placed with no model), the object's position, forward and up, its physics' centre of mass negated, its definition's origin offset, bounding offset and radius, its scale, the parent's node matrix if attached, the node count and the node orientations (32 bytes each, by node index);
- **out**, taken where the function returns: the node matrices (52 bytes each, by node index) and the bounding sphere's centre and radius;
- **static**, once a model a capture: its node table as the tag has it (parent, first child, next sibling a node);
- **kind:** the object's type; **id:** the object's datum index.

The records, byte for byte, are in the adapter's head. **Tolerance:** relative, 1e-4 of the larger magnitude or 1e-3 absolute (the PSP's old code multiplies on the VFPU, whose last bits differ from the box's FPU, and a node's error grows down its chain). **Budget an entity:** ~25 µs an object (2.3 ms over the ~91 objects b30's flight poses a tick [E]).

**Proven so far** [desktop only]: over a made-up capture (random trees of 1-64 nodes and random places, `tools/cap1.py posing`, whose outputs are the engine's walk restated in Python: its node stack and matrix4x3_multiply term for term), **1,456 objects, 0 outside the tolerance**, and a spoiled output named (`bench/halo_box/host/test.sh`). Not yet: the PSP toolchain's build, a capture of the game, any time.

## 7. Proof

Against the capture first: every posed object's node matrices equal the old code's within the box's tolerance (bit-identical where the maths is the same; the VFPU product's last bits where it isn't), and every object a reader read was posed. Then in the frame: play and both seeds on c20, a30, b30 and d20 if the maths is identical, the outcome census if not; pictures only if the ME writes node blocks (§3).

## 8. States

- 2026-11-11: spec (this page).
- 2026-11-11: v0's walk and its adapter built (§6b, Sierra-III bottleneck/box-posing), proven on a desktop over a made-up capture; the game's capture (`PSP_CAPTURE=posing`) is the master's to build. The capture and harness are REBUILD.md §7 step 2 (research designs, the master builds; `research/opportunities/harness.md` takes posing as its worked example).
- **2026-11-13, posing's split on the PSP [M]** (the lab's sierra-hw 43465321056, PSP-3000, b30_play2 to 200 s, objsteps, B against A, repeat 2; findings `posing_objsteps_b30_psp3000`): object_compute_node_matrices (the sampling, interpolation, overlays and matrix products together) is ~95% of posing's time in the flight and ~90% on the beach; the postprocess (the IK with it) ~5% and ~10%. The emulator's count gave the IK a third of posing's misses [E]; the PSP doesn't make it a third of the time. So the compute part leads, the decode first inside it, for the main CPU's half (§3). objsteps doesn't split the compute part: its pieces need timers inside it on the PSP before the decode's own share is known [U]. B against A: compute -2.7% in the flight, -1% on the beach; postprocess +0.1 ms.
- **2026-11-13, alone against in the frame [M]** (the lead 12:56): the box alone over the beach capture (the lab's bench-hw 43483104594, PSP-3000, 2abeb5f, findings `boxposing_standalone_beach_psp3000`) is 0.86 ms a tick warm, 1.25 emptied, 1.33 crowded (0.91 / 1.44 / 1.60 with the GE at 50%), against its 2.3 ms budget; posing in the frame on the same stretch is several times that (object_compute_node_matrices 6.8-6.9 ms a frame on the beach, 20 in the flight; `posing_objsteps_b30_psp3000`, a recon build, its instruments in it). The gap is the crowded cost: the box's code and data evicted by the rest of the tick between objects, and the misses that follow. That gap is what the two-core split (two_core_tick.md: posing in the parallel phase, its working set kept per core) and the world store (packed hot words, BOX-WORLD) are for, not a faster decode alone.
