# Box: the near loop's verdicts on the VME (near-vme)

Listed in the lab's REBUILD.md (`[box:near-vme]`, the lead 2026-11-20). This page is its first step: the shape of a kernel, what staging it would cost, and what has to be true before a kernel is worth building.

## 1. What it would take off the ME

The near corner loop (`model_geometry`'s skinned near path) works out each corner's verdict once a batch: the corner skinned into camera space, then tested against five planes (the near test's limit and `NEAR_CLIP`, and the side planes x = ±guard_x·z, y = ±guard_y·z). On the PSP that per-corner part is about half the loop [M, one run each, recon `nearsteps`]:

- b30's ride: the loop 5.88 ms a frame, verdicts 50% (~3.0 ms), cuts 15%, the combine and the rest 35%.
- a30's peak window: the loop 8.31 ms, verdicts 56% (~4.65 ms), cuts 13%, the rest 31%.

The GE's own near clip would take only the cuts' GE-clippable share (under 1 ms), so it stays unbuilt.

## 2. The kernel's shape

A skinned corner is Σ_s w_s·(M_s v) over the batch's bone slots, with the byte weights w_s = record[s]/128 and v carrying w = 1. Each plane P is linear (affine) in camera space, so P·(Σ_s w_s M_s v) = Σ_s w_s·((P M_s)·v) exactly. Per batch the ME would multiply the five plane rows by each slot's matrix, and the VME would form each vertex's five values as a weighted sum of 4-wide dot products: the batched 4x4 shape (Dink's mat4x4 batch, right for 16 of 16 vectors on the PSP-1000) plus multiply-accumulate. It needs none of the 13 opcodes that fail on the PSP-1000 (lab `vme_kernels.md` §6). The ME keeps the sign tests, the recomputation in float of corners in a band around a plane, and the combine by index.

**The band** (the lead 2026-11-21 01:3x): a sum in a new order, and 24-bit fixed-point products, err by about the size of the terms (each slot's w_s·P_s·v, which can cancel), not of the result. So the band is chosen against each verdict's largest slot term, not only its distance. Corners whose byte weights don't sum to 128 move the planes' affine term with the sum. Recon `nearband` counts both (§4).

## 3. Staging, priced before any kernel [D on the records, M on the rates, U on the totals]

- **Bytes in:** a skinned record is 28 or 32 bytes (`part->stride`: 4 or 8 weight bytes, 8 of UV, 4, then the position's 12). At a30's peak, ~4,190 verdicts a frame means ~134 KB a frame. b30's ride, ~2,560, means ~82 KB. The slot matrices add ~47 loops × up to 8 slots × 48 bytes, about 18 KB at a30's peak.
- **Bytes out:** five plane values a corner, ~20 bytes, ~84 KB a frame at a30's peak.
- **The rates** (`hardware_facts.md`):
  - the VME's DMAC from main RAM, 28-36 MB/s at no cost to the CPU's misses: ~4.2 ms for a30's 134 KB in, about the whole 4.65 ms the verdicts cost
  - through eDRAM, staged: 88 MB/s, ~1.5 ms in and ~1 ms out
  - the fast mode (0x310): 262 MB/s, ~0.5 ms in, but it raises a main-CPU miss ×1.33 (measured on the PSP-3000 only)
- **The conversion:** the records hold the position as three floats, and the VME works in 24-bit fixed point. The ME would convert each corner's three floats before the VME could read them: per corner, work of the same order as the dot products it saves.

**So, as staged today, the hand-off would eat most of the gain.** The budget is the verdicts off the ME's CPU with the hand-off under 1 ms at a30's peak. The fast mode alone gets the bytes near that, but not the conversion.

## 4. What would make it pay, and the counts it waits on

1. **A fixed-point copy of the near parts' positions, made at the bake:** the kernel then reads them straight and the ME converts nothing. It costs memory (12 bytes a vertex, near-drawn skinned parts only), to be sized against the PSP-1000's room.
2. **The band:** how many corners the ME would recompute in float at each fixed-point scale. Recon `nearband` (this branch) counts each verdict's least distance to a tested plane (2^-6..2^-14 units) and against its largest slot term (2^-10..2^-18 of it), plus the weight sums.
3. **The fast mode's miss cost on the PSP-1000:** measured on the PSP-3000 only.

If (1) doesn't fit the PSP-1000's memory, or (2) puts most corners in the band, the box stops here.

## 5. The near path's code (codediet's read, taken into this box)

The near paths sit inline in the part's body, so every part that isn't near steps over them: the rigid near block (~180 lines) and the skinned near loop (~370 lines) in `psp_ge.c`. Moved into functions of their own (the same calls, order and floats), a far part's replay would stop touching those lines. The loop also stores to counters a corner or a triangle (near_cluster_*, probe_corners, rigid_runs_*, near_triangles_dropped). Both are this box's to take or leave when its code is written (codediet's worker read, lab or-job 44875882292 [D, unchecked]).
