# The DMA engines: what can feed the VME, and how fast

The question: the PSP decodes H.264 at 480p and 30 frames a second, so its media hardware is built to move data. Is there more headroom than round 1 found? Round 1 (`vme_capability.md` §5) measured one way of feeding the VME. This page maps every DMA engine that can reach the ME's side and prices the realistic pipeline: main RAM → the ME's eDRAM by the fastest engine, then eDRAM → the VME's buffers.

Labels: [M] measured on the PSP (the PSP is named), [D] read in code or notes (the source is named), [U] inference. Rates are in MB/s of 10⁶ bytes; the ME's cycles are at 331.4 a µs. The bench is `bench/halo_bench15_vmecap` on branch `recon/vme-dma`, keep words `dmamodes` and `dmapipe`. The findings rows are the `dma_*` rows in the lab's `measurements.tsv`.

**In short:**

- **Three engines move data, and two of them reach the ME's side:** the VME's own DMAC and DMACplus. The system DMAC doesn't reach the eDRAM.
- **The best pipeline measured is 88 MB/s** from main RAM into the VME's buffers, with the ME's CPU free [M, both PSPs: 88.0 on the PSP-1000, 87.9 on the PSP-3000]. DMACplus writes block k+1 into the eDRAM while the VME's DMAC reads block k. The limit is the VME's DMAC reading eDRAM, at 110 MB/s.
- **Superseded by round 3** (`vme_capability.md` §9, both PSPs [M]): with bits 8 and 9 of the VME DMAC's config word set (0x310), it reads main RAM straight into the VME's buffers at 262 MB/s (5.05 cycles a word) and eDRAM at 448 MB/s (2.96), so no staging through eDRAM is needed. This section's own measurements are at the config word's rest value (0x10). The fast mode doesn't leave them alone inbound from main RAM: a main-CPU miss goes 227 → 303 ns beside it (×1.33) [M, PSP-3000]; outbound to main RAM and the eDRAM paths cost nothing (`vme_capability.md` §9).
- **For scale:** a 480×272 4:2:0 picture at 30 frames a second is 5.9 MB/s, and 720×480 is 15.6 MB/s. The feed has 6–15× headroom over video's own data rate. So for the port, the DMA isn't the limit. The limits are still the ones round 1 found: the fit of the work (no gather, no lookup), 24-bit words, and the ME's time.
- **The VME's DMAC costs the main CPU nothing** [M, both PSPs]. Its cache misses stay at 225 ns while the DMAC reads or writes main RAM back to back. The DMAC yields instead. By contrast, the system DMA doubles the main CPU's misses and DMACplus adds 44%.

## 1. The engines

