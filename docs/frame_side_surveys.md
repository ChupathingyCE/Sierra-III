# Frame-side surveys (2026-10-29)

Six read-only surveys of the port's frame side, run on the lab's API lanes (Sheepdog01, 17:26: spend the month's credit on surveys that feed the same-look rebuild). Each answer was read in full, and at least two of its file:line quotes were checked against the code before anything below was taken from it.

Every row is a lead, not a result. The code facts are [D]; every cost is [U] until a PSP pair prices it. Each fix keeps the frames identical, and each one that touches the caches needs a PSP pair, because the cache model may neither size nor dismiss it (Sheepdog01, 2026-10-27).

| Survey | Lab run | Model | Read for |
|---|---|---|---|
| geperf | api-job 40973212021 | Sonnet | `psp_ge.c` |
| meperf | api-job 40973319547 | Sonnet | `psp_me.c` |
| texgeoperf | api-job 40973393198 | Sonnet | `psp_tex.c`, `psp_geo.c` |
| pdcdesign | api-job 40973501562 | Opus | PDC's forms and risks: written into `pdc_plan.md` |
| cplan | api-job 40973577774 | Opus | C's plan against the code: written into `hardware_utilization.md`, section 4 |
| psppriorart | or-job 40973694043 | DeepSeek | leads from other ports and PSP practice; checked by api-job priorartcheck 40975581725 (Sonnet) |

## 1. The hand-offs between the CPUs (geperf, meperf)

The surveys agree that the cost is in the hand-offs between the CPUs: whole caches written back and dropped, and uncached polling on the bus the ME reads.

| # | Where | What it does | Fix |
|---|---|---|---|
| 1 | `psp_ge.c` `sceKernelDcacheWritebackAll()` at each geinflight and gemeoverlap hand-over; `sceKernelDcacheWritebackInvalidateAll()` after the ME's frame and at drains | The main CPU's whole 16 KB cache, written back before and dropped after the ME's frame. The next tick starts cold, though little of what it reads is what the ME wrote | Write back and drop by range: the records, pspgu's state, this file's state and the residency words, put on lines of their own. Keep one whole write-back only where the records point into engine data |
| 2 | `psp_me.c` `psp_me_writeback(NULL, ME_RING_ALL_OF_RAM)` for rays, visibility, poses and points | Up to four whole-cache write-backs a frame, one a job kind | One a tick, with later pushes marked as continuing (`ME_RING_ARG_CONTINUES` does this for rays). Check that nothing they read is written between |
| 3 | `psp_me.c` `meLibDcacheWritebackInvalidateAll()` on both sides of the non-audio slot jobs, and at ring jobs with large inputs | The ME's whole cache dropped at a job's start and written back at its end | Write back by range, as the audio job's `psp_dsound_me_lines` does |
| 4 | `psp_me.c` `while (slot->done != slot_request[which])` in `me_finish_slot` | The main CPU's wait for the frame: an uncached look every 64 nops, with a clock system call on every look | Space the looks and the clock reads by default (`mefinishslow` does) |
| 5 | `psp_ge.c` `inflight_serve()` on every record (`psp_me_yield`) | An uncached read of the audio slot and the ring after every replayed record, hundreds to thousands a frame | Serve every 16 records, or by the ME's cycle counter, with audio's wait still bounded |
| 6 | `psp_ge.c` `inflight_forward_us = 1000` | The forwarding thread wakes every 1 ms while a frame is out: ~2% of the main CPU by its own comment | 4 ms (`gefwd` allows it); the frame's last address is forwarded at the wait anyway |
| 7 | `psp_ge.c` `ge_writeback(indices, ...)` in each level draw | Cache ops over the index list on the ME, which the main CPU already wrote back at the hand-over | Skip on the ME when the indices weren't copied; check mechunk's early pieces |
| 8 | `psp_ge.c` `GE_DEFER_DRAIN()` in `psp_ge_text_changed` | A text update drains the whole record buffer: a wait for the ME and a cold cache mid-frame | Record the glyph write-back as a record kind |
| 9 | `psp_ge.c` the decal record's `memcpy` of up to 16 KB of corners | Used-once data through the main CPU's cache, evicting the engine's hot lines | Ring the decal vertices as the triangle ring does, or copy only the quads kept |
| 10 | `psp_me.c` `me_run_slot`: `sceKernelDelayThread(100)` a look | Synchronous classify and angle jobs, each look a scheduler wake | The submit and finish pair, so the main CPU works meanwhile |

**The list itself** (geperf row 4): every GE command word is an uncached store, on the ME too. pspgu's `sceGuStart` sets bit 30, and `listcachedall`, which would write the list through the cache, is off with gemeon. It was shelved after a GE hang (`docs/hardware_b114.md`). Under the proper-fix rule that hang wants its cause found, not another shelving. It also decides PDC's copy form (`pdc_plan.md`).

## 2. The texture and geometry caches (texgeoperf)

| # | Where | What it does | Fix |
|---|---|---|---|
| 1 | `psp_geo.c` `if (place_index_on && !me)` | On the ME, a geometry lookup that misses the 64-slot table scans up to 512 entries (~12 KB, most of the ME's cache). A frame asks about more pointers than 64. PDC's key calls `psp_geo_generation` on every part | A larger slot table for the ME (each slot is re-checked, so answers can't change), or the main CPU's sorted index published read-only at the hand-over |
| 2 | `psp_tex.c` `if (bind_blocks_on && !PSP_ME_WORK())` and the bind memo's `!PSP_ME_WORK()` | On the ME every bind recomputes its palette and levels and makes about a dozen pspgu calls; neither the bind blocks nor the memo run there | Bind blocks made by the main CPU at pretouch, written back once, and only CALLed by the ME (the texture bind is ~1.4 ms of c10's ME, `pdc_plan.md`) |
| 3, 4 | `psp_geo.c` and `psp_tex.c` `fits` and `allocate`: entry scans inside 16-step and 6-try loops | Every load or miss pulls the entry tables (~12 KB) through the cache, evicting the draw's working set | Resident extents kept sorted by address; a binary search and the neighbours |
| 5 | `psp_geo.c` two `sceKernelDcacheWritebackInvalidateRange` calls a synchronous load | The second can't find dirty lines; blocks over 16 KB may become whole-cache operations | Invalidate only after the read; write back first only when a converter wrote the block |
| 6 | `psp_tex.c` `find`: a 64-slot memo, then a binary search | 2-3 lookups a draw, plus pdcprobe's five | A wider memo, or the texture index kept in the material record |
| 8 | `psp_tex.c` `sceIoRead` into staging, then `sceDmacMemcpy` | Every texture read twice, with a staging write-back | Read RAM-arena textures straight into the arena |
| 9 | `psp_tex.c` `frame % 900 == 450` | A sort of the VRAM-resident entries with float divides every 900 frames, gated on `bind_counts`, not on debug | Gate it on the log level, or keep a running top 12 |

## 3. Leads from other ports and PSP practice (psppriorart, priorartcheck)

Most of the 20 leads are what the port already does: bone sub-lists, bind blocks, double-buffered lists, stall batching, the ME's jobs, swizzled CLUT textures, slant buckets, LOD for 272 lines. The GE's two-deep call limit, which the job asked to verify first, is already measured (lab, research/gecall). Three leads were new, and the check found:

| Lead | Source | Applies? | Saves |
|---|---|---|---|
| **The model's regions and parts walked once a frame and kept for the decal and transparent passes** | BirchWood's Halo CE Vita port, `models.c` ("walked the regions and parts once per pass ... The walk is made once"; GPL-3.0, so its code only with his grant, `research/research_vita.md` R1) | Yes: the engine at the pin walks them once a pass (`models.c` `for (pass = _render_model_pass_solid; pass<=last_pass; pass++)`). RSO already skips the walk for still objects | Main-CPU work; unmeasured: size the walk on b30's moving models first |
| **Decals of the same key merged into one draw** | The same port, `rasterizer_xbox_decals.c` ("790 decals went out as 25 draws, fading as 180") | Partly: same key and same colour looks exact. Moving the colour into a vertex stream works only for blend 1, since blend 2's texture-env constant comes from the colour | Part of b30's 8.4 ms of decal CPU work in the GE layer (`ge_audit.md`); the split between state and the per-quad loop isn't known |
| **The GE's bounding-box test (BBOX)** | pspsdk, `psp_hardware_reference.md` | No: it saves GE time only, the GE idles 50-77%, and `ge_audit.md` already ruled it small | Nothing on the bound lanes |

## What goes where

- **The hand-offs (1) and the caches (2):** the hardware session's compact-hot-data and bus work, through the master. Rows 1.1-1.6 and 2.1 are the cheapest to try.
- **2.2 (bind blocks for the ME) and the uncached list:** PDC's, since they share its steps and its proofs.
- **The walk kept across passes (3):** the main CPU's drawing walk, which C would otherwise move to the ME.
