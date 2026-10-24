# Review: fix-tick140 (w140's tick rise on the openings)

## What was seen

On the PSP-3000, w140 (509ec38) ticks slower than b139 over ticks 675-1350 of the openings: a10 +5-6%, c10 +10-12% [M]. Frames per second did not change, because GL1 and GL2 save more on the drawing side than the tick lost.

## What carries it

**The profile, not a lever.** w140 was built with `PSP_PGO_PROFILE=campaign-b139`, which was counted at b139's code. GCC matches each function's counts against a checksum of its code, so the 52 functions that pass 2 changed (in `psp_dsound.c`, `psp_ge.c`, `psp_geo.c`, `psp_me.c` and the engine units the levers touch) compile with no counts at all. Without counts they are treated as cold. For example, `valid_real_matrix4x3` is inlined into the node-matrix loop in b139, but in w140 it becomes a call (`valid_real_matrix4x3.localalias`): on c10 that call costs 50 M tick fetches and 1.85 M I-misses over 1,350 ticks [E].

The cache model's tick phase on the a10 and c10 openings, hands off, ticks 0-1350. Cycles = fetches + 72 × D-misses + 64 × I-misses (the model's defaults) [E]:

| | a10 | c10 |
|---|---|---|
| b139, its own profile | 2,019 M | 8,330 M |
| w140, b139's profile | 2,158 M (+6.9%) | 8,259 M (-0.8%) |
| w140, b139's profile, every pass-2 off word | 2,159 M | 9,114 M |
| b139, no profile | 2,247 M | 9,828 M |
| w140, no profile | 2,183 M (-2.8%) | 9,103 M (-7.4%) |

- Turning every pass-2 off word on together does not bring a10 back. On c10 it makes the tick slower. So no single lever carries the rise.
- With no profile on either side, w140 is faster than b139 on both maps: the levers are a net gain.
- The profile is worth -10.1% on a10 and -15.2% on c10 to b139, but only -1.2% and -9.3% to w140. That gap is the rise.
- c10 off one lever group at a time (b139's profile): objwalk+typehdr 8,309 M, exact group 8,373 M, GL1+GL2 8,255 M, ME group 8,941 M (fetches -3.5%, I-misses +31%), against w140's 8,259 M. The emulator runs the ME's jobs on the main CPU, so the ME group's timing is the PSP's to settle [D] (`media_engine.md`).

## With its own profile

The cache model, same stretch: w140 with `campaign-w140` (strict) runs 2,006 M cycles on a10 (-0.7% against b139) and 7,725 M on c10 (-7.3%). The rise is gone [E].

On the PSP, w140 with b139's profile, a keep-word A/B in one build, ticks 675-1350, mean of 2 runs [M]:

| set | a10 (PSP-3000) | c10 (PSP-1000) |
|---|---|---|
| as built | 8.15 ms | 18.23 ms |
| ME group off (`megraceoff meclaimold nomepoints`) | 8.13 ms | 18.18 ms |
| every pass-2 off word | 8.21 ms | 19.06 ms |

The ME group doesn't carry the rise on the PSP either, and the levers together save 0.7% on a10 and 4.6% on c10.

The same build with its own profile, the same keep words and the same PSPs, mean of 2 runs [M]: a10 7.52 ms a tick (-7.7%; frame 41.4 ms, -3.6%), c10 16.63 ms (-8.8%; frame 64.0 ms, -5.3%). That takes back the whole rise seen against b139 (+5-6% a10, +10-12% c10).

## The change

`pgo/campaign-w140.tgz`: w140 509ec38's own counts, 13 counting runs (the openings of a10, a30, a50, b40, c10, c20, c40, d20, d40, b30's flight and its three routes; the same recipe as campaign-b139), merged with `tools/pgo_merge.sh`. Build w140 with `PSP_PGO_PROFILE=campaign-w140` and `PSP_PGO_STRICT=1`.

No code changes and no keep word: the profile changes only how the same code is compiled. Every lever still has its off word.

## For the builds after this

Each merge into w140 makes the profile stale again for the functions it touches. Count again at the build's final commit, as every release does. `PSP_PGO_STRICT=1` refuses a mismatched link. Any A/B between two branches should be built either with each branch's own counts or with no profile on both sides; otherwise the side with stale counts loses by up to ~9% of its tick [E].
