# B2 findings: cellperformance.com (Mike Acton, 2006–2009)

Part of the **Halo CE PSP: performance review checklist** research (check numbers 1–53, backlog R1–R12). This file is the single place for B2 results.

**Finding the site:** in about 2008 cellperformance.com moved to `cellperformance.beyond3d.com/articles/`, and the URLs changed (`mike_acton/` was dropped, underscores became hyphens). The Wayback captures of the old addresses are just redirects. Use `https://web.archive.org/web/2010/<new URL>`.

**Status:** 9 of about 12 relevant articles read. Remaining: Branch Patterns Using GCC, A Practical GCC Trick, Increment And Decrement Wrapping Values (all lower value).

## Article index (from the captured sidebar)

| Article | URL path (`/articles/…`) | Relevance |
|---|---|---|
| Understanding Strict Aliasing | 2006/06/understanding-strict-aliasing.html | **Read** (check 43) |
| Demystifying The Restrict Keyword | 2006/05/demystifying-the-restrict-keyword.html | **Read** (43)  |
| Performance and Good Data Design | 2006/04/performance-and-good-data-design.html | **Read** (10–13)  |
| Background on Branching | 2006/04/background-on-branching.html | **Read** (44)  |
| Benefits to Branch Elimination | 2006/04/benefits-to-branch-elimination.html | **Read** (44)  |
| Reducing The Costs Of Comparisons and Branches | 2006/04/reducing-the-costs-of-comparisons-and-branches.html | **Read** (44)  |
| Using masks to accelerate integer code | 2006/04/using-masks-to-accelerate-integer-code.html | **Read** (44)  |
| More Techniques for Eliminating Branches | 2006/04/more-techniques-for-eliminating-branches.html | **Read** (44)  |
| Branch Patterns, Using GCC | 2006/04/branch-patterns-using-gcc.html | Medium (44) |
| A Practical GCC Trick To Use During Optimization | 2006/04/a-practical-gcc-trick-to-use-during-optimization.html | Medium (39–44) |
| Box Overlap | 2006/06/box-overlap.html | **Read** (collision, 16–21)  |
| Increment And Decrement Wrapping Values | 2006/07/increment-and-decrement-wrapping-values.html | Low–medium |
| Roundup: concurrency, data design and performance | 2009/08/roundup-recent-sketches-on-concurrency-data-design-and-performance.html | Low–medium |
| Branch-free half-precision float | (sidebar only) | Low |
| Skip: Avoiding Microcoded Instructions On The PPU, Altivec example, atan2 on SPU, 4x4 Matrix Inverse, PS3 Linux posts, Utility: match, Open Source and Console Games, Three Big Lies | — | PPU/SPU-specific or covered |

## Understanding Strict Aliasing (June 2006, updated Dec 2006)

**What strict aliasing is:** the compiler assumes pointers to different types never point at the same memory. GCC turns it on at `-O2` and above. Exceptions: types differing only in sign or qualifiers (`const`, `volatile`), a struct or union containing a compatible member, and `char*`, which may alias anything (but not the reverse).

**The cost of turning it off (`-fno-strict-aliasing`):** Acton's example loop `values[i] += uniform->b` reloads `uniform->b` on **every iteration**, because the compiler can't rule out that the store to `values[i]` changed it. With strict aliasing on, the load moves out of the loop.

