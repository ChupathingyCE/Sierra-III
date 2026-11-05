# The redesign's recon: what we map before we build

Sheepdog01, 2026-11-04 13:01 and 13:14: if the port is to be rebuilt, every part is examined for how it fits the whole and where it best runs, and no part of the hardware is left unused or underused. **Gaps in our hardware knowledge are catalogued and chased first, in order of what they might add to the goal; then we build.**

Sheepdog01, 14:51: "no stone unturned as far as hardware capability is concerned." A unit's limit found by one method (one DMA mode, one set of known opcodes) is our method's limit until a full mapping shows it's the hardware's.

This page is the plan for that recon: what's mapped, in what order, by whom, and what each part must answer. The pages it produces are the design's inputs; `cpu_cost_map.md` is the first.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or docs, [U] inference.

## The goal, stated as capacity

Sheepdog01: "We take what we can get." Headroom isn't a fixed margin. It's knowing which costs we can shrink and which we're stuck with, so we can say what the hardware can carry:
- **A firefight mode:** how many AI in waves at once? Thinking and posing set that.
- **Co-op:** can the PSP run the campaign with 4 players, or 16?
- **Multiplayer as a client:** what would 128 players take?
- **Custom campaigns and expansions:** what room is left for new content?

So the design is judged by the **capacity it leaves**: for each kind of thing the game holds (an actor thinking, a biped posed, a vehicle, a projectile, a particle, a light, a sound voice, a remote player), what it costs each unit and memory, and how many fit once the campaign's own load is carried at 30/30.

## The order

Each ledger is a page in `docs/` (or the lab's findings, for numbers), kept current like the cost map.

| # | Ledger | What it answers | Mostly from | PSP time |
|---|---|---|---|---|
| 1 | **Hardware capabilities and gaps** (`hw_capabilities.md`) | Every unit and feature the PSP has: what it can do, what it costs [M], whether we use it and where, what it could carry. The gaps (unknown, unmeasured, unused), ranked by what they might add | The docs, the SDK's headers, the research sources, the code; then one bench round | One `halo_bench` round for the unmeasured features |
| 2 | **Workload and capacity** (`capacity_model.md`) | Every job a frame does: its time, code and data footprint, inputs, outputs and dependencies, deadline, unit now, units it could run on. Each as a **fixed cost per frame plus a cost per entity**, so capacity can be read off | The emulator's censuses, the code, the lab's findings | A few runs with entity counts varied (more AI, more bipeds) |
| 3 | **The frame timeline** (`frame_timeline.md`) | One chart per heavy stretch: every unit's busy and waiting time a frame (the main CPU's phases, each ME job, the GE with the ME's lists), and who waits on whom. **First pass done 11-04** (tock162, four runs): the main CPU's ticks bind b30, c20 and d20's walk; the ME binds d20's heavy view; b40 waits on the display. Open: the AI job's own time (the ME's report line is cut), lanetrace windows at a chosen game time | The PSP's instruments (`hwprofile`, `gebusy2` with its phase split, `frametrace`, `lanetrace`; `mebusN` left out: it adds a synthetic load) | 4 runs, one per map |
| 4 | **Memory** (`memory_ledger.md`, refreshed) | Every pool (RAM, VRAM, eDRAM, scratchpad, the volatile 4 MB), its peak and room by map, what a KB buys where | The ledger's 10-27 version, the builds' heap lines | None new |
| 5 | **The cost map** (`cpu_cost_map.md`) | The main CPU's cache costs and the ways to cut them | Done 11-04; kept current | — |

**The bake** (Sheepdog01, 2026-11-04 13:22) comes after the design but belongs in it: whatever the PSP need not work out at run time (data laid out for the cache, the GE's own formats, precomputed visibility, lighting or posing tables) can move to bake time. So the design says, job by job, what is baked and what is computed, and the bake tool's rebuild follows from that.

Then **the design**: where each job runs, how data flows between the units, the cache strategy for each stage, the capacity it leaves, and a staged build order. Each stage is proven in the lab, keeps the campaign playable, and rides the tick/tock cadence.

## Ledger 1's first gap list

A shallow first pass, for the sweep to confirm or correct. These features exist on the PSP; whether and how we use them is the question [U unless marked]:

- **The GE:**
  - hardware skinning (bone matrices, vertex weights);
  - vertex morphing;
  - bounding-box culling inside a list;
  - call lists and reusable sub-lists;
  - the texture formats we don't use (DXT1/3/5: 4 or 8 bits a texel; no code names them [D, a grep]);
  - mipmaps;
  - hardware lights;
  - Bézier and spline patches;
  - the clip planes, logic ops and stencil uses.
- **The VFPU:** which tick and drawing maths still runs on the FPU (`hardware_utilization.md` §3 has the float inventory); its matrix and vector forms against today's uses.
- **The ME:** its 2 MB eDRAM (SNDEDRAM2 uses part of it), its own caches, its scheduler (the rebuild Sheepdog01 deferred until after the tick work, 2026-11-04 12:13), what it could carry beyond today's jobs.
- **The VME** (the ME's reconfigurable engine): VMEMIX was parked half-measured.
- **The AVC decoder** and the rest of the ME's media blocks: whether any could be used as general hardware.
- **DMA:** 2.6–3× memcpy [M]. Where it runs today, and what it costs the bus when it runs beside the tick.
- **The scratchpad:** the BSP's top nodes now; what else is hot across the whole tick.
- **VRAM:** 2 MB, nearly full [D]. What sits there and what buys most.
- **The volatile 4 MB:** the texture RAM cache. Is it the best use?
- **The memory stick's async reads**, and what a smarter streaming order could free.
- **The firmware's libraries** (sceSas, the codecs): which a game may use, at what cost.

## Who does it

- **The master:** owns this page and the design; judges each ledger's findings; asks Sheepdog01 where his call is needed.
- **Recon (Thing 2):** the PSP runs and the instruments (ledgers 2 and 3), and the bench round for ledger 1.
- **API lanes and helpers** (the lab's `coord/delegation.md`): the reading sweeps (ledger 1's docs and code, ledger 2's dependencies). Their answers are drafts until checked.
- **The tick and tock go on meanwhile:** the current levers (P1, P2) and tock160's release work don't wait on the recon.

## Done when

- Ledger 1 lists every feature with its status and a measured cost or a reason it can't be measured. Every gap is chased or ranked out.
- Ledger 2 gives each job's fixed and per-entity cost on each unit it could run on. The capacity questions above each have a first answer.
- Ledger 3 has a timeline for b30, c20, d20 and one light map. (Done 11-04 for tock162, light map b40; its gaps are listed in `frame_timeline.md`.)
- Ledger 4 is current on today's builds.
- The design is written against all five, and Sheepdog01 has read it.

Started 2026-11-04.
