# The redesign plan: the frame budget, the parts, and how they're built

Sheepdog01, 2026-11-05 15:41-16:24 (the bottleneck session): the redesign had turned back into a build and test of small fixes. "The whole point of the redesign was to attack the time costs directly." Patching over replacing "has killed the project timeline". This page is the plan that replaces the lever order as the redesign's spine. `redesign_tick.md` (T1-T7) and `redesign_recon.md` stay as its inputs. The rules for building it are in this repo's CLAUDE.md and the lab's ("Redesign work").

Revised 2026-11-05 17:05 after a second-opinion review (lab or-job 42123727478, each point checked against the sources): the GE enters the floor; one bare baseline; S's target derived, and shared with R and B; R's saving tied to its frame rate; criteria, owners and a fallback per part.

Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Lab paths are the lab repository's.

## 0. What the plan delivers

- **The floor first: real time (30 ticks a second, at least 15 fps) on every benched stretch** (the goals, `redesign_tick.md`). The target (30 fps per level) follows on the lighter levels; on the heaviest it isn't promised (§5).
- **Viability, as of this page** [U on M]:
  - **Six of the ten benched stretches** (b30's opening and beach, c20, d20's walk and flashlight, b40's opening) reach the floor on paper with the parts below and the pieces already measured.
  - **The four heaviest** (b30's flight and landing, b40's cliff path and snowfield) need every unit cut at once (§1). The known ceilings don't add up to that yet. Counts C3, C4 and C6 decide whether the parts close it, and §5's first decision is what happens if they don't.

## 1. The frame budget

Every unit has 1000 ms a second. One second of real time at 15 fps asks each unit for:
- **main CPU:** 30 ticks + 15 × (drawing, other and frame end, working);
- **ME:** 15 × the replay + audio at its real-time rate + 30 × (the AI job and ring jobs a tick);
- **GE:** 15 × its own work (busy), with the time it sits stalled for the ME's next chunk shown apart (that's the ME's lateness, not the GE's work).

**Where the formula's assumptions fail** [U]:
- The work a frame is held as measured, at today's 7-15 fps. At higher frame rates less changes between frames (R's saving grows, §3), and contention is held as measured.
- The tick's cost is taken as independent of the frame rate. That's true of the game's work, not of contention: the toll depends on what the GE reads beside the ticks.

**The baseline.** Ledger 3 (`frame_timeline.md`, tock162 64647bb5) carries the instruments `hwthreads frametrace gebusy gebusy2 mevisring` and the PC sampler. Measured against a bare run, they cost b30's flight tick +5.2% and +1.8%: ~7% [M, lab sierra-hw 42114568213]. So the ticks below are ledger 3's less 7%. The other columns keep ledger 3's figures; their instruments' cost isn't split yet.

| At 15 fps, ms a second | 30 ticks (bare) | One tick (bare) | Main CPU | ME | GE work | GE stalled |
|---|---:|---:|---:|---:|---:|---:|
| b30 opening | 996 | 33.2 | 1305 | 689 | 542 | 28 |
| **b30 flight** | **1218** | **40.6** | **1855** | **1498** | **1252** | 39 |
| **b30 landing** | **1101** | **36.7** | **1831** | **1572** | **1209** | 20 |
| b30 beach | 947 | 31.6 | 1405 | 844 | 479 | 14 |
| c20 | 530 | 17.7 | 885 | 775 | 632 | 8 |
| d20 swamp walk | 945 | 31.5 | 1386 | 916 | 360 | 74 |
| d20 flashlight on | 604 | 20.1 | 1097 | **1379** | 430 | 708 |
| b40 opening | 304 | 10.1 | 566 | 494 | 250 | 15 |
| **b40 cliff path** | **1094** | **36.5** | **1673** | **1110** | 694 | 46 |
| **b40 snowfield** | **1186** | **39.5** | **1782** | 656 | 550 | 2 |

What it says:
- **The main CPU is over on every stretch but c20 and b40's opening.** The ticks alone are over on the four heavy stretches, and at 93-100% on three more.
- **What one tick may cost.** With drawing, other and frame end at ~15 ms a frame, a tick has 25.8 ms on every stretch ((1000 − 15 × 15) / 30). Today's bare ticks need: b30's flight −36%, landing −30%, b40's snowfield −35%, cliff −29%, b30's opening −22%, beach −18%, d20's walk −18%. With drawing left at ~22 ms a frame, the flight needs −45%.
- **Drawing has to fall 2-3× on the heavy stretches** (b30's flight 42.5 ms a frame of drawing, other and frame end; landing 48.7; b40's 38.6-39.7).
- **The ME is over on b30's flight and landing and on d20's flashlight.** The replay must lose ~33 ms a frame on the flight, ~38 on the landing and ~25 on the flashlight.
- **The GE's own work is over on b30's flight (1252) and landing (1209)**, so the GE enters the floor there. d20's flashlight is the ME's lateness: the GE's own work (430) fits.

