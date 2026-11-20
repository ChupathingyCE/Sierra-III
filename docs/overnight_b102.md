# Overnight run, 2026-10-07: build 102

Labels: **[M]** measured, **[E]** estimated, **[H]** needs the hardware to confirm. Emulator numbers count cache hits and misses (deterministic enough to compare configurations); they say nothing about the PSP's own timing.

## What changed in b102

**Default behaviour (fixes):**

| Change | Why | Evidence |
|---|---|---|
| **Object lighting from the real lightmaps** | The game lights every object from the lightmap texel under it (`sample_lightmap` → `bitmap_2d_get_pixel`). On the PSP the lightmap pixels live in our texture pack, so the read answered "no pixel" = white: every object (characters, vehicles, weapons, rocks, decorations) lit as if standing in full light, the bug Sheepdog01 saw. Now sampled from the pack (`psp_tex_light_texel`): the resident texel, or one byte plus one palette entry read from the pack while the map loads, at most 2 reads a frame in play, else the page's last colour. The surface colour (second light's tint) comes from the texture's average. | [M] emulator, The Library, same spot: the rifle goes from pale grey to the room's dark blue. 99% of samples from resident textures. `lightwhite` in the launch menu restores the old look. |
| **The crash handler, rebuilt** | pspsdk's handler returns from the exception into the handler's code, which ran in kernel memory with the crashed thread still in user mode: an instant second exception, for ever. That's why no crash ever showed anything. Now (as ARK-4's own crash-screen module does) the kernel stub saves the registers into the game's memory and returns into a handler in the game (`psp_crash.c`), on its own stack: it draws the report on both frame buffers, writes `halo/crash.txt`, and flushes the log. A magenta band along the top = the stub ran; yellow = a second exception (the handler itself crashed). | [M] the game's half in the emulator (`crashdraw` shows the report, writes crash.txt). [H] the kernel half needs **crashplay** on the PSP. |
| **Heap +256 KB** (4672 → 4928 KB) | The AI debugger's static state (231 KB: the console's path debugger and the selected squad's records) is now allocated only if the console's `ai_debug_path` is used; the log's buffers are back to 8 KB (the blackbox gets its 64 KB from free system memory instead). | [M] build log. |
| **Sound cache 1 MB → 1.125 MB** | Half the freed memory. Sheepdog01's b101 run 2 streamed 99 MB of sound in 6.5 minutes (2,954 loads, 35 failed for room). The other 128 KB stays as headroom: the b101 hardware peak was 4,660 of 4,672 KB. | [E] |
| **Colour conversions in single precision** | The decomp rounds colours via doubles, which the PSP does in software (~400 instructions a channel). Found by the I-cache profile (below). | [M] emulator, The Library, same shots: the frame's CPU 11.0 → 9.5 ms (−14%), `widgets_render` 2.25 → 1.45 ms; the HUD looks the same. [H] the PSP's own gain. |
| **Load trace only with `blackbox`** | It wrote hundreds of lines per map load; Blood Gulch's preload took 9 s instead of 1.2 s. | [M] b101 run 3. |
| **Memory Stick time per thread** | The stick lock (b100) waited 59 s in all, 0.9 s at worst, in b101 run 2. The heartbeat now says which thread spends what in stick calls, and waits, every 30 s. The lock stays on by default: `stickparallel` turns it off for an A/B. | [H] |

**Options (launch menu), not default:**
- `assettrack` / `assetdetail`: the maps' `_track` / `_detail` texture packs (below).
- `geo15` / `geo2`: the campaign geometry cache at 1.5 / 2 MB (taken from the texture RAM cache).
- `machinesleep`: idle machines sleep (null result, below).
- `lightwhite`: objects lit as before b102.
- `crashdraw` (keep.txt only): the crash report drawn 20 s in, no exception.
- benchrun: 8 switches now (below).

## Memory ledger (b102, PSP-1000)

