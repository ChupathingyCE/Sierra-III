# Memory traffic is the crux

**The governing principle of the port's performance (agreed with Sheepdog01, 2026-10-05):** on the PSP-1000, the main CPU and the graphics chip (GE) share one main-RAM bus. The CPU can't hide a cache miss, so anything that reads main RAM in bulk while the game runs slows the game, whichever unit does the reading. Frame time is mostly a question of **how many bytes cross the main-RAM bus per frame**, not how many instructions run or how busy the GE is.

Every optimisation is judged first by what it does to main-RAM traffic.

## The evidence (independent sources)
| Source | What it showed | Status |
|---|---|---|
| halo_bench v7 | The Allegrex stalls on every missed load: no hit-under-miss, no miss overlap. A miss is ~72 cycles (~220 ns). | measured on hardware |
| halo_bench v7 | DMA running alongside the CPU: CPU misses 218 → **499 ns**. | measured on hardware |
| halo_bench v8b ("the bus rule") | With the GE busy: VRAM textures/vertices leave CPU misses unchanged; **RAM textures +65–96%**; **RAM vertices +272%** (804 ns). | measured on hardware |
| x86 profile of the tick | About half the tick is miss stalls (~110–150 K misses a tick on a 16 KB cache). | measured on x86; projected to the PSP |
| b78 (Soften / temporal blend) | One 255 KB buffer copied to RAM and read back each frame cost **~7–8 ms**, far above the GE time it should take. | measured on hardware |
| **b79 run 3 (GE serial)** | Same spot: overlapped, the CPU took ~62 ms; serialised, ~44 ms of CPU + ~20.5 ms GE. **Overlapping the GE slowed the CPU by ~18 ms, almost the GE's whole busy time.** The GE was reading 4 MB of RAM-resident textures and RAM vertex copies. | measured on hardware (two windows; inference) |
| halo_bench v10 | Memory Stick DMA barely competes (misses +5%); the Media Engine idle or busy costs the CPU nothing measurable. | measured on hardware |
| b85 vramcold + GE serial | Spawn view: the overlap costs the CPU ~16 ms; with the large textures moved to RAM (vramcold), the same. **Inconclusive, see b87:** the normal layout was already serving ~90% of binds from RAM, so the two layouts barely differed. | measured on hardware; inference withdrawn |
| **b87 benchrun** | Three stable views: GE busy 18–27 ms; **the overlap costs the CPU 12–19 ms**. **levelcompact (level vertices 20 → 10 bytes): no change** (−0.1 to +0.1 ms). And the log showed **90% of texture binds served from RAM**, because the learned VRAM ranking had never been saved since b68 (below). | measured on hardware |
| b86 ME audio | Mixing on the ME: the main CPU's share fell from ~500 to ~110 µs a block, and frames were unchanged (61–63 ms both ways). The ME working on its own buffers doesn't slow the CPU. | measured on hardware |
| **b93 benchrun** | **GE kept as busy as usual but on VRAM only: the CPU runs as fast as with the GE idle.** The slowdown is the GE's main-RAM reads, nothing else. It costs **4–17 ms of the whole frame** (11–29%), not only the renderer. Models and the level each carry about half; the display list ~10%. | measured on hardware (below) |

## What follows from it
1. **VRAM is a performance resource, not just storage.** Textures and vertices the GE reads from VRAM cost the CPU nothing; the same data in RAM can cost it heavily (bench v8b). VRAM placement decisions are performance decisions.
2. **The GE's "idle time" isn't free capacity** unless the work stays in VRAM. Full-screen passes through RAM (Soften, the temporal blend) are ruled out for that reason.
3. **The offload plan is ranked by bytes saved, not by work moved:**
   - compact vertex formats fed to the GE directly, so no float copies are read every frame (b87: levelcompact, below);
   - stored command lists (less list traffic, no per-frame rebuilds);
   - the visible set's vertices and textures kept in VRAM, refilled in short bursts at cluster changes;
   - compact texture formats (4- and 8-bit palettes) so more fits in VRAM.
4. **The Media Engine is the clean extra resource:** measured twice as costing the CPU nothing when it works on its own memory (bench v10, b86 audio). That makes it suitable for self-contained jobs.
5. **Anything bulk that has to cross the bus goes early in the frame or in short bursts**, so it overlaps as little of the CPU's work as possible.

