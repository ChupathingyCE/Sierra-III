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

**How to read these (the lead, 2026-11-21 02:0x):** the DMAC moves the data while the ME and the VME do other work, so a transfer's length is latency, hidden by double-buffering batches, not ME time. The ledger is:
- **The ME's time saved** = the verdicts' ~3.0 ms (b30's ride) or ~4.65 ms (a30's peak), less what the ME still pays: the DMAC's setup a batch, the float-to-fixed conversion (zero with the bake below), and the band's recomputation in float (from `nearband`).
- **Checked, not charged:** the transfers' length (~1.5 ms in and ~1 ms out at a30's peak through eDRAM, ~0.5 ms in by the fast mode) against the frame's slack, to make sure the double-buffering can hide it.
- **Priced only by a PSP pair:** the transfers' real cost, contention for main RAM with the main CPU and the GE.

As staged today, the float-to-fixed conversion alone is of the same order as the dot products it would save, so the kernel waits on the bake.

## 4. What would make it pay, and the counts it waits on

1. **A fixed-point copy of the near parts' positions, made at the bake:** the kernel then reads them straight and the ME converts nothing. It costs memory (12 bytes a vertex, near-drawn skinned parts only), to be sized against the PSP-1000's room.
   **Sized (2026-11-21) [E, recon `nearband`; the lab's findings `nearbake_size`]:** b30 84 skinned parts drawn near, 29,906 vertices, a copy of 350 KB; a30 64 parts, 30,019 vertices, 351 KB; c20 28 parts, 7,895 vertices, 92 KB. An upper bound: a part reloaded at another address counts twice. Against the heap [M]: b30's flight has a heap free low of 1,406 KB and b40's drive has 1.4 MB untaken, so it fits where measured. The PSP-1000's worst map's low isn't filed yet. **Another form [U]:** the near parts' positions stored as 24-bit fixed in their records in place of the floats (the same 12 bytes, no added heap), read by the ME's float path through a fixed-to-float. That changes every near part's skinning path, so it's to be priced, not assumed.
2. **The band:** how many corners the ME would recompute in float at each fixed-point scale. Recon `nearband` (this branch) counts each verdict's least distance to a tested plane (2^-6..2^-14 units) and against its largest slot term (2^-10..2^-18 of it), plus the weight sums.
   **Counted (2026-11-21) [E, recon `nearband`; the lab's findings `nearband_b30`, `nearband_a30`]:** against each verdict's largest slot term, which is what a reordered or fixed-point sum errs by, the corners within 2^-18 of it are 0.0-0.1 a frame at every window of b30 and a30, and within 2^-14 at most 0.7 (b30's ride, a30's peak). Absolute distances are wider (a30's peak: 57.8 corners within 2^-10 units, 3.0 within 2^-14), but that isn't the error's measure. The byte weights sum to exactly 128 on every corner, so the planes' affine term never moves. The largest slot term is 17.5 units and the deepest corner 15.5, so a 24-bit fixed point with ~5 integer bits (±32 units, an LSB of 2^-18) holds them. **So the ME would recompute in float under one corner a frame [U on the kernel's own error, a few LSBs]: the band is small, and condition 2 holds.**
3. **The fast mode's miss cost on the PSP-1000:** measured on the PSP-3000 only.

If (1) doesn't fit the PSP-1000's memory, or (2) puts most corners in the band, the box stops here.

## 5. The near path's code (codediet's read, taken into this box)

The near paths sit inline in the part's body, so every part that isn't near steps over them: the rigid near block (~180 lines) and the skinned near loop (~370 lines) in `psp_ge.c`. Moved into functions of their own (the same calls, order and floats), a far part's replay would stop touching those lines. The loop also stores to counters a corner or a triangle (near_cluster_*, probe_corners, rigid_runs_*, near_triangles_dropped). Both are this box's to take or leave when its code is written (codediet's worker read, lab or-job 44875882292 [D, unchecked]).
