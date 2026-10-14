# Applying the research (2026-10-14)

Sheepdog01 asked (2026-10-14) for the research we collected to be checked against the port: what applies, what's done, and where the port isn't using the hardware as it should. Three read-only passes went through the research docs (`research_icache.md`, `research_halo_activation.md`, `research_syphon_filter_psp.md`, `research_brief_for_master.md`, the B1–B20 notes, `code_review_b119_me.md`) and checked each item against the code at `85ebea3` and the lab's levers table. This is what came out. Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code, [U] inference.

## 1. Where the port goes against the research

Each of these was found by two or three of the passes on their own.

| # | What we do | What the research says | Fix | Status |
|---|---|---|---|---|
| W1 | The ME serves audio only after it has run every job in the ring (`me_ring_drain`), so a mix block can wait behind several ray jobs (each ~0.4–5 ms) while the audio thread holds `mixer_lock` [D] | Keep jobs coarse but never let them starve the deadline work (B4; `media_engine.md`) | Serve audio between ring jobs (`psp_me_serve_audio()` already exists) | b123, branch `mehyg123` |
| W2 | Each 16-ray job: the main CPU writes back its whole cache, and the ME drops its whole cache [D] | Write the payload back once, then publish (brief §1.3–1.5; Acton's queue). A whole-cache op is right past ~16 KB (YAPSPD), but once per hand-over, not per job | One write-back per hand-over; the ME drops its cache only on the hand-over's first job | b123, `mehyg123` |
| W3 | Audio: a whole-cache write-back on the main CPU and two whole drops on the ME for every block, ~43 a second [D] | Range operations over what's actually shared (B4 #2/#3, B15) | Ranges: the voice list, the source samples, the output | b123, `mehyg123` |
| W4 | `psp_me_wait` drops the output with cache op `0x1B` (write back, then invalidate). A dirty line left in the output range at push time would overwrite the ME's answer [D] | One writer per line (brief §1.3) | `psp_me_push` writes back and invalidates the output range itself. Not broken today: every caller does `WritebackAll` first | b123, `mehyg123` |
| W5 | The GE hand-over path after ME jobs (`gemeoverlap`, L1) calls `WritebackInvalidateAll`, throwing away the game thread's working set [D] | The main CPU never reads what the ME writes, or invalidates only those ranges (brief §1.3; code review b119 §4) | Ownership by range. May be part of why L1 measured 3–6% slower [U] | **Before ME phase 2** |
| W6 | GCC's "hot" label covers 562 KB / 769 functions, ten times the tick's core. So `hotgroup` tested "group 562 KB", not "group the hot core", and was rejected for the wrong reason [D] | Lay out the small hot set first (research_icache T1/T2) | A tighter threshold (`hot-bb-count-fraction`), then a call-graph order file with the cold parts placed beside it | Threshold: b123, `flags123`. Order file: next |
| W7 | Release PGO flags still allow the loop-duplicating passes [D] | Turn them off for code size (T4(a), the HackerSM64 precedent) | `-fno-unswitch-loops -fno-split-loops -fno-version-loops-for-strides -fno-predictive-commoning`, judged in the cache model | b123, `flags123` |

## 2. What applies and is being built for b123

Every item is exact and has an off word. The three branches start from `w122` and come back with review docs.

- **`mehyg123`** (ME and cache hygiene): W1–W4, a 128-byte debug stash (the ME's progress word, dumped when a job times out), and the cutscene creep: b122's ladder opened cutscenes 0.4–1.3 ms slower than b121's. The suspects are W1 (audio waiting behind rays while dialogue plays) and settle waits at the epoch functions while scripted objects move. The rig A/B on c40 (lab 38611163488) separates them. If rays are the cause, they aren't handed over while a cinematic plays: still exact, since the main CPU casts the same rays.
- **`flags123`** (compiler and small levers): W6's threshold sweep (1000, 300, 100) and W7, plus function alignment. Judged on the tick in the cache model, since the model's drawing I-miss gains haven't shown on hardware. Also:
  - **`ai_profile_update`** walks every actor twice a tick only for a debug meter: its sampling is skipped unless the meter is shown (off word `aiprofilefull`);
  - an **activation census** in the heartbeat: active against total objects and the encounters forced active. It says whether any faithful activation lever is left.
- **`lititems123`**: ground items with attached lights (Covenant weapons) are kept awake today. Since f6d2617 a sleeping object's lights are reconnected, so they can sleep exactly (off word `litemsawake`). Also the plan for biped collision on the ME (below).

## 3. What applies next, in order

1. **W5, the GE hand-over by ownership.** It's the prerequisite for ME phase 2 paying off.
2. **A warm ME cache for rays.** The level's BSP and tags don't change during a level, so the ME needs to drop only what does (the object pool, cluster lists, `psp_object_spheres`, the queue). First, count the ME's cycles per job, warm against cold.
3. **Biped collision on the ME.** Bipeds are 18–26 ms of the tick and the ME is 85–92% idle [M]. The batch recipe (B1, GDC08: group the calls, define inputs and outputs, queue them) applies to the ground and sweep tests, and the ME copies of the ray functions already exist. One catch: `psp_vfpu_matrix4x3_multiply` differs from the FPU in the last bit, so anything moved there needs an FPU-matched path. The plan is `docs/plan_biped_collision_me.md`.
4. **Collision layout** (B13, Ericson): BSP nodes in depth-first order at bake time (the left child is the next node), nodes packed to cache lines, and a "linearization cache" of the leaves and surfaces near each biped while it stays in one leaf set (the general form of X9's sphere memo). Exact as long as no value is quantized.
5. **Asynchronous audio** (B15, TYL): the audio thread submits block N+1 and outputs block N, so it never waits on the ME holding `mixer_lock`. Fits with the music path in `audio_fidelity.md`.
6. **The call-graph order file** (T2): hot functions ordered by call edges in 4–8 KB clusters, through `PSP_TEXT_LAYOUT=<order.txt>`. Scored in `PSP_ICACHE_MAP` before anything is built.
7. **Wide-frustum reject, then exact clipping** (B15, dQuakePlus): only if `cutroom` proves to be the Pelican wall. It would cut far fewer triangles into the cut buffer.
8. **Small checks**, each S and exact:
   - the VFPU context only on threads that use it (E20, a thread-switch bench);
   - a cache-map survey of empty-slot skipping in the datum tables (RB);
   - profiler ratios per subsystem (misses per load, stall per miss, bus accesses);
   - bulk copies through the uncached view so they don't evict the hot set;
   - FCR31 logged once.
9. **A cache microbench as rig filler** for the PSP's idle gaps: I-cache associativity and replacement, sequential line prefetch, and whether code can run from the scratchpad. It decides how far the layout work should lean.

## 4. Settled: the research is answered or doesn't hold here

- **Stall on miss against stall on use is answered:** the CPU stalls on every miss, with no hit-under-miss [M] (halo_bench v7, `memory_traffic.md`). Ericson-style preloading is out; the load-into-locals and branch-free levers (RJ, RL) are worth only the instructions they save.
- **The Allegrex has a prefetch** (`cache 30`), and placing it by hand matters: the wrong distance is slower [M v9] (X5).
- **The ME's idle polling costs the main CPU nothing measurable** [M] (bench v10; `mehalt` gained nothing on the rig), against B4/B8's 10–20%. Polling stays.
- **DMA into eDRAM** raises the CPU's miss time from 218 to 499 ns while it runs [M]. Any eDRAM staging happens at level load, never per job.
- **Running the array checks once a tick instead of twice** (activation opp. 5) is unsafe: they're ChupathingyCE's port recovery code, not diagnostics. Not pursued.
- **cachegrind (B20)** models x86, so its I-cache results don't transfer; the PPSSPP cache model replaces it. **ARS (B19)** is legal background only.

## 5. Sheepdog01's calls: behaviour, not speed

Shipped PSP shooters (Syphon Filter, Resistance: Retribution, God of War) used no exact lever we lack. What they did change behaviour or look:
- enemies capped at 4–6 at a time;
- coarse idle, curious and alarmed AI states;
- small per-archetype skeletons;
- an aim-assist box for the single nub;
- loads hidden behind background playback.

Two of our own levers belong here too:
- **sorting actors by AI state** (RH) changes the order of random draws, so it isn't exact as the levers table says;
- **batching objects by type** (T8) is a behaviour change for the same reason.

Each would be an A/B behind a keep word, and only if the exact work runs out.

## Housekeeping

- Fixed in b123 (`flags123`):
  - `optimization_ranking.md`'s X8 (`object_connect_lights` re-links attached lights; it computes no lighting, and the lever measured ~0);
  - `campaign_recon_b30.md`'s "every moving biped" line;
  - the levers table's RJ, RL (stall on miss answered) and RH (not exact).
- The research docs live only in Sheepdog01's project. Sub-agents can't read the project, so the passes recovered them from the chat where they were written. Future research passes should be handed the docs' text.
