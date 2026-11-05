# The PSP-1000's hardware: what we use, what we don't, and what to chase

Ledger 1 of the redesign's recon (`redesign_recon.md`). Sheepdog01, 2026-11-04 13:14: "we catalog gaps, chase them based on what they might contribute to the goal, and THEN we build."

**This is a first draft.** It comes from:
- the lab's OpenRouter sweep of every doc and the port's code (or-job 41895328253, checked by `check_quotes.py`: 201 of 240 citations confirmed; the rest were mostly "no hits" claims cited against a context line);
- a check of every `sceGu*` function and `GU_*` switch in the SDK's `pspgu.h` against the port's code (master, 2026-11-04);
- the master's spot checks of the claims below.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or docs, [U] inference.

## 1. The units, feature by feature

### The main CPU (Allegrex, 333 MHz)

| Feature | In use | Measured | Note |
|---|---|---|---|
| 16 KB I and D caches, 64-byte lines, 2-way | yes | `cpu_cost_map.md` §2 [M] | The tick's main cost: §1 there |
| `cache 30` (Fill), the data prefetch | in places (the node walk, off by default) | v9: non-blocking, one in flight, needs ~70 cycles of miss-free work [M] | `prefetch_map.md` |
| `cache 24` (create dirty exclusive) | drawing's list writes (`psp_ge.c`) | halves whole-line writes [M] | Unused in the tick |
| Instruction prefetch | none possible from user code | IPREFETCH [M] | Closed |
| Allegrex's extra instructions (min/max, ext/ins, clz, wsbh, madd) | as GCC emits them | — | Unchecked: whether `-march=allegrex` emits them in hot code [U] |

### The VFPU

| Feature | In use | Measured | Note |
|---|---|---|---|
| Matrix and quad ops | at least these sites in `psp/src` [D]: the node product (`psp_vfpu.c`, on), the quaternion forms (`psp_vfpu_quat.c`, on), two drawing paths in `psp_ge.c` (built, off) | 1.9–2.6× the FPU on the node product [M] | `hardware_utilization.md` §3 holds the float inventory. Its summary line ("one job in the whole port") is stale |
| Approximations (`vsin`, `vcos`, `vrsq`, `vexp2`) | not checked | — | Where the engine's hot maths could take them, exactly or "plays the same" [U] |
| Saving its registers across threads | — | — | Whether more threads could use it [U] |

### The scratchpad (16 KB)

| In use | Measured | Note |
|---|---|---|
| The level BSP's top nodes (SCRATCHBSP, on; `psp_scratch.c`) | −0.3 ms cinematic and flight [M] | Code runs from it at 14.3 cycles an instruction, so code is out (CODESPAD). Table candidates: SPX, ~0.15 ms [E]. `hardware_utilization.md` §4 still says "never built": stale |

### The Media Engine and its 2 MB eDRAM

| Feature | In use | Measured | Note |
|---|---|---|---|
| The second Allegrex, with its FPU (no VFPU) | audio, the drawing replay, AI (AIME 2a), point physics, visibility, sound copies | its misses don't slow the main CPU's [M]; branchy scalar work at 0.88–0.96× the main CPU's time [M] | **On d20's heavy view it's the binding unit:** the render job takes 66 ms a frame, and the GE stalls 23–24 ms a frame waiting for its chunks [M, recon 41888761035, 41893041680] |
| The scheduler | slots and deadlines per job; no arbiter | — | The rebuild Sheepdog01 deferred to after the tick work (12:13) |
| eDRAM, 2 MB | the ME's code and stacks, the sound cache's tier (SNDEDRAM2), the music decoder's room | — | No one page maps all 2 MB: a gap |

### The VME (the ME's reconfigurable engine)

