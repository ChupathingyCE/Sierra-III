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

## 4. Budget

**v0 (REBUILD.md §3.2): 2.3 ms a tick on the beach fight**, against ~5.5 today (the combat steps scaled to the bench's tick [M, PSP-3000]), i.e. ~2.4x cheaper; on the flight and the snowfield the same 2.3 ms asks ~2.5-2.8x.

**Per entity** [U on M, to be replaced by the standalone price]: bipeds are ~75% of node-matrix time (b30 flight 7.8 of 10.6 ms instrumented, b40 cliff 6.7 of 10.0 [M]) and the beach updates ~25-31 bipeds a tick, so **~60 µs a posed biped**, ~2.3 µs a node (a 26-node biped; ~770 cycles at 333 MHz), and ~10-15 µs a weapon or scenery object. Today's per-node cost, with its I-misses, is about 2.5x that [U].

**Memory:** the plans (tens of KB a map, counted at the capture) inside the heap's room (c20 ends Sheepdog01's play with ~89 KB free [M]: the plans must come from what the box replaces, PSC's 112 KB cap and pose-ahead's tables being the candidates).

## 5. Means

- **A (data owned and packed):** the plan replaces the tag-link walk (the top data-miss site), and the small kernel replaces a 22-24 KB code path: the node-matrix stage's own instruction cache drops its I-misses by 4,157 a tick on b30 and 5,834 on c20 [E], ~0.7-1.2 ms a tick net [U on E]; the price is the PSP's (cache work is under-priced 2-3x by the model).
- **B (work on need):** only objects a reader needs this tick are posed; the never-read share (1.35 ms on b40's cliff) and, at one tick a frame, the superseded share go.
- **C (both cores):** whole families on the ME in the parallel phase, once the ME has room (§3).

**What A + B have to reach together:** ~3.2 ms of ~5.5 on the beach. A alone is ~1 ms [U on E]; B's never-read share is ~0.4-1.35 ms [M]; the rest of the gap is the per-node work's own cycles, which the capture's standalone price will size first.

## 6. What it replaces

The engine's `object_compute_node_matrices_with` (and its postprocess), the units' preprocess where it only reads nodes, and `model_get_marker_by_name`'s per-read search (`model_find_marker` with `csstrcmp`, 1.0-1.5% of the tick [M]). The port's earlier tries on this ground (`tools/levercheck.py --ground posing`): SKELBATCH (its `noinline` kernel shell is this box's; its VFPU-only gain was noise), NW/NW1/NW2/PNC/MEPOSE (per-node work through records: the hand-over ate it), NB and LEAF (computes moved after the loop found their data cold; this box poses inside the reader-driven order instead), LBM/LNM/NMS (skipped nodes made later on reads at ~17 µs each: this box decides by reader before the walk, not after), PF (a per-object memo past the data cache), PH (the decode stays the ME's), L10 and POSEK (merged on the old path; the box replaces their ground).

**Upstream manifest** (`psp/boxes/posing.upstream`, REBUILD.md §5b): `objects.c` object_compute_node_matrices_with and its callers' node reads, `models.c` model_find_marker / model_get_marker_by_name, `units.c`'s node preprocess, the animation interpolation it consumes; taken at the rebased pin (§5c).

## 7. Proof

Against the capture first: every posed object's node matrices equal the old code's within the box's tolerance (bit-identical where the maths is the same; the VFPU product's last bits where it isn't), and every object a reader read was posed. Then in the frame: play and both seeds on c20, a30, b30 and d20 if the maths is identical, the outcome census if not; pictures only if the ME writes node blocks (§3).

## 8. States

- 2026-11-11: spec (this page). The capture and harness are REBUILD.md §7 step 2 (research designs, the master builds; `research/opportunities/harness.md` takes posing as its worked example).
