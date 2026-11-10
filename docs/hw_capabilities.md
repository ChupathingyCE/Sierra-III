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
| texture uploads (`sceDmacMemcpy`), the DMACplus for the eDRAM | The system DMAC 182–204 MB/s, the CPU's misses ×2.1 at full duty, ×1.2 at half, none at a quarter (bench12) [M]; DMACplus 250–263 MB/s into the eDRAM and 264–273 out, the misses +44% [M]; the VME's DMAC 110 MB/s from eDRAM and 28–36 from main RAM, **no cost to the CPU's misses** [M, both PSPs]; main RAM → eDRAM → VME 88 MB/s with the ME's CPU free [M] (`dma_engines.md`) (medians; as time paid ×1.38 at half, ×1.20 at a quarter: `hardware_facts.md`, 2026-11-10) | Other bulk copies still go through the CPU [U, to count]; the VME's DMAC is a mover that never slows the CPU (C8) |

### The GE

| Feature | In use | Note |
|---|---|---|
| Hardware skinning (8 bone matrices, vertex weights) | yes, on by default (`sceGuBoneMatrix`, `GU_WEIGHTS`) | The blend is already the GE's. **Its rate [M, bench 17]:** the GE blends every weight a draw declares, zeros included, ~60 ns a weight a vertex (1: 82, 2: 139, 3: 199, 4: 259, 8: 501 ns unlit). `skin_plan` spreads a part over up to 8 slots (each node on the least-used free slot) though a Halo vertex blends at most two nodes [D], and bending batches are triangle lists. Both are open: SKINW and SKINSTRIP (gap C11). The GE can't take the posing itself: no hierarchy and no output but pixels, so node matrices stay CPU (or ME) work [D] |
| Hardware lights | yes on models: all 4 (2 distant + 2 point, `sceGuLight`) | A dynamic light adds no pass to a model: its falloff and cone are folded into one value per model, and rarely more than two lights reach one [D/E, recon's light census lc1 41904975882]. **`sceGuLightSpot` is never called**, so a spot on models would be a look change (a cone across the model instead of one value), not a capacity one: Sheepdog01's A/B. The world's dynamic lights are a separate pass (the flashlight, machines' lights), worked out per vertex on the ME |
| Call lists | yes, hand-built (BASE/CALL/RET) | BASE + CALL + RET 0.33 µs (~32 plain commands at 10.4 ns) [M, bench 17; earlier 0.27 µs]. A bone matrix load (13 words) 132 ns in the list, 164–191 ns called from RAM in eights, 105 ns called from VRAM [M]. The bindblocks showed no saving [M]. Closed: a call is cheap, and nothing it holds is expensive to fetch |
| Texture formats | CLUT8 and CLUT4, swizzled, mipmapped | **DXT1/3/5 never used, and closed by the bench** [M, bench 17, both PSPs]: at 1:1 the GE samples DXT1 17.1 (RAM) / 24.2 (VRAM) ns a pixel, DXT3 and DXT5 19.6 / 14.9, against CLUT8 6.9 / 7.4 and CLUT4 5.9 / 7.1 (8888 15.2 / 12.3; flat fill 4.95); minified 2:1 at level 0, DXT1 49.5 against CLUT8 11.3. DXT1 holds the same 4 bits a texel as CLUT4 at ~3x the sampling cost; VRBTEX's in-game bench found 3–65x. Only room, never time, could pay for it, and CLUT4 gives the same room where 16 colours hold the look |
| Mipmaps | yes, every CLUT texture | Closed. What a level saves at 2:1 [M, bench 17]: CLUT8 11.3 → 7.2 ns a pixel (−36%), DXT1 49.5 → 17.7 (−64%). Whether every minified draw samples its right level (the LOD bias) is a separate question |
| Fog, dither, alpha test, blend, depth, scissor, culling, texture matrix, environment map | yes | — |
| The GE's clip planes switch | enabled; no plane set | The GE clips at the near plane only; the CPU cuts far triangles [D] |
| Bounding-box culling in a list (`sceGuBeginObject`) | no | A box that passes costs 46 ns; one that fails costs 1.18 µs in place of its draw [M, bench 17; the feasibility note had ~1.6 µs]. Cheap, but BBX's count leaves it nothing: 1.7–6.8 draws and 25–79 triangles a frame wholly off screen (~0.05 ms of GE at 211 ns a vertex). The keep word `noboxtest` is a CPU test, not this |
| Morphing (`sceGuMorphWeight`) | no | Halo's models have no per-vertex blend shapes [U]. Its rate [M, bench 17]: like a weight, ~65 ns a morph target above the lighting floor (2: 211, 4: 261, 8: 521 ns a vertex, lit). Closed |
| Patches (`sceGuDrawBezier`, `sceGuDrawSpline`) | no | Halo has no curved surfaces [U] |
| Stencil (`sceGuStencilFunc`, `sceGuStencilOp`) | no | Replaced by the stencil bake (the GE's stencil is too limited) [D] |
| Logic ops, colour test | no | No Halo material needs one [U] |
| Material colours (`sceGuColorMaterial`, `sceGuModelColor`, `sceGuAmbientColor`) | no | Colours come with the vertices [U]. Whether material colours could shrink vertices is open |

### The GE's rates (bench 17, 2026-11-04)

Measured on both PSPs (PSP-1000 lab bench-hw 41960963933, PSP-3000 41960453758: equal to 0.1% on every vertex case), one guarded list a case, 5 rounds, the empty list subtracted. Synthetic lists with nothing else on the bus, so these are the GE's own rates, not its cost to the CPU [M]:

| What | ns |
|---|---|
| A vertex, the rigid record (float uv, 8-bit normal, float position): unlit / 2 distant lights / 2 distant + 2 point / + specular | 65 / 96 / 211 / 320 |
| A vertex, the bending record by weights, unlit: 1 / 2 / 3 / 4 / 5 / 6 / 8 | 82 / 139 / 199 / 259 / 320 / 380 / 501 |
| The same lit (2 distant + 2 point): the slower of lighting and skinning sets the rate | 211 up to 3 weights, then as unlit |
| A vertex from VRAM against RAM; 16-bit positions and uv against float | the same (within 1.4%) |
| An index (indexed mesh, each vertex in ~6 triangles): rigid lit / 8 weights | 255 / 501: no reuse, every index is a transform |
| A strip vertex | as a list vertex: a strip triangle costs ~1 transform, a list triangle 3 |
| A plain command; BASE + CALL + RET | 10.4; 328 |
| A pixel, flat fill (32-px strips) | 4.95 |

What it changes [U on these M]:
- **The GE's vertex work is compute, not fetch.** Moving vertices to VRAM or shrinking them (MV14, MVDIET) changes the bus the CPU shares, never the GE's own time. Less GE time on models means fewer transforms or cheaper ones.
- **A bending part at 8 slots costs 2.4x a rigid part's vertex with 4 lights, 5.2x with 2 distant lights.** In a list, its triangle is 3 transforms: ~1.5 µs at 8 weights against ~0.15–0.4 µs for a rigid strip triangle (1.2–1.6 indices at 96–255 ns). Bending models are where the GE's vertex time goes, where the GE binds (c10's later stretch, d20's heavy view, b30's flight with its marines and Pelican). Gap C11.

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
| C4 | **The bus's priority** | If it exists, up to the GE's 8–18 ms cost to the CPU | No public source has one (§6.3); disassembly closed. The register snapshot (§6.4, run 2, 9 of 11 states) found no register that moves with load beyond clock, status and address words: none of the documented ones is a priority setting [M]. A static word may be one (DDR 0x30), testable only by a write (Sheepdog01's call). The snapshot's stops were not the registers: kernel-mode time at a user address (seg-0 kcalls) shut the PSP down, the ME running or not (§6.4, §6.5) | Closed by reading |
| C5 | **The VME's capability** | A third processor for fixed work | **Done 2026-11-04** (`vme_capability.md`): computes on both PSPs, 2 ops a cycle; candidates sized there (none ready: the mix's tail only, decode and palettes don't fit) | — |
| C6 | **The VFPU's coverage:** the float inventory against tock161, and the approximation ops | Tick and drawing maths | Refresh `hardware_utilization.md` §3 | Code reading, emulator |
| C7 | ~~DXT textures~~ **closed** | Room only: the GE samples DXT1/3/5 at 2–3.4x CLUT8's cost and ~3x CLUT4's at DXT1's size (bench 17 [M]) | — | — |
| C8 | **DMA for more copies** | CPU time on bulk copies, weighed against its bus cost | Count the CPU's bulk copies | Emulator census |
| C9 | **The eDRAM's map:** all 2 MB, who holds what | Room for ME code and data | One page | Code reading |
| C10 | The rest (BBOX, clip planes, morphing, patches, stencil, logic ops, material colours, AVC, sceSas) | Small or nothing, each with its reason above (BBOX and morphing priced by bench 17) | — | — |
| C11 | **The GE's vertex work on bending parts** (SKINW, SKINSTRIP in the lab's levers) | GE time where the GE binds: up to 58% of an 8-slot batch's vertex time with 4 lights, 72% with 2 lights (fewer weights a draw), and ~45–60% more as strips [U on M] | A census in the emulator: skinned indices a frame by the draw's weight count, and the strip lengths the batches would take, on b30's flight, c10's later stretch and d20's heavy view. Then the plan change (first-fit slots, each batch drawn at its highest slot, strips) and a PSP pair | Emulator census, then a PSP pair |

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

### 6.4 The register snapshot by our own bench (18 runs on the PSP-1000; why it stopped)

- **The bench:** `bench/halo_bench16_regsnap` on branch `research/regsnap`.
  - It reads, in kernel mode and read-only, 241 documented registers in 11 blocks: memory access, system control, clock ratios, profiler, DMACplus, DMAC0/1, DDR, GE, GE eDRAM and audio. The ME reads its own short list (the clock-ratio words, its view of system control, the AVC transfer words).
  - The states, in the second run's order: before the ME starts, the ME idle, the ME streaming eDRAM, the ME streaming main RAM, the GE texturing from main RAM, the system DMAC, DMACplus into the eDRAM (the ME idle), the converter's channel playing, all of them together, the ME idle again, and last the GE texturing from VRAM.
  - Blocks whose bus clock is off are skipped. The fuse ID (0xBC100090–98) is never read.
  - Since f5d0e898 each state's values go to the stick as it ends, and while the GE is busy each GE and GE eDRAM register is read in a kernel call of its own, its address flushed first, so the log's last word names a read that hangs.

- **Run 1** (98a86766, bench-hw 41923464223; log recovered by psp-probe 41926842519) [M]:
  - The first pass read all 11 blocks idle, 2–7 µs a block (DMAC0/1 clock off, skipped). States 0–4 completed, 37–48 µs a snapshot. The ME's counter ran 331.35 cycles a µs (§6.2: the ME at the CPU's full clock).
  - It hung in the GE-from-VRAM state, untraced, with every value still in memory.

- **Run 2** (f5d0e898, bench-hw 41942770497, launched 18:38:32; the PSP **switched itself off** ~18:40, Sheepdog01's report) [M]:
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

- **Runs 3–18: what the stops need** (Sierra-III `research/regsnap` d1fe984b → ccc5d1ad, one PSP-1000 run each; the PSP on AC throughout, battery 99 %, 4218 mV, 20 °C in every supply line) [M unless marked]:
  - **Each stop was the PSP switching itself off** (Sheepdog01 saw runs 2–17; run 1, unseen, ended with no answer at wall + 4 min, which a shutdown would also give). Not a freeze and not a reboot: like B47.

    | Run | Bench | Where it stopped, or "clean" |
    |---|---|---|
    | 3 41944515417 | the states, traced | state 4 (GE from RAM), sample 5, on 0xBD500080 |
    | 4 41945381801 | eDRAM left out under GE load | state 5 (system DMAC), untraced |
    | 5 41947077100 | eDRAM probe, **no ME** | clean: 132,000 eDRAM reads (GE idle, under RAM and VRAM load, after), 30 s of GE load with no reads |
    | 6 41948011177 | eDRAM probe, ME started | the ME 20 s alone clean; then GE idle, round 25 on 0xBD500040 (~27,000 reads) |
    | 7 41949300166 | block probe, ME | round 0, the GE block (its clock off) |
    | 8 41950051159 | block probe, GE blocks gated | round 8, system control (~100,000 reads of the plain blocks first) |
    | 9 41950804503 | control probe, ME | **the ME alone 90 s clean; 1,223 stick writes clean**; then interrupt-off kcalls reading only main RAM: ~1.4 s, ~52 calls |
    | 10 41951469181 | the kcall taken apart, ME | clean: 1,205 user-mode I-cache invalidates, 1,219 interrupt suspends, 1,219 empty kcalls; then the main-RAM kcalls: ~1.3 s, ~49 calls |
    | 11 41952011001 | the reads taken apart, ME | interrupt-off kcalls reading **through the cache** (no bus traffic past 64 lines): ~3 s, ~100 calls |
    | 12 41952406335 | the interrupts taken apart, ME | clean: **795,000 uncached main-RAM reads from user mode, interrupts on**; then cached kcalls with **interrupts on**: ~3 s |
    | 13 41952854966 | time in the kcall, ME | clean: 800 kcalls spinning 2 µs (≤ 8 µs measured); **kcalls spinning 10 µs, interrupts on, a counted nop loop touching nothing** (the timer read only before and after): ~1–2 s, ~80 calls |
    | 14 41954147841 | 10 µs spins, **the ME never started** | ~36 calls, ~1 s, in the first stretch: the ME isn't needed |
    | 15 41954703761 | 3 µs spins, no ME | ~615 calls, 15 s |
    | 16 41954888207 | 10 µs spins, no ME, **a 20 ms delay in place of the stick write** | ~100 calls, ~2 s: the stick isn't needed either |
    | 17 41955069726 | 10 µs spins, no ME, **at the kernel alias** (seg 0x80000000) | **clean: 809 calls of 35–103 µs**; then 10 µs at seg 0 with **interrupts off**: ~187 calls, ~4 s |
    | 18 41955337648 | 10 µs spins at the alias: interrupts on, off, then with the ME polling | **clean: 810, 814 and 812 calls**, every call 35–97 µs |

  - **A spin's "µs" is nominal** (166 turns of a nop loop a µs). The alias stretches timed it at 35 µs a 10 µs call (35–36 with interrupts off), so a nominal µs is ~3.5 real ones [M]: 2 µs ≈ 7 µs (run 13 measured ≤ 8), 3 µs ≈ 10 µs, 10 µs ≈ 35 µs. The seg-0 stretches never reached their closing line, so their length is taken from the same loop at the alias [U, the same instructions].
  - **The cause [M for the split, U for the mechanism]: kernel-mode time with the PC in the user segment.** mcidclan's `kcall(f, seg)` jumps to `seg | f`. With seg 0 (every call in this bench before run 17, and every kcall the game makes, §6.5) the function runs in kernel mode at its user address, 0x08xxxxxx. That is what switches the PSP off:
    - **Not the ME:** without it, 10 µs spins stopped in ~36 calls (run 14) and 3 µs ones in ~615 (run 15).
    - **Not the stick:** with a delay in place of each call's stick write, ~100 calls (run 16).
    - **Not interrupts arriving inside the call alone:** with them off, ~187 calls (run 17). Those that arrive during the hold are taken at the resume, which is still inside the call.
    - **Not kcall's way in, its I-cache invalidate or kernel mode as such:** the same spin at the kernel's cached alias of the same code (seg 0x80000000, the segment me-core-mapper's own mutex calls use) ran 3,245 calls clean, interrupts on and off, with and without the ME (runs 17–18, ~114 ms in the calls).
  - **It is a rate, not a threshold** [M, the fit E]. Runs 13–17 at seg 0 gave 5 shutdowns in ~26 ms of time inside the calls (7 µs × 800 clean, 35 µs × ~80, ~36, ~100 and ~187, 10 µs × ~615): **about one per ~5 ms of user-segment kernel time**, in calls of any length. At that rate the alias's 114 ms would have given ~22. So no seg-0 call is safe, only a short one is unlikely to hit: an empty kcall's ~1 µs is ~0.02 % a call.
  - **The mechanism [U].** The rate, ~200 a second, is near the PSP's interrupt rate (the vblank, the system timer, the stick's driver). The likeliest reading: an interrupt taken while the CPU is in kernel mode with its PC in the user segment is mishandled by the firmware's exception path (its context judged by the address), and the kernel's fault handling powers the PSP down. Not tested further: the split above is what our code needs.
  - So the register reads were never the cause, and the ME was beside it, not in it. Every regsnap stop sat in a stretch of 5–40 µs seg-0 kcalls; the blocks the trails named (eDRAM, GE, system control) are where the next long call fell.
  - **mcidclan's `kcall()`** (me-core-mapper `kernel/main.c`) [D]: a plain export of a kernel module (attribute 0x1006). It invalidates the I-cache and calls `seg | f`. It touches neither k1 nor the stack; the firmware's syscall entry sets both, as for any syscall.
- **For C4 the values above stand** (states 0–8 of run 2, read in kcalls the PSP survived). The stops were the bench's way of reading them, not the registers.
- **The bench:** `ctlx` in Sierra-III `research/regsnap` ccc5d1ad takes a plan of spin stretches as keep words (`x<µs>` with `k` the alias, `o` interrupts off, `d` a delay in place of the stick write, `m` the ME started), so a split needs no rebuild.

### 6.5 Rules for our own code in kernel mode [M from §6.4's runs; E for call lengths read in code; U where marked]

- **Run kernel-mode code at a kernel address, never at its user address.** Call mcidclan's `kcall()` with seg `0x80000000` (`CACHED_KERNEL_MASK`), so the function runs at the kernel's cached alias of the same code (0x88xxxxxx). With seg 0 it runs in kernel mode at 0x08xxxxxx, and every µs there carries about a 1-in-5,000 chance of switching the PSP off, the ME running or not (§6.4).
  - The same holds for any user-address function the firmware calls in kernel mode: a sysevent handler, an exception handler. Pass their alias.
  - At the alias, the function's direct calls (`jal`) stay in the alias (a `jal` keeps the PC's top bits), and its import stubs work as before. A call through a function pointer to a plain user address would go back to the user segment: keep kernel functions free of them, or alias the pointer.
  - Firmware calls (stick writes, cache calls, interrupt suspend) and real kernel modules (`halo_profile.prx`, `halo_crash.prx`) run at kernel addresses already: measured safe.
- **Safe beside a running ME, measured:** user-mode memory traffic, uncached or not (795,000 reads in 20 s); the firmware's own syscalls, stick writes included; user-mode cache and interrupt calls; alias kcalls of any length (3,245 calls of 35–97 µs). The ME's polling is not a partner.
- **Hardware registers are not themselves the hazard.** Still, keep the GE's register space (0xBD4xxxxx, 0xBD5xxxxx) out of our code: the firmware's GE driver owns it, and with the GE's clock off (0xBC100050 bit 1) its registers read one stale bus word. The bus probe no longer reads them (Sierra-III tock162 c3f3dba5, b150 7e2f647a).
- **Where the game stands** (tock162 c3f3dba5, `psp/src/psp_me.c` and the me-core-mapper library it links). Every entry is at seg 0 today. Lengths are the time at a user PC, read from the code [E]; time inside a firmware call it makes doesn't count:

  | Where | Kernel-mode code at a user address | When | Length [E] |
  |---|---|---|---|
  | 2276 `meLibDefaultInit()` (library) | `kcall(meLibInit, 0)`: the witness word, the table pick, `meLibReset` (the ~276-byte handler copied to 0xBFC00000 uncached, then the cache calls and the reset pulse) | each launch | ~2–10 µs |
  | same, `kinit(eventHandler)` (library) | the library's `eventHandler`, installed as the firmware's ME sysevent handler; on sleep and wake it runs the game's `meLibOnSleep` / `meLibOnWake` (with `mewakeearly`, the whole restart) | every sysevent of that handler; sleep, wake | < 1 µs a call; ~5–10 µs with `mewakeearly` |
  | 2190 `me_cpu_number_kernel` | one CP0 read | launch | < 1 µs |
  | 2334 `me_sysregs_running_kernel` / `me_sysregs_kernel` | 4–8 register reads | launch | ~1 µs |
  | 1981, 2043, 2303, 4272 `me_hold_reset` | the reset written | an ME stop or fault, the exit, a failed start | < 1 µs |
  | 2047, 4269 `me_sysregs_kernel` | 8 register reads | exit; after a restart | ~1 µs |
  | 4264 `me_restart_kernel` | two register snapshots, the reset held, the stub and handler (~376 bytes) copied uncached, the reset released | each wake from sleep (`psp_me_wake_poll`) | ~3–10 µs |
  | 4334, 4354 `me_sleep_kernel`, `me_wake_kernel` | `meLibOnSleep` / `meLibOnWake` | `mewaketest` only | < 1 µs |
  | 2151–2158 `dmacp_start/poll/stop_kernel` | a few DMACplus registers a call, the poll a loop of calls | `sndtierdma` only (off) | < 1 µs a call; the poll's total grows with the transfer |
  | `halo_crash.prx`'s handler (0x08AC28FC in one build) | the game's exception handler, entered in kernel mode at its user address | a crash only | n/a |

  - So a launch spends some ~5–15 µs at a user PC in kernel mode, a wake ~5–10: about **0.1–0.3 % a launch** at §6.4's rate [E]. Over the rig's hundreds of launches that is a few shutdowns. **B47** (the PSP-3000 off with the log ending at `ME: starting the Media Engine`, i.e. inside `meLibDefaultInit`) fits it [U]; the bus probe it was put on is cleared by §6.4 (the registers aren't the hazard, and `halo_profile.prx` runs at kernel addresses).
- **The fix** (a proposal for the master, the lab's `coord/handoff/regsnap3_2026-11-04.md`): every kcall through the alias, the launch's library init done by the game itself through the alias, and the sysevent handler installed at its alias.
- **The lab's benches that make many kcalls:** halo_bench16_regsnap (every run before ctlx's `k`); halo_bench15_vmecap (recon/vme-r4: 11 kcall sites, tens of calls, csc and hardware-mutex tests; vme-dma, vme-dmafast: 3), whose own seg-0 calls are short but whose result cut short by a shutdown would look like this. Pass seg `0x80000000` in new bench code.
