# The decal ring (lever DECALRING, 2026-11-13)

Labels: **[M]** measured on a PSP, **[E]** emulator or a count, **[D]** read in code, **[U]** inference.

**What it replaces [D]:** a decal made on a surface (a tyre mark, a bullet hole, a scorch) takes room in the decal vertex cache through `lruv_block_new`, which walks every block for the oldest one not in use. Once the cache is full, every new decal walks it whole. On b40's warthog drive that walk was **0.4-2.4 ms a decal, with 3.7 decals a tick** [M, the lab's findings `matfxdecal_split_b40_psp3000`].

**What it does [D]:** `FIXES_DECALRING` (branch `builder/decalring`, ff36482) takes the room in turn, as a ring. The next block after the last one taken is used unless it's locked (made this tick, or held by the locked proc). At the wrap, the tail's blocks are freed oldest-first. `lruv_block_new` stays as the safety net when the ring finds no room; each use is counted and the first 8 are logged. Every 512 blocks it logs a `decalring:` line. Keep word `decalringold` brings the old walk back.

**Proof [E]:**
- Play and both seeds are identical on b30 and b40, and the strict run is clean (`decalring_proof`, `decalring_strict_proof`).
- b30's pictures sit inside the A/A. b40's differ in 229 frames against the A/A's 9, mostly in the drive: once the cache is full, the ring lets the marks after its turn go, where the old walk let the least recently made go, so a different few tyre marks show. Shots are in the lab's `projects/sierra/findings/shots/decalring/`.
- In the b40 run, the ring made 5,632 blocks with 4 wraps; the old walk was used 3 times.

**Price [M]** (the lab's sierra-hw 43499740294, PSP-3000, 5cba2c7, b40_play1 to 400 s, `decalringold | -`, two repeats each; findings `decalring_pair_b40_psp3000`):

| Stretch (game time) | Tick, old walk → ring | Frame, old walk → ring |
|---|---|---|
| warthog (10,900-11,950) | 45.7 → 38.2 ms (**-16.6%**) | 151.7 → 135.4 ms (-10.7%) |
| its shooting (11,200-11,950) | 48.1 → 38.1 ms (**-20.7%**) | 162.8 → 141.6 ms (-13.0%) |
| early (300-5,300), quiet (5,300-10,900) | in the noise | in the noise |

Every gap is real past the repeats' spread.

The room a decal takes went from 404, 1,017, 2,120 and 2,426 µs (the windows to 5,210, 11,221, 11,521 and 11,821) to 8, 11, 14 and 20 µs. The ray (~32 µs), the clip (~95 µs) and the insert (~7 µs) are unchanged, so they are now what a decal costs.

**State:** ~~priced; the default the lead's call with Sheepdog01's look~~ **on by default in the tock** (2026-11-13: the lead 15:12, efficiency under 'plays and feels the same', the oldest marks freed first as Sheepdog01 asked; Sheepdog01 looked 15:13, the lab's decisions.md). Merged into master/tock162 (ff36482); keep word `decalringold` is the way back; it goes forward into the next tick. Multiplayer: drawing only, nothing on the wire.

**A known residue [E]** (the lab's lruvcallers1, sierra-batch 43518797587, tock162 1593fe6, 2026-11-13; the lead 17:43: noted, nothing to build): on d20's swamp the ring still falls back to `lruv_block_new` 130 times in 2,700 ticks, ~205 data misses each (26,647 in all, against 1.1 million with the old walk). On b30 it never falls back. The residue is the ring finding its next block locked; the walk is the safety net, as designed.