| In use | Measured | Note |
|---|---|---|
| A test path only (`psp_vme.c`) | Measured on both PSPs [M, `vme_capability.md`]: 4 PEs at **2 operations a cycle** (a pass 72 + 2n cycles), 17–32× the ME's CPU on long streams, the ME's CPU free during a pass; **16,384 words of 24 bits**; a context ~709 cycles to write, a refresh only the pass; the VME's DMAC inbound 12–45 cycles a word, outbound 2.7–7.5, beside the CPU; **13 of 64 opcodes differ on the PSP-1000** (no-ops or zeros there); no lookup op. Round 2 [M, PSP-1000]: a saturator to n bits and rounding, free; a secondary ALU behind each PE, free (**4 ops a cycle**, 1.29 G/s sustained, refresh 56 + 2n), with no multiplier; **every write into the buffers during a pass is dropped**, so moving and computing are serial; the AVC block is unpowered in a game | Compute is cheap, moving data and set-up cost. Fits: long fixed-point streams kept resident. Not: ADPCM decode, palettes, recursive filters. `layout_plan.md`'s "computes nothing" is stale. VMEMIX stays parked (`vme_capability.md` §6) |

### The AVC decoder and the other media blocks

| In use | Note |
|---|---|
| no | **The colour-space converter is measured** [M, both PSPs, `vme_capability.md` §7]: powered by the firmware's own switch before the ME is taken (~23 ms at load, the ME's start undisturbed), it turns 4:2:0 byte planes in the ME's eDRAM into a 480×272 8888 or 565 frame in VRAM in **0.56 ms** (930 MB/s). It costs the CPU's misses ×1.09 and a GE texture list ×1.26, and it yields: ×3–3.7 slower itself beside them. It is a fixed matrix with no lookup, and no design needs it yet. The VLD and the transforms are undocumented [D]. Taking the ME over gives up the firmware's own use of them [D, `psp_hardware_reference.md` §2] |

### DMA

| In use | Measured | Note |
|---|---|---|
| texture uploads (`sceDmacMemcpy`), the DMACplus for the eDRAM | The system DMAC 182–204 MB/s, the CPU's misses ×2.1 at full duty, ×1.2 at half, none at a quarter (bench12) [M]; DMACplus 250–263 MB/s into the eDRAM and 264–273 out, the misses +44% [M]; the VME's DMAC 110 MB/s from eDRAM and 28–36 from main RAM, **no cost to the CPU's misses** [M, both PSPs]; main RAM → eDRAM → VME 88 MB/s with the ME's CPU free [M] (`dma_engines.md`) | Other bulk copies still go through the CPU [U, to count]; the VME's DMAC is a mover that never slows the CPU (C8) |

### The GE

