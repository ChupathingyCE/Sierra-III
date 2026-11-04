# What the VME can carry, and at what cost (C5)

The VME is the Media Engine's reconfigurable vector engine. This page reports a bench round on both test PSPs. All figures are in ME cycles (the ME's counter ran at 331.4 a µs on both) and labelled [M] measured on the PSP, [D] read in code or notes, [U] inference. The bench is `bench/halo_bench15_vmecap` on branch `recon/vme-capability`. The findings rows are `vme_*` in the lab's `measurements.tsv`.

**In short:** the VME computes at **2 operations a cycle across its 4 processing elements**, 17–32× the ME's own CPU on long streams. It does so beside the ME's CPU, which runs freely during a pass. It has **16,384 words of 24 bits** (48 KB). Three things limit it:

- **Set-up:** writing a context costs ~709 cycles. A pass's own overhead is 72 cycles.
- **Moving the data:** inbound transfers cost 9–38 cycles a word, against 2 cycles an element of compute.
- **No lookup operation exists,** and 13 of 64 opcodes behave differently on the PSP-1000 than on the Slim.

Good fits are long fixed-point streams that stay resident or are reused. Gathers, tables and recurrences are poor fits.

## 1. Throughput (a)

The layout is mcidclan's distributed 4×4 sample made long. Each of the 4 PEs streams one top buffer against base buffer 0, and PE n writes its output into base buffer n.

| n a PE | MAC pass (done bit) | Same 4 dot products on the ME's CPU | Ratio |
|---|---|---|---|
| 4 | 67–80 (ends before the first look) | 266 | ~3× |
| 16 | 104 | 890 | 8.6× |
| 64 | 200 | 3,440 | 17× |
| 256 | 584 | 13,670 | 23× |
| 1,024 | 2,120 | 54,758 | 25.8× |

