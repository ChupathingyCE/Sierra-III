# Blocked-list research: index and rollup (Oct 13, 2026)

Single entry point for the **Halo CE PSP: performance review checklist** blocked-list work (B1–B22). Each row links to the detailed findings file in this folder. The dashboard session can fold the proposals and tests below into the checklist.

**What applies, and what's done:** `../research_application.md` (2026-10-14) checked every item here against the code.

## Status of B1–B22

| # | Item | Status | Findings file |
|---|---|---|---|
| B1 | Insomniac R&D (14 talks) | Done | `research_b1_insomniac.md` |
| B2 | cellperformance.com (Acton) | Done (9 articles) | `research_b2_cellperformance.md` |
| B3 | Ghoti tutorials I, II, IV | Done, low value | `research_b3_ghoti.md` |
| B4 | ps2dev p=58333 (ME cache) | Done via Daedalus source (post never archived) | `research_b4_me_cache.md` |
| B5 | Gamasutra MediEvil / GDC Europe 2005 | Done, thin | `research_b5_medievil.md` |
| B6 | Daxter postmortem (GDC Vault) | Skipped: members only | — |
| B7 | Cold, Hard Cache (GDC Vault) | Skipped: members only | — |
| B8 | YAPSPD | Done, all 33 chapters | `research_b8_yapspd.md` |
| B9 | GDC PSP audio sessions | 2004 talk covered by GAME Watch write-up; transcript dropped | — |
| B10 | Edge, God of War PSP | Not archived; closed | — |
| B11 | WipEout Pure postcard | Same session as B5; closed | — |
| B12 | 1UP God of War preview | Skipped: low value | — |
| B13 | Ericson GDC 2003 + GDC 2005 | Done | `research_b13_ericson.md` |
| B14 | ScummVM PSP wiki | Done, thin (on-demand engine plugins noted) | (in reply only) |
| B15–B17 | FBA4PSP, snes9xTYL, PSP Quake lineage | Done via GitHub source | `research_b15_b17_sources.md` |
| B18 | ElDewrito Waypoint statement | Dropped by Sheepdog01 (legal angle not needed) | — |
| B19 | Ars "Beyond emulation" | Done | kept privately (legal background) |
| B20 | Valgrind cache limits | Done by direct test | `research_b20_cachegrind.md` |
| B21 | VAG / PSX ADPCM docs | Not started; low priority (R8) | — |
| B22 | lan.st, Wololo /talk | Not started; low priority | — |

## Proposed checklist additions (all sources)

| ID | Tier | Proposal | Source |
|---|---|---|---|
| A | 5 | Actor LOD "drones": distant actors skip AI and movement; promoted on damage, scope zoom or timer. Gameplay-visible, case by case | B1 |
| B | 2 | Dense live-index roster for hot datum tables (no scanning empty slots) | B1 |
| C | 2 | Batch-update recipe steps 1–3 per hot subsystem (group, define I/O, queue external calls) before any ME work | B1 |
| D | 3 | Cook-time bounding spheres per model for culling and posing LOD without a full pose | B1 |
| E | 4 | Large hot-path lookup tables replaced with arithmetic, verified exhaustively on PC | B1 |
| F | 4 | ME job infrastructure: coarse jobs, per-core queue, ME idles with `halt` woken by interrupt (`0xBC100044`), alignment-asserting transfer wrappers, watchdog, debug stash | B1, B4, B8 |
| G | 4 | CPU↔ME queue: one writer per counter, counters on separate 64-byte lines accessed uncached, payload written back before publishing | B1 |
| H | 2 | Sort actors by AI state and objects by type before update; one loop per state | B1 |
| I | 5 | Lightweight object path for purely cosmetic objects (must look identical) | B1 |
| J | 4 | Hot loops as load-to-locals → compute → store; verify load order in disassembly | B2, B13 |
| K | 4 | `restrict` on hot leaf functions' pointer parameters, in prototypes, each verified and covered by R4 | B2 |
| L | 4 | Branch-free selects in hot inner loops; combine conditions before remaining branches | B2 |
| M | 3/4 | Bounds tests branch-free and 4-wide on the VFPU over cook-time SoA bounds | B2 |
| N | 2 | Struct padding audit of hot structs with `pahole` | B13 |
| O | 2 | Copy struct/global counts and pointers into locals before loops that write through other pointers | B13 |
| P | 1 | PC reference build float settings matched to PSP for R4 (SSE single precision, no fast-math, `-ffp-contract=off` both sides) | B13 |
| Q | 2/3 | Collision/BSP compaction keeps shared edges identical; fall-through/snag test on worst-case geometry | B13 |
| R | 3/4 | VFPU write-through stores (`sv.q` cache policy 0) for GE-bound data, verify first | B8 |
| S | 3 | Big-polygon clipping: cheap VFPU wide-frustum reject, exact side clipping only for failures; static world drawn from RAM, only clipped output copied | B15–B17 |
| T | 1 | FPU control setup checked (enabled exceptions, denormal handling) | B5 |
| U | 1 | Toolchain targets Allegrex (`-march=allegrex` or equivalent), so Allegrex-only instructions can be emitted | B8 |