| Region | Size | Use | Room |
|---|---:|---|---|
| program code + constants | 3.27 MB | 2.81 MB code, 0.46 MB read-only data | |
| program data + zeroed data | 2.30 MB | display list 384 KB, render globals 133 KB, game statics | display list peak 235 KB of 384 (b101): 64–128 KB possible, at a risk of skipped draws |
| low reserve | 256 KB | main thread, early allocations | |
| heap | **4,928 KB** (b101: 4,672) | sound cache 1,152 KB, level vertex copies ≤ 1,024 KB, the game's allocations | b101's hardware peak was 4,660 of 4,672; b102 leaves ~140 KB spare |
| game state | 3,348 KB | Xbox-sized pools | **actors 457 KB for 256 (peak 16), props 234 KB for 768 (peak 63), lights 108 KB for 896 (peak 77)**: sized for the PSP (128 / 256 / 256) that's ~420 KB, if the game state's base moves up to give it to the heap. Every campaign map's peaks need checking first |
| tag window | 9 MB | the map's tags + its largest BSP | The Library uses 8.99 MB of it; a10, b30, b40 need 11.3–12.3 (below) |
| top 1 MB | 1 MB | thread stacks, crash block (16 KB), the blackbox's log (64 KB, only with it on) | ~560 KB free in play |
| volatile partition | 4 MB | texture RAM cache 3 MB + geometry cache 1 MB (campaign); `geo2`: 2 + 2 | |
| VRAM | 2 MB | 2 frame buffers 544 KB, depth 272 KB, motion sensor 16 KB, texture cache 1,216 KB | |

## Strict-mode sweep (interpreter, PSP-1000 memory map, unaligned + range + FPU checks)

| Run | Game time covered | Findings |
|---|---|---|
| The Library: intro cinematic | 18 s | none |
| The Library: combat (firing, grenades, Flood hit), b102 | 65 s | none |
| Blood Gulch: firing, grenades | 116 s | none |

Final b102, normal speed: Blood Gulch and the other eleven multiplayer maps all load and play at 30 ticks a second in the emulator; The Library's cinematic, combat, BSP switch and Sheepdog01's route all run.

The interpreter runs about 23× slower than real time, so each run covers well under a minute of play. With the volatile-RAM false positive removed from the checker, logs are clean. The b101 alignment fix holds.

## The BSP switch [M]

`switch_bsp 1` in the emulator: no fault, BSP 1 drawn. All four c20 BSPs load at 0x09A9E000–0x09BA0000 and end at 0x09F00000: inside the 9 MB window, below the PSP-1000's 0x0A000000.

## Bandwidth: what the evidence says before building more

b87 and b93 showed that **halving the level's vertex bytes changed nothing** on hardware (`levelcompact`, three times). The GE's main-RAM reads do cost 4–17 ms a frame (b93), split between models and the level, but *which* bytes was still open: vertices or texels. Converting model vertices to a compact format (the planned item) would likely repeat levelcompact's null result. So instead:

1. **benchrun now splits texels from vertices** (8 switches; `level compact` and `list in VRAM`, both settled, are out):
   baseline · GE serial · GE skips models · GE skips the level · **stand-in texture** (every draw binds one 16×16 VRAM texture: no RAM texels at all) · **stand-in lightmaps** (only the lightmap pass) · **textures one level smaller** (mip bias +1, the remedy if texels are it) · GE busy on VRAM only.
   If "stand-in texture" recovers most of the overlap cost, the next work is texture bytes, not vertices.
2. **The Library's textures are 78% lightmaps:** 306 single-level pages, 10.9 MB of the 14.0 MB pack, which itself is 3× the 4.3 MB of texture caches. Lightmaps also have no mip levels, so far surfaces read them scattered.

## Asset track: half-size lightmaps [M]

`texpack.py --lightmap-scale 2`: each lightmap page shrunk to half, after growing every chart's colours outward from the level's own lightmap triangles (rasterized from the BSP's lightmap UVs, `lightmap_masks.py`), so the shrink mixes no gutter into the seams. The Library's pack: **14.3 → 6.7 MB**. Same spot in the emulator: visually the same at 480×272.

Cache sizing on **Sheepdog01's own b101 route** (`route.txt` replays the 92 positions from his log's "player: at" lines, 2 s apart):

| Textures | Geometry cache | Geometry misses | Frames missing a block | Geometry read | Textures read | Untextured frames |
|---|---|---:|---:|---:|---:|---:|
| usual | 1 MB | 1,163 | 111 | 5.7 MB | 4.7 MB | 27 |
| usual | 1.5 MB | 915 | 96 | 4.5 MB | 4.8 MB | 27 |
| usual | 2 MB | 673 | 55 | 2.9 MB | 4.7 MB | 26 |
| track | 1 MB | 896 | 88 | 4.2 MB | 3.0 MB | 15 |
| track | 1.5 MB | 844 | 79 | 4.0 MB | 4.2 MB | 19 |
| track | 2 MB | 686 | 58 | 2.9 MB | 3.9 MB | 17 |

