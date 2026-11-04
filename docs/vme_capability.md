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

The saturator, the secondary units and the refresh rules are §7 (round 2, PSP-1000).

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

**Every DMA engine, and the staging pipeline through eDRAM, is mapped in `dma_engines.md`:** main RAM → eDRAM by DMACplus beside eDRAM → the VME by this DMAC moves 88 MB/s with the ME's CPU free [M, both PSPs]. The DMAC's group modes (0x50/0x58) gather and scatter on the way. The DMAC never slows the main CPU's misses.

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

## 7. Round 2: the saturator, the secondary units, the sustained rate, the media blocks

The bench is the same program with the keep words `vmeunits` and `csc` (branch `recon/vme-units`). Everything here is [M, PSP-1000] unless it says otherwise; the PSP-3000's run is pending.

### The saturator (`fu_sat(n)`, bits 11:7 of a unit's word)

- **It clamps the result to n signed bits,** [−2^(n−1), 2^(n−1) − 1], for every n tried (1, 2, 3, 4, 5, 8, 9, 12, 16, 17, 20, 23, 24, 31). This is mcidclan's formula, and it works on the PSP-1000.
- **It acts on the wide result, before the 24-bit word.** 0x7fffff + 7 saturates to 0x7fffff at n = 24, where without it the word wraps to 0x800006. So n = 24 gives saturating 24-bit arithmetic. n = 31 wraps as no saturator does.
- **Order:** product, then the shift k, then the clamp. A MUL with k = 4 and n = 8 gives clamp(p >> 4, −128, 127).
- **On a MAC it clamps the output, not the accumulator.** The running sum −5,024 comes out as −2,048 at n = 12, and the next partial (+6,000) comes out as 976.
- Round 1's undecoded ADD result was this clamp at n = 1: each output was the sign, 0 or −1.
- **Bit 6 rounds to nearest** before the shift: −5,000 >> 4 gives −312 with it, −313 without.

So int16 and int8 saturation, and rounding, are free in any pass.

### The secondary units

Each PE has a second unit behind its primary. It is fed from the staging latch (its back input `STAGING_n`), with FU_1's enable set (0xf0000000) and the write delay at 9 instead of 6 (mcidclan's `saturate-clamp-second-fu` sample).

- **It is free.** A MUL with a clamp behind it ran 1,000 refreshes at n = 1,024 in exactly the cycles of a MUL alone (2,104 each), every result exact. So the VME does **4 operations a cycle** with both units, 1.29 G a second.
- **Its operations** [M]:
  - add with A, shifts, the clamp, the constant and passthrough work, each with its own saturator.
  - **MUL and MAC give 0 on the secondary.** It is an ALU, with no multiplier.
