# Box: the world store (bottleneck, 2026-11-11; v0, spec in progress)

The lab's `research/opportunities/REBUILD.md` is the spine (§3.3, §4, §7 step 1). This page follows its §4 layout. Labels: **[M]** measured on a PSP, **[E]** emulator or a count, **[D]** read in code or docs, **[U]** inference.

**What it is:** the objects' hot state laid out for the tick's walks. It holds what each tick box reads and writes every tick, apart from what scripts and rules touch rarely. It's the root of the tick: every other tick box (posing, biped motion, the collision service, AI, vehicles, effects) reads and writes through it. Datum indices and object slot numbers stay the disc's (the multiplayer rule).

**State:** spec. The read and write map below is being taken (the lab's worldmap1); the field lists are filled from it.

## 1. Contract

- **Reads:** the engine's object datums at the tick's start (once, when a box is cut over: the store is filled from them and written back to them where the engine still reads), and each tick box's writes.
- **Writes:** nothing of its own; it's the storage the boxes write into.
- **Who reads it, and when:**
  - the tick boxes, in the tick schedule's phases: the parallel phase reads **last tick's** store (both cores), the serial phase reads and writes **this tick's** (`two_core_tick.md`, the lab's `tick_core.md`);
  - the engine's remaining readers (scripts, rules, damage, the netcode) through the engine's datums, which stay right at the tick's end (the write-back, §5);
  - the drawing (the draw list box), after the tick, for what it carries between frames (positions, node matrices, the far ladder's state).
- **Its rule:** one writer a field in each phase. A field two boxes both write is split, owned by one and handed to the other, or written in the serial phase only (the ME's rule for its lines: one writer a 64-byte line).

## 2. Data

**Today's layout** [D, the datum layouts as the port compiles them, the lab's `psp/sierra/datum_layouts`]: one datum per object, its type's whole struct in one block: biped 1,152 bytes, vehicle 1,148, weapon 636, projectile 552, scenery 424, machine 472, light fixture 476, equipment and garbage 476, control 460, device 452; the node orientations and matrices follow each datum as header blocks. The pool is ~1 MB on b30 (the lab's findings `tickwrites_kb_a_frame`).

**What the tick touches of it** [E]:
- a biped's update touches **497 words (1,988 bytes) in 40 lines that half of all updates touch**, which packed would fit 32 lines; weapons 11 of 23 lines (13 hot, 8 packed); vehicles 24 of 51 (33 hot, 23 packed); scenery and machines one line for one word each (the lab's findings `objhot_biped_lines_touched`, cachecensus1 41308914906);
- the tick writes **163-236 KB a frame of object data** on b30 (objects, lights, particles; the AI's props and actors 46-98 KB) (the lab's `tickwrites_kb_a_frame`, snap1 40903127317);
- the costly steps' loads are **~97% their own family or last tick's state**: a parallel phase reading the tick's start is consistent with them (the lab's `tickcore_census`, 42927567774). The stores that need care are named there: collision's shared scratch, the walk's bookkeeping, the clusters' lists, the AI's shared scratch, the global seed, function values written into other objects, and fresh reads of others' nodes.

**The store's layout** (to fill from the map): per type, the fields each box reads and writes every tick, grouped by box, in arrays by type (structure of arrays where a walk reads one field of many objects, the datum's own order where a box reads many fields of one object), with each field's bytes per object and per section. The engine's cold fields stay in the datum.

**The read and write map, per box** [E, to fill: the lab's worldmap1, `psp/sierra/worldstore.py` on `TICKCORE: stepfield`, b30, b40, d20 and a30]: for each tick step (posing's node matrices, biped physics, vehicles, the per-type update, the object update, the AI's think), the fields it reads (R), writes (W) or both, with loads and stores a tick, and the fields written by more than one step.

## 3. Placement

Main RAM, in the layout above; the parallel phase's two copies (last tick's and this tick's) are the double buffer only for the fields the parallel phase reads and the serial phase writes (`two_core_tick.md`, the one-tick-old view). Not eDRAM (2 MB, the ME's code and data and the drawing's); not the scratchpad (16 KB, and the ME can't see it). Code: none of its own beyond the fill and the write-back.

## 4. Budget

- **Memory:** the store's fields, per object, times the live objects of each type, plus the double buffer's share. To set from the map. The heap's room is the limit: c20 ends Sheepdog01's play with 89 KB free [M], and a whole-object snapshot (448-672 KB) didn't fit (the lab's `snapdry_buffer_kb`), so the store must replace datum fields, not copy them, wherever the engine's own readers allow.
- **Time:** the store has no time of its own; it's paid back in the boxes' misses (means A). Its fill and write-back (§5) are budgeted in the tick schedule's serial phase.

## 5. Means

- **A (data owned and packed):** a biped's hot words fit 32 lines packed against 40 today, and a walk over one field of many objects reads one line per 16 objects in place of one per object [E counts; the price is the PSP's].
- **C (both cores):** it's what lets the parallel phase run: each core reads last tick's copy and writes its own family's fields.
- **B:** none directly; it carries the boxes' "changed" marks (work on change).

## 6. What it replaces

The engine's object datum as the tick's working storage (`object_get` and the datum walks in `objects_update`); the port's earlier tries on this ground: OBJHOT (the hot words packed), DATUMSPLIT, WORLDVIEW (Sheepdog01 declined on its standalone ceiling 2026-11-07; the one-tick-old view is now what the parallel phase needs), Plan C's snapshot (didn't fit, `snapdry_buffer_kb`). Each by `tools/levercheck.py` before the store's design is settled.

**Upstream manifest:** the engine's datum structs and the functions that read them in the tick, at the pin after the rebase (REBUILD.md §5b, §5c): taken on the rebased engine.

## 7. Proof

The store filled and written back with no box using it yet: play and both seeds identical every tick on b30, b40, d20 and a30 (the check build compares the store's fields against the datum's after each step).

## 8. States

- 2026-11-11: spec started; the map's census (the lab's worldmap1) running.
