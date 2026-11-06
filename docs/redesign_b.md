# B, the memory map by design: the design page (Phase 1 draft)

Part B of `redesign_plan.md` (§3 B). It has no go of its own: it places what R and S hold, and is priced inside their milestones. Under the redesign rule a placement is proven for correctness in the emulator and priced on the PSP (the cache rule: never by the cache model). This page is the map B works from, and the order it decides things in.

Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Figures from `memory_ledger.md` §0.4 and the lab's `psp/sierra/findings/edram.md` unless named.

## 1. The memories and who can read them

| Memory | Size | Main CPU | ME | GE | Today |
|---|---|---|---|---|---|
| Main RAM (user) | 24 MB | yes | yes | yes | heap, tag window (9 MB), geometry cache (1,280 KB +), texture RAM cache (~2.5 MB), the frame list (768 KB), the record buffer (128-224 KB) |
| VRAM | 2 MB | yes | **no** | yes | frame and depth buffers 816 KB (fixed), textures 616 KB (2-16 free), model copies 616 KB (mevram; 11-32 free on the busy maps) |
| The ME's eDRAM | 2 MB | DMA only | yes | **no** | 0-256 KB reserved; **256-800 KB free (544)**; 800-1,456 the sound tier; 1,456-2,048 the AI job's data and stack |
| Scratchpad | 16 KB | yes | **no** | yes | the level collision BSP's top 304 nodes (SCRATCHBSP), the profiler's slots and table (6.6 KB) |

The tightest single place is c20's heap: 160 KB at the top, largest block 182 KB [E].

## 2. What R and S change

**R (retained rendering, `redesign_r.md`) frees and claims main RAM:**
- frees most of the frame list (768 KB → tens of KB for the dynamic kinds, C2) and of the record buffer (128-224 KB);
- claims the form store, ~250-480 KB on most maps without super high (C1 with C4's words a form) [U on M];
- needs the forms GE-readable: main RAM or VRAM, never eDRAM;
- makes PDC's claim on the eDRAM (~240 KB of tables and stores) unnecessary: R replaces PDC.

Net, R should leave main RAM a few hundred KB better off [U], to be measured at R's slice 5 (it logs the store's bytes).

**S (the tick by change rate) claims small, hot tables:**
- each object's version (for the unchanged-object skip and the collision reuse key): ~4-8 bytes an object, ~2-5 KB for b30's ~650 objects;
- the feature queries' reuse table (C3: the repeating share is bipeds' feature queries, a fifth to two fifths): a few KB;
- the far-biped tier's per-pass arrays (if it takes the staged form, PHASEB): a few KB a pass;
- and, if the tier takes the ME-a-tick-behind form (BW), the world it reads one tick old: C's snapshot, ~192 KB (edram.md), or a copy-on-write subset (recon's census, not run).

## 3. The placement, by memory

### 3.1 The scratchpad (16 KB): the tick's hottest tables

Its rule: a table hot across the whole tick that fits (`cpu_cost_map.md` §4). In the new shape the candidates are:

| Candidate | Size | Why it might win |
|---|---|---|
| SCRATCHBSP (today) | ~9.7 KB | a small win [M]; collision is ~25% of the heavy ticks |
| S's object versions | ~2-5 KB | read for every object every tick by the skip and by collision's reuse |
| S's feature-query reuse table | a few KB | read on every feature query |
| the hot tag fields (TAGHOT's copy) | unsized | the tag window is 32% of the tick's data misses |
| the profiler's slots (today) | 6.6 KB | none in play: moving them out gained nothing, but frees room |

The ranking is by misses per byte on the new shape's cache map, once S's design page fixes its tables; the winner gets a PSP pair. The profiler's 6.6 KB is the first room to take: in a play build it has no reason to be there.

### 3.2 The eDRAM's free 544 KB (256-800): the ME's working sets

Only the ME reads it, so it's for what R and S put on the ME:

| Candidate | Size | Status |
|---|---|---|
| MECODE: the AI pass's code, run from an ME-linked copy (recon, 17:10) | 120-230 KB (118 KB covers 99.9% of b30's fetches) | census done [E]; ~1-1.6 ms off a pass and the ME's code fetches off the bus; a pair when placed |
| R's ME-side state: the instance list DMA'd in, the lights and bone-slot memo | tens of KB | R's design (§2.4) |
| MEVGATHER: visibility's gathered inputs | 20-44 KB, double-buffered | proposed (layout plan) |
| S's tier's snapshot, if BW | ~192 KB, or a copy-on-write subset | only if S takes the BW form |
| L1: the level's positions for the ME's near-plane cuts | ~190 KB | plan; under R the near-eye cutting stays immediate but small (1-6 KB a frame, C2), so L1's worth falls [U] |
| PDC's tables | ~240 KB | **released by R** |
| MIST's reserve (the PSP-1000's ME syscall table) | ? | to pin before anything lands at 256-320 KB |

The likely fit [U]: MECODE (~120-230) + R's ME state (tens) + MEVGATHER (~40) leaves ~150-350 KB, enough for a copy-on-write snapshot but not a full one.

### 3.3 VRAM: no room until something leaves

The buffers' 816 KB are fixed. Textures and model copies each have a 616 KB share, full. Trading the textures' share for geometry was tried and lost (VRSP: untextured frames ×10). The room only grows if:
- R's forms reference geometry already in the model copies (mevram), so no second copy is made;
- SSPR's simplified levels of detail (R's slice 9) shrink the model copies' bytes;
- a texture format change (CLUT4W, on for more world textures) shrinks the textures' share.

Freed VRAM is worth ~17.6 µs of b30's tick a frame a KB for the first 200 KB, as model geometry the GE then reads off the bus (lead A [U on M]). That's B's price list for any VRAM it can free.

### 3.4 Main RAM: what R's net frees goes to

In order of what a KB buys (`memory_ledger.md` §0.5):
1. c20's heap (the tightest place in the machine);
2. the geometry cache (fewer misses on b30, whose cache is full; MC3 found the PSP-1000's time didn't move when misses fell, so this is priced by a pair, not assumed);
3. S's tables that don't fit the scratchpad.

## 4. The order

1. **At R's slice 5:** the store's real bytes and the frame list's and record buffer's real shrink, logged. B sets main RAM's use of the net.
2. **At S's design page:** S's tables fixed; the scratchpad re-ranked on the new shape's cache map; the winner gets a pair.
3. **When MECODE is built (recon):** its eDRAM claim placed at 256-800 KB beside R's ME state; MIST's reserve pinned first.
4. **If S takes the BW form:** the snapshot's size from recon's copy-on-write census, then its place.
5. **At R's switch-over:** PDC's claim formally released in edram.md; the VRAM question revisited with SSPR's sizes.

Each placement changes only addresses, so it's exact by construction (play and both seeds identical); its price is a PSP pair.

## 5. Open questions

- **MIST's table address** on the PSP-1000 (blocks any claim at the eDRAM's low end until pinned).
- **The sound tier** (800-1,456 KB, `sndedram`): whether candidate part A (audio built around the VME) changes what audio keeps in eDRAM. Part A's census comes first.
- **The far-biped tier's form** (half rate, BW on the ME, or staged) decides whether a snapshot is needed at all: settled by recon's biped count and S's design page.
