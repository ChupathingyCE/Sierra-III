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

(Filled from the lab batches; numbers [E] unless marked.)

- Compiles: campaign PGO and `PSP_METRICS=0`, both clean.
- Exactness: pending.
- Removal: pending.

## PREDEC count

For each leaf that still computes, `aimcachestat` records whether its four corner frames (animation, cell) were already fetched this tick by another leaf, were fetched last tick (a prediction from last tick would have had them), or are new. Pending.

## Cost

Memory: 68 KB of heap while on (plus 1.8 KB a check scratch under `aimcachecheck`). Time: a hit is a key hash, a 4-way compare and the tail ops; a miss adds the stores to the leaf.