## 2. Why the time is what it is

- **The tick** waits on memory for 56-67% of its cycles on every stretch [M, ledger 3]: a PC engine's data, scattered behind pointers, against a 16 KB data cache. On b30's flight: computing 36%, its own misses 47%, and the GE's contention toll 17% (15.1 ms a frame, `frame_timeline.md` lead A [U on E and M]). The flight's tick has never been split by subsystem.
- **The drawing and the replay do the per-part work twice.** The engine's immediate-mode renderer runs on the main CPU and records every part's draw (37 ms a frame on b30's flight). The ME decodes each record and re-derives its GE state (70 ms). Both are limited by instruction misses: the drawing's hot code is ~45 KB and the replay's ~29 KB, against a 16 KB I-cache (`draw_rebuild.md`, cachecensus1 [E]).
- **Most of it repeats.** A model part's GE words are 97-98% unchanged frame to frame apart from its matrices, and 544 of 552 model draws a frame on b30 match last frame's (`pdc_plan.md`, pdc1 [E]; the share held on the PSP, pdcprobe [M]). That census ran at 1.1-1.6 ticks a frame; at today's two ticks a frame less repeats.
- **Every part's commands cross main RAM every frame**, ~500 KB a frame of list on b30's flight. The GE reads them beside the tick: the list's own bytes are 4.4 ms of b30's toll.

## 3. The parts

Each part: what it changes, its ceiling, its milestone (when the PSP prices it), its stop rule, its fallback, and who does it (proposed; Sheepdog01 assigns).

### S. The tick, by how often its work changes

Today every object's work runs every tick. The redesigned tick computes what's read, when it's read, and keeps results until their inputs change. T3, T5, T6 and T7 are its pieces.

- **Target, derived (§1):** a bare tick of ≤ 25.8 ms with drawing at ~15 ms a frame. That is b30's flight −36%, b40's snowfield −35%, b30's landing −30%, b40's cliff −29%; the lighter stretches −18 to −22%.
- **The target is shared.** Part of the tick is the GE's toll, which R and B shrink (the list's own bytes; vertices and indices moved to VRAM where room is freed). What's left after them is S's.
- **What's already in the measured figures:** VISLAZY and aicad3 are on by default (the lab's `levers.tsv`), so their gains are in ledger 3.
- **What's known on top, on b30's flight** [U on M]:
  - AITICKOLD, the AI's think on the ME from a world one tick older: tick −14.5% [M, PSP-3000 sierra-hw 42041663929]. It needs ME room, which R frees.
  - The toll R and B can remove: ~2-4 ms a tick.
  - T5 and T7: ~0.5-1.
  - Together: 40.6 → ~30-32 ms against ≤ 25.8. **A gap of ~4-6 ms a tick has no known source.** C3 has to find it.
- **Milestone:** C3's census done and S's pieces chosen against the target; the PSP prices S when the first of them replaces the old tick path on b30.
- **Stop rule:** if C3 shows the pieces can't reach the target exactly or "plays the same", Sheepdog01 decides (§5.1).
- **Fallback:** the old tick path behind S's keep word; it stays the safety net for a failure, per CLAUDE.md's failure rule.
- **Who:** recon (C3); the master builds.

### R. Retained rendering

The GE was built for retained mode: lists that CALL sub-lists, 8 bone matrix and 4 light registers, a stall address. Native PSP games build a part's commands once and write only what changes.

| Changes | What | Built by | Lives in |
|---|---|---|---|
| once a map | each model part's commands per level of detail and shader pass; each level cluster's static geometry | the bake | main RAM, VRAM where room allows |
| when it happens | a texture moved in VRAM: the words that point at it patched | the unit that moves it | the prebuilt lists |
| every frame, per instance | world matrix, bone matrices, lights, tint, then CALLs | a short emit pass | a small frame list |
| every frame, dynamic | particles, decals, contrails, sorted see-through, the HUD, first person | immediate mode, as now | the frame list |

