# The redesign plan: the frame budget, the parts, and how they're built

Sheepdog01, 2026-11-05 15:41-16:24 (the bottleneck session): the redesign had turned back into a build and test of small fixes. "The whole point of the redesign was to attack the time costs directly." Patching over replacing "has killed the project timeline". This page is the plan that replaces the lever order as the redesign's spine. `redesign_tick.md` (T1-T7) and `redesign_recon.md` stay as its inputs. The rules for building it are in this repo's CLAUDE.md and the lab's ("Redesign work").

Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

## 1. The frame budget

Every unit has 1000 ms a second. One second of real time asks each unit for:
- **main CPU:** 30 ticks + fps × (drawing, other and frame end, working);
- **ME:** fps × the replay + audio at its real-time rate + 30 × (the AI job and ring jobs a tick);
- **GE:** fps × (busy + stalled).

The work a frame is held as measured, contention included [U on M]. Figures from ledger 3 (`frame_timeline.md`, tock162). They carry the instruments' cost, ~5% of b30's tick (the master's bare-run split, lab sierra-hw 42114568213: flight frame 118.6 ms against 132.8, tick 39.9 ms against 43.7).

| ms a second, at 15 fps / at 30 fps | 30 ticks alone | Main CPU | ME | GE |
|---|---:|---:|---:|---:|
| b30 opening | **1071** | 1380 / 1689 | 689 / 978 | 570 / 1140 |
| b30 flight | **1310** | 1947 / 2584 | 1498 / 2545 | 1291 / 2583 |
| b30 landing | **1184** | 1914 / 2645 | 1572 / 2668 | 1228 / 2457 |
| b30 beach | **1019** | 1476 / 1934 | 844 / 1257 | 492 / 984 |
| c20 | 570 | 925 / 1281 | 775 / 1373 | 639 / 1278 |
| d20 swamp walk | **1016** | 1457 / 1898 | 916 / 1279 | 434 / 867 |
| d20 flashlight on | 649 | 1143 / 1636 | **1379** / 2526 | 1138 / 2277 |
| b40 opening | 327 | 589 / 852 | 494 / 754 | 266 / 531 |
| b40 cliff path | **1176** | 1755 / 2334 | 1110 / 1801 | 741 / 1482 |
| b40 snowfield | **1275** | 1871 / 2466 | 656 / 982 | 552 / 1104 |

What it says:
- **The tick alone is over budget on 7 of the 10 stretches** (bold). Nothing done to drawing, the ME or the GE reaches the floor there. The tick has to fall first: on b30's flight, from 39.9 ms (bare) to ~26 ms (−35%) once drawing is down to ~15 ms a frame.
- **The ME is the second wall.** It's over the floor on b30's flight and landing and binds d20's flashlight; at the target it's over on every heavy stretch. Moving more work onto it (T1, T2, AITICKOLD) needs room something else frees first.
- **The GE binds nothing at the floor** [M: the main CPU waits on it 0-2 ms a frame]. At the target it's 2.3-2.7× over on b30, d20's flashlight and the landing: there the GE's own work has to shrink, which with the same geometry may mean a compromise (§5).

## 2. Why the time is what it is

- **The tick** waits on memory for 56-68% of its cycles on every stretch [M]: a PC engine's data, scattered behind pointers, against a 16 KB data cache. On b30's flight: computing 36%, its own misses 47%, the GE's contention toll 17% (lead A). The flight's tick has never been split by subsystem.
- **The drawing and the replay do the per-part work twice.** The engine's immediate-mode renderer runs on the main CPU and records every part's draw (37 ms a frame on b30's flight); the ME decodes each record and re-derives its GE state (70 ms). Both are limited by instruction misses: the drawing's hot code is ~45 KB, the replay's ~29 KB, against a 16 KB I-cache (`draw_rebuild.md`, cachecensus1 [E]). And most of it is the same as the frame before: a model part's GE words are **97-98% unchanged apart from its matrices**, 544 of 552 model draws a frame on b30 match last frame's (`pdc_plan.md`, pdc1 [E]; the share held on the PSP, pdcprobe [M]).
- **Every part's commands go through main RAM every frame** (~500 KB a frame of list on b30's flight), and the GE reads them beside the tick: the list's own bytes are 4.4 ms of b30's toll.

## 3. The parts

### S. The tick, by how often its work changes (first)

The tick is the wall on most stretches, so it leads. Today every object's work runs every tick. The redesigned tick computes what's read, when it's read, and keeps results until their inputs change.

- **Known:** animation and node matrices are 19-40% of the tick on every map (9-29 ms a frame, framesplit [M]); 17-54% of that work is for objects neither drawn nor read before the next draw (nmstudy, VISLAZY [U on E × M]). Collision is 25% of the beach's tick. Sleeping objects already skip work.
- **The target:** b30's flight tick −35% (39.9 → ~26 ms, bare), with drawing at ~15 ms a frame.
- **First:** the census (C3, §4). What's known adds up to well short of −35%: the census decides whether S closes the gap by itself, or T6's tick-start world and T7's layout have to carry the rest.
- T3 (parts of the tick less often), T6, T7 and VISLAZY become S's pieces, judged against S's target, not one by one.

### R. Retained rendering (second, alongside S)

The GE was built for retained mode: lists that CALL sub-lists, 8 bone matrix and 4 light registers, a stall address. Native PSP games build a part's commands once and write only what changes.

| Changes | What | Built by | Lives in |
|---|---|---|---|
| once a map | each model part's commands per level of detail and shader pass; each level cluster's static geometry | the bake | main RAM, VRAM where room allows |
| when it happens | a texture moved in VRAM: the words that point at it patched | the unit that moves it | the prebuilt lists |
| every frame, per instance | world matrix, bone matrices, lights, tint, then CALLs | a short emit pass | a small frame list |
| every frame, dynamic | particles, decals, contrails, sorted see-through, the HUD, first person | immediate mode, as now | the frame list |

- **The units' jobs:** the main CPU runs the tick and the visibility, and hands over an instance list, not a record of every call. The ME writes the frame list from the instances and makes the dynamic geometry: the replay as a stage goes. The GE draws as now.
- **What it buys on b30's flight** [U]: the ME ~1400 → ~850-1000 ms a second at the floor (the models' per-part replay, after PDC's census: 22-30 ms a frame); the main CPU's drawing ~640 → ~300-400. The ticks are untouched. **R makes the ME fit the floor and frees the room T1 and AITICKOLD need**; it doesn't fit b30 by itself (§1).
- **Why PDC, PDCCALL and X2 didn't pay:** they kept the record, the key, the hash and the replay, and copied prebuilt words into the old per-frame list, bit for bit: PDCCALL's ceiling was 3.5-4.3 ms of a 40 ms ME job [M], and its store didn't fit b30's heap. They didn't show retained mode loses; they showed half of it costs what it saves. R is built under the rules in §6.
- **What could sink it:** the shader forms a map needs (C1); the store's room, which has to be GE-readable (main RAM or VRAM, not eDRAM; the room is the frame list it replaces, 768 KB plus the 224 KB record buffer [D]); the dynamic floor (C2); Halo's object lighting on the GE's 4 lights (C5).

### B. The memory map (with R)

R decides what has to be GE-readable, and S's layout what the tick reads hot. B places them: VRAM, main RAM and the ME's eDRAM by design, not job by job. Known: VRAM has 13-48 KB free behind 816 KB of fixed buffers; freed VRAM is worth ~17.6 µs of b30's tick a frame a KB for the first 200 KB (lead A [U on M]); the eDRAM's 544 KB is the largest free block, readable by the ME only. B follows C1's sizes.

### The old parts

T1 and T2 (work onto the ME) wait for the ME room R frees. T4 is B's. T3, T5, T6 and T7 are S's pieces. The lever list goes on beside the plan (§6), not on its parts' paths.

## 4. The first counts (no PSP time)

| | Count | Answers | Where |
|---|---|---|---|
| C1 | Each map's prebuilt forms: parts × levels of detail × shader passes, and their bytes | whether R fits the machine | the bake, offline |
| C2 | The frame list's inline data (b30's flight 254 of 501 KB) by kind: cut pieces, decals, kept index copies, the rest | R's floor: what stays per frame | emulator, `listcensus` |
| C3 | The tick by subsystem and by change rate: what's recomputed each tick against what changed | whether S reaches −35% | emulator, the PC sampler and tick steps |
| C4 | The replay split by record kind, bare (the ledger's `replaypart`) | R's ceiling on the ME | emulator for the shape, one PSP run for the ms |
| C5 | Reads: what the GE does at the near plane (do the cut pieces need doing?), and how the replay lights a model (GE lights or software) | two of R's unknowns | code and docs |

C3 and C1 decide the plan; C2, C4 and C5 size R.

## 5. Decision points for Sheepdog01

1. **After C3:** whether S reaches the floor's tick on b30 exactly, or needs "plays the same" work (the simulation rule of 2026-10-30) or a compromise.
2. **After C1:** whether R fits, and the go to build it as a separate path.
3. **At the target:** where the GE is 2.3-2.7× over (b30's flight and landing, d20's flashlight), 30 fps needs less GE work for the same picture or a compromise. Not needed for the floor.

## 6. How it's built

The redesign rule (Sierra-III CLAUDE.md and the lab's, "Redesign work", Sheepdog01 2026-11-05 16:24):
- **Budget first.** A part starts as its ceiling on each unit and stretch against §1's gap, and Sheepdog01's go.
- **A separate path, switched whole.** A part is built beside the old path, not inside it, with its own machinery, under one keep word. The old path stays as the A/B side and the fallback until the switch-over, then goes.
- **Proven for correctness, priced once.** Slices are proven in the emulator for correctness: pictures for drawing, play + seeds or the outcome census for the simulation. GE-stream identity with the old path isn't asked of a part that replaces it. The multiplayer boundary still holds. A slice slower on its own is not a rejection. The PSP prices a part at its milestone (the old layer bypassed) and at the switch-over.
- **Levers stay off a part's ground.** A lever on a ground a part replaces isn't started (levercheck names the part), unless it pays before the part lands and Sheepdog01 says so.
- **Progress is the budget.** A milestone updates §1's table; a lever reports its effect in its terms.

## 7. Why the habits changed

On the dashboard's lever table (2026-11-05): 513 levers, 145 in the build, 93 rejected, 54 shelved; `levers.tsv` changed in 510 commits since the tick-and-tock rule (2026-10-24). The large wins were sized before they were built: PDC's census put the replay's saving at 22-30 ms a frame on b30, and its build, held to the old path's GE stream through the old layers, measured +1.2-2.3% and was rejected. The rule above is so that a design whose payoff comes when the old layer goes isn't judged on its first slice.
