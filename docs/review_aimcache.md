# AIMCACHE: the aim and look screens' deltas kept and applied again

Keep words: `aimcacheoff` (as before), `aimcachecheck`, `aimcachestat`. On by default in the campaign build; compiled out with `PSP_MULTIPLAYER`.
Code: `psp_fixups.py`, `FIXES_AIMCACHE` (model_animations.c's `aiming_screen_apply`; game.c's new-map hook).

## What it does

A unit's aim screen and look screen (and a vehicle's aiming overlays) each call `aiming_screen_apply`: four frames of an overlay animation a node (the yaw/pitch cell's corners), three normalised lerps of four quaternions and a bilinear translation, then one `quaternions_multiply` into the node's rotation and one add into its translation. The blended delta is a pure function of the animation, the screen's bounds and the yaw and pitch. A unit that holds its aim makes the same delta tick after tick [E: the pre split's same-input share, 4-11% of animation + node matrices on the busy maps, 21.5% on a10's last third].

The cache keeps each delta under its full key (animation pointer, bounds pointer, yaw bits, pitch bits) in a 4-way table of 32 sets (128 entries, 68 KB, allocated at first use), oldest entry out. On the same key it applies the kept delta with the leaf's own tail: the same `quaternions_multiply` on the same quaternion, the same add of the same float. So the result has the same bits.

Key lines:
- The leaf's translation delta is held in a local before its add. `x += E` and `d = E; x += d` round alike (`-ffp-contract=off`, floats evaluated as floats). So the off path and the miss path are one code; the miss only also stores the delta.
- A leaf that returns early (frames outside the animation) is kept as an empty delta, so a hit changes nothing, as the leaf would.
- Animations whose delta needs more than 128 floats (about 32 rotated nodes) are not cached; `aimcachestat` counts them as `uncached`.
- The table is cleared at each new map, because the next map's tags may sit at the same addresses. A checkpoint revert needs nothing: the key's inputs are tag data and arguments, not game state.
- Main CPU only: no Media Engine code calls the leaf.

## Checks

- `aimcachecheck`: each hit also makes the leaf on a copy of the nodes. It compares the fresh delta with the kept one, and the copy's nodes with the real nodes after the kept delta is applied, bit for bit. Then it applies the kept delta, so the play under check is the cached path's play. The `aimcache:` line counts `check N compared M differed`.
- `aimcachestat`: a line every 30 ticks with calls, hits, misses and uncached calls, the leaf's microseconds by path, and PREDEC's count (below).

## Evidence

- **Compiles:** campaign PGO and `PSP_METRICS=0`, both clean, before and after the merge onto the current base.
- **Exact [E]:**
  - `aimcachecheck` compared 193,359 hits on b30 (route), a30 (route), c20 (route), d20 (swamp) and a10 (hands off), plus two strict-interpreter runs. None differed in the delta or in the resulting nodes.
  - Play hash and both seeds matched `aimcacheoff` tick for tick on all five maps: 6,000 / 7,050 / 6,000 / 3,000 / 4,800 ticks, under the cache model and under the JIT.
  - The strict interpreter showed no bad access on b30 or a10.
  - The b30 pictures differ from off at 587 of 2,997 frames. That is inside the off-vs-off control's own 592: no picture change.
- **Removal [E x M, U]:** the aim/look leaves' time a tick, on vs off in the cache model, scaled by the aim + look share of animation and node matrices and the PSP's time for those:

| Stretch | Hits | Removal | PSP ms a frame (gate) |
|---|---|---|---|
| b30 flight | 30% | 11.6% | 0.88 (1.72) |
| b30 landing | 32% | 15.3% | 1.15 (1.55) |
| b30 beach | 26% | 4.6% | 0.15 (0.97) |
| a30 fights | 37% / 25% | 14.5% / 10.5% | 0.36 / 0.44 (1.14 / 1.40) |
| c20 swarm | 42% | 19.7% | 0.51 (1.25) |
| d20 fly-through / swamp | 9% / 52% | -9.6% / 28.6% | -0.39 / 0.43 (1.08 / 0.71) |
| a10 last third | 72% | 39.0% | 0.92 (1.12) |

  Every stretch is under its gate. The cache's own cost takes about half of what the hits save gross:
  - a hit costs 30-45% of a leaf (hash, compare, the tail ops reading a cold entry), not the 20% estimated;
  - a miss costs 10-30% more than the plain leaf (the lookup and the stores into a 68 KB table that the 16 KB data cache can't hold).

  On d20's fly-through (9% hits) it is a net loss. The leaf times include vehicles' aiming overlays; the PSP base is aim + look only.
- **What would make it pay [U]:** cutting the miss cost. Skip the store when the last key in the same slot also missed, or use a smaller table so entries stay in cache. Or key on the units' aiming vector and facing, so their own solve is skipped too (8% of animation + node matrices on b30).
- **PSP check:** a PSP-1000 b30 pair (on | `aimcacheoff`, two runs each, `nomouth`). The flight and landing estimates, 0.9-1.2 ms, are above that stretch's 0.1-0.6 ms floor.

## PREDEC count

For each leaf that still computes, `aimcachestat` records whether its four corner frames (animation, cell) were already fetched this tick by another leaf, were fetched last tick, or are new. With the cache on:
- last tick: 52-78% of computes;
- this tick: 15-40%;
- new: 4-10%. The 256-entry table is direct-mapped, so "new" is an upper bound.

So the cell is known a tick ahead for 90-98% of computes.

Sizing counts only the last-tick cells, since a cell another leaf already fetched this tick is warm. Moving their fetch + decode (aim 40-53%, look 77-90% of a leaf) off the main CPU is worth at most:
- b30 flight 1.96, landing 1.97, beach 1.16 ms a frame (gates 1.72 / 1.55 / 0.97);
- d20 fly-through 1.61 (1.08);
- under the gate elsewhere.

Verdict:
- **No-go for a main-CPU cell cache:** decoded frames are twice the size of their 8-byte source, and the cost is the memory fetch, which a cache only moves.
- **Conditional go for the Media Engine fetch** on b30 and d20: it is memory-bound and the main CPU must still read the Media Engine's output, so it needs one early PSP point first.

## Cost

Memory: 68 KB of heap while on (plus 1.8 KB a check scratch under `aimcachecheck`). Time: a hit is a key hash, a 4-way compare and the tail ops; a miss adds the stores to the leaf.