- **The units' jobs:** the main CPU runs the tick and the visibility, and hands over an instance list, not a record of every call. The ME writes the frame list from the instances and makes the dynamic geometry: the replay as a stage goes. The GE draws as now.
- **What it must deliver at the floor (§1):** the ME's replay −33 ms a frame on b30's flight (−38 landing, −25 d20's flashlight); drawing, other and frame end 42.5 → ~15-20 ms on the flight; some of the GE's work (the list words the GE re-reads: 24% of b30's list reads repeat what the GE holds, LISTBYTES in `levers.tsv`).
- **What's known of its worth** [U on E and M]: PDC's census puts the models' removable share at 22-30 ms a frame of the replay on b30, measured near one tick a frame. At the floor's two ticks a frame it's smaller (`pdc_plan.md`: "the saving today is smaller than at the target. It grows as the frame rate rises"). The level's per-cluster lists add to it, unsized. **So R as known meets the floor's ME need on d20's flashlight and falls ~5-15 ms short on b30's flight and landing.** C4 and C2 size the rest.
- **Why the earlier tries didn't pay** (the lab's `levers.tsv` and `grounds.tsv`, rows PDC, PDCCALL, X2):
  - PDC copied prebuilt words into the old per-frame list through the old layers, bit for bit, from an eDRAM store the GE can't read. Its pairs ran even to +1.6 ms a frame.
  - PDCCALL CALLed retained blocks, but kept the key, the hash, the replay and the built parts. It measured +1.2-2.3% frame on c10 [M, PSP-1000 sierra-hw 41216890230], and its store didn't fit b30's heap.
  - Neither is R: R is bake-built per level of detail and pass, with no per-frame record or replay. They showed that half a retained mode costs about what it saves, not that retained mode loses.
- **What could sink it:**
  - the shader forms a map needs (C1);
  - the store's room, which has to be GE-readable: main RAM or VRAM, not eDRAM. The room is the frame list it replaces, 768 KB of list plus the record buffer, 128 KB by default and 224 with the default keep set [D, `psp_ge.c` DEFER_INFLIGHT_KB; IFKB];
  - the dynamic floor (C2);
  - near the camera, geometry still has to be cut each frame (C5: the GE can't draw it uncut), so what's near the eye stays per-frame work.
- **Milestone:** b30's models drawn from prebuilt lists with the record and replay bypassed for them; one PSP pair against the old path. **Switch-over:** every kind drawn by R; one pair.
- **Stop rule:** if C1 shows the store can't fit, or C4 that R's ME saving can't reach the floor's need with S and B, Sheepdog01 decides before any build.
- **Fallback:** immediate mode stays in the build for what R can't hold: parts whose forms didn't fit the store, and any failure. The old record and replay go at the switch-over; immediate drawing doesn't.
- **Who:** the bottleneck session (C1, C2, C5); the master (C4, the build).

### B. The memory map by design (with R)

R decides what must be GE-readable, and S's layout what the tick reads hot. B places them: VRAM, main RAM and the ME's eDRAM by design, not job by job. T4 joins it.

- **Known:** VRAM has 13-48 KB free behind 816 KB of fixed buffers. Freed VRAM is worth ~17.6 µs of b30's tick a frame a KB for the first 200 KB (lead A [U on M]). The eDRAM's 544 KB is the largest free block, readable by the ME only.
- **Milestone and stop:** set by C1's sizes. B is a layout, priced inside R's and S's milestones.
- **Who:** the master, from C1.

### The old parts

T1 and T2 (work onto the ME) wait for the ME room R frees. T4 is B's. T3, T5, T6 and T7 are S's pieces. The lever list goes on beside the plan (§6), not on its parts' paths.

## 4. The counts

Sent to the master 2026-11-05 17:20 (the lab's coord/inbox/master.md) for those that need a build; the rest done here. Results go into the lab's findings and this page's §1 and §3.

| | Count | Answers | Where | Who | State |
|---|---|---|---|---|---|
| C1 | Each map's prebuilt forms (parts × levels of detail × shader passes) and their bytes | whether R fits | a counter in the baker, run as a sierra-bake job that prints counts only | the master (or this session drafts the counter, built by the master) | asked |
| C2 | The frame list's inline data (b30's flight 254 of 501 KB) by kind: model and level cut pieces, decals, kept index copies, effects, see-through, HUD | R's floor: what stays per frame | emulator, `listcensus` (Sierra-III recon/listbytes2; lab coord/handoff/listbytes_2026-11-05.md) | the master | asked |
| C3 | The tick by subsystem on the four heavy stretches, and its change rate: the share of each subsystem's per-object work whose inputs didn't change since the last tick | where S's last 4-6 ms a tick comes from | emulator: the PC sampler, `PSP_TICK_STEPS=1`, `nmstudy` extended (lab coord/handoff/nmstudy.md) | recon, through the master | asked |
| C4 | The replay by record kind, and the models' time by step | R's ceiling on the ME | emulator for the shape; one bare PSP run for the ms (meparts' ME cycle counter) | the master | asked |
| C5 | What the GE does at the near plane; how the replay lights a model | two of R's unknowns | code | this session | **done** (below) |
| C6 | The GE's own time on b30's flight and landing by what limits it | whether the GE's work can lose ~15 ms a frame for the same picture | existing measures, then one PSP pair | this session; the pair through the master | **half done** (below) |

C3, C4 and C6 decide the heavy stretches; C1 decides whether R is built at all.

### What the reads found

- **C5, the near plane** [D, `psp_ge.c`'s header, tock162 70bc508]: the GE "drops a whole triangle when a corner falls behind the camera or lands too far off the screen (its coordinates only reach 2048 pixels either side)". So triangles near the eye are checked and cut on the CPU each frame, and the pieces drawn at the frame's end. For R: a prebuilt part can be CALLed only when it's wholly and safely in front; the rest (the ground under the player, models beside the camera) stays per-frame work. That's part of R's dynamic floor, and C2 sizes it.
- **C5, model lighting** [D, `psp_ge.c` model_lighting]: already the GE's own lights. Two directional lights and two point lights, with the ambient, are set per part through `sceGuLight` and `sceGuLightColor` (the light pass of the world's lights is separate). For R that's a few per-instance words before a part's CALL. Lighting is not a blocker.
- **C6, what doesn't limit the GE** [M on E]: moving its reads off main RAM barely changes its own time. mevram's measured rate is 1.63 µs of GE time a KB moved to VRAM, so M6B's 294-551 KB a frame was worth 0.3-0.9 ms of GE time (the lab's levers.tsv, M6B, GEV). On b30's flight (83.5 ms busy) the GE is bound by its own vertex and pixel work, not by memory. Its reads still matter to the tick (the toll), not to itself.
- **C6, what's left:** whether that work is vertices or pixels. No count separates them. The cheapest test is one PSP pair on b30's flight with a keep word that clips every draw to a 1-pixel scissor: the GE still fetches and transforms every vertex but fills almost nothing, so the gap in GE busy is the fill. Built by the master; the pair sized as the rule's default (two sides, repeat 2, the flight only).
- **C1, the order of magnitude** [E]: one frame's model commands on b30 are ~37,800 words (~150 KB, `pdc_plan.md`'s census), ~560 bytes a hit (pdcprobe's ~140 words). A map's whole store is a multiple of one frame's visible set, by how many parts, levels of detail and passes it holds [U]. The frame list R shrinks is 768 KB. Whether the store fits is C1's question.
- **C4, what's known** [E]: on b30 the models were 61% of the replay's ME time in the older split by kind (mekinds, in `pdc_plan.md`); on c10 the models' steps are bind + state + lighting 38%, geometry 29%, two-sided and glow passes 24%, convert 6.5%, node matrix 3% [M, meparts 40961270594]. On b30's flight that puts the models at ~43 of the replay's 70 ms and the level and the rest at ~27 [U]. R's need there (−33 ms) takes most of the models' removable share plus part of the level's: C4 settles it.

## 5. Decision points for Sheepdog01

1. **After C3, C4 and C6:** if the parts' ceilings don't reach the floor on the four heavy stretches, what fills the gap there: "plays the same" work (the simulation rule of 2026-10-30), or compromises. Compromises are frozen apart from L15, FX15, RENDERTICK, AIFAR and PRIO60 (CLAUDE.md, 2026-10-25 and 10-29), so this one is Sheepdog01's to lift.
2. **After C1:** whether R fits, and the go to build it as a separate path.
3. **The target:** 30 fps per level looks reachable with R and S on the lighter levels: a10 and c10 at 20.7 fps today, c20 17.2, c40 27.5 (tock161's ladder, PSP-1000 [M]). It isn't promised on b30, a30 or d20 (×2.2-3.7). Whether to chase it there comes after the floor.

## 6. How it's built

The redesign rule (Sierra-III CLAUDE.md and the lab's, "Redesign work", Sheepdog01 2026-11-05 16:24):
- **Budget first.** A part starts as its ceiling on each unit and stretch against §1's gap, and Sheepdog01's go.
- **A separate path, switched whole.** A part is built beside the old path, not inside it, with its own machinery, under one keep word. The old path stays as the A/B side until the switch-over. A failure fallback stays after it (§3's fallbacks), per CLAUDE.md's failure rule.
- **Proven for correctness, priced once.** Slices are proven in the emulator for correctness: pictures for drawing, play + seeds or the outcome census for the simulation. GE-stream identity with the old path isn't asked of a part that replaces it.
- **The multiplayer boundary is proven in the campaign build.** State the server consumes or checks (the rules, the 30 Hz tick, collision's answers, tag indices, object slots) stays exact, proven by play + seeds against the old path, while multiplayer builds are left out (CLAUDE.md, 2026-10-21).
- **A slower slice is not a rejection.** The PSP prices a part at its milestone and at its switch-over.
- **Levers stay off a part's ground.** Once a part has Sheepdog01's go, a lever on a ground it replaces isn't started (the lab's levercheck names the part), unless it pays before the part lands and Sheepdog01 says so. Before the go, a lever there says how it fits the part. A lever that failed through the old layers is marked revivable under the part (PDC, PDCCALL and X2 are, under R).
- **Progress is the budget.** A milestone updates §1's table. A lever reports its effect in its terms.

## 7. The road

**Phase 0, the counts (now; no part has Sheepdog01's go).**
- C1-C6 above, in parallel: the master and recon on the builds, this session on the reads.
- PSP time: C4's one bare run and C6's one pair. Nothing else.
- Each count's result goes into the lab's findings and this page's §1 and §3, and the dashboard's Redesign tab follows (`redesign.tsv`).
- **Ends when** C1, C3, C4 and C6 are in.

**Gate 1 (Sheepdog01): the go per part.**
- For each heavy stretch, the parts' ceilings from Phase 0 against §1's gap.
- Sheepdog01 rules on §5.1 (what fills any gap: "plays the same" or a lifted compromise) and §5.2 (R's go).
- A part without its go stays a plan; its levers carry on, saying how they fit it.

**Phase 1, the design pages.**
- One page per part with its go, in `docs/`: the data it keeps, its interfaces with the old path, its slices in order, each slice's proof, its keep word, its fallback, and its milestone's stretch.
- R's names the instance list's format, the store and its placement (with B), patch-on-change for textures, and the dynamic path (cut pieces, decals, effects).
- S's comes from C3: which subsystems become computed-on-read, and what each keeps.
- Reviewed before building: a second-opinion read of the page against the code, as this page had.

**Phase 2, the build.**
- Each part on its own branch, as a separate path under its keep word. The master builds; recon builds S's census-driven pieces.
- Slices are proven in the emulator for correctness only (§6).
- The milestone's PSP pair: R at "b30's models from prebuilt lists, record and replay bypassed for them"; S at its first piece replacing the old tick path on b30.
- After each milestone, §1's table is redone with the measured figures. If a part falls short of its share, Sheepdog01 decides before the next slice.

**Phase 3, the switch-over.**
- One PSP pair per part at the switch-over. The old record and replay go; immediate drawing stays as R's fallback (§3).
- With the ME room R frees, T1's remaining jobs and AITICKOLD move onto it, priced as placements (CLAUDE.md).
- B's layout is set from what R and S hold.
- The ladder and the ledger bench run on the first tick that carries them (the lab's bench rule).

**Beside it all, the lever lane.** Levers go on in the tock as now, off a part's ground once it has its go. They report in §1's terms.

**Who keeps it true.** This page is the redesign's record. It's updated in the same turn as a result that changes it, like the cost map. The lab's `redesign.tsv` and the dashboard follow.

## 8. Why the habits changed

On the dashboard's lever table (2026-11-05): 513 levers, 145 in the build, 93 rejected, 54 shelved; `levers.tsv` changed in 510 commits since the tick-and-tock rule (2026-10-24). The large wins were sized before they were built. PDC's census put the replay's saving at 22-30 ms a frame on b30. Its build, held to the old path's GE stream through the old layers, ran even, and PDCCALL measured +1.2-2.3%. Both were judged slice by slice. The rule above is so that a design whose payoff comes when the old layer goes isn't judged on its first slice.