| Engine | Address | What it connects | Channels | Who drives it |
|---|---|---|---|---|
| The VME's DMAC (mcidclan's "primary DMAC") | 0x440ff000 (the ME's view) | Main RAM or eDRAM (by address) and the VME's 16,384 buffer words. It also loads the VME's context and fills buffers | One transfer at a time, with an outer and two inner address generators | The ME, with four register writes |
| DMACplus | 0xBC800000 | Main RAM and the ME's eDRAM, both ways. Also main RAM to main RAM, the LCD's scan-out and the colour-space converter | Three copy channels beside the display's | The main CPU, in kernel mode |
| The system DMAC (two controllers) | 0xBC900000, 0xBCA00000 | Main RAM, VRAM and the peripherals' request lines | 8 each, 16 in all | The kernel's dmacman (`sceDmacMemcpy` for copies) |

Sources [D]: the VME's DMAC is from mcidclan's psp-media-engine-cracking-the-unknown `0x440ff000.md` (f921778), psp-media-engine-reload (006ac29) and psp-media-engine-custom-core's `vme-lib.h`/`vme-lib.c` (0197c99). DMACplus is from his me-dmacplus-drawing-transfer `dmacplus.h` (ea4a894) and Sierra-III's DMACplus probe bench (`research/dmaprobe`, `bench/halo_bench15`). The system DMAC is from uofw's `src/kd/dmacman/dmacman.c` (28306bb). psdevwiki wasn't reachable from here.

## 2. The VME's DMAC (0x440ff000)

**Registers** [D, mcidclan]:

- 0x000 status: 0x200 transfer done, 0x800 the VME's pass done.
- 0x004 config (bit 4 is needed before the waits).
- 0x008 control: writing it starts the transfer.
- 0x010 the memory address, as a useg address (0x40000000 | physical), so main RAM or eDRAM.
- 0x014 the count − 1. The high half is an outer count in the group modes.
- 0x018 the buffer word: 0–0x1fff for the base buffers, 0x8000–0x9fff for the top buffers.
- 0x01c clear mode.
- 0x020 the memory step, in bytes.
- 0x024 the group size − 1.
- 0x028 the group steps, as outer << 16 | inner.
- 0x02c the fill value.
- 0x030–0x038 a second inner address generator.

**Control codes** (round 1 measured the plain codes; the group modes are new here):

| Code | What | Source | Measured |
|---|---|---|---|
| 0x40 / 0x48 | Memory → buffer / buffer → memory, 32-bit words. A word lands as its low 24 bits and goes out sign-extended | his fft sample | [M, both PSPs, round 1] |
| 0x50 / 0x58 | The same with the group address pattern (0x020–0x028): a 2-D gather or scatter | his mat3x3/mat4x4/fft samples | [M, PSP-1000] below |
| 0x52 / 0x5a | 16-bit halves in (each half sign-extended into a word) / out | his texture-processing sample | [M, both PSPs, round 1] |
| 0x21 | Fill buffer words | his `vmeLibClearLocalBuffer` | [M] 2.0 cycles a word |
| 0x0c | Load a VME context | his `vmeLibSendCustomContext` | [M] inside the ~709 cycles of a context |
| 0x18 | Start a pass (not a transfer) | his `vmeLibTrigger` | [M] |

**Rates by length** [M, PSP-1000; every word exact, nothing written past the count]:

| Words | eDRAM → VME | VME → eDRAM | main RAM → VME |
|---|---|---|---|
| 16 | 254 | 108 | 716 |
| 64 | 826 | 215 | 2,518 |
| 256 | 3,138 | 741 | 9,706 |
| 1,024 | 12,422 | 2,805 | 38,884 |
| 2,048 | 24,814 | 5,587 | 76,296 |
| Fit | **~60 + 12.09 a word (110 MB/s)** | **~65 + 2.70 (491 MB/s)** | **~120 + 37.2 (36 MB/s)** |

The fixed cost is small. A 64-word transfer already runs at ~80% of the long rate from eDRAM, so splitting work into small transfers costs little. Main RAM is 3× slower than eDRAM.

**The PSP-3000** [M]: the eDRAM paths match to the cycle (in 255 … 24,814, out 109 … 5,557). Main RAM inbound is slower on the Slim: ~165 + 46.6 a word (28 MB/s). The group probes placed every word as on the PSP-1000.

**The group modes** [M, PSP-1000]. 0x50 is 0x40 with a second address pattern. With the group registers at 0, it is a plain copy: 64 words take 842 cycles, against 0x40's 826. With mcidclan's settings, each probe wrote only inside its window. Every other buffer word was tagged first, and 40 KB of guarded memory stayed untouched:

- **His mat3x3 upload** (dims 7 << 16, step 4, group size 2, steps 9 << 16 | 1) placed 8 contiguous memory words at buffer words 0, 1, 2 / 9, 10, 11 / 18, 19. That is groups of 3, starting 9 words apart.
- **His fft stage gather** (dims 1 << 16 | 19, group size 7, steps 4 << 16 | 1) wrote 24 words: memory 0–3, 8–11, 16–19, then 4–7, 12–15, 16–19. That is the even/odd split of 4-word groups, in one transfer.
- **0x58 with his mat3x3 download** (from buffer word 2, dims 7 << 16 | 2, step 4) wrote buffer words 2, 5, 8 … 23 (every third), then 24 and 25, into 10 contiguous memory words.

So the DMAC can **deinterleave, interleave and stride** for free while it moves data. Interleaved stereo, or a field of a struct array, can come in already split. The fields' exact meaning isn't fully decoded yet (the fft gather's second pass repeats 16–19). Each pattern must be proven by a probe like these before it's used [U].

**Priority against the main CPU** [M, PSP-1000]. The main CPU walked 4 MB at random, one miss a load, while the ME ran the VME's DMAC back to back:

| The DMAC beside the walk | The main CPU's miss | The DMAC's own transfer (2,048 words) |
|---|---|---|
| None | 225–226 ns | — |
| Main RAM → VME (0x40) | 225 ns | 132k cycles (alone 76k: ×1.74) |
| VME → main RAM (0x48) | 217 ns | 18.6k (alone 9.2k: ×2.0) |
| eDRAM → VME (control) | 224 ns | — |

The PSP-3000 is the same [M]: 227 ns alone, 226 beside 0x40, 220 beside 0x48.

**The main CPU wins every time.** The VME's DMAC is a background mover that never slows the main CPU's misses. It pays for that in its own speed.

## 3. DMACplus (0xBC800000)

**What it is** [D]: Sony's controller, laid out like an ARM PL080.

- **Channel registers** sit 0x20 apart: source, destination, next descriptor, control, then a status word whose bit 0 enables the channel.
- **The channels:**
  - 0x100: the LCD's scan-out. Its buffer, pixel format, size and stride are set by mcidclan's `dmacplusInitLcd`.
  - 0x120–0x160: the colour-space converter (his me-vme-hardware-csc).
  - 0x180: main RAM → the ME's side ("Sc2Me").
  - 0x1A0: the ME's side → main RAM.
  - 0x1C0: main RAM → main RAM.
- **The interrupt bits:** a finished transfer sets raw interrupt bit 2 for 0x180 and bit 3 for 0x1A0 [M, the DMACplus probe bench].
- **The control word** [D, his `dmacplus.h`]:
  - Bit 31: the interrupt at the end.
  - Bits 27/26: the address increments. They must be clear for any transfer with the ME's side in it. The probe bench's runs with them set hung the PSP-1000.
  - Bits 25/24: the masters, 0 the main bus and 1 the ME's.
  - The width code, twice (bits 21–23 and 18–20): 0–4 for 1–16 bytes. The count is counted in the width [M, the probe bench].
  - The burst code, twice (bits 15–17 and 12–14).
  - Bits 11–0: the count, up to 4,095.
- **Chains:** linked descriptors of src, dst, next, ctrl and status, 16-byte aligned, gather larger transfers (his drawing-transfer sample). Not benched here.
- **The eDRAM's address:** 0 on the ME's side [M, the probe bench].

**Rates** [M, PSP-1000; every transfer exact, a guard after each]:

| Transfer | Time | MB/s |
|---|---|---|
| Main → eDRAM, 16 B wide, burst 1: 4 KB | 42–45 µs | 97 |
| 16 KB | 82–83 µs | 198 |
| 32 KB | 147–148 µs | 222 |
| 65,520 B (one descriptor's most) | 248–250 µs | **263** |
| Fit | ~28 µs a transfer + ~54 ns a 16-byte count | ~296 streaming |
| 16 KB, burst 0 | 280–282 µs | 58 |
| 16 KB, **burst 2** | **stalled**: stopped at the 20 ms bound with ~20 words landed | — |
| 16 KB, 8 B wide, burst 1 | 121 µs | 135 |
| **eDRAM → main**, 16 KB, 16 B wide, burst 1 | 60 µs | **273** |

- **The PSP-3000** [M]: 4 KB in 42–43 µs, 16 KB 85–89, 32 KB 151–153, 65,520 B 262–280 (234–250 MB/s); burst 0 289–291 µs; 8 B wide 128 µs; eDRAM → main 62 µs (264 MB/s). All exact, no hang. Burst 2 wasn't asked on the Slim.
- **The burst code matters 4.5×,** and mcidclan's code 1 is the largest that works. Code 2 stalls the channel; codes 3–7 weren't asked after it. Stopping the stalled channel left the controller working: every later transfer ran normally, with no hang. A likely reason [U]: the ME's side takes bursts of at most 4 × 16 bytes, one 64-byte line.
- **The fixed cost is large** (~28 µs, including the main CPU's kernel calls to start and poll). Chains or long transfers amortise it; 8 KB blocks run at only 132 MB/s.
- **Unexplained:** the probe bench timed 16 KB at 61 µs, and here it took 82–83 µs. Here the ME polled main RAM about once a µs, and the VME was clocked (enables 0x2f) [U].

**Its cost to the main CPU's misses** [M, PSP-1000, the probe bench]: +44% (221.5 → 318.2 ns) with 4 KB pieces at 99% duty. The probe hung in its next row, so lower duties are unmeasured.

**Hangs:**

- **Previous:** DMACplus has hung the PSP-3000 when it wrote into the eDRAM while the ME's render and ring jobs ran (the split sound cache's first fill).
- **This bench:** in two runs on the PSP-1000 and one on the PSP-3000, the ME touched only its eDRAM and the VME during a transfer, and neither PSP hung.
- **So the hazard is the pairing, not the engine:** DMACplus with the ME mastering main RAM [U].

- **The rest of the block, from jpcsp's model** (`MMIOHandlerDmacplus`, GPL, read only) [D]: 0x004 completed flags, 0x00C error flags; 0x100–0x110 the LCD's scan-out; 0x150–0x15C four codes it calls the MPEG/AVC codes, inside the converter's span; three channels, at 0x180, 0x1A0 and 0x1C0. No priority field in it (`hw_capabilities.md` §6.3).

## 4. The system DMAC (0xBC900000, 0xBCA00000)

**What it is** [D, uofw dmacman]: two PL080-style controllers with 8 channels each.

- **Channel registers:** at +0x100 + 0x20·n (source, destination, descriptor, control, config).
- **Global registers:** +0x30 enables the controller; +0x08 and +0x10 clear its interrupts.
- **Copies:** `sceKernelDmaOpSetupMemcpy` builds the control 0x8C4BF000 | count. Read with its `&` as an `|`, that sets the end interrupt, both increments, 32-bit width and the highest burst code on both sides. The config is 0xC001.
- **Longer copies:** a count under 0x1000 a descriptor, with linked descriptors for more.
- `sceDmacMemcpy` itself is in lowio, which uofw lists but hasn't decompiled.
- **PPSSPP** (c68f43c, `sceDmac.cpp`) times it at 236 bytes a µs.

**Rates and cost** [M, both PSPs, bench12]:

- **Rate:** RAM → RAM 182–204 MB/s idle, 62–65 MB/s while the main CPU misses.
- **Its cost to the main CPU's misses:** ×2.12 at full duty, ×1.2 at half, none at a quarter. RAM → VRAM costs ×1.93.

**Its clocks and the audio's use** [D, uofw `audio.c` and `sysreg_driver.c`, jpcsp]:
- DMAC0's and DMAC1's bus clocks are bits 5 and 6 of 0xBC100050, and DMACplus's is bit 4.
- The audio driver's three paths (the ordinary mix, the sample-rate converter and the input) are DMAC descriptor lists into the audio block's FIFOs at 0xBE000060, 0xBE000070 and 0xBE000080. The converter's channel is read straight from the game's buffer.

**It doesn't feed the VME:** the eDRAM is on the ME's bus, and the only path to it from the main side known here is DMACplus's ME master [U].

## 5. The staging pipeline: main RAM → the VME

256 KB in 32 blocks of 8 KB, every block checked in 24 bits [M, PSP-1000; the PSP-3000 in brackets]:

| Path | Time | MB/s | The ME's CPU |
|---|---|---|---|
| The VME's DMAC straight from main RAM | 2,324,282 cycles | 37 (30.6) | Free |
| The ME copies each block into eDRAM, then the DMAC, in turn | 1,564,872 cycles | 55 (54.4) | 100% |
| The ME copies block k+1 beside the DMAC's block k | 1,197,627 cycles | 72.6 (70.6) | 100% |
| DMACplus into eDRAM, then the VME's DMAC, in turn | 4,622–4,664 µs | 56.7 (56.1) | Handshakes only |
| **DMACplus block k+1 beside the VME's DMAC block k** | **2,978–3,022 µs** | **88.0 (87.9)** | **Handshakes only** |
| The same at burst 0 | 4,963 µs | 52.8 (51.3) | Handshakes only |
| Bound: the VME's DMAC from eDRAM | — | 110 | — |

- **The two DMACs overlap almost fully.** The VME's DMAC slows ×1.14 while DMACplus writes the same eDRAM (904k cycles against 794k), and the run reaches 92% of that slowed bound.
- **The ME's own copy overlaps worse:** 66% of its ideal. Its CPU and the DMAC contend on eDRAM, and the CPU is fully spent.
- **Out of the VME is faster:** VME → eDRAM runs at 491 MB/s (the VME's DMAC), then eDRAM → main RAM at 273 MB/s (DMACplus).
- **What drives DMACplus in a real pipeline** is open. Here the main CPU starts and polls each block through kernel calls. In the port that would be a kernel-side job, chained descriptors, or the ME's own copy [U].

## 6. What this changes

- **Headroom:** the feed isn't the bottleneck for anything the port would hand the VME. 88 MB/s is 2.9 MB in a 33 ms frame.
  - The audio mix's voices: ~12 × 1,024 words, 48 KB a 23 ms block.
  - From eDRAM, where the decode already writes them: ~0.45 ms of the VME's DMAC a block, beside the ME's CPU.
  - So moving the data is no longer the mix's obstacle. VMEMIX's ceiling is unchanged: the pitch gather and the lowpass still don't fit the VME (`vme_capability.md` §6).
- **The group modes** split interleaved data on the way in, so planar stereo or a struct field needs no CPU pass.
- **A copier that never slows the main CPU:** the VME's DMAC from main RAM costs the main CPU's misses nothing, at 36 MB/s (the system DMA costs ×2.1). For background copies the ME could start, it is the gentlest mover on the bus. It's a candidate for C8 ("DMA for more copies"), unsized [U].
- **A rule for any DMACplus use:**
  - Burst code 1 and 16-byte width.
  - The increments clear.
  - A bounded wait that stops the channel.
  - Never beside the ME's main-RAM jobs until that hang is understood.

## 7. Not done

- Chained descriptors on DMACplus, and its main → main channel against the system DMAC.
- Burst codes 3–7.
- A second VME DMAC command issued before the first ends (mcidclan's mat3x3 does this).
- The group fields' full meaning.
- The VLD's own memory access. Its registers hold a bitstream address and a coefficient output address (psp-media-engine-reload), so it masters memory itself [D].
- The colour-space converter's rate.
- DMACplus's miss cost at lower duty.

## 8. Credits and terms

- **The register maps and control codes** of the VME's DMAC and DMACplus, and every mode's settings tried here, are mcidclan's (m-c/d), MIT: https://github.com/mcidclan/psp-media-engine-cracking-the-unknown, https://github.com/mcidclan/psp-media-engine-reload, https://github.com/mcidclan/psp-media-engine-custom-core, https://github.com/mcidclan/psp-virtual-mobile-engine-ext, https://github.com/mcidclan/me-dmacplus-drawing-transfer.
- **The system DMAC's layout** is from uofw (https://github.com/uofw/uofw, GPL; read only, no code taken).
- **DMACplus's other registers and the bus-clock bits** are from jpcsp's register models (https://github.com/jpcsp/jpcsp, GPL; read only, no code taken).
- **PPSSPP's timing** is from https://github.com/hrydgard/ppsspp (GPL; read only).

His repositories accept no AI-attributed pull requests. The measurements above are ours, to share with him by hand.
