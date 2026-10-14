# B13 findings: Christer Ericson, "Memory Optimization" (GDC 2003) + "Numerical Robustness for Geometric Calculations" (GDC 2005)

Part of the **Halo CE PSP: performance review checklist** research. Source: slides (.ppt) from realtimecollisiondetection.net/pubs, downloaded by Sheepdog01 Oct 13, 2026. Ericson was at SCE Santa Monica; both talks expand on chapters 11 and 13 of *Real-Time Collision Detection*.

## Memory Optimization (GDC 2003)

Most of this confirms tier 2 and B1/B2 from a second, independent source. What's worth recording:

### Framework: the 3 C's and the 3 R's
- Misses are **compulsory** (first touch), **capacity** (working set too big) or **conflict** (addresses mapping to the same cache set).
- Fixes: **Rearrange** (layout for locality: helps compulsory and conflict), **Reduce** (smaller formats: helps compulsory and capacity), **Reuse** (touch lines again while they're cached: helps capacity).
- **Useful for the dashboard:** tag each tier 2 change with which C it targets, and check that the counter it should move (D miss, D writeback) actually moves.

### Preloading = the stall-on-use question again
- Ericson's "preloading (pseudo-prefetch)" loop loads element *i+4* at the top of each iteration, labelled "cache miss, **non-blocking**," then processes elements already in registers. This only helps if the CPU supports **hit-under-miss** (keeps running until the missed value is used).
- That's exactly the open Allegrex question from B2/B8. The profiler-counter microbenchmark in `research_b8_yapspd.md` decides whether this pattern is worth using in posing and BSP loops.
- Software prefetch instructions (`pref`) are a MIPS IV feature; **whether Allegrex has one is unverified**. If it doesn't, preloading is the only option.

### Structures
- **Field reordering:** put fields used together next to each other (his example: `key` and `next` together, the big `count[]` array after).
- **Hot/cold split** with array-style allocation so the cold part needs no pointer (indexed by the same slot). Same as check 10.
- **Padding:** order fields largest to smallest. His example struct drops from 40 to 24 bytes just by reordering. **Cheap check for the hot Halo structs** (object, biped, actor headers): `pahole` on the PC build's debug info lists holes per struct.
- **Find hot fields by measurement:** log accesses per (struct, field) and per field pair to get activity and correlation. A field-level version of R1; cachegrind gives lines, not fields, so a small instrumented build may be needed for the biggest structs.

### Trees (checks 16, 17)
- Layouts: breadth-first (implicit children `2n`, `2n+1`; needs a complete tree), **depth-first (left child = next node; only the right child index is stored)**, van Emde Boas (cache-oblivious, recursive blocking).
- Size reduction: implicit pointers, quantized values, **values stored relative to the parent**.
- **Compact k-d node in 4 bytes:** a union of a float split value and an int leaf index, with the **node type in the low 2 bits** (x/y/z split or leaf). Stealing the float's lowest mantissa bits costs a tiny amount of precision. A direct template for check 17. **But see the robustness caution below before quantizing collision data.**

### Linearization caching
When data is reached through a traversal or indices, copy what you touch into a **linear buffer** and work from that, then reuse it next frame if it's still valid. Fits frame coherence (B2): e.g. the BSP leaves and surfaces near the player, gathered once and reused while the player stays in the same area.

### Allocation (check 22)
Pools, not the heap. **Free as soon as possible and reuse immediately**: the just-freed block is likely still in cache. A free list with first fit.

### Code cache (checks 39, 40)
Reorder functions (by hand within a file, by object file order at link, or `__attribute__((section("…")))`), compile for size where it helps, and watch for hidden calls such as **float-to-double conversions** (`fptodp`; ours would be `__extendsfdf2`, already in check 5).

### Aliasing (check 43; matches B2)
- **"Consume inputs before producing outputs":** his 2×2 matrix multiply goes from 16 reads to 8 by loading all inputs into locals first. Same lesson as Acton's load → compute → store.
- **Members are as bad as globals:** in `for (i = 0; i < numVals; i++) pBuf[i] = 0;` inside a class, `numVals` is re-read every iteration because `pBuf` might point at it. Copy members into locals before the loop. Decompiled code that loops over a struct's count field while writing through a pointer has exactly this pattern.
- **`const` does not prevent aliasing.** It only stops writes through that pointer.
- **`restrict` in practice:** you may need it on *both* pointers; two restricted pointers are trivially non-aliasing. Wrong use is silent: "incorrect use = slow painful death."

## Numerical Robustness (GDC 2005)

This matters for the port in two places: **R4/R5 (PC vs PSP divergence)** and **collision correctness if collision data is compacted (check 17)**.

### Floating point facts that bite ports
- Floats are **not associative**: `(a + b) + c != a + (b + c)`. Any change in evaluation order (compiler reordering, fused multiply-add, a VFPU rewrite) changes results.
- Spacing grows with magnitude: next float after 1,000 is +0.000061; after 1,000,000 it's +0.0625; after 10,000,000 it's +1.0. **A fixed epsilon is wrong at some scale.** Halo world coordinates are modest (world units of ~3 m), but epsilons copied from double-precision code or tuned for one scale break at another.
- NaN compares false to everything, so `if (a > b) X else Y` is not the same as `if (a <= b) Y else X`.
- **"Intel uses 80-bit format internally… gives different results in debug and release."** The original Xbox (x86, x87 FPU) computed intermediates at extended precision unless told otherwise. The Allegrex FPU is strictly 32-bit single precision.

### What this means for R4/R5 (differential testing)
- The PC reference build should compute floats the way the PSP does, as far as possible: **SSE scalar single precision, not x87** (`-mfpmath=sse` on 32-bit x86; the default on x86-64), **no `-ffast-math`**, and **`-ffp-contract=off` on both builds** so neither side fuses multiply-adds the other doesn't.
- Remaining divergence then comes mostly from **libm functions** (sin, cos, sqrt) and any **VFPU approximations** (B3). That makes R4's first mismatch easier to interpret.
- Decide R5 with this in mind: bit-exact PC↔PSP lockstep is realistic only if both sides avoid libm differences in gameplay paths (e.g. shared minimax polynomials, per the Remez note in B1).

### Collision robustness (checks 16, 17, 21, 32)
- Spatial structures (k-d trees, **BSP trees**, grids) have robustness failures on both **insertion** (a primitive lands on the wrong side of a split) and **query** (a ray slips through the crack between two triangles sharing an edge).
- Fixes: **thick (fat) planes** with a tolerance, **conservative insertion** (put a primitive in every cell it might touch, accounting for error), and **sharing calculations for shared edges** (his robust ray-triangle test evaluates each edge once in 3D, so two triangles sharing an edge can't both reject a ray that hits the edge).
- **Caution for check 17 and the B1 collision formats:** quantizing collision vertices or planes (e.g. the 6-byte 1.5.10 format) adds error. Bungie's BSP and collision code were tuned for the original float data. Any compaction must keep shared edges bit-identical between neighbouring triangles and be **tested for fall-through and snagging** on the worst places (ledges, thin walls, vehicle seams). This is gameplay-visible, so it goes through Sheepdog01's case-by-case evaluation.
- **Same caution for check 32** (splitting large polygons at cook time): new vertices on split edges must be computed identically for both pieces, or cracks appear in rendering (and in collision, if the same split ever reaches collision data).

### Tolerances
Use a **combined tolerance**: `abs(x - y) <= eps * max(1, abs(x), abs(y))`, absolute below 1, relative above. Or an integer ULP comparison (which needs a strict-aliasing-safe float-to-int reinterpretation; see B2). Exact integer arithmetic (cross-multiplying to avoid division) for yes/no tests where inputs are integers.

## Proposed additions

| Proposed | Tier | Check | Source |
|---|---|---|---|
| N | 2 | Struct padding audit of hot structs with `pahole` (largest-first field order) | Memory Optimization |
| O | 2 | Copy struct/global counts and pointers into locals before loops that write through other pointers | Memory Optimization |
| P | 1 | PC reference build float settings matched to PSP for R4: SSE single precision, no fast-math, `-ffp-contract=off` both sides | Numerical Robustness |
| Q | 2/3 | Any collision or BSP compaction keeps shared edges identical and passes a fall-through/snag test on worst-case geometry | Numerical Robustness |
| — | Unverified | Allegrex prefetch instruction (`pref`): present or not? | Memory Optimization |
