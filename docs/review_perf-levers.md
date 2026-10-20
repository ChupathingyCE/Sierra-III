# Review: perf-levers (pass 2, off w140)

What this branch holds, and what the levers it was given are worth by the hardware's numbers. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference.

## The change: `nmskip` (shelved, opt-in, off)

**What:** node matrices of still scenery-kin left to a frame's last tick (lever #2's matrix half, `levers_next_b138.md`). `FIXES_NMSKIP` in `psp_fixups.py` (applied after `FIXES_BONUS`); its header comment gives the rules.

- **Who may skip:** scenery, sound scenery and placeholders with no parent, no children and no collision model (the collision tests and the Media Engine's rays read node matrices without the accessors), interpolation off, the root still (no node-0 channel, no overlays, root inputs equal to those of the last compute: a 256-slot table noted at the end of each compute).
- **When:** on ticks that aren't a frame's last (`psp_lights_link_tick == 0`, LD1's flag).
- **Catch-up:** `PSP_NMS_READ` in both node-matrix accessors and in `object_deactivate` makes the left-out compute first, from the recorded inputs and game time (`OBJECT_FRAME_INDEX_GET` overridden while it settles; the epoch restored). A compute anywhere drops a pending entry; the frame's last tick settles the rest after the objects loop; map dispose and game-state load forget them all.
- **Keep words:** `nmskip` (on), `nmskipcheck` (computes as before; checks every would-be skip and every catch-up bit for bit; logs a census every 900 ticks).

**Evidence [E]:**
- `nmskipcheck` on b30's route, c10, c20 and d20 (10 windows of 900 ticks each): the skip test was never wrong and no catch-up differed.
- **The gain is small.** Computes left out a tick: b30 13.7, c10 39.9, c20 14.3, d20 4.7. Their cost: 0.10, 0.34, 0.03 and 0.02 ms a tick on the emulator's clock, about 1% of a tick at most. Most computes are kept, for three reasons: collision models (b30, d20), animated roots (c10) and moved roots (b30, c10).
- **It isn't exact.** Pairs on a50, c20 and d20 (9,000 fixed ticks each): play hash and both random seeds were identical at every tick. But the whole-state hash differs at 89-99% of ticks, with the `objects` allocation first (lights, first-person weapons, contrail points and looping sounds follow at checkpoints). Pictures differ at 12-54 of ~4,500 frames, and with no off-vs-off control those can't yet be pinned on nmskip.

**Verdict:** shelved, opt-in and off: worth ≤ ~1% of a tick, and not exact. Cost when off: the hooks' size (accessors +64-76 B, `object_update` +68 B, compute +108 B) and one flag test a call.

**nmskip: to resume.**
1. Run an off-against-off control pair (statecrc, framecrc) on c20 to separate nmskip's picture differences from the run-to-run ones.
2. Dump the `objects` allocation's bytes at the first differing tick (c20 tick 4, a50 tick 850, both pairs' first) and name the field.
   - **A bookkeeping field** (a stamp or flag a compute writes): the lever is exact, still worth ≤ 0.35 ms a tick.
   - **A matrix:** a reader that bypasses the accessors (the Media Engine or the draw) is missed; add it to the catch-up.
3. If exact: full campaign and `PSP_MULTIPLAYER=1` builds, a cache-model A/B on b30's route, c10 and c20 (`nmskip` on against off, tick I/D misses), then a PSP pair `keepsets="- | nmskip" repeat=2` on b30 and c20.

## The other levers sized against today's hardware numbers

The source is the b139 hand test on the PSP-1000 (its `vehicles:` and `objects_update by type` lines), plus the measurement round's budget and pass tables. On the PSP-1000, ticks are a30 25.7 ms and a10 6.8 ms; on the PSP-3000, b30's tick is 33.0 ms. Ticks take 54% of the frame on b30 and 45% on a30 [M].

| Lever | Worth now | Verdict |
|---|---|---|
| **#9 vehicles, moving** (VH1) | A driven warthog's update, riders included, costs 4.3 ms on a30 and 7.6 ms on b30; a moving pelican 2.4-2.7 ms; a scorpion 3.0 ms [M]. The vehicle's own type update is b30 2.9 ms a tick for 1.6 vehicles, and up to 9.2 ms a tick for three warthogs on a30 [M]. That's ~11-17% of the tick while driving | **The largest of the five.** No exact design yet: the update first needs splitting by function (below) |
| **#9 vehicles, parked but awake** (VH2) | a10's lifeboat: 11 chair vehicles at rest, ~0.29 ms each, 3.1-3.8 ms a tick of a10's 6.8. Of that, own update, damage and node matrices are only ~1.2 ms; the rest is the seated riders, which update as children. a30's scorpion_cinematic at rest 0.41 ms, pelican_astral 0.32; b30's warthogs at rest but awake 0.32 ms [M] | A pose cache would save at most ~1.2 ms a tick on a10's lifeboat. Split before designing |
| **#1 GL3**, bending batches' index lists into VRAM | Bounded by the GE's tax on drawing's misses (+10-30% a line [M]). The index lists are ~25-35% of b30's and a30's GE reads from RAM [E]. The models' VRAM room is full (536-597 of 600 KB [E]) | **Small**, and a VRAM place costs a model or texture its own. Cheaper variant, no VRAM: 8-bit index lists for skin parts under 256 vertices. The plans unroll strips into lists (3 indices a triangle), so this halves their bytes [U] |
| **#6 TB**, change colour baked into textures | 6-8 ms a frame of CPU on b30's ride and landing [M] | Blocked on memory: its 350-900 KB cache against ~84 KB of RAM headroom and a full VRAM |
| **#7 light merge** (LM) | a10's level pass 21.1 ms a frame [M], half of it the light pass; d20's light pass 22.5 ms [M] | Same look, not bit-exact (a vertex's summed light clamps once); needs side-by-sides. Not started |
| **#8 near layer** (PN) | 12.3 ms a frame on b30's ride [M, older]; today's b30 window starts after the ride | Same look; needs side-by-sides. Not started |

**Next for #9:** run the cache model on b30's hand-test replay through the warthog drive, and on a10's lifeboat. The tick's functions by fetches and misses will show where `vehicle_update`'s ~136 k instructions and ~1,240 D-misses an update go [E] (`physics_update`'s mass points, `collision_test_vector`, `unit_update`, the riders). Design the exact speed-up from that.