| Feature | In use | Note |
|---|---|---|
| Hardware skinning (8 bone matrices, vertex weights) | yes, on by default (`sceGuBoneMatrix`, `GU_WEIGHTS`) | Closed |
| Hardware lights | yes on models: all 4 (2 distant + 2 point, `sceGuLight`) | A dynamic light adds no pass to a model: its falloff and cone are folded into one value per model, and rarely more than two lights reach one [D/E, recon's light census lc1 41904975882]. **`sceGuLightSpot` is never called**, so a spot on models would be a look change (a cone across the model instead of one value), not a capacity one: Sheepdog01's A/B. The world's dynamic lights are a separate pass (the flashlight, machines' lights), worked out per vertex on the ME |
| Call lists | yes, hand-built (BASE/CALL/RET) | A CALL costs 0.27 µs [M]; the bindblocks showed no saving [M] |
| Texture formats | CLUT8 and CLUT4, swizzled, mipmapped | **DXT1/3/5 never used.** The docs disagree on whether it's worth trying (`streaming.md` S6 against `halo-psp-feasibility.md`). It's lossy, so it would be a look call for Sheepdog01 |
| Mipmaps | yes, every CLUT texture | Closed |
| Fog, dither, alpha test, blend, depth, scissor, culling, texture matrix, environment map | yes | — |
| The GE's clip planes switch | enabled; no plane set | The GE clips at the near plane only; the CPU cuts far triangles [D] |
| Bounding-box culling in a list (`sceGuBeginObject`) | no | ~1.6 µs a box [feasibility note]. Saves GE work only, small [D]. The keep word `noboxtest` is a CPU test, not this |
| Morphing (`sceGuMorphWeight`) | no | Halo's models have no per-vertex blend shapes [U] |
| Patches (`sceGuDrawBezier`, `sceGuDrawSpline`) | no | Halo has no curved surfaces [U] |
| Stencil (`sceGuStencilFunc`, `sceGuStencilOp`) | no | Replaced by the stencil bake (the GE's stencil is too limited) [D] |
| Logic ops, colour test | no | No Halo material needs one [U] |
| Material colours (`sceGuColorMaterial`, `sceGuModelColor`, `sceGuAmbientColor`) | no | Colours come with the vertices [U]. Whether material colours could shrink vertices is open |

### Memory

| Pool | In use | Note |
|---|---|---|
| Main RAM, 24 MB user | yes | `memory_ledger.md` |
| VRAM, 2 MB | frame and depth buffers 816 KB, the texture cache, the lists | Nearly full [D] |
| The volatile 4 MB | the texture RAM cache | Settled in its current use (`memory_ledger.md`: short by 0.4–0.9 MB on a10 and b30 without it) [D] |
| The bus | shared by the CPU, the ME, the GE and DMA | The GE's RAM reads cost the CPU 8–18 ms of a 29–69 ms frame (b102 [M]). A priority setting: none found among the registers read (b105 [M]), and none in any public source (§6.3 [D]). The disassembly route is closed (decision 2026-11-04 16:55); what's left is in `psp_hardware_reference.md` §9.1 |

### Input and output

| Feature | In use | Note |
|---|---|---|
| Memory Stick async reads | **off by default** (`psp_geo.c`) | With them on, the texture cache's synchronous reads waited behind them (2.5–5 s frames). The bench showed async reads work if the I/O priority sits above the game thread's [M, v10]. The geometry reads are synchronous now: up to 98 ms in one frame on the ladder [M, 41821725723]. A hitch source |
| sceSas, the firmware's codecs | no | Superseded by the port's own ME mixer [D] |
| The audio converter's channel (sceAudioSRC) | yes: the ME's mix at 22,050 Hz (or 44,100 with `audio44`/`musadpcm`) | The hardware's one sample-rate converter; fed by DMA straight from our buffer, so the main CPU pays only the call. There is no hardware mixer: the 8 ordinary channels are mixed by a kernel thread on the main CPU (§6.1 [D]) |

## 2. The gaps to chase, by what they could add

| # | Gap | Could add | First step | Where it runs |
|---|---|---|---|---|
| C1 | **The ME's capacity and scheduling.** On d20's heavy view the ME binds the frame and starves the GE | Frame time wherever the ME binds; capacity for more AI and effects | The frame timeline (ledger 3), then the scheduler's design | PSP runs (recon) |
| C2 | **Lighting, split model and world** (recon's census, round 1): models already take all 4 GE lights, with no extra pass. The world pass is the cost where it's big (d20: 5,893 triangles a frame, machines' lights 3,244 and the flashlight 2,153; b30's bunker with the flashlight 1,533; the beach <300). Vehicles cost through the GE's vertex work (8,789 indices a frame each against a biped's 961), not lighting. ~92-95 of b30's ~100 attached-light relinks a frame are weapons whose lights never reach the pass | The world pass's share of the ME on d20 and in the bunker; the wasted relinks (DAL/LSAME ground); vehicles via their vertex counts | Round 2: the pass's counts and lc2's cache-model prices; then whether a projected-texture pass beats the ME's per-vertex work where the ME binds | Emulator, then a PSP pair |
| C3 | **Memory Stick reads async, done right.** The design exists, never built: `streaming.md` §3, S1–S5 (one input stage owning the stick, prefetch by place, pack order, smallest mips first, pinned effects) | The streaming hitches (up to 98 ms a frame; the game thread waited for the stick 2.5 s a run, worst 767 ms [M, b114]); the caches sized to the working set | S1 first (one reader, its priority measured on the PSP), then S2–S3 with the bake | Code and the bake, then PSP pairs |
| C4 | **The bus's priority** | If it exists, up to the GE's 8–18 ms cost to the CPU | No public source has one (§6.3); disassembly closed. The register snapshot (§6.4, two PSP-1000 runs, 9 of 11 states) found no register that moves with load beyond clock, status and address words: none of the documented ones is a priority setting [M]. A static word may be one (DDR 0x30), testable only by a write (Sheepdog01's call) | Closed by reading. Left: run 1's VRAM hang and run 2's stop in the second idle state (d1fe984b traces both; a third run only with the master's OK) |
| C5 | **The VME's capability** | A third processor for fixed work | **Done 2026-11-04** (`vme_capability.md`): computes on both PSPs, 2 ops a cycle; candidates sized there (none ready: the mix's tail only, decode and palettes don't fit) | — |
| C6 | **The VFPU's coverage:** the float inventory against tock161, and the approximation ops | Tick and drawing maths | Refresh `hardware_utilization.md` §3 | Code reading, emulator |
| C7 | **DXT textures** | VRAM and RAM room (8 → 4 bits a texel on CLUT8 textures) | A bench of its sampling cost and look; Sheepdog01's call on the look | PSP bench |
| C8 | **DMA for more copies** | CPU time on bulk copies, weighed against its bus cost | Count the CPU's bulk copies | Emulator census |
| C9 | **The eDRAM's map:** all 2 MB, who holds what | Room for ME code and data | One page | Code reading |
| C10 | The rest (BBOX, clip planes, morphing, patches, stencil, logic ops, material colours, AVC, sceSas) | Small or nothing, each with its reason above | — | — |

## 3. Docs found stale (to fix)

- `hardware_utilization.md`: "the VFPU does one job in the whole port"; the scratchpad lever "was never built".
- `big_levers.md`: "two hardware lights for models" (the code sets four).
- `layout_plan.md`: the VME "computes nothing".
- `psp_hardware_reference.md` §2.3: the audio path on the main CPU (it's on the ME since b86/b87).

## 4. Still to read

- The GE's command set itself (`pspge.h`, the GE registers) for anything `pspgu` doesn't wrap.
- The Allegrex's instruction set as GCC uses it in the hot code.
- The VFPU's full instruction set against the engine's hot maths.

Started 2026-11-04.

## 5. The stick, read for C3 (or-job 41901339013, checked: 124 of 162 citations confirmed, the key figures spot-checked)

- **Six readers, no owner.** Geometry (192 KB a frame), textures (96 KB a frame) and animations (no budget; a miss always waits, since the pose must be right) read synchronously on the game thread. Sound goes through the engine's reader thread (priority 0x1C), the music through its own (0x1D), and the log is written by the heartbeat (0x18). All of them share one stick under one lock, so each waits behind the others [D].
- **What the stick gives in play:** ~2 MB/s, against 16 MB/s for large reads. The reads are small (5–13 KB on average), and each pays ~2 ms of fixed cost [M, b114, `streaming.md` §2].
- **Who uses it most:** sound reads ~7× the other three together (118 MB a b114 run); the log writer is the second user (15.9 s a run) [M].
- **Nothing reads ahead.** The packs are in bake order, not first-use order, so a prefetch today would still be many small reads; S3 (pack order) is bake work.
- **The caches are smaller than their working sets:** geometry's 900-frame working set is 2.0–2.7 MB against a 1,792 KB carve [E, `memory_ledger.md`].
- **Its priority choice is open:** the async reads at 0x10, the engine's reader at 0x1C, music at 0x1D, the heartbeat at 0x18, audio at 0x12, the game at 0x20. One reader takes one of these, and which one is best is unmeasured.


## 6. Open sources, read for C1, C4 and the audio path (2026-11-04)

Sources, all read only, no code taken: uofw (github.com/uofw/uofw 28306bb, GPL: `src/kd/audio`, `clockgen`, `codec`, `me_wrapper`, `lowio/sysreg_driver.c`, `include/common/hardware.h`); jpcsp's register models (github.com/jpcsp/jpcsp 00ac4d6, GPL: `memory/mmio/`, `HLE/modules/sceDdr.java`); mcidclan's psp-media-engine-custom-core (0197c99, MIT: `me-core.h`, `hw-registers.h`, `me-core-custom.c`); ARK-4's `contrib/nidmap.txt` (old-to-new NIDs only, no names); PPSSPP (a2f4ce2: nothing on these blocks). psdevwiki can't be reached from the cloud sessions.

### 6.1 The audio hardware (uofw's `audio.c`, `codec.c`, `clockgen.c`) [D]

- **The sample-rate converter is real and the port already uses it.** One stereo channel (`sceAudioSRCChReserve`, also `sceAudioOutput2*`):
  - input at 8, 11.025, 12, 16, 22.05, 24, 32 or 48 kHz, or 44.1 passed straight through; out at the hardware's rate (44.1 kHz, or 48);
  - its volume in hardware (0xBE000050, the call's volume >> 5);
  - fed by the system DMAC straight from the caller's buffer (a linked pair of descriptors into the converter's FIFO at 0xBE000070). The main CPU only writes its cache back and makes the call;
  - a block of 17 to 4,111 samples. There is only one such channel.
- **There is no hardware mixer.** The 8 ordinary channels (`sceAudioChReserve`, `sceAudioOutput*`) are mixed in software by the kernel thread `SceAudioMixer`, at priority 5 on the main CPU. It wakes every 64 samples (~690 times a second at 44.1 kHz), scales each channel by its volume, sums in 32 bits, clamps to 16, and DMAs the block to the FIFO at 0xBE000060.
  - The two paths, the ordinary mix and the converter's, play at once into separate FIFOs, so the hardware sums those two outputs. That sum is the only mixing the hardware does.
  - **This explains a measured cost** [D explains M]: `audio44` on an ordinary channel cost b30's frame +7.8% (sierra-hw 41211384661), and moving it to the converter's channel at 44,100 Hz got 6.3% of that back (41213051173). An ordinary channel puts the kernel mixer on the main CPU.
- **What it constrains:** any second output stream (music beside the effects, say) must use an ordinary channel. That brings the kernel mixer back, so a second stream is best mixed on the ME into the converter's one stream, which is what MUSADPCM does.
- **`codec.c` is the audio DAC,** not a media codec: the WM8750 on I2C, for output, headphone and speaker volume and the sample rate. **`clockgen.c` is the CY27040 clock chip** on I2C: the audio clock (44.1 or 48 kHz family), the UMD DSP's clock and spread spectrum. Neither touches the CPU, ME or bus clocks.
- **Audio DMA for anything else:** none that the game can reach without writing the audio block's registers itself.

### 6.2 The clocks, the ME and the AVC block (C1) [D unless marked]

- **The clock tree:**
  - the PLL setting is 0xBC100068's low 4 bits (uofw `HW_SYS_PLL_FREQ`; jpcsp applies a write with bit 0x80);
  - each clock is the PLL times a ratio of two 9-bit fields (numerator << 16 | denominator): 0xBC200000 for the CPU and 0xBC200004 for the bus (jpcsp).
- **The ME has its own ratios, and runs at the full one.**
  - Sony's ME firmware takes "set CPU granularity" and "set bus granularity" commands (390 and 391, `sceMePower_driver_1862B784`/`E9F69ACF`; uofw's comments), and the ME's boot seeds 511/511 (0x1ff01ff) for them.
  - mcidclan's start, which the port uses, writes 511/511 into **0xBC200000, 0xBC200004 and 0xBC200008** from the ME.
  - So **the ME and its bus already run at the full ratio of the PLL. The only ME clock headroom is the PLL itself, which is overclocking and ruled out.** C1 is a scheduling and placement question, not a clock one.
  - The ME's counter reads 331.4 a µs at the "333 MHz" setting (`vme_capability.md`); the 0.5% gap is the PLL's real rate [U].
- **0xBC200008, a third ratio word,** is named by no source except mcidclan's write. It may be the VME's clock or the AVC's [U]. Telling recon's two VME clock models apart (`vme_capability.md`, "The VME's clock") would take a write of a lower ratio there, or to 0xBC200000, around one VME pass. That is a write to a word his start already writes, at a lower rate, so not overclocking, but it's still a write: recon's lane, with the master's OK.
- **The AVC block's power, in order** (uofw `sceMePowerControlAvcPower`):
  1. syscon's `sceSysconCtrlTachyonAvcPower(1)` (the power rail);
  2. 0xBC100070 |= 4;
  3. ME command 8 (inside Sony's ME firmware, which our ME start replaces; what it does isn't public);
  4. the AVC reset released (0xBC10004C bit 0x20).
  - Off is the reverse.
  - At the PLL's higher settings (≥ 266 MHz) the firmware also switches the AVC's clock source (ME command 7, `sceSysregMsifClkSelect(0, n)`).
  - Step 3's ME-side work isn't public, so the way to power the block is the firmware's own switch, called as an export. recon's cscrecon did that on both PSPs and timed the converter [M, `vme_capability.md` §7].
- **The reset and bus-clock maps** (uofw `sysreg_driver.c`; jpcsp `MMIOHandlerSystemControl`):

  | Bit | 0xBC10004C (reset) | 0xBC100050 (bus clock) |
  |---|---|---|
  | 0 | top | ME |
  | 1 | SC | AW A |
  | 2 | ME | AW B |
  | 3 | AW | eDRAM |
  | 4 | VME | DMACplus |
  | 5 | AVC | DMAC0 |
  | 6 | USB | DMAC1 |
  | 7 | ATA | KIRK |
  | 8–9 | MSIF0–1 | ATA, USB |
  | 10 | KIRK | MSIF0 |
  | 11–15 | –, ATA HDD, USB host, two unknown | MSIF1, EMCDDR, EMCSM, APB, audio 0 |
  | 16 | – | audio 1 |

  - **Neither has a priority field.**
  - **b105's reading is corrected by this map:** `bc10004c` 0x40 → 0 is the USB block's reset released, not the ME's. mcidclan's ME start writes 0x14 and then 0 to 0xBC10004C, which releases every reset at once. `bc100050` gaining 0x20 is DMAC0's bus clock (the system DMAC, for the texture uploads).
  - **The ME has a system-control block of its own** [M, recon, `vme_capability.md` §7]. Its 0xBC100050 holds 0x0F, the value its start writes, where the main CPU's holds 0xD41D. Read with this map, the main CPU's value is: ME, AW B, eDRAM, DMACplus, MSIF0, EMCDDR, APB and audio 0 on; DMAC0, DMAC1, AW A and KIRK off. So the system DMACs are likely clocked only while a copy runs [U].

### 6.3 DMACplus, the DDR controller and the bus's priority (C4) [D]

- **The DDR controller (0xBD000000): only offsets are public.**
  - 0x04 flushes: `sceDdrFlush(n)`, n from 0 to 15. The audio driver flushes 4 after it rewrites its DMA lists. The ME's boot flushes 4 after copying its start code; `me_wrapper` flushes 5 before each signal to the ME and 8 around the ME's reset and start. So n names a master whose writes must be seen [U].
  - 0x20, 0x24, 0x2C and 0x38 are used by the IPL; 0x30, 0x34, 0x40 and 0x44 during `sceDdrChangePllClock`.
  - jpcsp names all 14 `sceDdr_driver` exports with both NIDs. `sceDdrWriteMaxAllocate` is 0x655A9AB0 (1.50) and 0x4F30BFE8 (6.60); the others are `Flush`, `ChangePllClock`, `Get`/`SetPowerDownCounter`, `ResetDevice`, `ExecSeqCmd`, `Setup` and five unnamed. **No public source implements or describes any of them.** uofw lists them as NIDs, jpcsp as empty stubs.
- **DMACplus (0xBC800000)** (jpcsp's model, with `dma_engines.md` §3):
  - 0x004 and 0x00C: the completed and error flags;
  - 0x100–0x110: the LCD's scan-out (address, pixel format, width, stride, flags);
  - 0x150–0x15C: four codes jpcsp calls the MPEG/AVC codes, inside the colour-space converter's span;
  - three copy channels at 0x180, 0x1A0 and 0x1C0 (source, destination, next, control, status).
  - No priority or arbitration field in any model.
- **So, plainly: nothing public documents a bus arbiter or a per-master priority on the PSP.** Searched: uofw, jpcsp, PPSSPP, mcidclan's repositories, ARK-4's NID map, and the web for every function name. psdevwiki's register page was read earlier, from a saved copy (`psp_hardware_reference.md` §9.1).
  - The one named candidate, `sceDdrWriteMaxAllocate`, can't be read (disassembly closed). It could be called, as an export through kcall, but its arguments are unknown and a wrong value may hang the bus until power-off. **Not proposed without Sheepdog01's call.**

### 6.4 The register snapshot by our own bench (two runs on the PSP-1000; 9 of 11 states read)

- **The bench:** `bench/halo_bench16_regsnap` on branch `research/regsnap`.
  - It reads, in kernel mode and read-only, 241 documented registers in 11 blocks: memory access, system control, clock ratios, profiler, DMACplus, DMAC0/1, DDR, GE, GE eDRAM and audio. The ME reads its own short list (the clock-ratio words, its view of system control, the AVC transfer words).
  - The states, in the second run's order: before the ME starts, the ME idle, the ME streaming eDRAM, the ME streaming main RAM, the GE texturing from main RAM, the system DMAC, DMACplus into the eDRAM (the ME idle), the converter's channel playing, all of them together, the ME idle again, and last the GE texturing from VRAM.
  - Blocks whose bus clock is off are skipped. The fuse ID (0xBC100090–98) is never read.
  - Since f5d0e898 each state's values go to the stick as it ends, and while the GE is busy each GE and GE eDRAM register is read in a kernel call of its own, its address flushed first, so the log's last word names a read that hangs.

- **Run 1** (98a86766, bench-hw 41923464223; log recovered by psp-probe 41926842519) [M]:
  - The first pass read all 11 blocks idle, 2–7 µs a block (DMAC0/1 clock off, skipped). States 0–4 completed, 37–48 µs a snapshot. The ME's counter ran 331.35 cycles a µs (§6.2: the ME at the CPU's full clock).
  - It hung in the GE-from-VRAM state, untraced, with every value still in memory.

- **Run 2** (f5d0e898, bench-hw 41942770497, launched 18:38:32; Sheepdog01 power-cycled the PSP at ~18:40:36) [M]:
  - **States 0–8 came back with their values.** That includes both traced GE states: 48 samples, each reading all 29 GE and GE eDRAM registers one at a time under the GE texturing from main RAM (with the ME, the system DMAC and audio beside it in state 8). None of them hung. So reading the GE blocks under GE load from main RAM is cleared.
  - **It stopped in state 9, the ME idle again, before the VRAM state began.** state 8's table is the log's last text. Every contender (the GE thread, the DMA thread, the audio channel's release, the ME's stream) had ended before that table was written, and state 9 writes its own table before the VRAM state starts. State 9's work is bounded at ~2 s (the ME's 2 s answer wait, then 24 snapshots of ~40 µs). Yet nothing came for the ≥ ~40 s up to the power cycle [E: state 8 ran ~44.5 s by its 1,918 audio blocks of 23.2 ms; state 4 ≤ 33 s by its sample spans].
  - **The cause [U]**, two candidates, both inside state 9 (its reads were untraced):
    - (a) **the ME's own snapshot**: 0xBC200000–08, its system-control words and the AVC words 0xBCC00000–70. These are the same reads that passed in state 1. What changed since: the audio's clock and DMAC0's bus clock are now on, and the ME has just streamed main RAM for 45 s. The main CPU's wait for it is bounded, so only a read that stalls the bus would stop the main CPU too;
    - (b) **the main CPU's first read of DMAC0 (0xBC900000) or the audio block (0xBE000000) after the channel's release**. The audio driver turned DMAC0's bus clock on in state 7 and left it on, so the gate let DMAC0 through. Both blocks read fine while the channel played (states 7 and 8), but never after a release.
    - d1fe984b (built, not run) separates them: a line on the stick before the ME's snapshot, and in state 9 each block's base before its read. The GE states also log the GE thread's start on both sides.
  - **Run 1's VRAM hang is still open.** Left [U]: a GE or GE eDRAM read made only while the GE textures from VRAM, or the GE list on a VRAM texture itself. Two candidates are cleared [M]: the texture's copy into VRAM (0x04154000, 256 KB, inside the 2 MB, done at start in both runs, and both went on) and the GE thread's start (the same as state 4's, which passed twice).