## Hardware tests the research produced (for the test rig)

1. **Stall on miss vs. stall on use** (decides the value of J, L and preloading): profiler counters `stall (memory)` ÷ (D miss + uncached loads), for missing loads used immediately vs. hoisted with ALU work in between. Method in `research_b8_yapspd.md`.
2. **ME interrupt wake:** ME `halt` + `0xBC100044` interrupt from the main CPU; measure wake latency and `bus access` vs. a spinning ME.
3. **VFPU write-through store** for a GE vertex buffer with no writeback: correct output, and D-cache writeback count drops.
4. **D-cache associativity** (2-way assumed from Daedalus's `0x14` loop): confirm, since R1's cachegrind settings and flush helpers depend on it.

## Open questions still unverified

- Allegrex `min`/`max`/`movz`/`movn` emitted by our GCC? (Check `psp-objdump -d` on our build.)
- Allegrex prefetch instruction: present or not? (No source used one.)
- Allegrex FPU denormal performance.

## Corrections made during this research

- The reloads caused by `-fno-strict-aliasing` mostly **hit** the cache; their cost is extra instructions and lost scheduling freedom, not 39–72-cycle misses (B2).
- Texture-from-VRAM speedup: YAPSPD's 10× figure is older; prefer the newer 4–7× hardware measurement (B8).

## Round 2 (Oct 13, afternoon): new angles

| Angle | File | Headline |
|---|---|---|
| Instruction-cache pressure | `research_icache.md` | **T1:** PGO already names hot functions `.text.hot.*`, but the PSP link script (default, no `PSP_TEXT_LAYOUT`) doesn't group them (checked in `psp_build.py` [D]). A `hotgroup` layout (cold first, then hot together) is a few lines, exact, and checkable in the cache model before a build. Then call-graph ordering (C3) inside the hot group; per-function `-Os` only for "streaming" code; cut loop-duplicating passes. Exchange rate from the PGO test: 1% fewer tick I-misses ≈ 0.3% of the tick [M/E] |
| Halo's own skip/LOD logic | `research_halo_activation.md` | The original engine has **no** distance/visibility LOD for AI, posing or props; its skips (PVS activation, dormant actors, inactive encounters, AI timeslicing, resting vehicles/items physics) already run in the port. New exact leads: **resting weapons/equipment never sleep** under the port's byte-identical rule (ground weapons cost 1.5–4.7 ms a tick on the PSP, hardware_b114); `ai_profile_update` walks every actor twice a tick even with the debug display off; light re-link when a light hasn't moved; data-array checks once a tick, not twice. **Correction:** X8's description is wrong: `object_connect_lights` re-links attached lights into the light clusters (portal flood), it computes no lighting. **Lead for #25:** footstep/impact effects are gated on the camera position, updated once a frame |
| Shipped PSP shooters | `research_syphon_filter_psp.md` | Very little public tech detail on Bend's engine. Logan's Shadow: 333 MHz, 4–6 enemies at a time; Retribution: 15 Leapers on screen, rebuilt streaming. Aim assist (bracketed lock box) is the PSP shooter norm. Ready at Dawn: one shared bus, graphics traffic slows the CPU; 333 MHz spent on more enemies and particles. No evidence any shipped shooter used the ME for game work. Free GDC 2006 audio: Amaze on PSP constraints (gdcvault.com/play/1013191) |