The final b102 (with the 1.125 MB sound cache), track + 2 MB: 607 misses, 43 frames missing a block, 2.5 + 3.7 MB read, 16 untextured frames, and **no sound load failed for room** (4–21 a run with 1 MB).

Reading: **a 2 MB geometry cache halves the frames missing geometry and costs the textures nothing** (their RAM cache gives up 1 MB and reads the same). With the track's textures as well, the Memory Stick reads about a third less in all (10.3 → 6.8 MB on this route). The emulator has no stick timing, so whether that removes the flashing is [H]: the Library walk with `geo2` and `assettrack`.

## Asset track: plants that keep their leaves [M]

`texpack.py --alpha-coverage`: alpha-tested textures (model shaders unless "not alpha tested", environment shaders with "alpha tested") get mip levels whose alpha is rescaled so the same share of texels passes the GE's test (alpha > 127) as at the top level. Blood Gulch: 19 textures changed. Example (128×128 foliage): coverage at 16×16 was 0.348, now 0.387 (top 0.383); at 8×8 0.281 → 0.359. That's the thinning that makes far plants shimmer. In the track packs (the bake script bakes them together).

## Per-class detail (lever, off) [M]

`texpack.py --full-detail fp,weapons,vehicles,characters` keeps those classes' top level and lifts the cap to 512; the bake script's `--detail` makes `_detail.ptex`, chosen with `assetdetail`. Cost on Blood Gulch for **first-person only: 33 textures, 461 KB → 2.0 MB**. Blood Gulch's pack then outgrows the caches (6.7 MB against 5.3) and would stream. A lever to try, not a default.

## Idle machines (null result) [M]

Machines that leave themselves byte-identical sleep, with their power and position groups in the checksum (so a switch or a script wakes them), and automatic doors running their look-around ticks. On The Library: **no gain** (1.73 ms a tick either way in the emulator). Of its machine updates, 60% are type 2 (always moving), 38% type 1, 2% doors, and the idle ones were cheap. Off by default (`machinesleep`). The 7.5 ms a tick on hardware is in the always-moving machines: next idea, update far, unseen moving machines at a lower rate.

## The one-shot Linux bake [M]

`halo-psp-bake <disc image or maps folder> <output>`: a pure-Python Xbox disc reader (xiso.py), the retail maps' SHA-256 check, the Invader fork (bundled with its libraries), dez, texpack (usual + track), stencilbake. A first run makes its own Python environment (numpy, Pillow, shapely, triangle, scikit-learn). Tested here from a maps folder on ui, Blood Gulch and The Library (18 minutes for the three): **the output c20.ptex and c20.pgeo are byte-identical to Sheepdog01's**, the map identical except the pixels Sheepdog01's had zeroed. Both maps boot in the emulator.

