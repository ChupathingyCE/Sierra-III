# B8 findings: YAPSPD (Yet Another PlayStation Portable Documentation)

Part of the **Halo CE PSP: performance review checklist** research. Source: groepaz/hitmen, last modified Dec 23, 2006; archived copy (Wayback, June 2010, `id_` raw captures of `hitmen.c02.at/files/yapspd/psp_doc/chapN.html`), fetched by Sheepdog01 on Oct 13, 2026.

**How to fetch it again:** the document is one file per chapter. Use the `id_` flag in the Wayback URL to get the raw page:
`https://web.archive.org/web/20100608105109id_/http://hitmen.c02.at/files/yapspd/psp_doc/chapN.html` for N = 1–33.

**Coverage:** all 33 chapters received (the first batch had 1–20; the archive rate-limited the rest, fetched in a second pass with retries). Chapters 12–17 (audio, IR, WLAN, USB, UMD, memory stick) are empty stubs. Most of 19–25, 28–30 is firmware, flash, UMD and modchip material with no bearing on the port.

**Caveat:** the author marks the whole document "work in progress… may be missing, incomplete or plain wrong." Treat numbers as leads to verify, not facts.

## Biggest finds

### 1. The ME can be woken by interrupt, so it doesn't have to spin (check 45, B4)
- **SC/ME RPC interrupt register `0xBC100044`:** write 1 to post an interrupt to the *other* processor. The documented sequence is `syncn; sw 1 → 0xBC100044; syncn`. From the main CPU this interrupts the ME (and vice versa). The main-CPU side is wrapped by `sceSysregInterruptToOther`. Chapter 5 adds: interrupt 31 catches the ME's interrupt on the main core.
- **Allegrex `halt` instruction** (`0x70000000`): "halt execution until next interrupt." The kernel uses it in its idle thread.
- **Hardware semaphore `0xBC100048`:** a lock register both processors can write, used as a spin lock.
- **Implication:** the idle ME can `halt` and be woken by the main CPU's interrupt instead of spinning on uncached RAM. That removes the bus traffic flagged in B1 and B4. **Needs a hardware test:** confirm the ME wakes reliably, measure the round-trip latency, and compare bus_access counts against the spinning version. *Unverified: whether the ME's interrupt handler setup in our ME library supports this (chapter 5 says the ME's exception handler address comes from COP0 register 25).*

### 2. Full profiler counter list (checks 2, 4, 37, and the stall-on-miss question)
Profiler block at `0xbc400000` (user-mode access at `0x5c400000` when the thread's profile mode is set). Clear all counters, then write 1 to `ENABLE`.

| Offset | Counter | Unit |
|---|---|---|
| 0x04 | system clock | cycles |
| 0x08 | CPU clock | cycles |
| 0x0c | stall (total) | cycles |
| 0x10 | stall (internal) | cycles |
| 0x14 | **stall (memory)** | cycles |
| 0x18 | stall (COPz) | cycles |
| 0x1c | **stall (VFPU)** | cycles |
| 0x20 | sleep | cycles |
| 0x24 | bus access | cycles |
| 0x28 / 0x2c | uncached load / store | count |
| 0x30 / 0x34 | **cached load / store** | count |
| 0x38 | I-cache miss | count |
| 0x3c | D-cache miss | count |
| 0x40 | D-cache writeback | count |
| 0x44 / 0x48 / 0x4c | COP0 / FPU / **VFPU** instructions | count |
| 0x50 | local bus | cycles |

**New uses beyond check 2 as written:**
- **D-cache miss rate** = D miss ÷ cached load. Per subsystem, this is the number tier 2 changes should move.
- **Average stall per miss** ≈ stall (memory) ÷ (D miss + uncached loads). This gives a **measured miss cost** per subsystem.
- **Answers the stall-on-miss vs. stall-on-use question (B2) with existing counters:** run a microbenchmark of independent missing loads, first back-to-back with uses right after, then with the loads hoisted and ALU work between load and use. If stall (memory) per miss drops when loads are hoisted, the CPU stalls on use and load scheduling (proposals J/L) hides real latency. If it stays at the full miss cost, it stalls on miss.
- **stall (VFPU)** and **VFPU instruction count** measure check 37 directly: how much VFPU work there is and how much of it waits.

### 3. Texture bandwidth by source (checks 29, 30)
Section 11.7, author's figures:
- Texture reads from **user RAM: ~50 MB/s**
- Texture reads from **VRAM: ~500 MB/s** (10×)
- Copying a texture **RAM → VRAM: ~150 MB/s**

The checklist's newer hardware timings say VRAM textures are **4–7×** faster. The direction agrees; the size differs, likely from method and workload. Prefer the newer measurement. The 150 MB/s upload rate is useful for **budgeting texture streaming into VRAM** per frame (check 29, 50).

### 4. VFPU stores can write through (checks 7, 24, 25)
`sv.q` has a cache-policy bit: **0 = write-through, 1 = write-back**. Write-through sends the data to RAM as it's written, so data the GE reads (vertex buffers, display-list data written with the VFPU) wouldn't need a later writeback. **Verify on hardware** and check whether our toolchain's assembler and inline asm expose the bit. (YAPSPD says `lv.q`/`sv.q` need 64-byte alignment; the checklist's VFPU notes say 16. Trust the pspdev VFPU docs.)

## Confirmations and smaller notes

- **Caches (4.10):** 64-byte lines, write-back, write-allocate, no L2, no snooping/coherency. (YAPSPD says "32k L1 cache," which matches 16 KB I + 16 KB D.)
- **Uncached alias rules:** cached and uncached use of the same memory is "a world of pain": rare, non-deterministic bugs. Always allocate 64-byte aligned and padded (`memalign`), and **write back + invalidate a range before first using it through the uncached alias, even freshly allocated memory**, because allocators hand back memory that's still partly cached.
- **Writeback range vs. all:** author's rule of thumb: over **~16 KB**, writing back the whole cache is probably cheaper than a range. (The D-cache is 16 KB, so beyond that a range op walks more lines than the cache holds.)
- **`sceKernelDcacheInvalidateRange` discards dirty data**: only for fully aligned ranges you're sure aren't dirty.
- **Memory map (7):** user RAM `0x08800000`–`0x09ffffff` (24 MB); kernel `0x88000000`–`0x887fffff`; VRAM `0x04000000` (2 MB); **scratchpad `0x00010000`, 16 KB**; ME internal RAM 2 MB at `0x00000000` (ME side); `0xbfc00600` is the ME RPC call struct used by `me_wrapper` (the address Daedalus writes to).
- **VRAM mirrors (10.2):** reading VRAM at +6 MB returns a **linearized depth buffer**. Useful only if depth is ever read back (e.g. a debug view or a soft occlusion test).
- **GE floats (11.2):** command-stream floats are 24-bit (the CPU float shifted down 8 bits; low mantissa bits lost). Matters for precision of matrices and values sent through raw GE commands (check 35).
- **GE command 0xcc TSYNC** and the "invalidate texture cache on texture change" bit: relevant if textures are updated in place.
- **FPU control registers (4.4.2):** only FIR (implementation register) documented; no details on exception or denormal handling, so B5's new FPU check stays open.
- **Allegrex extras:** YAPSPD documents only `halt`, `mfic`, `mtic` as Allegrex-specific. It does **not** confirm `min`/`max`/`movz`/`movn` (B2's open item). VFPU `vmin`/`vmax` are confirmed.

## Changes to proposals and open items

- **B4 open item answered (pending hardware test):** yes, there is a CPU↔ME interrupt (`0xBC100044`) plus `halt` on the ME. Update proposal F: "ME idles with `halt`, woken by interrupt; no spinning on shared RAM."
- **B2 open item (stall on miss vs. use):** method found, using the profiler counters. Add as a tier 1 microbenchmark.
- **New candidate:** VFPU write-through stores for GE-bound data (tier 3/4, verify first).

## Second batch (chapters 21–33)

### Texture formats and swizzling (27; checks 27, 28, 36)
- Pixel formats: 1555/4444/565 are **ABGR/BGR bit order** (red in the low bits), 8888 ABGR. The cook step must write this order.
- **Swizzle unit:** the GE works on texture blocks of **16 bytes × 8 rows**, regardless of pixel format (so a 32×32 32-bit texture is 128 bytes × 32 rows to the swizzler). Unswizzled textures force scattered reads when a block loads into the texture cache; swizzled ones load a block with one sequential read. Confirms check 28; the cook step owns the reorder.
- **DXT on the PSP is not standard DDS layout:** DXT1 wants the 2-bit per-texel indices **first**, then the two 565 colours; DXT3/5 want the colour block **first**, then the alpha block (reversed from DDS). Only matters if any texture is ever cooked as DXT (check 27 keeps CLUT as the default), but a cook tool that copies DDS blocks as-is would render garbage.

### EBOOT.PBP layout (26.3; for the XMB menu work, see `xmb_menu.md`)
A PBP is a plain concatenation with an index: `\0PBP`, version `00 00 01 00`, then eight little-endian 32-bit offsets, in order: **PARAM.SFO, ICON0.PNG (thumbnail), ICON1.PMF (animated icon movie), PIC0.PNG (overlay image, purpose unclear to the author), PIC1.PNG (background), SND0.AT3 (menu music), DATA.PSP (the executable), DATA.PSAR**. No alignment requirements noted. Tools: `pack-pbp` / `unpack-pbp` (Oopo).
- That maps onto Sheepdog01's XMB plan directly: the spinning-ring icon is **ICON1.PMF**, the Pillar of Autumn shot is **PIC1.PNG**, and the menu music from the user's disc at bake time becomes **SND0.AT3**.
- Section 26.4 documents PARAM.SFO (PSF) fields in detail; 26.5 the PSP executable header. Forwarded for the XMB session; not performance-relevant.

### GCC appendix (31)
Bootstrapping a toolchain from vanilla GCC with `--with-cpu=r4000` works but is "suboptimal"; an Allegrex-patched toolchain is "highly recommended." The pspdev toolchain already is one. Worth one confirmation for check 8: the build uses the Allegrex target (e.g. `-march=allegrex`), not a generic MIPS CPU, so Allegrex-only instructions can be emitted.
