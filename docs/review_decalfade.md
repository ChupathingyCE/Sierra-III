# Review: dynamic decals fade out after 30 s (branch `decalfade`, on w140 924682c)

Bullet holes, scorches and blood from a fight now fade over 2 s and go about 30 s after they're made; the level's own placed decals are never touched. Asked for after The Library (c20) filled with tracer and grenade marks. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## How the engine keeps decals [D]

- A decal is a datum in `decals` (game state, at most 2,048) whose vertices sit in the decal vertex cache (`rasterizer_xbox_decals.c`: 2,560 pages of 64 bytes, least recently used first). The datum's index is its cache block's.
- **Placed decals** (the scenario's painted markings) come from `structure_decals_update` → `decal_new(..., permanent = 1)` as a cluster becomes active, and are dropped and remade with it. They carry `_decal_permanent_bit`, which the cache's locked proc treats as unpurgeable.
- **Dynamic decals** come from effects (`effects.c` → `decal_new(..., FALSE)`). About one in ten is *locked* (unpurgeable, a local-random draw in `decal_insert`); the rest the cache purges, oldest first, when a new one needs room (`rasterizer_decal_vertices_purge_proc` → `decal_delete`).
- **The tag's lifetime is honoured.** `decals_update`, called from the renderer's frame begin, runs `decal_update` on every decal every drawn frame: for a non-permanent decal with a lifetime, intensity falls over the last `decay_time` seconds and the decal is freed at `lifetime`. 0 means forever. This is the Xbox's own path; ChupathingyCE and the port keep it, and the GE draw multiplies by that intensity for every blend.

## Not a restoration [E]

The census line counted every tag lifetime met: on c20 and b30 **every** dynamic decal's tag has a lifetime (none of 2,218 on c20 or 2,385 on b30 had 0), the longest 180 s on c20 and 300 s on b30. The port already ages them as the Xbox does; decals pile up because a long fight makes them faster than 180-300 s retires them. So 30 s is a departure from the original (fewer marks on the walls), made because it was asked for, and on by default for that reason. `decalfadeoff` is the original.

## The change (`psp_fixups.py`, `FIXES_DECALFADE`, `source/effects/decals.c`)

- `psp_decalfade_new(&decal->lifetime, &decal->decay_time)`, called in `decal_new_from_collision` right after the tag's lifetime and decay time are drawn, only `if (!permanent)`. If the drawn lifetime is 0 or longer than N seconds, it becomes N and the decay `min(2 s, N)`. Shorter tag lifetimes stay as they are. It runs after the tag's local-random draws, so the seed moves as before.
- Nothing else: the engine's own `decal_update` does the fade and the free. No per-frame code is added, so it costs nothing where no decal reaches 30 s.
- `psp_decalfade_seconds`: `decalfadeoff` → 0 (off); `decalfadeN` → N; default 30. Read once keep.txt is loaded.
- `decalcensus` (off by default): from `decals_update`, every 150 ticks, one `decals:` line: live dynamic (fading, locked), live placed, made, tag lifetimes met (none / own / longest in tenths of a second), given the fade, expired.
- **Campaign only** (`#if !defined(PSP_MULTIPLAYER)`). Freeing locked decals earlier changes how often `decal_insert`'s unlock pass (over 512 locked) draws from the local random seed, which vehicles, animations and breakable surfaces also read. It never ran in these tests (locked peaked at 220), but a client can't be proved safe without a networked A/B, so the multiplayer build is unchanged.
- `psp/src/psp_sdl.c`: test keep word `inputgametime`: the input script's times in game time (ticks / 30), as a route's are. The script ran on the PSP clock, so a change in frame cost moved every press to another tick and the first c20 pair parted at tick 2118.

## Evidence [E] (campaign PGO campaign-w140c, bake b140x, `nomouth` both sides)

| Map, stretch | Play + both seeds, on vs `decalfadeoff` | Live dynamic, peak (on / off) | Frame ms (on / off) | Tick ms |
|---|---|---|---|---|
| c20, the opening room, player firing 8-194 s (AR, grenades, infinite ammo), to 300 s | same at all 9,000 ticks | 403 / 1,926 (off near the 2,048 cap; the cache purging) | 17.1 / 29.6 (−42%); 4500-6000: 18.9 / 50.2 (−62%) | −0.9% |
| b30, the b113 route (flight, landing, beach), to 300 s | same at all 9,000 ticks | 295 / 1,119 | 26.1 / 26.7 (−2.4%); 6000-7500: −10% | −0.4% |
| a10, the b139 route, to 240 s | same at all 7,200 ticks | 0 / 0 (no dynamic decals) | +0.2% (noise) | 0.0% |

- **Placed decals untouched:** the census's placed count is the same on both sides at every line on every map (c20 11; b30 16 then 0 as the cluster changes; a10 0, 11, 25). a10's shots on vs off are identical but for the clock in the corner.
- **Shots, c20 at 240 s:** off shows the pillar the player fired at riddled with holes; on, 46 s after the firing stopped, it is clean.
- **Strict:** c20 strict with `decalfade5` (187 decals freed by the fade by tick 1,652): 0 distinct sites.
- Whole-state hashes differ from the first decal (its lifetime field), and the `objects` allocation differs on a10 too, which made no dynamic decal: drawing writes into objects, as between any two builds.
- Frame time is the emulator's CPU frame: 1,500 fewer decals aged and submitted each frame on c20. The GE's own decal fill falls too; only the PSP can say how much.

Builds: campaign PGO, `PSP_MULTIPLAYER=1`, PGO + `PSP_METRICS=0` all compile.