- **Its own buffer input arrives 3 words ahead.** `(b·f) + f[n]` came out as `(b·f) + f[n+3]`. To use a buffer there, offset its address unit by 3 (or try the interconnect's skew; not tested).
- **The staging latch is 24 bits.** A wrapped product reaches the secondary's clamp wrapped, where the primary's own saturator sees it wide.
- **Any PE's latch can feed any PE's secondary.** PE 2's secondary took PE 3's primary result exactly. So chains can run across PEs.
- mcidclan's sample (ADD, saturate to 7 bits, then clamp to [0, 0x7fffff]) gives his [0, 63] on the PSP-1000.

### Refreshes and the sustained rate

- **A refresh costs 56 + 2n cycles** (2,104 at n = 1,024, 88 at n = 16). Across 1,000 back-to-back refreshes the longest pass was the shortest plus one cycle. The VME keeps the ME's clock to the cycle over 6.35 ms. Sustained, that is 1.95 elements a cycle (646 M a second), or 4 operations a cycle with the secondary in use.
- **Write the flow word (VCTRL = 3) before each refresh** of a primary-only context. Without it, every second back-to-back trigger runs at the wrong length: 2,104 cycles at n = 16, and 88–112 at n = 1,024. The outputs stayed right in the cases checked. With the flow word, or with a secondary in the path, every pass is the same length. The flow word costs nothing measurable.
- The write delay must match the path: 6 for the primary alone, 9 with the secondary. A delay of 9 with the secondary off left 7 of 4,096 words right.

### No feeding beside a pass

- **While a pass runs, every write into the VME's buffers is dropped.** This holds for the ME's stores and for the DMAC, and for any buffer half the pass doesn't touch (both runs):

  | Writer | Target | Lost |
  |---|---|---|
  | ME stores (6.0 cycles each) | top 0–3, upper halves | 342 of 4,096 |
  | ME stores | base 1–3, upper halves | 328 of 3,072 |
  | DMAC | top 0, upper half | 160 of 1,024 |
  | DMAC | base 2, upper half | 165 of 1,024 |

  Each loss is about the number of writes that fall inside the pass's ~2,100 cycles. The pass's own results stayed exact, except 4 words in one run of the first case.
- **The DMAC doesn't wait for the pass.** Started during one, it finishes in its own time (12,555 against 12,426 alone), with bits 0x800, 0x400 and 0x200 all set at once. The words it moved during the pass are lost.
- **So moving and computing are serial,** a hard limit on any stream. With round 1's reads (a stale latch during a pass), the buffers are the VME's alone while it runs.
- **What streaming costs:** two input streams through the DMAC from eDRAM (12.1 cycles a word) and one out (2.7), per element, is ~27 cycles. Compute is 0.5. The ME's CPU does the same MUL in 16.6.
  - **So a one-pass stream is slower on the VME than on the ME's CPU.** The VME pays only when data stays resident through several passes, about six or more a load, or when the CPU's own time is what's short: the DMAC moves the data while the CPU does other work (§4).

### The VME's clock [U]

- No source gives it. The PSP's figures are in the ME's cycles, at 331.4 a µs.
- Every pass is an exact whole number of them, and that number holds steady.
- **An element takes 2 ME cycles a PE,** and the DMAC's fill takes 2.0 a word.
- That fits a datapath at half the ME's clock (~166 MHz, the bus clock) handling one element a clock in each PE. A datapath at the ME's clock running at half rate fits equally. Telling them apart needs a clock change, which the bench doesn't make.

### The media blocks as general hardware (4)

- **The AVC block is unpowered in a game** [M, PSP-1000]: 0xbc100070 reads 0.
  - The firmware powers it only when a video codec opens. Its sequence (uofw's `me_wrapper.c`, `sceMePowerControlAvcPower`, called from the video open `sceMeVideo_driver_E8CD3C75`) is three steps: syscon's `sceSysconCtrlTachyonAvcPower(1)`, then bit 2 of 0xbc100070, then the AVC reset released (sysreg bit 0x20) [D].
  - The bench therefore made no access to the converter's registers. An unpowered block's registers might hang the bus.
  - The only AVC registers the bench touches are mcidclan's transfer set-up at 0xBCC00000–0xBCC00070, at every ME start. They answer on both PSPs.
- **The colour-space converter** (0xBC800120–0xBC800160, his `me-vme-hardware-csc`) [D]:
  - What it does: a fixed 3×3 matrix with Q3.7 coefficients, from 4:2:0 YCbCr planes of bytes in the ME's eDRAM to 8888 or 565 RGB in VRAM, 480×272 in blocks of 16×8, started by one register.
  - It is a per-pixel matrix: no lookup, no recursion. Outside video it could only recolour three byte planes into a framebuffer.
  - Not measured: the run needs the AVC block powered (below).
- **The VLD and the decoder's transform and filter units** [D, U]:
  - No public source maps their registers. mcidclan's notes name a VLD and the ME's vendor move instructions (LDL/SDL reused, banks $8/$9 holding data). He reports their results unstable, possibly shared between the VME and the decoder.
  - uofw drives the decoder only through the ME firmware's RPC. PPSSPP replaces the codecs at a high level (sceVideocodec in HLE) and models none of the blocks.
  - A VLD decodes by tables inside itself, but nothing known exposes that as a lookup on general data.
- **Lookups and recursive filters, plainly:**
  - **No reachable unit provides either.** Round 1 found none in the VME's main array.
  - The secondary units are ALUs, and feed forward.
  - The IIR codes are running sums: (Σb) >> k, the output shifted but not fed back.
  - The address units step by constants.
  - The decoder's blocks are undocumented and unpowered in a game.
- **The question behind this round** (the ME decodes H.264 at 480p 30 fps, so is there no headroom?): the headroom is in the decoder's own fixed blocks (VLD, transforms, prediction, deblocking) and the AVC DMA paths. Those are a fixed video pipeline, not a general engine. The VME's general part is measured above (4 ops a cycle when data is resident), with the moving serial. The video rate comes from fixed blocks reading the ME's eDRAM directly, not from the VME's buffers [U].

**What's left:**
- The PSP-3000's run of round 2.
- The converter, timed with the AVC block powered by the firmware's own path. Opening the video codec before the bench takes the ME is one way. It is untested, and it needs its own small run and recon's word.
- The interconnect's skew for the secondary's input.
- The VME's clock, which needs a clock change.

## 8. Credits and terms

The register map, the wipe and configuration sequence, the context layout and every opcode tried are mcidclan's (m-c/d), MIT, from https://github.com/mcidclan/psp-media-engine-custom-core and https://github.com/mcidclan/psp-virtual-mobile-engine-ext. The colour-space converter's registers are from https://github.com/mcidclan/me-vme-hardware-csc (MIT). The secondary unit's chain is his saturate-clamp-second-fu sample (psp-virtual-mobile-engine-ext 15884a0, MIT), and the saturator's field is from his datapath notes (https://github.com/mcidclan/psp-media-engine-cracking-the-unknown f921778, MIT). The AVC power sequence is read from uofw's me_wrapper (https://github.com/uofw/uofw 28306bb).

His repositories accept no AI-attributed pull requests, so nothing from this work goes upstream to them. The measurements and the Phat's differences above are ours, to share with him by hand if Sheepdog01 chooses.