**Correction to an earlier claim in this conversation:** that reload usually **hits the cache** (it's the same line every time), so it's mostly **not** a 39–72-cycle miss. The real costs on Allegrex are extra instructions in the loop and, more importantly, **lost scheduling freedom**: the compiler can't move loads above stores, which is exactly what check 44 (issue loads early on an in-order core) needs.

**Why flipping the flag globally is risky for a decomp:**
- Code that type-puns (casts a pointer to a different type and dereferences it) has **undefined behavior** under strict aliasing. Decompiled Xbox code and tag-data handling very likely do this.
- Acton shows the broken result **changes with inlining, with whether a loop exists after early optimization, with constant vs. variable loop counts, and even between 32- and 64-bit builds of the same GCC version**.
- `-Wstrict-aliasing` and `-Wstrict-aliasing=2` miss real cases (e.g. a union of two pointer types) and also give false positives. Warnings are hints, not proof.

**Safe ways to reinterpret data:** copy through a union by value (well supported by GCC); cast to a pointer to a union/struct that contains a compatible member; access as `char*`. Not safe: a union of *pointers* to different types; casting `char*` to another type and dereferencing.

**Acton's verdict on `-fno-strict-aliasing`:** "a very good indication that the code relies on aliased memory access and is likely to be dominated by poor memory access patterns." Keep it to the fewest files possible.

### What this means for the port (check 43)

1. **Don't flip the global flag.** Keep `-fno-strict-aliasing` as the default.
2. **Cheapest fix, works under any flag:** in hot loops, copy loop-invariant values from memory into local variables before the loop. The compiler can then keep them in registers. Zero risk, no flags.
3. **`restrict`** on the hot loop's pointer parameters tells the compiler those pointers don't overlap. In GCC it's a separate mechanism from type-based aliasing, so it should still take effect under `-fno-strict-aliasing`. **Verify in the disassembly** (loads hoisted out of the loop or moved above stores). Acton's restrict article is next and should confirm.
4. **Per-file strict aliasing**, only for hot modules after an audit with `-Wstrict-aliasing`, as a later step.
5. **Test discipline from the article:** check generated code, not just warnings; test functions both inlined and not inlined; test with constant and variable loop counts. **R4 differential testing** (same input, PC vs PSP, compare state each tick) is the safety net, because aliasing bugs show up as divergence between differently optimized builds.

## Demystifying The Restrict Keyword (May 2006)

- `restrict` is a **promise** to the compiler: loads and stores through this pointer (and its copies) are the only accesses to those addresses while it's in scope. Break the promise and the result is undefined, **with no warning**.
- **What it buys:** the compiler can keep values in registers and **reorder loads above stores**. Acton's particle `move()` (velocity, position, acceleration arrays): without `restrict`, every position load waits for the velocity store before it. With restricted pointers, all loads are issued first and the stores go at the end, matching the best case (separate global arrays).
- **Restrict the leaves, not the parent.** He splits each array into per-field restricted pointers (`velocity_x`, `velocity_y`…, nine "stripes"). Once you make child pointers, don't touch the parent in that scope. A child can't be declared in the same block as its parent (undefined).
- **Overlapping memory can still be non-aliased.** Fields interleaved inside one struct array (position, velocity, acceleration in a `particle`) are valid restricted stripes with a stride. Checking whether address ranges overlap is not the test; whether the same *bytes* are accessed is.
- **Put `restrict` in function prototypes** so callers can see the contract. (C99's `memcpy` is declared that way for this reason.)
- **Acton's advice:** make sure code compiles safely with strict aliasing before adding `restrict`. For our port, `restrict` on a few hot leaf functions is still the narrower risk than flipping the global flag; **verify each one in the disassembly**.
- **GCC scheduling tip, the most portable lesson:** "Do not mix memory access with calculations." Write hot loops as **load everything into `const` locals → compute → store**. GCC then issues loads early instead of loading right before each use.

## Performance and Good Data Design (April 2006)

The philosophy piece behind tier 2:
- **Know the data and its access patterns.** Log real data as it moves through the pipeline and look at it (even hex dumps). Don't optimize a loop without knowing what data it actually sees. → supports **R2 (content survey)** before layout changes.
- **Be prepared to reorganize data** late; the cost of not changing it is higher.
- **Sort by the dominant type** (the data change that would flush the most pipeline).
- **Separate read-only, write-only and read-write data.** RO and WO are far easier to lay out and to share between processors. → check 10 and the CPU↔ME split.
- **Almost everything belongs to a set** ("where there's one there's many").
- **Use frame-to-frame coherence**: much data is identical or close between frames. Look for unexpected cases at runtime.

## The branch series (Background; Benefits; Reducing Costs; Masks; More Techniques)

**Why eliminate branches, even well-predicted ones:**
- Each branch splits code into basic blocks; compilers schedule well **inside** a block and poorly across blocks. Fewer branches → loads can be grouped and hoisted (**load → update → store**).
- Branch-free code has **fixed timing**, which makes profiling cleaner: the remaining variation is cache behavior.
- **Easier to test:** every input takes the same path, so tests exercise all the logic.
- Easier to move to SIMD (for us, the VFPU).
- Downside: harder to read. Fix with well-named helper functions (`cmpnz_u32`, `sel_u32`).

**Techniques:**
- **Combine conditions before branching:** compute `t = (a & X) | (b & Y)` and branch once on `t`, rather than `if (a & X || b & Y)`.
- **Integer masks:** "not zero" mask = `(uint32_t)((int32_t)(x | -x) >> 31)`; select = `(a & mask) | (b & ~mask)`. Use signed types for math, unsigned for bit work, and cast back.
- **Step functions** (`if x < x1 … else if x < x2 …`) become a chain of sign-bit masks and selects.
- **Predicated read:** select between the value and a zero, then add unconditionally.
- **Predicated write:** select the destination address between the real field and a junk variable on the stack (which is almost surely in cache), then always write.
- Too many register-to-register moves in the disassembly is a hint that something is blocking optimization.
- Inline small functions.

### PSP translation for the branch material (check 44)

Acton wrote for PowerPC (condition register, `fsel`, no conditional move on the integer unit). The Allegrex is MIPS, so some of the pain he describes doesn't exist and some tools differ:
- **No condition register on MIPS.** `slt`/`sltu` write 0 or 1 into an ordinary register, so `-(a < b)` gives a mask in two instructions. Acton's CR-contention warnings don't apply.
- **Allegrex extras worth checking the compiler uses:** conditional moves (`movz`/`movn`) and integer `min`/`max`. *(Believed present on Allegrex; verify against the pspdev Allegrex instruction docs and look for them in our disassembly.)* If GCC emits them, many integer selects are already branch-free.
- **Float selects:** the scalar FPU has no `fsel`, but the **VFPU** has compare and conditional-move ops, and min/max. *(Verify exact instruction names in the pspdev VFPU docs.)*
- **The scheduling benefit is the part that transfers fully.** On an in-order core, branch-free hot loops let the compiler hoist loads.
- **Open question that decides how much hoisting helps misses (add to "Still unverified"):** does the Allegrex stall **when a load misses** or only **when the loaded register is first used** (non-blocking loads)? If it stalls on use, issuing loads early hides part of the 39–72-cycle miss. If it stalls on miss, early loads only hide the ~3-cycle hit latency, and data layout (tier 2) matters even more relative to scheduling. A microbenchmark on the PSP-1000 answers this: two independent missing loads back-to-back vs. separated by ALU work.

## Box Overlap (June 2006)

A worked example of everything above on a 3D AABB (axis-aligned bounding box) overlap test, the core of broad-phase collision:
1. Original: field accesses mixed into the `&&` chain → nearly every instruction **waits on a load**, and the compiler hides loads behind branches it predicts unlikely.
2. **Load all 12 floats into `const` locals first, then compute, then compare.** Alone this helped little, because GCC re-interleaved loads and math.
3. **`GCC_SPLIT_BLOCK`** (`__asm__("")`) between the load section and the math section forced the loads to be issued first. *(2006-era GCC behavior; a plain empty asm may not act as a scheduling barrier in our GCC. Verify in disassembly before relying on it.)*
4. Replace the three compare-and-branches with selects (`fsel`) on `extent_sum - |center_delta|`: one result, no branches.
5. Then SIMD: test one box against **four boxes at once** with the four boxes stored structure-of-arrays (all x's together, all y's together…). Absolute value via clearing the sign bit.

**For Halo:** a direct template for the broad-phase and bounds tests in collision and culling (checks 16–21, 33): locals-first, branch-free compare, then a VFPU 4-wide version with SoA bounds. The SoA layout is a cook-time decision (check 36).

## Proposed additions from B2

| Proposed | Tier | Check | Source |
|---|---|---|---|
| J | 4 | Hot loops written as load-to-locals → compute → store; verify loads issue first in disassembly | Restrict, Box Overlap |
| K | 4 | `restrict` on hot leaf functions' pointer parameters, published in prototypes; each verified in disassembly and covered by R4 | Restrict |
| L | 4 | Branch-free selects (masks, conditional moves, min/max, VFPU selects) in hot inner loops; combine conditions before any remaining branch | Branch series |
| M | 3/4 | Bounds tests (AABB, sphere) branch-free and 4-wide on the VFPU over cook-time SoA bounds | Box Overlap |
| — | Unverified | Allegrex: stall on miss vs. stall on use (decides the value of J and L for misses) | Branch series |

Fold into existing rows: **43** (local copies first, then `restrict`, per-file strict aliasing last), **44** (now split into J and L), **R2** (look at real data before layout changes).
