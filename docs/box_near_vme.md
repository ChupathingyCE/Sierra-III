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
   **Counted (2026-11-21) [E, recon `nearband`; the lab's findings `nearband_b30`, `nearband_a30`]:** against each verdict's largest slot term, which is what a reordered or fixed-point sum errs by, the corners within 2^-18 of it are 0.0-0.1 a frame at every window of b30 and a30, and within 2^-14 at most 0.7 (b30's ride, a30's peak). Absolute distances are wider (a30's peak: 57.8 corners within 2^-10 units, 3.0 within 2^-14), but that isn't the error's measure. The byte weights sum to exactly 128 on every corner, so the planes' affine term never moves. The largest slot term is 17.5 units and the deepest corner 15.5, so a 24-bit fixed point with ~5 integer bits (±32 units, an LSB of 2^-18) holds them. **So the ME would recompute in float under one corner a frame [U on the kernel's own error, a few LSBs]: the band is small, and condition 2 holds.** *Superseded 2026-11-21 (§6.3): the kernel's own error is not a few LSBs but its inputs' rounding. With sums that need only the 32 bits measured on the PSP (safe32) it is up to 2^-10.45 units, so the band is 2^-10 units (25.4 corners a frame on b30's ride, 57.8 at a30's peak, 1-1.4% of the verdicts). With a 45-bit accumulator (wide, unmeasured) it is up to 2^-15.47 of the largest slot term, so the band is 2^-14 of it (0.6-0.7 a frame). Condition 2 holds either way.*
3. **The fast mode's miss cost on the PSP-1000:** measured on the PSP-3000 only.

If (1) doesn't fit the PSP-1000's memory, or (2) puts most corners in the band, the box stops here.

## 5. The near path's code (codediet's read, taken into this box)

The near paths sit inline in the part's body, so every part that isn't near steps over them: the rigid near block (~180 lines) and the skinned near loop (~370 lines) in `psp_ge.c`. Moved into functions of their own (the same calls, order and floats), a far part's replay would stop touching those lines. The loop also stores to counters a corner or a triangle (near_cluster_*, probe_corners, rigid_runs_*, near_triangles_dropped). Both are this box's to take or leave when its code is written (codediet's worker read, lab or-job 44875882292 [D, unchecked]).

## 6. The kernel, proven in vmee (2026-11-21) [E: Th6uD1nk's vmee; the lab's findings `nearvme_kernel_vmee`]

`psp/src/vme/near_verdicts.vmeg.c` (vmeg), `psp/src/vme/near_verdicts.c` (its C, from vmegc `--compat fat`), `tools/nearvme_ref.c` (the same arithmetic on the host), `tools/nearvme_check.py` (the proof). The game's near loop is untouched: the ME's hand-off is the next step.

### 6.1 Its shape

- **Five plane rows a slot, all carried by the kernel** (the lead's correction, 05:0x): Z = z, then the four sides normalized as the census measures them, (g_x z ∓ x)/√(1+g_x²) and (g_y z ∓ y)/√(1+g_y²). The near pair shares the Z row: the byte weights sum to 128 on every corner (§4), so Σ w_s (z_s − c) = Σ w_s z_s − c, and the ME tests the one value against `limit` and against `NEAR_CLIP` with two compares and no arithmetic. **Why not three rows (x, y, z) with the ME deriving the sides:** each side would then cost the ME a multiply-add a corner (four a corner), the work this box takes away, and g·z multiplies z's error by g (4.2 at 6 screens, ~10 with `guardwide`), so the sides would err 4-10 times as much. Five rows cost the VME 5/3 of the stream instead, which runs beside the ME.
- **Two stages, each the batched 4x4's accumulate.** The form is Th6uD1nk's `vmegc-mat4x4-mul-batch` (vme2-next-level): pe0 `mac`, pe1 the cancel mask (`&`), pe2 `acc` of the masked values, pe3 subtracting the running sum's delayed copy and then that from the delayed MAC (`subacst` twice). No accumulator is ever reset: each result is a difference of running sums, so a carry across groups cancels.
  - **Stage 1** (`nearDots`): for each vertex, each row, each slot, the 4-wide dot product of the slot's plane row with the vertex's (x, y, z, w). Stream order (vertex, row, slot, component): the rows are one ring of 4 · 5 · G words, the vertex repeated 5 · G times by the DMAC's group mode (his sample's way), one result on each group's last word in base3.
  - **Stage 2** (`nearSum`): for each (vertex, row), Σ_s w_s · d_s over the batch's G slots, with the weights repeated 5 times by the DMAC. Stage 1's results are read where they lie, in base3, one word in four (a mode-2 ring with step 4), and the plane values go after them in base3.
  - **G, the slot group:** 4 for batches of 1-4 slots, 8 for 5-8, unused slots with rows and weights of 0. Below 4 the delayed copy's sync read can't see its data (it needs 6 − cyc + bubbles ≥ 7; bubbles are G − 3).
- **Opcodes:** 0x240 (`mac`), 0x0c0 (`&`), 0x250 (`acc`), 0x034 (`subacst`). None is among the 13 that fail on the PSP-1000 (`vme_capability.md` §2), and none is one `--compat fat` replaces.
- **AGUs:** every read is mode 2 (the rings, the strided read, and the flat reads as inner0 without repeat), since the PSP-1000 matched vmee in 21 of 21 mode-2 kernels (`vme_agu_modes_psp1000`). Every write is mode 4, as in his batched kernel that was right 16 of 16 on the PSP-1000: mode 2's write AGU is unmeasured. Mode 4 isn't used for a read anywhere.
- **Lead-ins read zeros that the kernel loads** (`nearZero`), not the tail of top0, which on a PSP holds whatever the last tile left.
- **Tiles:** both stages' streams share base3's 2,048 words. That fits 19 vertices a tile at G = 4 (stage 1 1,600 words, stage 2 396) and 9 at G = 8. A batch is cut into tiles, the last padded with vertices of weight 0.

### 6.2 The scales

All results are at 2^-18 units (24 bits: ±32 units), as the census asked (§4).

| | positions (x, y, z) | rotation coefficients | translation | w word | products | k1 | stage 2 | needs an accumulator of |
|---|---|---|---|---|---|---|---|---|
| **safe32** (the build's) | 2^-11 (±4,096) | 2^-15 (±256) | 2^-17 | 2^9 | 2^-26 | 8 | w (128ths) × d: 2^-25, k2 = 7 | **24 + k = 32 bits** |
| wide | 2^-18 (±32) | 2^-21 (±4) | 2^-17 | 2^22 | 2^-39 | 21 | the same | 24 + 21 = 45 bits |

- **Why these:** the running sums are never reset, so a group's result is right only if the accumulator keeps the low 24 + k bits of its running sum: floor(S/2^k) mod 2^24 depends on no higher bit. The lab measured the MAC's sum at least 32 bits wide, floored by its shift (`vme_mac_shift`, the PSP-3000 only); past that it's unmeasured, and vmee models it unbounded. So k ≤ 8 asks nothing the PSP hasn't shown, and the dot product's 26 fraction bits are split between positions and coefficients to minimize the rounding error: p = 11, q = 15 (with p = 10, 12 or 13 the largest error came out the same or worse, the comparison in §6.3).
- **What the PSP must still show** (one PSP-1000 pair, before the switch-over): that the accumulator wraps rather than saturates when a long tile's running sum passes 2^31 (both scale sets need it, and so does his cancel form with any long stream), and for `wide` that it holds 45 bits. Also, a PSP's accumulator carries across passes (vme4r), where vmee's starts at 0, so a hardware result may differ from vmee's by 1 LSB (2^-18 units, the floor of a different running sum). The band covers that.
- **The bake:** a part's positions at 2^-11 (or 2^-p, chosen by the part's extent, with q = 26 − p for its slots' coefficients) are the fixed-point copy §4 sized.

### 6.3 The proof

- **Word for word:** vmee (`--compat fat`) against `tools/nearvme_ref.c`, which keeps the same running sums from 0, the same floors and the same 24-bit words. Stage 1's words and stage 2's plane words both count. **46 tiles, 21,010 words, 0 differ, at each scale set**: 24 synthetic tiles (3 for each slot count 1-8, positions anywhere within ±16 units, any rotation, weights summing to 128 with some 0, half of them at `guardwide`'s g = 10) and 4 realistic batches (60, 84, 100 and 120 vertices with 1-4 slots, a part ±3 units across, its bones near each other, out to 14.5 units deep), 22 tiles. **A negative control** (the cancel mask moved one word): 2,686 of 3,520 words differ, so the comparison does catch a wrong kernel.
- **The setup was checked first:** his batched mat4x4 under the same vmee gave M·v for 64 of 64 values, as the PSP-1000 did.
- **Error against a double-precision skinning of the unrounded inputs**, in units and relative to the vertex's largest slot term (the census's measure):

| | largest error | relative to the largest slot term | the arithmetic alone (rounded inputs) |
|---|---|---|---|
| safe32, all 46 tiles | 7.16e-4 units (2^-10.45) | 2^-10.87 (realistic), 2^-11.43 (synthetic) | 6.6e-6 units |
| safe32, a second set (30 tiles) | 6.96e-4 (2^-10.49) | **2^-9.05** (realistic: a vertex whose largest term was small) | 6.4e-6 |
| wide, all 46 tiles | 9.58e-6 units (2^-16.67) | 2^-15.47 (realistic), 2^-16.50 (synthetic) | 5.3e-6 |

- **What that means for the band:** at safe32 the error is the inputs' rounding (the arithmetic alone is ~100 times smaller). It follows the coefficients and positions, not the term, so it is bounded in units rather than relative to the term (2^-9.05 of a small term). **The ME's band at safe32 is 2^-10 units**, absolute: 25.4 corners a frame on b30's ride, 57.8 at a30's peak (§4's census). At `wide` the band is 2^-14 of the largest term (0.6-0.7 a frame), and 2^-16 doesn't cover it.
- **Splits tried** (safe32, the 30-tile set, p,q: largest error, relative): 10,16 2^-10.51, 2^-8.28; 11,15 2^-10.49, 2^-9.05; 12,14 2^-10.16, 2^-9.92; 13,13 2^-9.39, 2^-9.80.

### 6.4 Its cost [E: vmee's steps; U on M rates]

- **vmee's passes,** a tile: stage 1 1,617 steps and stage 2 413 (G = 4, 19 vertices) or 393 (G = 8, 9 vertices). At the PSP's measured 72 + 2n cycles a pass (`vme_capability.md` §1) that's ~3,300 + ~900 VME cycles a tile: **~220 VME cycles a vertex at G = 4, ~460 at G = 8**, beside the ME.
- **The staging is bigger than §3 priced, because the vertex goes in 5 · G times:** a G = 4 tile takes ~2,000 words in (1,520 of them the vertices' 20 copies, 380 weights, 80 rows, the zeros and the mask) and 31 DMAC transfers (51 at G = 8). §3 priced each record once. At ~100 ME cycles of register writes a transfer (`vme_kernels.md` §2) that's ~3,100 ME cycles a G = 4 tile, **~160 ME cycles a vertex against the float verdict's ~390** (b30's ride: ~3.0 ms for 2,564 verdicts). From eDRAM in the fast mode (2.96 cycles a word, off the bus) the words take ~6,000 DMAC cycles a tile. The buffers can't be fed during a pass, so that is VME-side time, not hidden. The repeated vertex is the shape's: an AGU can't replay one vertex for 20 groups and then move on. Fewer transfers (a vertex ring with the rows repeated instead needs a two-level read for stage 2) is the next thing to design before the hand-off.
- **Dink's vmeh** generates the PSP runner for the `tile` emulation, but its program sets the clocks with `scePowerSetClockFrequency` (`psppower.h`). It isn't in this repo and isn't run: a PSP check of this kernel would take that call out (no power calls; Sheepdog01 2026-11-17).

### 6.5 Dink's work it uses

Th6uD1nk's vme2-next-level (MIT, copyright m-c/d, Th6uD1nk; `psp/src/vme/LICENSE_vme2-next-level.md`): the vmeg language and vmegc, which wrote `near_verdicts.c`; vmee, the proof's emulator; and his batched mat4x4's cancel-masked accumulate, the form of both stages, marked in the kernel's header. The rest is Sierra-III's. The toolkit was run from a copy in a folder of its own, with `python3 -I`. **One local change to how it ran:** vmee builds its host trace program with `g++ -m32`, and the machine had no 32-bit libraries, so a wrapper dropped `-m32` (`-no-pie` keeps the static arrays under 4 GB; the program only records register writes for vmee's Python model). His mat4x4 under that build gave 64 of 64, as above.
