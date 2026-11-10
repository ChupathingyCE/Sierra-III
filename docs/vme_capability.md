# What the VME can carry, and at what cost (C5)

The VME is the Media Engine's reconfigurable vector engine. This page reports a bench round on both test PSPs. All figures are in ME cycles (the ME's counter ran at 331.4 a µs on both) and labelled [M] measured on the PSP, [D] read in code or notes, [U] inference. The bench is `bench/halo_bench15_vmecap` on branch `recon/vme-capability`. The findings rows are `vme_*` in the lab's `measurements.tsv`.

**In short:** the VME computes at **2 operations a cycle across its 4 processing elements**, 17–32× the ME's own CPU on long streams. It does so beside the ME's CPU, which runs freely during a pass. It has **16,384 words of 24 bits** (48 KB). Three things limit it:

- **Set-up:** writing a context costs ~709 cycles. A pass's own overhead is 72 cycles.
- **Moving the data:** inbound transfers cost 9–38 cycles a word at the DMAC's rest setting, 3.0 (eDRAM) and 5.1 (main RAM) with its config bits 8–9 set (§9), against 2 cycles an element of compute. The buffers can't be fed during a pass; the next context can (§9).
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

None gives a different answer on the Phat: each is a no-op or a zero there. **So every context must be proven on the PSP-1000.** Confirmed opcode by opcode with an independent harness (2026-11-10, the lab's findings `vme_opcodes_psp1000`): Dink's vme2-next-level kernels for 0x010, 0x014, 0x018, 0x038, 0x03c, 0x058, 0x05c, 0x08c and 0x098 each pass an operand through on the PSP-1000, while `add_ofa` (0x030) is exact; his toolkit's `--compat fat` marks only 0x010 not portable. Where an operation the job needs is among these, its Phat equivalent is listed above (add: 0x00020000).

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

The bench is the same program with the keep words `vmeunits` and `csc` (branch `recon/vme-units`). Everything here is [M, PSP-1000] unless it says otherwise. **The PSP-3000 agrees** [M, bench-hw 41917312890]: all 32 unit tests (the saturator and the secondary, on and off) give the same words on both models, unlike round 1's 13 primary opcodes; every sustained case is the same to the cycle (the flow word's need and the write delay's 7 of 4,096 included); and the writes lost inside a pass match within 4 words (342, 332, 164 and 165).

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
  - **So a one-pass stream is slower on the VME than on the ME's CPU** at the DMAC's rest setting. With its fast setting (§9) it is about 1.9× faster [U]. The VME pays only when data stays resident through several passes, about six or more a load, or when the CPU's own time is what's short: the DMAC moves the data while the CPU does other work (§4).

### The VME's clock [U]

- No source gives it. The PSP's figures are in the ME's cycles, at 331.4 a µs.
- Every pass is an exact whole number of them, and that number holds steady.
- **An element takes 2 ME cycles a PE,** and the DMAC's fill takes 2.0 a word.
- That fits a datapath at half the ME's clock (~166 MHz, the bus clock) handling one element a clock in each PE. A datapath at the ME's clock running at half rate fits equally. Telling them apart needs a clock change, which the bench doesn't make.
- **What the open sources add** [D, `hw_capabilities.md` §6.2]:
  - The ME has clock ratios of its own. mcidclan's start writes 511/511, the full ratio, into 0xBC200000 and 0xBC200004, and into a third word, 0xBC200008, that no other source names.
  - So the clock change needn't touch the PLL. A lower ratio in 0xBC200008, then in 0xBC200000, around one pass would show which clock the VME's 2 cycles an element follow. If 0xBC200008 is the VME's own ratio, the VME's cycles per element rise with it.
  - That is a register write, recon's lane with the master's OK. It isn't made here.

### The media blocks as general hardware (4)

- **The AVC block is unpowered in a game** [M, both PSPs]: the main CPU's 0xbc100070 reads 3, with bit 2 (the power flag) clear.
  - **The ME's 0xbc1000xx are its own registers.** The ME's 0xbc100070 reads 0 even with the block powered, and its 0xbc100050 holds its own clocks (0x0f at its start, where the main CPU's reads 0xd41d). So the power flag is read from the main CPU. Round 2's first "reads 0" was the ME's view.
  - The firmware powers the block when a video codec opens. Its switch is `sceMePowerControlAvcPower` in the ME wrapper (uofw's `me_wrapper.c`, called from the video open `sceMeVideo_driver_E8CD3C75`) [D]. It runs four steps: syscon's `sceSysconCtrlTachyonAvcPower(1)`, then bit 2 of 0xbc100070, then an RPC to the ME firmware (command 8), then the AVC reset released (sysreg bit 0x20). Its power-off has no RPC.
  - The only AVC registers the bench touches without the switch are mcidclan's transfer set-up at 0xBCC00000–0xBCC00070, at every ME start. They answer on both PSPs.
- **The colour-space converter** (0xBC800120–0xBC800160, his `me-vme-hardware-csc`): timed below.
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

### The colour-space converter, timed (keep words `csc`, `cscload`)

The bench is branch `recon/vme-csc` (ca189ff6). Everything here is [M]. It was measured on the PSP-1000 (bench-hw 41920041468, 41920606111, 41921292445) and on the PSP-3000 (41921285664), and the two agree within 2%.

**Powering it, by the firmware's own switch:**
- A small kernel module of the bench's (`avcpow`) imports `sceMePowerControlAvcPower` from `sceMePower_driver`, using uofw's 6.60 NID 0xB37562AA. The loader linked it on both PSPs. The module calls the switch only when the stub holds a jump.
- **The switch's power-on must come before the bench takes the ME.** Its RPC waits on the ME firmware with no time limit. So the bench calls it from a thread of its own and waits at most 2 s. It returned 0 every time.
- **Its cost:** power-on takes 21–36 ms (22.5 typical) and power-off 25–35 ms, through syscon. That is a load-time cost, never a per-frame one.
- The power flag goes 3 → 7 and back to 3. The power-off re-asserts the AVC reset (0xbc10004c bit 0x20). **Bit 2 read clear after every run.**
- **The ME's start is undisturbed.** With the switch before `meLibDefaultInit`, the start takes the image table as before (3 on the PSP-1000, 2 on the Slim). Its 100k nops take 600,021–600,115 cycles, against 600,020 without the switch, and its clock is 331.35 a µs.
- The converter's registers keep their values across the block's power-off and a game's relaunch. A fresh PSP reads 0x140 = 0x001e2d00 (720×480 in 16×16 macroblocks) and BT.601's coefficients (luma 0x95 = 1.164, Cr→R 0x0CC). These are either the firmware's set-up or the block's defaults [U].

**What it computes:**
- Each output is clamp(0..255) of cY·max(Y − 16, 0) + cCb·(Cb − 128) + cCr·(Cr − 128). The coefficients are Q3.7, and the fields are Y in bits 0–9, Cb in 10–19 and Cr in 20–29.
- Register 0x150 gives R, 0x154 gives G and 0x158 gives B. R is in the low byte and alpha is 0. 565 output has R in the low 5 bits.
- The one-input-per-output matrix proved the fields exactly. With mcidclan's matrix, the checked pixels match the formula to ±1.
- **0x160 is a busy flag:** it reads 1 just after the start and 0 once the frame is done.

**Its speed** (480×272, 4:2:0 bytes in the ME's eDRAM → VRAM):

| | One frame | Rate |
|---|---|---|
| 8888, alone (last pixel looked at every ≥ 10 µs) | **561–562 µs** | 232 Mpixel/s, **930 MB/s** into VRAM |
| 565, alone | 561–562 µs | 465 MB/s |
| 8888, with the main CPU polling VRAM without pause | 671–697 µs | |
| 8888, beside the main CPU's dependent misses | 2,062–2,086 µs (×3.7) | |
| 8888, beside the GE sampling 1 MB from main RAM | 1,657–1,669 µs (×3.0) | |

- **It is bound by the pixel rate, not by its output:** 565 is no faster than 8888. It reads 348 MB/s from the eDRAM.

**What it costs the others:**
- **The main CPU's dependent misses** (4 MB chase): 232.5 ns → 255.0 ns beside the converter (×1.09–1.10).
- **A GE list** (one 480×272 sprite of a 1 MB texture in main RAM): 47.3 ms → 59.7 ms (×1.26–1.27).
- **The converter yields:** it slows ×3–3.7 while the others lose 9–27%.
- The comparison point is the system DMAC, which costs the misses ×2.12 at full duty (§4 of `dma_engines.md`).

**So:** with the ME's planes ready, the converter can write a full-screen 8888 frame in about 0.56 ms, under 2% of a 30 fps frame, for a small share of the bus. It is still only a matrix from three byte planes, and it needs the block powered (one ~23 ms call at load) and the planes in the ME's eDRAM. No current design needs it. The candidates would be video playback, or a full-screen recolour that the ME writes as planes [U].

**What's left:**
- The interconnect's skew for the secondary's input.
- The VME's clock, which needs a clock change.

## 8. Sizing: the world light pass on the VME (recon, 2026-11-04)

The question (the lab's master, 2026-11-04 14:55, item 5): could the VME take the world light pass's per-vertex work off the ME, and how much of d20's ME time would that free, end to end with the transfers? A sizing from the figures above and ledger 2 (`capacity_model.md`). Nothing was built or benched for it.

**The work.** On d20's heavy hands-off view the pass looks at ~8,400 triangles a frame on the PSP and lights ~31% of them. The emulator counted 1,468 looked and 449 lit, but undercounts ~8× there (B58) [U on E, ledger 2 §2]. The ME's render job is ~65 ms a frame on that view, and the main CPU waits 14–17 ms a frame for it [M, `cpu_cost_map.md` §1]. The pass's own ME time on today's build hasn't been measured; LM's 15.1 ms is from an earlier build.

**On the VME, per lit corner** (element-wise, 24-bit fixed point relative to the light; positions within the light's reach fit easily) [U on M, this page's figures]:
- **Compute:** distance squared (a MAC over three vectors), the falloff `(1 - d²/r²)²`, the facing term `n·d / |d|`, and the clamp. There is no lookup and no divide, so `1/|d|` takes a Newton step from a polynomial seed over the known range `0 < d² ≤ r²`. That is about 12–15 dependent passes: at n = 2,048 elements a pass (56 + 2n cycles), ~30 VME cycles an element.
- **Feed:** writes into the VME's buffers are dropped during a pass (§7), so feeding is serial. Six input words a corner (position and normal) from eDRAM by the VME's DMAC at 12.09 cycles a word is ~73 cycles; one to three output words at 2.70 is ~3–8. Staging main RAM → eDRAM by DMACplus overlaps (88 MB/s, `dma_engines.md`).
- **Total:** ~105–110 cycles of VME and DMAC time a lit corner, beside the ME's CPU (§6: the ME's CPU loses nothing during a pass or a DMAC transfer).

**On the ME, what's removed and what's added** [U]:
- **Removed:** the per-corner falloff, facing and colour on the lit ~31%. At roughly half of a corner's all-in cost (~200 ME cycles a corner, if the pass is ~15 ms of ME over ~25,000 corners), that's ~100 cycles × ~7,800 lit corners ≈ 0.8 M cycles, **~2.3 ms of ME a frame**.
- **Added:** the gather. The lit corners are scattered by index through the level's vertex buffers in main RAM; neither DMA engine gathers by index (the VME DMAC's 2-D mode is strided, `dma_engines.md`), so the ME's CPU packs six words a corner at 8.6–10.5 cycles a word: ~60 cycles × 7,800 ≈ 0.47 M cycles, **~1.4 ms**. Plus a context (~709 cycles) a light and the triggers and polls.
- **Net: about 1 ms of ME a frame on d20's heavy view at best**, for ~2.6 ms of VME time beside it. Elsewhere it's less: b30's bunker flashlight lights ~0 triangles a frame, and the swamp's flashlight 14.

**Verdict: a poor fit.**
- The serial feed and the index gather eat most of what the VME's speed would save.
- The large share of the pass is static lights on static geometry: ~85% of d20's swamp light-frames are static [E, ledger 2]. LMC removes that work outright by keeping each static light's result across frames, without the VME or a transfer, and is ranked first (ledger 2 §4).
- What remains for the VME is dynamic lights on level geometry, which is small on every stretch measured.
- The VME would pay here only if the corners' light-relative data stayed resident across frames (a dynamic light over static geometry, with its data in eDRAM). The 48 KB of buffers holds ~2,700 corners at six words each, so that's one light's lit set at most.
- **No bench is proposed.** If one is wanted to confirm the per-corner figure, it is one PSP-1000 run timing the kernel's pass chain over 2,048 elements fed from eDRAM.

**Re-priced with round 3's fast DMAC (§9: config 0x310, the context double-buffered)** [U on M]:
- **The VME side shrinks:** six words in at 2.96 cycles a word is ~18 cycles a corner, and out ~3–7. So ~50–55 cycles a lit corner, half the figure above, and the ~709-cycle context set-up now overlaps a pass.
- **The ME side is unchanged:** the corners still need gathering by index, and neither DMA engine does that. So the net stays **about 1 ms of ME a frame** on d20's heavy view.
- **Skipping the gather:** the VME's DMAC reads main RAM directly at 5.05 cycles a word. It could stream whole runs of the level's vertices instead of the lit corners, all ~25,000 a frame.
  - That costs ~25,000 × (30 + 6 × 5.05 + ~3) ≈ 1.6 M cycles, ~4.8 ms of VME and DMAC a frame. That fits beside the ME's ~65 ms.
  - It would free the whole ~2.3 ms of the ME's falloff, facing and colour.
  - But the corners the pass needs aren't contiguous per light. The results would have to go back where the ME and the GE read them. And 262 MB/s from main RAM is a real share of the bus, whose cost to the main CPU's misses §9 hasn't measured.
- **Verdict unchanged: LMC first.** It removes the static ~85% with no transfer at all. At best the VME is worth ~1–2.3 ms of ME a frame on the heaviest view, and only after the fast mode's bus cost is measured.

## 9. Round 3: who owns the buffers while a pass runs

The question (Sheepdog01, 2026-11-04): a codec engine with 8 buffers normally ping-pongs, so is §7's serial result the hardware, or the way our set-up drives it? The bench is the same program with the keep words `vmeoverlap` and `vmeldl` (branch `recon/vme-overlap`). Everything here is [M] on both PSPs: bench-hw 41920673771 and 41921904314 on the PSP-1000 (the second also ran the vendor moves), and 41921919611 on the PSP-3000. The two models agree everywhere. Where they differ in cycles, the difference is named. Every run ended clean, with no wait reaching its bound and every guard intact.

**In short:**
- **The data buffers have one owner at a time, for all of them at once.** While a pass runs, every write into any buffer is dropped. This holds for every set-up tried, including a pass that touches only one of the 8 buffers. The DMAC's reads in the same window come back wrong.
- **The context is double-buffered.** A context loaded by the DMAC while a pass runs leaves that pass exact. It takes effect on the next flow word. That is the ping-pong this engine has.
- **The DMAC has a fast mode that round 2 didn't use.** Bits 8 and 9 of its config word (0x440ff004), part of the firmware's 0x31b, make transfers into the VME 4.1× faster from eDRAM and 7.3× faster from main RAM, with every word exact.

So moving and computing stay serial, but the moving is now 2.5–5.5 cycles a word, not 12–46. A one-pass stream from eDRAM comes out ahead of the ME's CPU [U, from the rates below].

### Bank ownership by set-up (item 1)

Each trial tags all 16,384 words. It triggers a pass of 768 elements, then stores into words 1792–2047 of all 8 buffers, words that no context here reads or writes. Each round of 8 stores is sampled against the busy bit (0x800) before and after. A round counts as "in the pass" only when both samples are busy, and "outside" only when neither is. A word the pass wrote is counted from a snapshot.

| Context | Buffers the pass wrote (base 0–3, top 0–3) | Stores in the pass that landed | Outside the pass |
|---|---|---|---|
| Round 2's four-PE stream | base 0–3, 768 each, all exact | 0 of 11–12 in every buffer | all (243–244 each) |
| The same, PE 1–3's units zeroed | base 0–3 (PE 0's exact; 1–3 zero products) | 0 in every buffer | all |
| PE 0 alone on its own address units (top 0 × base 0) | base 0 only | 0 in every buffer | all |
| PE 2 alone on its own (top 2 × base 2) | base 2 only | 0 in every buffer | all |
| Stores through the mirrors (+0x8000), four-PE and PE 0 alone | as above | 0 in every buffer | all |

- **The lock covers the whole buffer space, not the buffers in use.** A pass that reads top 0 and base 0 and writes only base 0 still drops a store into top 3. The interconnect's routing (all PEs on PE 0's address units, or each on its own) changes nothing.
- **The mirrors** [M]. 0x44008000–0x4400ffff mirrors base 0–3, and 0x44028000–0x44031fff mirrors top 0–3, with top 0 again at 0x44030000. Stores through a mirror land in the buffer. 0x44010000–0x4401ffff takes no store and reads back each buffer's last-read word, like round 1's stale latch. A mirror is the same port, not a second one.
- **DMAC reads in a pass fail too:** 256 words out of top 1 while a pass ran gave 1 right, against 256 of 256 (739 cycles) alone.
- **A DMAC transfer in doesn't wait.** Started in a pass, it runs at its own pace (3,156 cycles against 3,140 alone for 256 words). It lands only the words that arrive after the pass ends: 129–136 of 256, which is (3,156 − 1,592) / 12.2.
- **The trigger:** the pass trigger is the DMAC's control code 0x18, which mcidclan's library sends as `meCoreDMACPrimMuxSetCtrl_0x018`. A pass is the DMAC's own multiplexer at work, which fits a single owner. The other control codes weren't varied: an unknown code could move data with whatever addresses were left in the registers.

### The DMAC's config and status bits (item 2)

The config word reads 0x00000010 at rest on both PSPs. mcidclan's notes list 0x0, 0x31b, 0x831b, 0x1031b and 0x1831b as values the firmware sets. The bench tried those four, then each single bit added to 0x10 (0x1, 0x2, 0x4, 0x8, 0x20, 0x40, 0x100, 0x200, 0x8000, 0x10000), and the status bits 0x04, 0x08, 0x10 and 0x1c written before the trigger. Under every one, the stores in the pass were 0 of 12 in every buffer. Under every one, the pass was exact and a plain pass afterwards was exact.

**But bits 8 and 9 speed up the DMAC.** Each path was timed with 2,048 words, every word checked in 24 bits and the guards after each destination intact:

| Config (0x004) | eDRAM → VME | main RAM → VME | VME → eDRAM | VME → main RAM |
|---|---|---|---|---|
| 0x010 (rest) | 24,814 (12.1 a word) | 75,693 (37.0); Slim 93,911 (45.9) | 5,543 (2.71) | 7,000 (3.42); Slim 6,508 |
| 0x110 (bit 8) | 9,922 (4.8) | 24,597 (12.0); Slim 26,567 | 5,367 | 5,987; Slim 8,169 |
| 0x210 (bit 9) | 7,868 (3.8) | 15,677 (7.7); Slim 17,223 | 5,071 | 9,151; Slim 10,967 |
| **0x310 (both)** | **6,066 (2.96; 448 MB/s)** | **10,347 (5.05; 262 MB/s); Slim 11,225 (5.48)** | 5,049 (2.47) | 5,717 (2.79); Slim 5,113 |
| 0x31b (the firmware's) | 6,066 | 10,641; Slim 11,615 | 5,049 | 9,767; Slim 15,033 |

- **The cycle counts are the PSP-1000's.** The PSP-3000's eDRAM paths agree within 0.2%. Its main-RAM paths are named where they differ.
- **0x310 is the setting to use.** The firmware's extra low bits (0x1b) leave inbound unchanged but slow the write to main RAM. The single low bits (0x1, 0x2, 0x8) changed nothing measured.
- **Main RAM → VME at 262 MB/s** matches DMACplus's 250–263 MB/s into eDRAM (`dma_engines.md`). So the staging step through eDRAM that §5 and `dma_engines.md` priced at 88 MB/s isn't needed: the VME's DMAC reads main RAM directly at 3× that.
- **The fast mode's cost to the main CPU's misses** [M, PSP-3000, bench-hw 41925003539; the PSP-1000 was away]: the main CPU chased 100,000 dependent misses while the ME ran the VME's DMAC back to back.
  - **Main RAM → VME in fast mode takes the bus:** a miss goes from 227 ns to 303 ns (×1.33), and the DMAC keeps close to its own rate (5.9 cycles a word). That's DMACplus's kind of cost (+44% at 99% duty), not the rest mode's.
  - **In rest mode the same transfer yields:** misses stay at 227 ns, and the DMAC slows to ~65 cycles a word.
  - **Fast VME → main RAM** (229 → 231 ns) and **fast eDRAM → VME** (227 → 226) cost the main CPU nothing measurable.
  - So inbound from main RAM is a choice: fast and contended, or slow and polite. The fast mode belongs where the main CPU isn't missing (the frame's GE-bound stretches) or for short bursts. Any lever that streams main RAM into the VME during the tick has to be priced on the PSP with the fast mode on.
- **The registers in a pass** [M]: 0x000 reads 0x810 (0x10 at rest, 0x610 during a transfer), 0x008 reads 0x88808810, and 0x00c reads 0x88800000 (0 otherwise). Their meaning is not decoded.

### The context is double-buffered (the ping-pong found)

This follows mcidclan's switching-ram-context sample, which loads each next context while the last one runs. A is round 2's four-PE stream of 768. B is a context image in eDRAM: the same stream of 512, with products shifted by 2 so its output tells. B is loaded with the DMAC's context code (0x0c, after config 0x1c).

| Sequence | A | B |
|---|---|---|
| B loaded while A runs, then the flow word (VCTRL = 3) and a trigger | exact, 3,072 of 3,072 | ran, 2,048 of 2,048 |
| B loaded while A runs, B's trigger sent at once (no wait) | ran to its end (tail 1,024 of 1,024) | ran after A, 2,048 of 2,048 |
| B loaded (alone or in A), done bit acknowledged, trigger with no flow word | — | **didn't run:** the trigger ran the old context |
| B loaded alone, the ack, the flow word, then a trigger | — | ran, 2,048 of 2,048 |

- **Loading a context doesn't disturb the pass that is running.** The loaded context waits until the flow word, or until a trigger sent during the load, makes it the one that runs. So the datapath's configuration has a shadow, and the next job's set-up overlaps the current pass.
- **The load's cost:** 3,182 cycles on the PSP-1000 and 898 on the PSP-3000, each against a pass of 1,592. On the Slim a load fits inside a pass of about 450 elements or more; on the PSP-1000 it needs about 1,600 elements at rest, and about 400 at 0x310 (866 cycles, §10). That is §1's ~709-cycle set-up, taken off the critical path.
- The flow word's other job is §7's: without it, a refresh runs the wrong length.

### The vendor moves (item 3)

These are mcidclan's ldl/sdl reused, where the base register's number names a bank and the offset names a slot. Bank $8 was set to point at a guarded eDRAM block, so that an ordinary store would show there. Only the PSP-1000 ran this step.

- **Unforced** (0xBC400018 read 0x80008381): offsets 3 and 7 answer. Both read whatever was last written to either, so they are one slot. The rest read 0.
- **Forced** by his recipe: the pattern became offsets 0 and 4, again one slot, which matches his "00, xx, xx, xx, 04" pattern.
- **What they don't do:** no store reached memory (guard 0 of 64 changed), and **no VME buffer word changed** (0 of 14,336 checked, before any pass).
- **What they do in a pass:** they work. A value written in a pass reads back in it and after it.
- **So they are a 16-bit register or two beside the ME, not a way into the buffers.** The forced pattern stays on that PSP until a reset (his notes: some patterns persist).
- A bench slip: this step ran after the DMAC sweep had overwritten rows 2–3. Its "pass right 1,544 of 3,072" counts rows 0–1 only, and the "VME words changed in all" figure counts the pass's own outputs. Neither is an effect of the moves.

### The AVC block's paths into the VME (item 4) [D, U]

- **Only public sources were read** (Sheepdog01, 2026-11-04: Sony's ME decoder is not read).
  - uofw's `me_wrapper` drives the AVC block only through RPC indices into the ME's firmware: the power switch (§7), the AVC clock select (7), and the AW eDRAM bus clock (387/388).
  - It names an ME-side DMAC0 bus clock in its bus-granularity call (391).
  - No register-level path from the AVC block into the VME's buffers is in uofw or in mcidclan's notes.
- **mcidclan's VLD** (psp-media-engine-reload) sits at 0x44100000, clocked by bit 6 of 0xBC100050. It reads its bitstream and writes its coefficients by address registers (0x1c and 0x30), so it masters memory itself.
  - Whether its output address may be a VME buffer is untested [U].
  - Under this section's lock, its writes would also be dropped while a pass runs, unless it uses another port [U].
- **His notes name the VME's buffers "ping-pong buffers"** and say the DMAC "shares hardware control registers with Multiplexer DMAC but uses different configuration parameters". The measured shape fits that, between passes rather than during one: a config word switches the transfer mode, and the context alternates.
- **No AVC step ran this round,** so the AVC block wasn't powered for it.

### What this changes (§7's streaming cost)

- **A one-pass stream, serial,** with two inputs and one output a element at 0x310:
  - **From eDRAM:** 2 × 2.96 + 2.47 + 0.5 ≈ 8.9 cycles an element, against the ME's CPU's 16.6 [U, from the rates above]. So the VME now wins a one-pass stream, about 1.9× over the ME's CPU, with the ME's CPU free while the DMAC runs (§4).
  - **From main RAM:** 2 × 5.05 + 2.79 + 0.5 ≈ 13.4 cycles an element, with no staging step.
- **§7's "about six or more passes a load" falls to about one.**
- **Re-priced:** the world light pass (§8, the addendum) and VMEMIX's revive_if (the lab's grounds.tsv) now use 0x310. Neither verdict moves: the light pass is still behind LMC, and VMEMIX's ceiling (~3% of the ME) still parks it.

**What's left:**
- Whether bits 8 and 9 are a burst length, which would explain the rate.
- 0x004's bits above 17.
- ~~The context load's cost on the PSP-1000 (3.5× the Slim's).~~ 866 cycles at 0x310 (§10).
- The VLD's output into a VME buffer.