- **A pass is 72 + 2n cycles,** for the MAC (0x00240000) and the element-wise MUL (0x00200000) alike [M, both PSPs, identical to the cycle]. That is 0.5 elements a cycle a PE, so 2 MACs a cycle in all, or ~660 M a second.
- **The ME's CPU:** a MAC costs it 13.4 cycles from its cached eDRAM, and a multiply with its store 16.6. At n = 1,024 the element-wise MUL is 2,120 against 67,888 cycles (32×).
- **Exactness:** every final sum, every running partial and all 4,096 products are exact in 24 bits.
- **Set-up costs** [M]:
  - Writing a context (the datapath's 106 words cleared, then ~20 settings, all uncached) takes 709–741 cycles.
  - A **refresh**, the same context triggered again, costs only the pass: 2,108 cycles at n = 1,024.
  - So a short job pays mostly for its set-up. Build a context once and refresh it.
- **The end signal:** the busy bit (0x800 of 0x440ff000) is never seen on passes under ~100 cycles, because they end before the first look. On longer passes it is always seen.
- **Don't read a buffer while a pass uses it** [M]. A CPU load from such a buffer returns the previous pass's last output, not the word addressed. After the done bit every word is right, and the reads don't disturb the pass. Hand data over only at the done bit.

## 2. The operations beyond the MAC (b)

Sources [D]: mcidclan's `vme-opcodes.h` (psp-media-engine-custom-core 0197c99) and his sample notes (psp-virtual-mobile-engine-ext 15884a0). Both were written on a Slim, and his README warns that the functional-unit opcodes may differ on the Phat. His me-vme-hardware-csc sample (dc43ee3) doesn't use the VME's units at all. It drives a separate fixed colour-space converter (0xBC800120–0xBC800160, kernel mode) that turns YCbCr planes in the ME's eDRAM into RGB in VRAM, using one 3×3 matrix.

The bench ran 64 opcodes on both PSPs, one on each PE, with 16 inputs: front from top buffer 0, back from top buffer 1, and the constants A and B in the unit's registers [M]:

| | On both models (24-bit results) |
|---|---|
| Arithmetic | b×f >> k (k is the low 6 bits of the unit's word), b×B >> k, −(b×f) >> k, b+f+A (0x00030000), (b+f) >> k (0x00020000), b−(f>>B) (0x00050000), (b<<k)+B, (x+B)>>k, (x>>B)+A, (x>>B)−A, −b, \|b−f\|+A, \|b+f\|, \|b−B\| |
| Running | MAC (Σ b×f, every partial written), running sum of b, (running sum of b) >> k (his IIR_RUNNING_SMOOTH), SAD (running, 2-word lag), running max |
| Compare and select | max(b, f), min(b, f) (0x00078000; his MIN_IBUF code 0x000c0000 is an AND), clamp min(A, max(B, x)) (A and B the other way round from his note), (front & A) ? back : B (his COND_CMOV_BIAS: a select, not a +B), (front & A) ? −back : back, x ≠ 0 |
| Bits | AND, OR, XOR, NOT (stream with stream, or with B), x << B, b << f, b >> f (a **logical** shift of the 24-bit word), b >> B (0x000ac000, arithmetic), parity, the constant B |
| Not found | **A lookup or gather by data value:** no opcode in his table, and the address units step by constants. A recursive filter (y += (x−y)·a): the IIR codes are running sums. Division |

**13 of 64 opcodes differ between the models** [M]. On the PSP-1000 each one passes back through unchanged or gives 0, where the PSP-3000 gives his formula:

- ADD_VEC 0x00011000 and ADD_IBUF 0x00010000 / 0x00038000
- (f−b)>>k 0x00018000 / 0x0003c000
- RSHIFT 0x00014000 / 0x0001c000
- MUL_VEC_RSHIFT_BIAS 0x00270000 and MUL_DELAYED 0x00610000
- the bit test 0x0008c000
- both 8-bit channel adds (0x0005c000, 0x00058000)
- BARREL 0x00098000

None gives a different answer on the Phat: each is a no-op or a zero there. **So every context must be proven on the PSP-1000.** Where an operation the job needs is among these, its Phat equivalent is listed above (add: 0x00020000).

The saturator bit (`fu_sat`) changes ADD's result on both models in a way not yet decoded. The secondary units weren't tried.

## 3. The local buffers (c)

- **8 buffers of 2,048 words:** 4 base at 0x44000000 and 4 top at 0x44020000, 0x2000 bytes apart. All 16,384 words hold their own tag, with no aliasing [M, both PSPs]. The DMAC counts them as words 0–0x1fff (base) and 0x8000–0x9fff (top).
- **Each word is 24 bits,** sign-extended from bit 23 on a load [M]. 0x55555555 reads back 00555555, 0xaaaaaaaa reads ffaaaaaa and 0x80000000 reads 0. That is 48 KB of data in all. The MAC's 24-bit output (`vme_mac_widths`) is the same width.
- **vme_copy_cpu's mismatch, explained:** the same block is wrong in 510 of 512 words in 32 bits and in 0 in the low 24 [M]. The words are 24 bits wide, and nothing else was wrong.
- **What fits:**
  - 16-bit samples, 16-bit pixels and Q-format values to ±2²³ fit.
  - 8888 pixels and 32-bit floats don't.
  - A product of two 16-bit values needs a shift (the MUL's k) before its 24-bit output, and the sum inside is kept wide (`vme_mac_shift`).

## 4. Overlap with the ME's CPU (d)

The test is trigger, then work, then poll, with the work sized to the pass (a 1,024 MAC, 2,117–2,129 cycles) [M, both PSPs]:

| Work beside the pass | Alone | Inside the pass | Trigger to done |
|---|---|---|---|
| ALU loop, no memory | 2,115 | 2,115 | 2,227 |
| eDRAM reads, a miss each | 2,091 | 2,091 | 2,205 |
| VME-buffer reads (words the pass doesn't use) | 2,093 | 2,093 | 2,185 |

The ME's CPU loses nothing to a pass, and the pass loses nothing to it. Every result was exact after. Beside a DMAC transfer the result is the same: 69,675 cycles of work alone and 69,675 beside a 69,811-cycle transfer. So a job can be built as trigger, then the CPU's next item, then the done bit.

## 5. Feeding it: the VME's DMAC against the ME's copies (e)

The DMAC is at 0x440ff000, his "main VME DMAC". Its registers, as his samples use them: 0x010 the memory address, 0x014 the count − 1, 0x018 the buffer word, 0x008 the control. The test moved 2,048 words [M, PSP-1000; no hang, every guard after each destination intact; the PSP-3000 below]:

| Path | DMAC | The ME's CPU |
|---|---|---|
| Main RAM → VME (0x40) | 77,126 (37.7 a word) | 21,423 (10.5, cached loads) |
| eDRAM → VME (0x40) | 24,833 (12.1) | 17,598 (8.6) |
| 16-bit halves, main RAM → VME (0x52) | 39,021 for 2,048 halves (19.1 each; sign-extended into each word) | — |
| VME → main RAM (0x48) | 9,152 (4.5) | — |
| VME → eDRAM (0x48) | 5,550 (2.7) | 31,729 (15.5) |
| VME → main RAM as 16-bit halves (0x5a) | 5,582 for 2,048 halves (2.7) | — |
| Fill (0x21) | 4,140 (2.0) | — |

- **Inbound,** the DMAC is slower than the CPU, but it runs entirely beside the CPU (§4). The CPU pays only its four register writes (~100 cycles).
- **Outbound,** the DMAC is 3.4–5.7× faster than the CPU.
- **Memory receives the 24-bit words sign-extended to 32.**
- **The 16-bit modes** halve the memory traffic of 16-bit data at about the same cost a memory read [U].
- **The PSP-3000** [M]: the eDRAM paths are the same to within 0.3%: in 24,817 cycles, out 5,578, fill 4,140. The main-RAM paths are slower on the Slim: in 91,888 (44.9 a word), out 15,436 (7.5), 16-bit in 52,737 and out 11,162. The CPU's copies are alike: 17,606 / 31,741 / 22,159. No hang, guards intact, and the overlap is full (93,675 cycles of work alone and inside a 93,855-cycle transfer).

## 6. The candidates, sized

The common sum: the compute is nearly free and the moving costs. Each candidate's own cost must be set against moving its data in (8.6–12 cycles a word from eDRAM, or ~0 CPU by the DMAC beside it) and out (2.7 a word by the DMAC).

- **The audio mix (1,047 µs of the 23 ms block, b30's flight):**
  - The gain multiply ((s×g)>>14 is exact through the MUL's shift) and the voices' accumulate (0x00020000 on the Phat) fit. Each voice's 1,024 samples cost ~0.6k cycles of passes.
  - **Two parts don't fit:** the interpolation at the pitch step (a gather by fractional position, and no gather exists) and the one-pole lowpass (recursive). Both stay on the ME's CPU.
  - So the VME takes only the mix's tail. Its data would have to go in after the CPU's interpolation anyway, at ~12 voices × 1,024 words a block, ~106k cycles by the CPU or ~0 CPU by the DMAC.
  - VMEMIX's ceiling (~0.6–0.7 ms a block) isn't raised. Two of its revive conditions are now met: the width is settled and the PSP-1000 computes. The third, cheaper transfers, is met by the DMAC running beside the CPU, at the cost of its latency [U].
- **The ADPCM decode (832 µs a block):** each sample's predictor and step depend on the previous one through a table. That is sequential, with lookups. **Not a fit** [D].
- **Texture work:**
  - **Palettes:** a lookup, so no. The GE expands CLUTs itself anyway.
  - **Stand-ins and 16-bit repacking:** shifts, masks and selects fit for 16-bit pixels (5551/565/4444, his texture-processing sample, "ok" on the Phat). The 8-bit channel adds are Slim-only. 8888 doesn't fit a word.
  - **Decompression:** block formats need per-block selects between interpolated colours, which can be built from MUL, add and select. The GE decodes DXT natively (C7), so the VME would matter only for a format the GE lacks [U].
- **Fixed-point point physics:**
  - Element-wise integration fits: p += v×dt >> k, v += g, then a clamp.
  - Range is 24 bits: Q12 gives ±2,048 world units.
  - **Collision and anything that branches on data per point doesn't fit,** beyond the selects.
  - It pays only for batches of hundreds of points kept resident between ticks [U].
- **A new one, bulk copies out of the ME:** a VME → memory leg at 2.7–4.5 cycles a word against the CPU's 15.5. Only for data already in the VME's buffers; the inbound leg costs more than it saves [M figures, U use].

**What it would take, for any lever** (Sierra-III's ME rules):

- Waits polled and bounded, as here.
- Contexts built once and refreshed.
- Each opcode proven on the PSP-1000 (§2).
- Data handed over only at the done bit.
- Each job priced on a PSP pair.

The ME's own budget decides whether it's worth a context. The ME is ~9% busy with audio. Its render replay is the GE's bottleneck on d20 (the cost map, "Keeping the GE off RAM"), and that replay is list building, not vector maths.

## 7. Credits and terms

The register map, the wipe and configuration sequence, the context layout and every opcode tried are mcidclan's (m-c/d), MIT, from https://github.com/mcidclan/psp-media-engine-custom-core and https://github.com/mcidclan/psp-virtual-mobile-engine-ext. The colour-space converter's registers are from https://github.com/mcidclan/me-vme-hardware-csc (MIT).

His repositories accept no AI-attributed pull requests, so nothing from this work goes upstream to them. The measurements and the Phat's differences above are ours, to share with him by hand if Sheepdog01 chooses.
