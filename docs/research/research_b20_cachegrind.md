# B20 resolved: cachegrind cache-geometry limits (for R1)

Part of the **Halo CE PSP: performance review checklist** research. B20 asked for Valgrind's `cg_arch.c` to learn which cache geometries cachegrind accepts. The source host (sourceware.org) is blocked from the research environment, but **Valgrind 3.22.0 is installed there**, so the limits were tested directly (Oct 13, 2026).

**Superseded for the port (2026-10-14, `../research_application.md` §4):** cachegrind models x86 code, so its instruction-cache results don't transfer; the PPSSPP cache model replaces it.

## Result

`valgrind --tool=cachegrind --cache-sim=yes --I1=16384,2,64 --D1=16384,2,64 --LL=16384,2,64 <program>` runs and reports:

```
Cache configuration used:
  I1: 16,384 B, 2-way, 64 B lines
  D1: 16,384 B, 2-way, 64 B lines
  LL: 16,384 B, 2-way, 64 B lines
```

- The "L3 cache found, using its data" warnings print first but are **overridden** by the explicit options, as the "configuration used" block shows.
- `--LL` as small as the D1 size is accepted. 8 KB and 1-way LL also ran without errors.
- **`--cache-sim=yes` is required** (cache simulation is off by default since Valgrind 3.21).

## How to set up R1 with this

- **PSP geometry:** I1 and D1 = 16 KB, 64-byte lines. **Associativity 2-way** is the reading from Daedalus's cache code (B4: 8 KB per way) and needs confirming; run R1 at 2-way and 4-way and treat lines that rank as hot in both as the real hotspots.
- **No L2 on the PSP:** read **D1 misses** as the PSP's main-RAM misses. Ignore the LL columns. Cachegrind's LL is unified (instructions + data), so setting it equal to D1 doesn't model anything real; it just keeps the LL numbers out of the way.
- **Limits to keep in mind:** cachegrind does not count **writebacks** of dirty lines (the PSP's `D cache wb` counter has no equivalent), and it models an x86 PC build's code, so instruction-cache results don't transfer. Use R1 for **D1 read/write misses per source line**, and confirm totals against the PSP profiler counters (B8).