## Where the overlap cost comes from (b85 → b87)

- **b85 (withdrawn):** vramcold changed nothing, which I read as "textures aren't the cost". But the texture cache chooses its VRAM set from *learned* bind counts, saved to `<map>.vram`. That save has failed on every run since b68 (it only happened at map close and HOME exit, and the exit's file writes never land; see renderer_debugging.md). Without the file the set is "largest first", which put ~90% of binds in RAM in both of b85's layouts. The test couldn't tell them apart.
- **b87:** halving the level's vertex bytes (levelcompact) changed nothing. Level vertex traffic isn't the cost either.
- **So textures are the leading suspect again.** b46's A/B, with learning working, moved binds from 13% to 88% VRAM. **b88** saves the learned counts during play (copied at a texture census, written by the heartbeat thread). In the emulator the second launch served **97% of binds from VRAM** (79,200 VRAM vs 2,700 RAM) against 9% before.
- **The test (b88):** benchrun on a launch after the counts are saved, against b87 run 1. If the overlap cost drops from 12–19 ms, textures were the bus traffic.
- Remaining suspects if not: the display list (~110 KB a frame written by the CPU, read by the GE), and model vertices in the geometry cache.

## levelcompact (b87, a test switch, off by default)

The same level vertices at **10 bytes instead of 20**: 16-bit texture coordinates and 16-bit positions, which the GE reads natively.
- **Positions** sit on one global lattice of 1/256 unit (3.9 mm). Each material's origin is a lattice point, so the same world point lands on the same lattice point whichever material holds it (no cracks). The GE's 16-bit value (q/32768) times the material's model matrix (×128 plus the origin) is exact in floats.
- With it on, the float copies used for the near-plane cuts, shadows and lights are snapped to the same lattice, so every pass lies on the same points.
- **Texture coordinates** are q/32768 times a per-material scale, applied with `sceGuTexScale`. Lightmap coordinates are already 16-bit in the map.
- A material wider than ±128 units around its origin stays in floats; the log counts these.
- **Halves the bytes the GE reads per level vertex per pass.** b87 benchrun on hardware: **no measurable change** at any stable view. b93 again: renderer CPU 13.6 → 13.6, 31.7 → 31.1, 18.0 → 17.6, 16.6 → 16.4 ms. It stays a test switch, off.

## The texture test, answered (b92 benchrun, 2026-10-06) [M]

With the learned ranking finally saved and read, texture binds moved almost entirely to VRAM: the twelve most-bound textures from 3% VRAM (b87) to 100%, and the rest 79,410 VRAM binds against 12,008 RAM. **Frame times didn't move** (b87 → b92, baseline): blue base 44.6 → 38.4 ms (b87's window had a tick spike; renderer CPU 14.6 → 13.9), red base front 83.2 → 83.7, mid field 55.1 → 55.2, cliff top 50.4 → 49.7. The CPU still runs ~9 ms faster at red base when the GE is kept idle ("GE serial": renderer CPU 32.0 → 22.9; b87 31.7 → 22.1).

**So texture reads from RAM are not the overlap cost**, and neither is the level vertices' size (levelcompact, b87). b85's reading, withdrawn because its test was broken, turns out right. Left to test, in order of bytes the GE reads from RAM each frame:
- the display list (~90–110 KB a frame, written by the CPU and read by the GE);
- model vertices and indices in the geometry cache;
- the CPU's own cache misses rising for another reason while the GE runs (not bus traffic at all): the test is a frame with the GE busy on VRAM-only work.

**Caveat added after b93:** "binds" counts bind calls, not bytes read. The binds-per-byte ranking deliberately leaves the *largest* textures in RAM (b93: ~959 KB in VRAM, ~4,092 KB in RAM, 15–20% of binds). Large textures (ground, lightmaps) cover most of the screen, so they may carry most of the texel bytes. b92 moved the many small textures, not necessarily the bytes. Not yet ruled out.