## 10. Round 4: mcidclan's later samples, read and benched ahead

The question (2026-11-04): his later samples (psp-virtual-mobile-engine-ext 15884a0) show units chained, data kept resident, contexts pipelined, named opcodes, a lock between the CPUs and a texture pass. Which of these are new to us, and what does each cost on the PSP-1000? The bench is the same program with the keep word `vmer4` (branch `recon/vme-r4`). Its results are below, from two clean PSP-1000 runs with the ME's counter running ("Measured"); what his code answers is [D].

**What his code already answers** [D]:
- **The context's MMIO at 0x440f8000–0x440f81a4 is the datapath itself.** His notes call it the "fine grained controller": each word is one setting of the live datapath (106 words, `VME_DATAPATH_BASE`). Every round here has written contexts through it (§1's 709-cycle set-up is those writes). So "writes there change the datapath" is how every context so far was built. What's new is how it relates to §9's shadow context, which the bench tests.
- **Resident data with a moved offset** (mat4x4-mul-host-batching, "ok" on the Phat): 16 vectors are loaded once, and each pass rewrites one word, PE 0's base address unit's offset, then refreshes. That is §7's refresh plus one uncached store. The bench prices it.
- **The named opcodes were all in round 1's 64** (§2, both PSPs): SAD (0x00290000, running), XOR parity (0x000f8000), |b−f|+A (0x001e0000), |b−B| (0x00170000), |b+f|. His opcode table's other SAD codes (0x0069/0x00a9/0x00e9) are the same operation with bits 22–23 set, which his descriptor format gives to the staging selector [D, U].
- **The 64-bit accumulator** (his commit b5f96fb): the MAC's sum is kept 64 bits wide, and the shift k (6 bits, up to 63) reads any 24 bits of it. His notes add that it rotates rather than shifts ("barrel/cyclic", seen by shifting 1). §1 measured the sum as kept wide. The bench reads 1, 0x7fffff² and a 256-term sum through the shifts, so the main CPU can tell a shift from a rotation from a 32-bit sum.
- **The hardware lock** (custom-core's hw-mutex sample): register 0xbc100048. A CPU writes its id (the main CPU 1, the ME 2, from COP0 register 22) and holds the lock when it reads its own id back. Unlocking is a write of 0. His note says the main CPU can set only bit 0 and the ME only bit 1. On the main CPU it needs kernel mode (his `kcall`). The bench times both sides and each side's view while the other holds it.
- **The fourth way to talk to the VME:** his code uses four. They are the VME's DMAC (0x440ff000), the datapath window above, the buffers' MMIO (0x44000000/0x44020000), and the vendor move instructions (ldl/sdl reused, his mt-mf-vme.md). §9 measured the last: one 16-bit slot, not a way into the buffers. His code shows no fifth, so no step was built for it.
- **texture-processing** ("ok" on the Phat): 4,096 5551 pixels move into the four top buffers as 16-bit halves (DMAC 0x52). Each PE ORs its tile with B, and they come out as halves (0x5a). Only B is rewritten each update. Using OR rather than an add keeps a carry out of the next channel. This is the template for texture repacking, and the bench times it.
- **Skipped:** the samples his table lists as "todo" on the Phat (the FFT, mat4x4-mul-batch, pico-ai, 32bits-precision-output). His "ok" ones are covered by rounds 1–3 except the three above.

**First runs, PSP-1000** [M; bench-hw 41928505141 and 41929232240, both clean at the wall, the PSP answering after]. **The ME's cycle counter didn't run in either:** 100k nops read 0 cycles, against 600,020 in round 3 on the same PSP. This followed its restart after another bench's hang. So every cycle figure below is missing, and only exactness and behaviour were measured:
- **The latches:** staging 0–3 are the primaries of PE 0–3, and 4–7 their secondaries. Each fitted 8 of 8 words at lag 0.
- **Units in series work across PEs.** MUL (PE 0), then +37 (PE 0's secondary), then a clamp (PE 1's primary on latch 4) is exact in all 1,024 elements. Each unit stage adds 3 to the write delay: 6 for one unit, 9, 12, 15. All four PEs share PE 0's write delay, so in one pass only outputs at the same depth come out right.
- **A unit's own buffer input isn't delayed to match a latch.** The select (PE 2's primary, back from latch 1, front from the mask in top 2) was right in about half the elements at best (500 of 1,024 at delay 18). That's what a mask read out of step gives. Offsetting that address unit (round 2's secondary saw 3 words ahead), or the interconnect's skew word, is the fix to try.
- **Each PE's own address units run its own stream and count.** MUL plus clamp with the interconnect at 0x3210 was exact on all four PEs: 4,096 of 4,096 with equal counts, and 1,024, 512, 256 and 128 with mixed ones.
- **The window is write-only.** Word 8 written with 0x123456 reads 0. Reads return other state (0x10, 0x8010, 0x1fff, 0x88009a00, like the DMAC's own registers in §9).
- **A write during a pass doesn't change that pass.** The four primaries' shift was rewritten about 1,000 cycles into a MUL of 1,024, timed by look count because the counter was stopped [U]. All 4,096 outputs came out with the old shift.
- ~~But it wedges the VME.~~ Not so with the counter running (below): the next pass takes the new shift whole, and nothing hangs. This run's 1,920 of 4,096, and the hang after it, came with the counter stopped, when the 1,000-cycle wait was a look count and the bench's waits had no clock [U].
- Measured since (below): resident against streamed, the pipelined contexts, the accumulator, the texture pass and the lock. The first run stopped in the window step on a spin bounded by the counter alone (fixed, 3ed40834). The second wedged there.

**Measured, PSP-1000** [M; bench-hw 41936841784 at bbaa425b and 41938293728 at 76ce63a0; the counter written at the ME's start, 600,003 and 600,064 cycles for 100k nops]. The first run's contexts step started with a context load that never ended, so its last four VME steps hit their bounds. The second run sets the transfer registers before each load (the length every good load had: 256 words) and runs the loads after the steps that need none. Nothing in it hit a bound. Where both runs measured a figure they agree within 0.4%.

1. **Units in series cost nothing.** A pass of 1,024 takes 2,107 cycles at every depth, one unit to four (2,131 at write delays 18–21, the tail). The four-step chain refreshed 100 times averages 2,117 (2,116–2,179). The ME's CPU does the same four steps from cached eDRAM in 18,113 (17.7 an element). So the chain is **2.07 cycles an element, 8.6× the ME's CPU** on resident data.
   - **Against the ceilings:** the three exact steps (multiply-shift, add, clamp) are 1.46 operations a cycle on one stream, using three of the datapath's eight unit slots. A pass's 8 slots an element at 2.06 cycles is **3.9 a cycle, §7's 4** (1.29 G a second). Chaining fills more of the slots a stream can use; it doesn't raise the ceiling. **Sony's ~30 a cycle stays out of reach** [U: their count must take in the address units and the interconnect, which do no arithmetic we can use].
   - **Shared or own address units, equal or mixed counts:** 2,108 cycles in every case, all exact. Counts of 512, 256 and 128 on PEs 1–3 don't shorten the pass (2,192 once in the second run, the case after the chain without its flow word: §7's wrong-length trigger [U]).
   - The select is still unsolved: 500 of 1,024 at delay 18 in both runs. The other count (245 against the mask-plus-−7 form) fits a mask read out of step equally well as one testing the mask's bit 1, so the next try is an offset on its address unit, then A = 2 [U].
2. **The window and the shadow.**
   - The window is write-only, 6.7 cycles a word (669 for 100). §1's 709-cycle set-up is about 106 of these.
   - **A write during a pass leaves that pass alone** (4,096 of 4,096 with the old shift) **and takes effect at the next trigger,** with or without the flow word (4,096 of 4,096 with the new). Nothing hung after it.
   - **A context loaded by the DMAC wins over window writes made before the flow word:** B ran (2,048 of 2,048, n 512), and the window's shift reached 25 words. So the window writes the active context, and the flow word swaps the loaded one in over it.
   - A pass of 4 costs 142 cycles from the flow word to done: the fixed cost of a tiny pass.
3. **Resident against streamed.**
   - **The element-wise MUL, 4 × 1,024:**

     | Feed | Cycles | A result | Exact |
     |---|---|---|---|
     | resident, one refresh | 2,191 | 0.53 | 4,096 |
     | from eDRAM at rest (0x10): in 62,847, pass 2,233, out 11,724 | 76,804 | 18.8 | 4,096 |
     | from eDRAM at 0x310: in 15,686 (3.06 a word), pass 2,179, out 10,495 (2.56) | 28,360 | 6.9 | 4,096 |
     | the ME's CPU (§7) | | 16.6 | |

     **So a one-pass stream at 0x310 is 2.4× the ME's CPU,** better than §9's 8.9 estimate, and resident data is 31×.
   - **His host batching (a 4×4 MAC, 64 vectors):** resident with one window write a pass, 162 cycles a vector (10,341). Each vector streamed in, 321 at rest and 286 at 0x310. **Only 4 of 256 results were right in every variant,** resident and streamed alike. That fits a MAC whose sum carries from one refresh to the next, so only the first vector is right; his mat4x4-mul-reset-acc notes say no way is known to reset it at a stream position [U]. So the pattern is priced, but batched MACs need that reset first. And at 142 cycles a tiny pass, a 4-element job doesn't pay against the ME's CPU anyway.
4. **Context switches.**

   | Context load (0x0c) | At rest (0x10) | At 0x310 |
   |---|---|---|
   | from eDRAM | 3,210–3,302 | **866** |
   | from main RAM | 9,154–10,412 | 1,362–1,414 |

   - §9's 3,182 on the PSP-1000 is the rest setting (3,210 here). At 0x310 the PSP-1000 loads in 866, as fast as the PSP-3000 at rest (898). So the fast mode closes the 3.5× gap between the models (the 3000 is untested at 0x310).
   - **Eight contexts alternated** (A shift 0, B shift 2), from eDRAM at 0x310, all exact:

     | n | Pass | Serial, a context | Pipelined, a context |
     |---|---|---|---|
     | 64 | 264 | 1,079 | 951 |
     | 512 | 1,080 | 1,966 | 1,248 |
     | 1,024 | 2,112 | 3,001 | 2,265 |

   - **Pipelined, a switch costs ~150 cycles over the pass** once the pass is longer than the load (about 400 elements or more). Shorter passes run at the load's pace. At rest, or from main RAM, the load dominates (rest from main RAM: 10,100–11,900 a context). Every one of the 12 sequences ended exact.
5. **The accumulator is 64 bits, shifted, not rotated.** 1 and 0x7fffff² read through every shift k = 0–63 match a 64-bit arithmetic shift in 64 of 64. The 23 shifts that would tell a rotation from a shift say shift, so his "barrel/cyclic" note doesn't hold on the PSP-1000. The sum of 256 × 0x7fffff² (~2^54) reads exactly through 8 shifts of 8; a 32-bit sum would fit 3. So long dot products don't overflow, and any 24 bits of the sum can be read.
6. **The lock is a real mutex, owner-only** (first run; in the second the main CPU's answers didn't come inside the 0.6 s bound, so its awaits read 0).
   - A try and a free cost 118 cycles on the ME and 118.5 on the main CPU (100,000 pairs, 11.85 M cycles). The ids are 1 (main) and 2 (ME). Every try won with the lock free, and the main CPU took it at its first try.
   - While one CPU holds it, the other's tries fail (100 of 100), and its write of 0 leaves the lock held, both ways.
   - **The main CPU needs kernel mode for it:** an empty kcall costs 17.1 µs, 145× the lock. So a user-mode game would need a kernel module to use it per job [U].
7. **The texture pass: the repack template, priced.** 4,096 5551 pixels go from main RAM to the four top buffers as halves (0x52). Each PE ORs its tile with B, and the result goes back out as halves (0x5a):

   | Path | In | Pass | Out | A pixel | Exact |
   |---|---|---|---|---|---|
   | VME, 0x310 | 11,387 | 2,114 | 9,253 | **5.56** | 4,096 |
   | VME, rest | 78,040 | 2,176 | 10,983 | 22.3 | 4,096 |
   | ME's CPU, eDRAM to eDRAM (cached) | | | | 8.73 | 4,096 |
   | ME's CPU, main RAM uncached | | | | 83.9 | |

   - **At 0x310 the VME repacks main RAM to main RAM at 5.56 cycles a pixel,** 1.6× the ME's CPU working in its own eDRAM and 15× the ME's CPU on main RAM. The DMAC is 90% of it, and the pass 0.52 a pixel. Since units in series are free (item 1), a repack of a few shifts, masks and ORs a pixel should cost the same [U, OR alone measured].
   - Not a swizzle: only linear addressing is known (his mat4x4-mul-reset-acc notes: the other addressing modes are unknown) [D], so reordering pixels isn't in reach yet.
   - **The resident refresh failed:** B rewritten through the window, then the flow word, the pass and out, gave 128 of 4,096, exactly the 1 in 32 pixels where both ORs agree. So the previous output came back. Either the window write of B didn't take (item 2 says it should), or the out started before the pass's busy bit showed (the wait doesn't wait for it to appear) [U]. The chain's refreshes, which change nothing, were right.
   - Levers: VMETEX in the lab's levers.tsv (a template, no use yet).

**What's left:** the select's mask alignment; a MAC reset at a stream position; the texture refresh's failure; the PSP-3000 at 0x310 for the context load; the lock's main-side answers inside its bound (the main loop answers once a frame).

## 11. Credits and terms

The register map, the wipe and configuration sequence, the context layout and every opcode tried are mcidclan's (m-c/d), MIT, from https://github.com/mcidclan/psp-media-engine-custom-core and https://github.com/mcidclan/psp-virtual-mobile-engine-ext. The colour-space converter's registers are from https://github.com/mcidclan/me-vme-hardware-csc (MIT). The secondary unit's chain is his saturate-clamp-second-fu sample (psp-virtual-mobile-engine-ext 15884a0, MIT), and the saturator's field is from his datapath notes (https://github.com/mcidclan/psp-media-engine-cracking-the-unknown f921778, MIT). The AVC power sequence, and the power switch's NID the bench imports, are read from uofw's me_wrapper (https://github.com/uofw/uofw 28306bb). §10: the resident batching, the pipelined contexts and the texture pass follow his mat4x4-mul-host-batching, switching-ram-context and texture-processing samples (psp-virtual-mobile-engine-ext 15884a0, MIT); the lock is his hw-mutex sample (psp-media-engine-custom-core 0197c99, MIT); the MAC's missing reset and linear-only addressing are from his mat4x4-mul-reset-acc notes (psp-virtual-mobile-engine-ext, MIT); the accumulator's width and rotation, the datapath window's name and the opcode table are from his datapath notes (psp-media-engine-cracking-the-unknown f921778, MIT). §9: the context loaded during a pass follows his switching-ram-context sample (psp-virtual-mobile-engine-ext 15884a0, MIT); the vendor moves, their banks and his forcing recipe are from his mt-mf-vme.md, and the config word's firmware values from his 0x440ff000.md (psp-media-engine-cracking-the-unknown f921778, MIT); the buffers' mirrors, the "ping-pong buffers" and the multiplexer DMAC's shared registers, and the VLD's registers are from https://github.com/mcidclan/psp-media-engine-reload 006ac29 (MIT); the AVC clock and bus-clock RPC indices from uofw's me_wrapper 28306bb.

His repositories accept no AI-attributed pull requests, so nothing from this work goes upstream to them. The measurements and the Phat's differences above are ours, to share with him by hand if Sheepdog01 chooses.
