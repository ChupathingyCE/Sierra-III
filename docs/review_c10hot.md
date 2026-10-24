# Review: c10hot (the profile's hot threshold)

One change, in `psp_build.py`: a PGO build passes `--param=hot-bb-count-fraction=20000` unless `PSP_PGO_HOT_FRACTION` says otherwise (GCC's own is 10000; `PSP_PGO_HOT_FRACTION=10000` gives it back). A build setting, an exact speed-up: the program does the same work, compiled differently.

## The regression it takes back

b139's final candidate (the release profile `campaign-b139c`, the b139c bake) ran c10's opening tick about 10% slower on the PSP than the first candidate (`campaign-b139`, the b139a bake) [M]. In the cache model, the c10 opening hands off to tick 1350 [E]:

| Build | Tick, k cycles a tick | I misses | D misses |
|---|---|---|---|
| first candidate (its own profile) | 6,170 | 45.0 M | 25.7 M |
| final candidate (its own profile) | 6,647 (+7.7%) | 52.8 M | 27.5 M |
| final code, the first candidate's profile | 5,990 | 42.6 M | 24.3 M |
| final code and profile, the first bake | 6,648 | 52.8 M | 27.5 M |
| final, every new default-on change off | 6,643 | 52.8 M | 27.5 M |
| final, hot fraction 11000 | 6,148 | 45.9 M | 24.3 M |
| **final, hot fraction 20000** | **5,962 (-10.3%)** | 42.3 M | 24.1 M |

Instructions fetched barely move (+0.4%); the misses carry it. Neither the bake nor any keep word plays a part; the profile does.

**Why [D]/[U].** GCC calls a block hot when its count is at least the program's busiest counter / `hot-bb-count-fraction`. `campaign-b139c`'s busiest counter is 4.4% higher than `campaign-b139`'s (1.347e9 against 1.291e9), so the threshold rose with it, and the tick's small helpers fell just under it: in the final candidate the tick calls `object_type_export_function_values`, `quaternions_interpolate_and_normalize`, `vfpu_multiply`, `fabsf`, `psp_vfpu_matrix4x3_multiply`, `data_usable` and `animation_keyframe_search` out of line, where the first candidate inlined them (the cache map's tick by function: together ~5 M more instruction misses). A threshold at half gives the margin back, so a new profile's busiest counter moving by a few percent no longer moves the tick's inlining.

## Cost elsewhere [E]

| Stretch | Hot fraction 10000 | 20000 |
|---|---|---|
| a10 opening, tick (k cycles a tick) | 1,557 | 1,536 (-1.3%) |
| a10 opening, drawing (M cycles) | 4,351 | 4,346 (-0.1%) |
| c10 opening, drawing (M cycles) | 10,279 | 10,117 (-1.6%) |
| heap | 5,796 KB | 5,792 KB |

Every function keeps its counts (0 without them), so `PSP_PGO_STRICT=1` builds. Not yet measured: b30 and the other maps (the release check's batch), and the PSP.