- **What the values say** [M, PSP-1000, run 2]:
  - **No register moved with load that could be a priority or arbitration setting.** These stayed constant in all nine states:
    - the 32 memory-access words (0x00/04 0xCCCCCCCC, 0x40 0xFFFFFFFF, 0x44 0xFFFFFFBF, 0x50 0x010FFFCF);
    - the DDR controller's 0x20 0x8020, 0x24 0x20220, 0x2C 0x80000004, 0x30 0x11110410, 0x34 7, 0x38 0, 0x44 0xA;
    - the GE eDRAM block (0x00 0x00612223, 0x30 8, 0x80 0x200 ...).
  - **System control** changed only as clock bits:
    - 0xBC100050 bit 1 (the GE), on only while lists run;
    - bit 5 (DMAC0), turned on by the audio and left on;
    - 0xBC100058 bit 24, the audio's clock.
  - **The GE's bus clock is gated** (bit 1). With it off, every GE register reads back one stale bus word (0x088A9384, later 0x088A9498) and doesn't hang. The bench's table gives the GE no gate (-1), which is wrong but harmless.
  - **`sceDmacMemcpy` runs on DMACplus's third channel (0xBC8001C0–D0)**, not on DMAC0/1, whose clocks stay off. Its source and destination advance and 0x1C goes 0x01 → 0x11. Our own transfer into the eDRAM on channel 0x180 sets 0x1C bit 2 and 0x14 bit 2. **The audio's DMA is DMAC0's** (its clock comes on with the channel). The LCD scan-out words (0x100–0x110: 0x04000000, 480, 512, format 3) never moved.
  - **The DDR controller's moving bits:**
    - 0x00 bit 2 clears under the system DMAC and the combined load (0x0F → 0x0B; once in 24 idle samples);
    - 0x40 bit 8 toggles in two thirds of samples in every state, load or none.

    Both are status bits [U], not settings.
  - **The profiler (0xBC400000 = 1: on)** counts in 0x04–0x44 in every state. Its spread scales with each state's length (30–45 s traced against ~30 ms untraced), so this run gives it no rates.
  - **So for C4:** no documented register exposes a bus priority, and nothing configurable answers to load. Whether a static word is one (DDR 0x30's per-nibble 0x11110410 is the likeliest look) can't be told by reading. Finding out takes a write, which stays Sheepdog01's call, like `sceDdrWriteMaxAllocate` (§6.3).
