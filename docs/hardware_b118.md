# Build 118 on the PSP-1000 (2026-10-13)

Sheepdog01's runs of `halo_psp_b118.zip` (EBOOT `e1ce6b85…`, commit `be07195`, b117's PGO profile), tests from `build_b118.md`. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[M, rig]** the PSP-3000 rig, **[E]** emulator, **[U]** inference.

## Results

| # | Test | Result |
|---|---|---|
| 7 | The XMB | Wallpaper and tile look fine: **passed** [M] |
| 1 | b30 cinematic bench (preset 3) | **The milestone:** the cinematic 193.2 → **170.3 ms** a frame (5.17 → 5.87 fps) against b116; the flight 271.9 → 250.1 ms (3.68 → 4.00 fps) [M] |
| 2 | The same, 118's speed-ups off (preset 6) | **No gain on this stretch:** off was ~2% faster (cinematic 167.1 ms); the drawing 56.6 ms off against 60.1 on, the tick 55.4 against 54.0 [M] |
| 4 | The ladder (preset 2) | **All ten maps in one session** (F1 confirmed: "d40 was the last map on the stick"). No crash [M] |
| 5 | Play, the hallway line | Not yet |
| 3, 6 | Optional | Not run |

**Test 4, the ten openings** (second half of each 90 s, `hwreport`-style averages) [M]:

| Level | fps | Ticks a second | Tick (ms) | Rig (w118) fps |
|---|---:|---:|---:|---:|
| a10 | 14.4 | 25.4 | 7.0 | 14.2 |
| a30 | 6.5 | 12.8 | 11.2 | 6.5 |
| a50 | 9.0 | 17.9 | 32.1 | 9.0 |
| b30 | 3.8 | 7.6 | 59.3 | 3.8 |
| b40 | 15.9 | 24.7 | 15.3 | 14.6 |
| c10 | 8.7 | 16.9 | 23.0 | 8.6 |
| c20 | 7.8 | 15.3 | 26.2 | 7.7 |
| c40 | 13.4 | 26.2 | 15.7 | 13.1 |
| d20 | 7.5 | 13.7 | 16.4 | 7.6 |
| d40 | **18.3** | **28.0** | 5.4 | (16.3 on the rig, start only) |

- **The rig stands for the PSP-1000:** every level within ~2% except b40 (+9% here; its fights vary).
- **d40's opening is the nearest to the floor:** 18.3 fps, but 28.0 ticks a second (the floor is 15 fps at 30). **b40** is 15.9 fps at 24.7.

## What test 2 says

118's exact speed-ups were aimed at other levels: R1 at c10 and a30's swaying plants, X9a at a50's standing bipeds, NS at a30's near batches, M1 at c40's see-through parts. On b30's cinematic none of them has much to do, and with them on the drawing was 3.5 ms slower [M].

**Likeliest cause [U]:** a test that costs more where it rarely passes. NS works out each near batch's sphere through every slot (a Gram matrix each), and the cabin's marines beside the camera cross the near plane, so the test seldom saves the corner loop. **Being settled on the rig** with b118 and one off word at a time (`nearspherefull`, `tcutfull`, `multiscan`; lab runs 38430779438, -801507, -816234, -834148).

## Completion

117's open tests ran on 118 (folded in, Sheepdog01 2026-10-13). With test 5 the build is complete.