Limits: the bundled Invader needs glibc 2.39+ (Ubuntu 24.04, Fedora 40, Arch/CachyOS); older systems build it from the included source. The disc reader is checked against images it wrote itself, not a real disc image yet [H: Sheepdog01's ISO]. Licences: Invader is GPLv3-only, so the bundle ships the fork's full source and the patch against upstream; the decomp is CC0; the port's own code needs a licence of Sheepdog01's choosing before release.

## Where the Memory Stick's time goes: sound [M]

Sheepdog01's b101 run 2 (6.5 minutes in The Library): the sound cache loaded **99 MB**, against 5.2 MB of textures and 10.3 MB of geometry over the same run. **Sound is about 86% of everything read from the stick in play**, and the likely source of most of the stick lock's waits. The Library's sounds are 55 MB in all (census, `projects/sierra/research/audio/snd_census.py`): **40 MB of it is 44.1 kHz stereo (628 music permutations)**, the rest 22 kHz mono. So the cache (1 MB, now 1.125) re-reads the same sounds over and over.

The levers, largest first [E]:
- **Music at 22 kHz** in the bake: halves the biggest stream and frees cache for the effects (needs an Xbox ADPCM re-encode in the bake: Invader's sound tool can encode; not built tonight).
- **A larger sound cache**: b102 adds 12.5%; the heartbeat's "sound cache:" line shows whether failures and loads drop.
- Keeping music out of the cache (streamed through a small ring of its own) so it stops evicting effects.

## The I-cache experiment [M]

PPSSPP's interpreter now models the Allegrex's instruction cache (16 KB, 2-way, 64-byte lines) and can measure a reordered program on the same run without building it (`PSP_ICACHE_MAP`). The Library in combat, b102:

- 1.1% of instruction fetches miss; 50% of fetches come from 39 KB of code, 90% from 278 KB. The hot path is larger than the cache: the misses are capacity, not layout.

| Layout (measured on a second run, profile from the first) | I-cache misses |
|---|---:|
| as linked | baseline |
| cold parts (`.text.unlikely.*`) together, away from their functions | **−3.2%** |
| 16 KB of the hottest functions first | −0.8% |
| 32 KB hottest first + cold parts apart | −1.7% |
| 64 KB hottest first + cold parts apart | +1.5% |

**Ordering buys about 3% of the I-cache misses at best:** about 1% of the frame. Not worth an A/B build. `psp_build.py` can still do it (`PSP_TEXT_LAYOUT=coldfirst`). What reduces capacity misses is less code on the hot path.

The profile found something bigger: **software double precision was ~5% of all instructions.** The decomp converts colours the x87 way, `(long)rint((double)c * 255)`, and the Allegrex has no double-precision FPU. Most of it was `real_argb_color_to_pixel32` (the HUD and widget colours, every frame). b102 does these in single precision (`psp_fixups.py float_colours`): one colour bit can differ, now and then. Also in the profile: the debug overlay's text (`pspDebugScreenPutChar`, 0.8%: the launch menu's "overlay off" saves it), and `memset` at 3.4%.

## Not done, and why

- **Compact model vertices:** deferred behind the benchrun texel split (above).
- **Animation working set:** not measured. The keyframed animations (-K) already took The Library's from 2.68 to 1.43 MB of the tag window (campaign_census).
- **Strict sweeps of b30, a10, b40:** not possible yet. Built with today's full recipe (-G, -K, packed collision), they still don't fit the PSP-1000's 9 MB window [M]: **Pillar of Autumn 11.37 MB, Silent Cartographer 11.34 MB, Assault on the Control Room 12.31 MB**. The Library itself fills 8.99 of 9.00 MB. The other campaign levels need 2.4–3.3 MB more out of the window first (campaign_census lists the levers: the BSP's collision and position copy, cinematic animations, sound tag metadata).
- **VFPU/ME survey:** the candidates, from b101's worst window:
  - always-moving machines, 7.5 ms a tick: less work for far, unseen ones, not offloading;
  - bipeds, 8.8 ms a tick (type update 5.1, node matrices 1.7): the node matrices are VFPU-shaped (4×4 products are already on the VFPU: 1,528 a frame);
  - the GE renderer's CPU, 36 ms a frame: the level draws (11 ms) and the angle-bucket sort (2.9 ms) are per-draw bookkeeping, not maths;
  - the dynamic lights' pass (6.5 ms): per-triangle selection around each light.
  - The Media Engine, idle 94% of the time with audio on it, suits self-contained jobs on its own memory. The candidate it fits best is the sound streaming itself (above).

## Morning hardware tests (in order)

1. **crashplay** (launch menu): ~46 s into play on the overlay, a deliberate crash. Expect a **magenta band** at the top, then a red band and the register report, and `halo/crash.txt`. Photo it. Magenta only, or yellow: the handler ran but something after it didn't (say which). Nothing: the stub never ran.
2. **The Library, b102 defaults**, the same walk as b101 run 2. Look at characters, vehicles, the rifle and the rocks against the room's light, compared with b101. The log's "object lighting", "stick (...)" and "sound cache" lines say the rest.
3. **The Library, asset track**: the launch menu's `assettrack` and `geo2` (the zip puts `c20_track.ptex` in halo/maps). Same walk: the flashing at area transitions, and the lightmaps' look.
4. **Blood Gulch benchrun**, defaults: the 8-switch run (~6 minutes, don't touch anything). The "stand-in texture" lines against "baseline" answer texels versus vertices.
5. Optional: Library with `stickparallel`, for the stick lock's A/B.