## Open measurements
- ~~b88 benchrun with learned VRAM ranking vs b87 run 1~~ done (b92): no effect; see above.
- ~~GE busy on VRAM only~~ done (b93): the cost is the bus; see below.
- **Next split (queued, after the campaign work in b95):** a benchrun switch that binds one small VRAM stand-in texture for every draw. If the overlap cost vanishes, it was texels (large RAM textures); if the model share stays, it is model vertices (32-byte Xbox layout, re-read by every pass: two-sided, change colour, self-illumination, shadows).
- Fix before rerunning: the display list in VRAM (256 KB minus reserves) overflowed at red base (list peak 223 KB; 5,849 draws skipped with the list full), so that one cell is invalid.

## b93: switches that split the overlap cost (benchrun)

benchrun's switches are now: baseline, GE serial, levelcompact, **GE skips models**, **GE skips the level**, **display list in VRAM**, **GE busy on VRAM only** ("ME audio off" and "stencils old nudge" dropped: settled). The new ones change only what the GE does; the CPU builds each frame the same way:
- **Skips models / the level:** those draws are prepared but not sent (`sceGumDrawArray` gated per hook), so the GE neither reads their vertices nor fills their pixels. The renderer CPU time that comes back is that draw kind's share of the overlap.
- **Display list in VRAM:** the list starts in VRAM's last 256 KB (`psp_tex.c` keeps it free for the whole benchrun, so every switch has the same texture placement). The CPU's writes then cross to VRAM instead of RAM; the GE reads its commands from VRAM.
- **GE busy on VRAM only:** every real draw skipped; at the frame's start the GE copies a 512 × 256 VRAM texture over itself as sprites, sized in the settling window (GE alone, timed) to this view's serial GE time, then held for the measured window. No RAM traffic but a few hundred bytes of list. If the CPU still slows, the bus isn't the cause.
- **Counters:** benchrun starts the hardware profiler itself; each RESULT line carries the drawing phase's memory-wait share, D-cache misses per million cycles, **memory-wait cycles per miss** (the latency: rises under contention) and uncached stores.
- The skip switches show on screen: "GE skips the level" draws only models (rocks, vehicles, the player), and "GE busy on VRAM only" draws nothing real. These are expected, not load failures.

## b93 results (Sheepdog01's PSP-1000, 2026-10-06) [M]

Whole frame, ms (baseline → GE busy on VRAM only):

| View | Baseline | VRAM-only GE | Gone with the RAM traffic |
|---|---:|---:|---:|
| blue base | 38.5 | 34.1 | 4.4 (11%) |
| red base front | 82.4 | 65.3 | 17.1 (21%) |
| mid field | 55.9 | 40.6 | 15.3 (27%) |
| cliff top | 50.3 | 35.9 | 14.4 (29%) |

Renderer CPU, ms: baseline / skip models / skip level / list in VRAM / VRAM-only GE / GE serial:
- blue base 13.6 / 11.4 / 11.6 / 13.2 / 9.2 / 9.5
- red base front 31.7 / 27.6 / 26.9 / (invalid: list overflow) / 22.5 / 23.1
- mid field 18.0 / 14.5 / 15.5 / 17.2 / 11.8 / 12.1
- cliff top 16.6 / 13.7 / 13.7 / 16.1 / 10.5 / 10.9

Memory-wait cycles per D-cache miss (drawing phase): baseline 350–412; VRAM-only GE 192–207; GE serial 194–214.

What it shows:
1. **It is bus contention, settled.** With the GE busy for the same time on VRAM only, the CPU is as fast as with the GE idle, and each CPU miss costs about half as long. The GE's main-RAM reads roughly double the CPU's miss latency while they run.
2. **The cost covers the whole frame**, not only the renderer: the game tick and the engine's own rendering code slow down too. Removing it would take the three busy views back 14–17 ms each.
3. **The split adds up:** skipping models plus skipping the level ≈ the whole overlap cost (renderer CPU 4.2 vs 4.4, 8.9 vs 9.2, 6.0 vs 6.2, 5.8 vs 6.1 ms). Models and the level each about half; models a little more over the whole frame, though they are far fewer triangles.
4. **The display list is minor:** ~0.4–0.8 ms of renderer CPU.
5. **The level's share is not its vertex bytes** (levelcompact: nothing, third time). Its candidates: texels of the large RAM-resident textures, and its index data. **The models' share** could be vertices (32-byte Xbox layout, read again by each extra pass) or their texels. The stand-in texture switch separates the two.
